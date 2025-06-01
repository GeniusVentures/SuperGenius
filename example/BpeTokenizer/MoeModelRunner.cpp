#include "MoeModelRunner.hpp"

MoEModelRunner::MoEModelRunner( const std::string &modelDir, bool debug, bool fp16 ) :
    modelDir( modelDir ),
    debugMode( debug ),
    useFp16( fp16 ), // NEW
    hiddenSize( 7168 ),
    useSplitLmHead( false ),
    finalLayerNorm( nullptr ),
    finalLayerNormSession( nullptr )
{
}

MoEModelRunner::~MoEModelRunner()
{
    // Clean up final layernorm session
    if ( finalLayerNorm && finalLayerNormSession )
    {
        finalLayerNorm->releaseSession( finalLayerNormSession );
        finalLayerNormSession = nullptr;
    }
}

bool MoEModelRunner::initialize()
{
    // Load tokenizer (unchanged)
    std::string vocabPath  = modelDir + "/vocab.json";
    std::string mergesPath = modelDir + "/merges.txt";

    if ( !tokenizer.load( vocabPath, mergesPath ) )
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return false;
    }
    std::cout << "Tokenizer loaded successfully" << std::endl;

    // Initialize embedding loader with FP16 flag
    embeddingLoader = SplitEmbeddingLoader( useFp16 ); // MODIFIED
    if ( !embeddingLoader.initialize( modelDir ) )
    {
        std::cerr << "Failed to initialize embedding loader" << std::endl;
        return false;
    }

    // Initialize LM head loader with FP16 flag
    lmHeadLoader   = SplitLmHeadLoader( useFp16 ); // MODIFIED
    useSplitLmHead = lmHeadLoader.initialize( modelDir );
    if ( useSplitLmHead )
    {
        if ( debugMode )
        {
            lmHeadLoader.printChunkInfo();
        }
    }
    else
    {
        std::cerr << "Warning: Failed to initialize split LM head loader. Will try legacy approach." << std::endl;

        // Load legacy LM head model as fallback
        std::string lmHeadPath = modelDir + "/lm_head_f32.mnn";
        lmHeadPath             = LLMUtility::getFp16Path( lmHeadPath, useFp16 ); // MODIFIED
        legacyLmHeadNet.reset( MNN::Interpreter::createFromFile( lmHeadPath.c_str() ) );
        if ( !legacyLmHeadNet )
        {
            std::cerr << "Failed to load legacy LM head model from " << lmHeadPath << std::endl;
            return false;
        }
        std::cout << "Using legacy LM head model as fallback" << std::endl;
    }

    // Load all layers (0-61)
    if ( !loadAllLayers() )
    {
        std::cerr << "Failed to load layers" << std::endl;
        return false;
    }

    // Load final layernorm
    if ( !loadFinalLayerNorm() )
    {
        std::cerr << "Warning: Failed to load final layernorm" << std::endl;
    }

    return true;
}

bool MoEModelRunner::loadAllLayers()
{
    // Load all layers from 0 to 61 using LayerProcessor
    for ( int i = 0; i <= 61; i++ )
    {
        auto processor = std::make_unique<LayerProcessor>( i, modelDir, debugMode, useFp16 ); // MODIFIED

        if ( processor->initialize() )
        {
            allLayers.push_back( std::move( processor ) );
            std::cout << "Initialized layer " << i << ( i < 3 ? " (Standard)" : " (MoE)" )
                      << ( useFp16 ? " [FP16]" : "" ) << std::endl;
        }
        else
        {
            std::cerr << "Failed to initialize layer " << i << std::endl;
            return false;
        }
    }

    std::cout << "Loaded " << allLayers.size() << " layers total" << std::endl;
    return !allLayers.empty();
}

bool MoEModelRunner::loadFinalLayerNorm()
{
    std::string finalNormPath = modelDir + "/final_layernorm.mnn";
    finalNormPath             = LLMUtility::getFp16Path( finalNormPath, useFp16 ); // MODIFIED

    if ( !std::filesystem::exists( finalNormPath ) )
    {
        std::cerr << "Final layernorm not found at " << finalNormPath << std::endl;
        return false;
    }

    finalLayerNorm.reset( MNN::Interpreter::createFromFile( finalNormPath.c_str() ) );
    if ( !finalLayerNorm )
    {
        std::cerr << "Failed to load final layernorm from " << finalNormPath << std::endl;
        return false;
    }

    // Create session for final layernorm
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_CPU;
    config.numThread = 1;

    finalLayerNormSession = finalLayerNorm->createSession( config );
    if ( !finalLayerNormSession )
    {
        std::cerr << "Failed to create session for final layernorm" << std::endl;
        return false;
    }

    std::cout << "Loaded final layernorm from " << finalNormPath << std::endl;
    return true;
}

std::vector<float> MoEModelRunner::runFinalLayerNorm( const std::vector<float> &input )
{
    if ( !finalLayerNorm || !finalLayerNormSession )
    {
        if ( debugMode )
        {
            std::cout << "No final layernorm available, skipping" << std::endl;
        }
        return input; // Pass through if no final layernorm
    }

    try
    {
        // Get input tensor
        auto layerInput = finalLayerNorm->getSessionInput( finalLayerNormSession, nullptr );
        if ( !layerInput )
        {
            throw std::runtime_error( "Failed to get input tensor for final layernorm" );
        }

        // Resize input tensor
        finalLayerNorm->resizeTensor( layerInput, { 1, 1, (int)input.size() } );
        finalLayerNorm->resizeSession( finalLayerNormSession );

        // Copy input data
        MNN::Tensor inputHost( layerInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        layerInput->copyFromHostTensor( &inputHost );

        // Run final layernorm
        finalLayerNorm->runSession( finalLayerNormSession );

        // Get output
        auto output = finalLayerNorm->getSessionOutput( finalLayerNormSession, nullptr );
        if ( !output )
        {
            throw std::runtime_error( "Failed to get output tensor for final layernorm" );
        }

        // Copy to host
        MNN::Tensor outputHost( output, output->getDimensionType() );
        output->copyToHostTensor( &outputHost );
        float *outputData = outputHost.host<float>();

        // Get output size
        size_t outputSize = 1;
        for ( int d = 0; d < output->dimensions(); d++ )
        {
            outputSize *= output->length( d );
        }

        // Create vector for output
        std::vector<float> layerOutput( outputSize );

        // Copy output data with validation
        for ( size_t i = 0; i < outputSize; i++ )
        {
            float val = outputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                layerOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                layerOutput[i] = val;
            }
        }

        return layerOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running final layernorm: " << e.what() << std::endl;
        return input; // Return input as fallback
    }
}

std::string MoEModelRunner::generateText( const std::string &prompt, int numTokens )
{
    // Tokenize prompt
    std::vector<int> promptTokens = tokenizer.encode( prompt );
    std::string      decoded      = tokenizer.decode( promptTokens );
    std::cout << "Original: 'The cat ran down the wall'" << std::endl;
    std::cout << "Decoded:  '" << decoded << "'" << std::endl;
    if ( promptTokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return "";
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : promptTokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Create a vector to hold generated tokens
    std::vector<int> generatedTokens = promptTokens;

    // Generate tokens one by one
    for ( int i = 0; i < numTokens; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        // Disable debug mode after a few tokens
        if ( i >= 3 )
        {
            setDebugMode( false );
        }

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding using split loader
        std::cout << "Computing embedding for token ID " << lastToken << std::endl;
        std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

        // Print embedding stats if in debug mode
        if ( debugMode && !embedding.empty() )
        {
            float minVal = *std::min_element( embedding.begin(), embedding.end() );
            float maxVal = *std::max_element( embedding.begin(), embedding.end() );
            float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
            float mean   = sum / embedding.size();

            std::cout << "Embedding stats - Size: " << embedding.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }

        if ( embedding.empty() )
        {
            std::cerr << "Failed to get embedding for token " << lastToken << std::endl;
            std::cout << "Using synthetic embedding as fallback" << std::endl;
            embedding = LLMUtility::createSyntheticEmbedding( lastToken, hiddenSize );
        }

        // Process through all layers (0-61) using unified LayerProcessor
        std::vector<float> layerOutput = embedding;
        for ( size_t j = 0; j < allLayers.size(); j++ )
        {
            layerOutput = allLayers[j]->process( layerOutput, i );

            if ( debugMode )
            {
                std::cout << "Processed through layer " << allLayers[j]->getLayerId()
                          << ( allLayers[j]->isStandardLayer() ? " (Standard)" : " (MoE)" ) << std::endl;
            }
        }

        // Apply final layernorm
        layerOutput = runFinalLayerNorm( layerOutput );

        if ( debugMode )
        {
            std::cout << "Applied final layernorm" << std::endl;
        }

        // Run LM head model (using split or legacy approach)
        int nextTokenId = 0;

        if ( useSplitLmHead )
        {
            // Run split LM head
            std::cout << "Running split LM head model..." << std::endl;
            std::vector<float> logits = lmHeadLoader.runPrediction( layerOutput );

            if ( !logits.empty() )
            {
                // Get top tokens
                std::vector<std::pair<int, float>> topTokens = lmHeadLoader.getTopK( logits, 5 );

                // Print top tokens
                std::cout << "\nTop tokens:" << std::endl;
                for ( size_t j = 0; j < topTokens.size(); j++ )
                {
                    int   tokenId = topTokens[j].first;
                    float score   = topTokens[j].second;

                    std::string tokenText;
                    try
                    {
                        tokenText = tokenizer.decode( { tokenId } );
                        // Replace non-printable chars with dots
                        for ( char &c : tokenText )
                        {
                            if ( c < 32 || c > 126 )
                            {
                                c = '.';
                            }
                        }
                    }
                    catch ( ... )
                    {
                        tokenText = "[invalid token]";
                    }

                    std::cout << "  " << ( j + 1 ) << ". Token ID " << tokenId << " (score: " << score << "): \""
                              << tokenText << "\"" << std::endl;
                }

                // Get top token
                nextTokenId = topTokens.empty() ? 0 : topTokens[0].first;
            }
            else
            {
                std::cerr << "Failed to get logits from split LM head" << std::endl;
                if ( legacyLmHeadNet )
                {
                    std::cout << "Falling back to legacy LM head" << std::endl;
                    nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), layerOutput, &tokenizer );
                }
            }
        }
        else if ( legacyLmHeadNet )
        {
            // Use legacy approach
            nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), layerOutput, &tokenizer );
        }

        // Add next token
        generatedTokens.push_back( nextTokenId );

        // Display current text
        std::string currentText = tokenizer.decode( generatedTokens );

        // Clean up output for display
        std::string displayText;
        for ( unsigned char c : currentText )
        {
            displayText += ( c < 32 || c > 126 ) ? '.' : c;
        }
        std::cout << "\nGenerated so far: " << displayText << std::endl;
    }

    // Return the final generated text
    return tokenizer.decode( generatedTokens );
}

void MoEModelRunner::debugModelPipeline( const std::string &prompt )
{
    std::cout << "\n=== DEBUGGING MODEL PIPELINE ===\n" << std::endl;

    // Test tokenizer
    std::vector<int> tokens = tokenizer.encode( prompt );
    std::cout << "1. TOKENIZER TEST:" << std::endl;
    std::cout << "   Input: \"" << prompt << "\"" << std::endl;
    std::cout << "   Tokens: ";
    for ( int token : tokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Test decode
    std::string decoded = tokenizer.decode( tokens );
    std::cout << "   Decoded back: \"" << decoded << "\"" << std::endl;

    // Test a few individual tokens
    std::cout << "\n2. INDIVIDUAL TOKEN DECODE TEST:" << std::endl;
    for ( int i = 0; i < 10; i++ )
    {
        std::string tokenStr = tokenizer.decode( { i } );
        std::cout << "   Token " << i << ": \"" << tokenStr << "\"" << std::endl;
    }

    if ( tokens.empty() )
    {
        return;
    }

    // Test embedding
    int lastToken = tokens.back();
    std::cout << "\n3. EMBEDDING TEST (Token " << lastToken << "):" << std::endl;
    std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

    if ( embedding.empty() )
    {
        std::cout << "   ERROR: Empty embedding!" << std::endl;
        return;
    }

    // Print embedding stats
    float minVal = *std::min_element( embedding.begin(), embedding.end() );
    float maxVal = *std::max_element( embedding.begin(), embedding.end() );
    float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
    float mean   = sum / embedding.size();

    std::cout << "   Size: " << embedding.size() << std::endl;
    std::cout << "   Range: [" << minVal << ", " << maxVal << "]" << std::endl;
    std::cout << "   Mean: " << mean << std::endl;
    std::cout << "   First 5 values: ";
    for ( int i = 0; i < std::min( 5, (int)embedding.size() ); i++ )
    {
        std::cout << embedding[i] << " ";
    }
    std::cout << std::endl;

    // Test all layers using the unified processor
    std::vector<float> layerOutput     = embedding;
    int                maxLayersToTest = std::min( 10, (int)allLayers.size() );

    for ( int layerIdx = 0; layerIdx < maxLayersToTest; layerIdx++ )
    {
        int                layerId    = allLayers[layerIdx]->getLayerId();
        std::vector<float> prevOutput = layerOutput;
        layerOutput                   = allLayers[layerIdx]->process( layerOutput, 0 );

        std::cout << "\n"
                  << ( 4 + layerIdx ) << ". LAYER " << layerId
                  << ( allLayers[layerIdx]->isStandardLayer() ? " (STANDARD)" : " (MOE)" ) << " TEST:" << std::endl;

        if ( layerOutput.empty() )
        {
            std::cout << "   ERROR: Empty output from layer " << layerId << "!" << std::endl;
            layerOutput = prevOutput; // Fallback
            continue;
        }

        float minVal = *std::min_element( layerOutput.begin(), layerOutput.end() );
        float maxVal = *std::max_element( layerOutput.begin(), layerOutput.end() );
        float sum    = std::accumulate( layerOutput.begin(), layerOutput.end(), 0.0f );
        float mean   = sum / layerOutput.size();

        std::cout << "   Size: " << layerOutput.size() << std::endl;
        std::cout << "   Range: [" << minVal << ", " << maxVal << "]" << std::endl;
        std::cout << "   Mean: " << mean << std::endl;

        // Check if output changed from input
        bool changed = false;
        if ( prevOutput.size() == layerOutput.size() )
        {
            for ( size_t i = 0; i < layerOutput.size(); i++ )
            {
                if ( std::abs( prevOutput[i] - layerOutput[i] ) > 1e-6 )
                {
                    changed = true;
                    break;
                }
            }
        }
        else
        {
            changed = true;
        }
        std::cout << "   Output changed from input: " << ( changed ? "YES" : "NO" ) << std::endl;

        // Stop early if we hit very problematic outputs
        if ( std::abs( maxVal ) < 1e-20 && std::abs( minVal ) < 1e-20 )
        {
            std::cout << "   WARNING: Output values extremely close to zero - possible model issue!" << std::endl;
            std::cout << "   Stopping debug to prevent cascade of zero outputs..." << std::endl;
            break;
        }
    }

    // Test final layernorm
    std::cout << "\n" << ( 4 + maxLayersToTest ) << ". FINAL LAYERNORM TEST:" << std::endl;
    std::vector<float> finalNormOutput = runFinalLayerNorm( layerOutput );

    if ( !finalNormOutput.empty() )
    {
        float minVal = *std::min_element( finalNormOutput.begin(), finalNormOutput.end() );
        float maxVal = *std::max_element( finalNormOutput.begin(), finalNormOutput.end() );
        float sum    = std::accumulate( finalNormOutput.begin(), finalNormOutput.end(), 0.0f );
        float mean   = sum / finalNormOutput.size();

        std::cout << "   Size: " << finalNormOutput.size() << std::endl;
        std::cout << "   Range: [" << minVal << ", " << maxVal << "]" << std::endl;
        std::cout << "   Mean: " << mean << std::endl;
    }
    else
    {
        std::cout << "   Final layernorm not available or failed" << std::endl;
        finalNormOutput = layerOutput;
    }

    // Test LM head
    std::cout << "\n" << ( 5 + maxLayersToTest ) << ". LM HEAD TEST:" << std::endl;

    if ( useSplitLmHead )
    {
        std::vector<float> logits = lmHeadLoader.runPrediction( finalNormOutput );

        if ( logits.empty() )
        {
            std::cout << "   ERROR: Empty logits from LM head!" << std::endl;
            return;
        }

        float minVal = *std::min_element( logits.begin(), logits.end() );
        float maxVal = *std::max_element( logits.begin(), logits.end() );
        float sum    = std::accumulate( logits.begin(), logits.end(), 0.0f );
        float mean   = sum / logits.size();

        std::cout << "   Logits size: " << logits.size() << std::endl;
        std::cout << "   Logits range: [" << minVal << ", " << maxVal << "]" << std::endl;
        std::cout << "   Logits mean: " << mean << std::endl;

        // Check for all-zero logits
        bool allZero = true;
        for ( float val : logits )
        {
            if ( std::abs( val ) > 1e-6 )
            {
                allZero = false;
                break;
            }
        }
        std::cout << "   All logits are zero: " << ( allZero ? "YES (PROBLEM!)" : "NO" ) << std::endl;

        // Show top 10 logits
        std::vector<std::pair<int, float>> indexedLogits;
        for ( size_t i = 0; i < logits.size(); i++ )
        {
            indexedLogits.push_back( { (int)i, logits[i] } );
        }
        std::sort( indexedLogits.begin(),
                   indexedLogits.end(),
                   []( const auto &a, const auto &b ) { return a.second > b.second; } );

        std::cout << "   Top 10 logits:" << std::endl;
        for ( int i = 0; i < std::min( 10, (int)indexedLogits.size() ); i++ )
        {
            int         tokenId   = indexedLogits[i].first;
            float       score     = indexedLogits[i].second;
            std::string tokenText = tokenizer.decode( { tokenId } );
            std::cout << "      " << ( i + 1 ) << ". Token " << tokenId << " (score: " << score << "): \"" << tokenText
                      << "\"" << std::endl;
        }
    }

    std::cout << "\n=== END DEBUG ===\n" << std::endl;
}

void MoEModelRunner::debugModelPipelineDetailed( const std::string &prompt )
{
    std::cout << "\n=== DETAILED DEBUGGING MODEL PIPELINE ===\n" << std::endl;

    // Test tokenizer
    std::vector<int> tokens = tokenizer.encode( prompt );
    std::cout << "1. TOKENIZER TEST:" << std::endl;
    std::cout << "   Input: \"" << prompt << "\"" << std::endl;
    std::cout << "   Tokens: ";
    for ( int token : tokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    if ( tokens.empty() )
    {
        return;
    }

    // Test embedding
    int lastToken = tokens.back();
    std::cout << "\n2. EMBEDDING TEST (Token " << lastToken << "):" << std::endl;
    std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

    if ( embedding.empty() )
    {
        std::cout << "   ERROR: Empty embedding!" << std::endl;
        return;
    }

    // Print embedding stats
    float minVal = *std::min_element( embedding.begin(), embedding.end() );
    float maxVal = *std::max_element( embedding.begin(), embedding.end() );
    float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
    float mean   = sum / embedding.size();

    std::cout << "   Size: " << embedding.size() << std::endl;
    std::cout << "   Range: [" << minVal << ", " << maxVal << "]" << std::endl;
    std::cout << "   Mean: " << mean << std::endl;

    // Test first few layers with detailed diagnostics
    std::vector<float> layerOutput     = embedding;
    int                maxLayersToTest = std::min( 5, (int)allLayers.size() );

    for ( int layerIdx = 0; layerIdx < maxLayersToTest; layerIdx++ )
    {
        int layerId = allLayers[layerIdx]->getLayerId();
        std::cout << "\n" << ( layerIdx + 3 ) << ". DETAILED LAYER " << layerId << " TEST:" << std::endl;

        // Enable debug mode for detailed testing
        allLayers[layerIdx]->setDebugMode( true );

        std::vector<float> layerResult = allLayers[layerIdx]->process( layerOutput, 0 );

        if ( layerResult.empty() )
        {
            std::cout << "   ERROR: Empty layer output!" << std::endl;
        }
        else
        {
            float minVal = *std::min_element( layerResult.begin(), layerResult.end() );
            float maxVal = *std::max_element( layerResult.begin(), layerResult.end() );
            float sum    = std::accumulate( layerResult.begin(), layerResult.end(), 0.0f );
            float mean   = sum / layerResult.size();

            std::cout << "   Layer Output Size: " << layerResult.size() << std::endl;
            std::cout << "   Layer Output Range: [" << minVal << ", " << maxVal << "]" << std::endl;
            std::cout << "   Layer Output Mean: " << mean << std::endl;
        }

        layerOutput = layerResult;

        // Reset debug mode
        allLayers[layerIdx]->setDebugMode( false );
    }

    std::cout << "\n=== END DETAILED DEBUG ===\n" << std::endl;
}

void MoEModelRunner::testExpertModelDirectly( const std::string &expertPath, int layerId, int expertId )
{
    std::cout << "\n=== DIRECT EXPERT MODEL TEST ===" << std::endl;
    std::cout << "Testing: " << expertPath << std::endl;
    std::cout << "Layer: " << layerId << ", Expert: " << expertId << std::endl;

    try
    {
        // Load the expert model directly
        std::unique_ptr<MNN::Interpreter> interpreter( MNN::Interpreter::createFromFile( expertPath.c_str() ) );
        if ( !interpreter )
        {
            std::cerr << "ERROR: Failed to load expert model!" << std::endl;
            return;
        }

        // Create session
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_CPU;
        config.numThread = 1;

        MNN::Session *session = interpreter->createSession( config );
        if ( !session )
        {
            std::cerr << "ERROR: Failed to create session!" << std::endl;
            return;
        }

        // Get input tensor info
        auto input = interpreter->getSessionInput( session, nullptr );
        if ( !input )
        {
            std::cerr << "ERROR: Failed to get input tensor!" << std::endl;
            interpreter->releaseSession( session );
            return;
        }

        std::cout << "Input tensor info:" << std::endl;
        LLMUtility::printTensorInfo( input, "  Input" );

        // Test with simple input pattern
        std::vector<float> testInput( hiddenSize, 0.1f );

        // Resize tensor
        interpreter->resizeTensor( input, { 1, (int)testInput.size() } );
        interpreter->resizeSession( session );

        // Copy input
        MNN::Tensor inputHost( input, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        memcpy( inputData, testInput.data(), testInput.size() * sizeof( float ) );
        input->copyFromHostTensor( &inputHost );

        // Run model
        interpreter->runSession( session );

        // Get output
        auto output = interpreter->getSessionOutput( session, nullptr );
        if ( !output )
        {
            std::cerr << "ERROR: Failed to get output tensor!" << std::endl;
            interpreter->releaseSession( session );
            return;
        }

        std::cout << "Output tensor info:" << std::endl;
        LLMUtility::printTensorInfo( output, "  Output" );

        // Copy output
        MNN::Tensor outputHost( output, output->getDimensionType() );
        output->copyToHostTensor( &outputHost );
        float *outputData = outputHost.host<float>();

        // Calculate output size
        size_t outputSize = 1;
        for ( int d = 0; d < output->dimensions(); d++ )
        {
            outputSize *= output->length( d );
        }

        // Analyze output
        if ( outputSize > 0 )
        {
            float minOut = outputData[0];
            float maxOut = outputData[0];
            float sumOut = 0.0f;

            for ( size_t i = 0; i < outputSize; i++ )
            {
                float val  = outputData[i];
                sumOut    += val;
                minOut     = std::min( minOut, val );
                maxOut     = std::max( maxOut, val );
            }

            std::cout << "Output analysis:" << std::endl;
            std::cout << "  Total elements: " << outputSize << std::endl;
            std::cout << "  Range: [" << minOut << ", " << maxOut << "]" << std::endl;
            std::cout << "  Mean: " << ( sumOut / outputSize ) << std::endl;

            if ( std::abs( maxOut - minOut ) > 1e-6 )
            {
                std::cout << "  STATUS: Output has meaningful variation - MODEL WORKING!" << std::endl;
            }
            else
            {
                std::cout << "  STATUS: Output might have issues" << std::endl;
            }
        }

        // Clean up
        interpreter->releaseSession( session );
    }
    catch ( const std::exception &e )
    {
        std::cerr << "ERROR in expert test: " << e.what() << std::endl;
    }

    std::cout << "=== END DIRECT EXPERT TEST ===\n" << std::endl;
}

void MoEModelRunner::testStandardLayerDirectly( const std::string &layerPath, int layerId )
{
    std::cout << "\n=== DIRECT STANDARD LAYER TEST ===" << std::endl;
    std::cout << "Testing: " << layerPath << std::endl;
    std::cout << "Layer: " << layerId << std::endl;

    try
    {
        // Load the layer model directly
        std::unique_ptr<MNN::Interpreter> interpreter( MNN::Interpreter::createFromFile( layerPath.c_str() ) );
        if ( !interpreter )
        {
            std::cerr << "ERROR: Failed to load standard layer model!" << std::endl;
            return;
        }

        // Create session
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_CPU;
        config.numThread = 1;

        MNN::Session *session = interpreter->createSession( config );
        if ( !session )
        {
            std::cerr << "ERROR: Failed to create session!" << std::endl;
            return;
        }

        // Test with simple input
        std::vector<float> testInput( hiddenSize, 0.1f );

        // Get input tensor
        auto input = interpreter->getSessionInput( session, nullptr );
        if ( !input )
        {
            std::cerr << "ERROR: Failed to get input tensor!" << std::endl;
            interpreter->releaseSession( session );
            return;
        }

        // Resize tensor
        interpreter->resizeTensor( input, { 1, (int)testInput.size() } );
        interpreter->resizeSession( session );

        // Copy input
        MNN::Tensor inputHost( input, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        memcpy( inputData, testInput.data(), testInput.size() * sizeof( float ) );
        input->copyFromHostTensor( &inputHost );

        // Run model
        interpreter->runSession( session );

        // Get output
        auto output = interpreter->getSessionOutput( session, nullptr );
        if ( !output )
        {
            std::cerr << "ERROR: Failed to get output tensor!" << std::endl;
            interpreter->releaseSession( session );
            return;
        }

        // Analyze output similar to expert test
        MNN::Tensor outputHost( output, output->getDimensionType() );
        output->copyToHostTensor( &outputHost );
        float *outputData = outputHost.host<float>();

        size_t outputSize = 1;
        for ( int d = 0; d < output->dimensions(); d++ )
        {
            outputSize *= output->length( d );
        }

        if ( outputSize > 0 )
        {
            float minOut = outputData[0];
            float maxOut = outputData[0];
            float sumOut = 0.0f;

            for ( size_t i = 0; i < outputSize; i++ )
            {
                float val  = outputData[i];
                sumOut    += val;
                minOut     = std::min( minOut, val );
                maxOut     = std::max( maxOut, val );
            }

            std::cout << "Output analysis:" << std::endl;
            std::cout << "  Total elements: " << outputSize << std::endl;
            std::cout << "  Range: [" << minOut << ", " << maxOut << "]" << std::endl;
            std::cout << "  Mean: " << ( sumOut / outputSize ) << std::endl;

            if ( std::abs( maxOut - minOut ) > 1e-6 )
            {
                std::cout << "  STATUS: Standard layer working normally!" << std::endl;
            }
            else
            {
                std::cout << "  STATUS: Standard layer output issue!" << std::endl;
            }
        }

        // Clean up
        interpreter->releaseSession( session );
    }
    catch ( const std::exception &e )
    {
        std::cerr << "ERROR in standard layer test: " << e.what() << std::endl;
    }

    std::cout << "=== END STANDARD LAYER TEST ===\n" << std::endl;
}
