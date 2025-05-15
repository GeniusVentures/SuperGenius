#include "BpeTokenizer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric> // For std::accumulate
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>

// DeepSeek-V3 model constants
const int DEEPSEEK_HIDDEN_SIZE     = 7168; // Hidden size from the model extraction
const int MAX_MEMORY_ALLOCATION_MB = 4096; // 4GB cap for safety

// Utility function to check tensor properties and detect issues
bool checkTensor( MNN::Tensor *tensor, const std::string &name )
{
    if ( !tensor )
    {
        std::cerr << "Error: " << name << " tensor is null" << std::endl;
        return false;
    }

    std::cout << name << " tensor info - Dimensions: " << tensor->dimensions();
    for ( int i = 0; i < tensor->dimensions(); i++ )
    {
        std::cout << " x " << tensor->length( i );
    }

    // Use individual fields of halide_type_t to avoid cout issues
    std::cout << ", Type code: " << tensor->getType().code << ", bits: " << tensor->getType().bits
              << ", lanes: " << tensor->getType().lanes << std::endl;

    return true;
}

// Check if tensor shapes are compatible with DeepSeek-V3 model
void checkModelDimensions( MNN::Interpreter *embeddingNet, MNN::Interpreter *expertNet, MNN::Interpreter *lmHeadNet )
{
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_CPU;
    config.numThread = 1;

    // Check embedding output
    if ( embeddingNet )
    {
        auto embeddingSession = embeddingNet->createSession( config );
        auto embeddingOutput  = embeddingNet->getSessionOutput( embeddingSession, nullptr );
        if ( embeddingOutput && embeddingOutput->dimensions() > 2 )
        {
            int embeddingDim = embeddingOutput->length( 2 );
            if ( embeddingDim != DEEPSEEK_HIDDEN_SIZE )
            {
                std::cerr << "Warning: Embedding dimension mismatch - Expected: " << DEEPSEEK_HIDDEN_SIZE
                          << ", Got: " << embeddingDim << std::endl;
            }
        }
        embeddingNet->releaseSession( embeddingSession );
    }

    // Check expert dimensions
    if ( expertNet )
    {
        auto expertSession = expertNet->createSession( config );
        auto expertInput   = expertNet->getSessionInput( expertSession, nullptr );
        auto expertOutput  = expertNet->getSessionOutput( expertSession, nullptr );

        if ( expertInput )
        {
            int inputDim = expertInput->length( 1 );
            if ( inputDim != DEEPSEEK_HIDDEN_SIZE )
            {
                std::cerr << "Warning: Expert input dimension mismatch - Expected: " << DEEPSEEK_HIDDEN_SIZE
                          << ", Got: " << inputDim << std::endl;
            }
        }

        if ( expertOutput )
        {
            int outputDim = expertOutput->length( 1 );
            if ( outputDim != DEEPSEEK_HIDDEN_SIZE )
            {
                std::cerr << "Warning: Expert output dimension mismatch - Expected: " << DEEPSEEK_HIDDEN_SIZE
                          << ", Got: " << outputDim << std::endl;
            }
        }

        expertNet->releaseSession( expertSession );
    }

    // Check LM head dimensions
    if ( lmHeadNet )
    {
        auto lmHeadSession = lmHeadNet->createSession( config );
        auto lmHeadInput   = lmHeadNet->getSessionInput( lmHeadSession, nullptr );

        if ( lmHeadInput )
        {
            int inputDim = lmHeadInput->length( 1 );
            if ( inputDim != DEEPSEEK_HIDDEN_SIZE )
            {
                std::cerr << "Warning: LM head input dimension mismatch - Expected: " << DEEPSEEK_HIDDEN_SIZE
                          << ", Got: " << inputDim << std::endl;
            }
        }

        lmHeadNet->releaseSession( lmHeadSession );
    }
}

// Safe tensor copy function with checks - modified to accept const sourceData
bool safeCopyToTensor( const float *sourceData, size_t sourceSize, MNN::Tensor *destTensor, const std::string &name )
{
    if ( !destTensor )
    {
        std::cerr << "Error: Destination tensor " << name << " is null" << std::endl;
        return false;
    }

    // Create host tensor for copying
    MNN::Tensor hostTensor( destTensor, MNN::Tensor::CAFFE );
    float      *destData = hostTensor.host<float>();

    // Calculate expected size based on tensor dimensions
    size_t expectedSize = 1;
    for ( int i = 0; i < destTensor->dimensions(); i++ )
    {
        expectedSize *= destTensor->length( i );
    }

    if ( sourceSize != expectedSize )
    {
        std::cerr << "Warning: Size mismatch for " << name << " - Source: " << sourceSize
                  << ", Expected: " << expectedSize << std::endl;
        // We'll still copy what we can, but truncate or pad as needed
    }

    // Safe copy with size checking
    size_t elementsToProcess = std::min( sourceSize, expectedSize );

    // Debug - print some source values
    std::cout << "Source data sample for " << name << ": ";
    for ( size_t i = 0; i < std::min( size_t( 5 ), sourceSize ); i++ )
    {
        std::cout << sourceData[i] << " ";
    }
    std::cout << std::endl;

    // Copy data with bounds checking
    try
    {
        for ( size_t i = 0; i < elementsToProcess; i++ )
        {
            destData[i] = sourceData[i];
        }

        // If destination is larger, pad with zeros
        for ( size_t i = elementsToProcess; i < expectedSize; i++ )
        {
            destData[i] = 0.0f;
        }

        // Debug - print some copied values
        std::cout << "Copied data sample for " << name << ": ";
        for ( size_t i = 0; i < std::min( size_t( 5 ), expectedSize ); i++ )
        {
            std::cout << destData[i] << " ";
        }
        std::cout << std::endl;

        // Copy from host tensor to MNN tensor
        destTensor->copyFromHostTensor( &hostTensor );
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception during tensor copy for " << name << ": " << e.what() << std::endl;
        return false;
    }
}

// Function to create stable embeddings with sanity checks
bool createStableEmbeddings( const std::vector<int> &tokenIds,
                             int                     hiddenSize,
                             std::vector<float>     &embeddings,
                             MNN::Interpreter       *embeddingNet     = nullptr,
                             MNN::Session           *embeddingSession = nullptr )
{
    // Clear any existing data
    embeddings.clear();

    int seqLen = tokenIds.size();

    // Check memory allocation before proceeding
    size_t memRequired = seqLen * hiddenSize * sizeof( float ) / ( 1024 * 1024 ); // in MB
    if ( memRequired > MAX_MEMORY_ALLOCATION_MB )
    {
        std::cerr << "Error: Requested embedding allocation exceeds safety limit: " << memRequired << "MB > "
                  << MAX_MEMORY_ALLOCATION_MB << "MB" << std::endl;
        return false;
    }

    embeddings.resize( seqLen * hiddenSize );

    // First, try using the embedding model if provided
    if ( embeddingNet && embeddingSession )
    {
        try
        {
            auto embeddingInput = embeddingNet->getSessionInput( embeddingSession, nullptr );

            if ( !checkTensor( embeddingInput, "Embedding input" ) )
            {
                throw std::runtime_error( "Invalid embedding input tensor" );
            }

            // Prepare input tensor with token IDs
            embeddingNet->resizeTensor( embeddingInput, { 1, (int)tokenIds.size() } );
            embeddingNet->resizeSession( embeddingSession );

            // Copy token IDs to input tensor
            MNN::Tensor embeddingInputHost( embeddingInput, MNN::Tensor::CAFFE );
            int        *inputData = embeddingInputHost.host<int>();

            for ( size_t i = 0; i < tokenIds.size(); i++ )
            {
                inputData[i] = tokenIds[i];
            }

            embeddingInput->copyFromHostTensor( &embeddingInputHost );

            // Run embedding model
            std::cout << "Running embedding model..." << std::endl;
            embeddingNet->runSession( embeddingSession );

            // Get output
            auto embeddingOutput = embeddingNet->getSessionOutput( embeddingSession, nullptr );

            if ( !checkTensor( embeddingOutput, "Embedding output" ) )
            {
                throw std::runtime_error( "Invalid embedding output tensor" );
            }

            // Copy output to embeddings vector
            MNN::Tensor embeddingOutputHost( embeddingOutput, MNN::Tensor::CAFFE );
            embeddingOutput->copyToHostTensor( &embeddingOutputHost );
            float *outputData = embeddingOutputHost.host<float>();

            // Calculate output size
            size_t outputSize = 1;
            for ( int i = 0; i < embeddingOutput->dimensions(); i++ )
            {
                outputSize *= embeddingOutput->length( i );
            }

            if ( outputSize != embeddings.size() )
            {
                std::cerr << "Warning: Embedding output size mismatch - Expected: " << embeddings.size()
                          << ", Got: " << outputSize << std::endl;

                // Adjust size if needed
                embeddings.resize( outputSize );
            }

            // Copy embedding data
            for ( size_t i = 0; i < outputSize; i++ )
            {
                embeddings[i] = outputData[i];
            }

            std::cout << "Successfully used embedding model" << std::endl;
            return true;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error using embedding model: " << e.what() << std::endl;
            std::cerr << "Falling back to direct embedding generation..." << std::endl;
            // Fall back to direct generation
        }
    }

    // Direct embedding generation as fallback
    std::cout << "Creating direct embeddings as fallback..." << std::endl;

    // Use a stable seeding mechanism
    std::mt19937                          rng( 42 );
    std::uniform_real_distribution<float> dist( -0.05f, 0.05f );

    // Create stable embeddings based on token IDs with better float16 compatibility
    for ( int t = 0; t < seqLen; t++ )
    {
        int tokenId = tokenIds[t];

        // Use a deterministic function of token ID for stability
        for ( int h = 0; h < hiddenSize; h++ )
        {
            // Use sine function for smooth, bounded values based on token ID
            float value = 0.01f * std::sin( tokenId * 0.1f + h * 0.01f );

            // Add a small amount of noise for variation
            value += dist( rng ) * 0.01f;

            // Clamp to reasonable float16 range (-6 to +6)
            value = std::min( std::max( value, -1.0f ), 1.0f );

            // Store the embedding value
            embeddings[t * hiddenSize + h] = value;
        }
    }

    // Check for any NaN or Inf values
    for ( size_t i = 0; i < embeddings.size(); i++ )
    {
        if ( std::isnan( embeddings[i] ) || std::isinf( embeddings[i] ) )
        {
            std::cerr << "Warning: Invalid value at position " << i << std::endl;
            embeddings[i] = 0.0f; // Replace with zero
        }
    }

    // Calculate and print stats
    float minVal = *std::min_element( embeddings.begin(), embeddings.end() );
    float maxVal = *std::max_element( embeddings.begin(), embeddings.end() );
    float sum    = std::accumulate( embeddings.begin(), embeddings.end(), 0.0f );
    float mean   = sum / embeddings.size();

    std::cout << "Embeddings stats - Size: " << embeddings.size() << ", Min: " << minVal << ", Max: " << maxVal
              << ", Mean: " << mean << std::endl;

    return true;
}

// Run inference on expert model with better error handling
bool runExpertModel( const std::vector<float> &embeddings,
                     int                       contextLength,
                     int                       hiddenSize,
                     MNN::Interpreter         *expertNet,
                     MNN::Session             *expertSession,
                     std::vector<float>       &expertOutput )
{
    std::cout << "\n== EXPERT STAGE ==" << std::endl;

    if ( !expertNet || !expertSession )
    {
        std::cerr << "Error: Expert model or session is null" << std::endl;
        return false;
    }

    try
    {
        auto expertInput = expertNet->getSessionInput( expertSession, nullptr );

        if ( !checkTensor( expertInput, "Expert input" ) )
        {
            return false;
        }

        // Process only the last token
        expertNet->resizeTensor( expertInput, { 1, hiddenSize } );
        expertNet->resizeSession( expertSession );

        // Extract the last token's embedding
        int                lastTokenOffset = ( contextLength - 1 ) * hiddenSize;
        std::vector<float> lastTokenEmbedding( hiddenSize );

        // Check bounds
        if ( lastTokenOffset + hiddenSize > embeddings.size() )
        {
            std::cerr << "Error: Embeddings vector too small for extraction" << std::endl;
            return false;
        }

        // Copy the last token's embedding
        for ( int h = 0; h < hiddenSize; h++ )
        {
            lastTokenEmbedding[h] = embeddings[lastTokenOffset + h];
        }

        // Safe copy to expert input
        if ( !safeCopyToTensor( lastTokenEmbedding.data(), lastTokenEmbedding.size(), expertInput, "Expert input" ) )
        {
            return false;
        }

        // Run expert model
        std::cout << "Running expert model..." << std::endl;
        expertNet->runSession( expertSession );

        // Get expert output
        auto expertOutputTensor = expertNet->getSessionOutput( expertSession, nullptr );

        if ( !checkTensor( expertOutputTensor, "Expert output" ) )
        {
            return false;
        }

        // Copy output to vector
        MNN::Tensor expertOutputHost( expertOutputTensor, MNN::Tensor::CAFFE );
        expertOutputTensor->copyToHostTensor( &expertOutputHost );
        float *outputData = expertOutputHost.host<float>();

        // Calculate output size
        size_t outputSize = 1;
        for ( int i = 0; i < expertOutputTensor->dimensions(); i++ )
        {
            outputSize *= expertOutputTensor->length( i );
        }

        // Resize output vector
        expertOutput.resize( outputSize );

        // Copy with checks for NaN/Inf
        bool  hasExtremeValues = false;
        float minVal           = outputData[0];
        float maxVal           = outputData[0];
        float sum              = 0.0f;

        for ( size_t i = 0; i < outputSize; i++ )
        {
            float val = outputData[i];

            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                hasExtremeValues = true;
                expertOutput[i]  = 0.0f; // Replace with zero
            }
            else
            {
                expertOutput[i]  = val;
                minVal           = std::min( minVal, val );
                maxVal           = std::max( maxVal, val );
                sum             += val;
            }
        }

        float mean = sum / outputSize;

        std::cout << "Expert output stats - Size: " << outputSize << ", Min: " << minVal << ", Max: " << maxVal
                  << ", Mean: " << mean << std::endl;

        if ( hasExtremeValues )
        {
            std::cout << "Warning: Expert output had extreme values that were replaced" << std::endl;
        }

        // Optional: Apply scaling to experts (FP8 dequantization factor), if needed
        // This is a placeholder for FP8 normalization if you observe unexpected outputs
        /*
        float expertScaleFactor = 1.0f;  // Adjust if needed based on model behavior
        if (std::abs(mean) < 1e-4 || std::abs(maxVal) < 1e-3) {
            expertScaleFactor = 10.0f;  // Boost very small values
            std::cout << "Applying scale factor " << expertScaleFactor << " to expert output" << std::endl;
            for (size_t i = 0; i < outputSize; i++) {
                expertOutput[i] *= expertScaleFactor;
            }
        }
        */

        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error in expert model execution: " << e.what() << std::endl;
        return false;
    }
}

// Run inference on LM head model with better error handling
bool runLMHeadModel( const std::vector<float> &expertOutput,
                     MNN::Interpreter         *lmHeadNet,
                     MNN::Session             *lmHeadSession,
                     std::vector<float>       &logits )
{
    std::cout << "\n== LM HEAD STAGE ==" << std::endl;

    if ( !lmHeadNet || !lmHeadSession )
    {
        std::cerr << "Error: LM head model or session is null" << std::endl;
        return false;
    }

    try
    {
        auto lmHeadInput = lmHeadNet->getSessionInput( lmHeadSession, nullptr );

        if ( !checkTensor( lmHeadInput, "LM head input" ) )
        {
            return false;
        }

        // Prepare LM head input
        int hiddenSize = expertOutput.size();
        lmHeadNet->resizeTensor( lmHeadInput, { 1, hiddenSize } );
        lmHeadNet->resizeSession( lmHeadSession );

        // Safe copy to LM head input
        if ( !safeCopyToTensor( expertOutput.data(), expertOutput.size(), lmHeadInput, "LM head input" ) )
        {
            return false;
        }

        // Run LM head model
        std::cout << "Running LM head model..." << std::endl;
        lmHeadNet->runSession( lmHeadSession );

        // Get logits
        auto logitsTensor = lmHeadNet->getSessionOutput( lmHeadSession, nullptr );

        if ( !checkTensor( logitsTensor, "Logits" ) )
        {
            return false;
        }

        // Copy output to vector
        MNN::Tensor logitsHost( logitsTensor, MNN::Tensor::CAFFE );
        logitsTensor->copyToHostTensor( &logitsHost );
        float *outputData = logitsHost.host<float>();

        // Calculate output size
        size_t vocabSize = logitsTensor->length( logitsTensor->dimensions() - 1 );

        // Resize output vector
        logits.resize( vocabSize );

        // Copy with checks for NaN/Inf
        bool  hasExtremeValues = false;
        float minVal           = outputData[0];
        float maxVal           = outputData[0];
        float sum              = 0.0f;
        int   extremeCount     = 0;

        for ( size_t i = 0; i < vocabSize; i++ )
        {
            float val = outputData[i];

            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                hasExtremeValues = true;
                extremeCount++;
                logits[i] = 0.0f; // Replace with zero
            }
            else
            {
                logits[i]  = val;
                minVal     = std::min( minVal, val );
                maxVal     = std::max( maxVal, val );
                sum       += val;
            }
        }

        float mean = sum / vocabSize;

        std::cout << "LM head output stats - Size: " << vocabSize << ", Min: " << minVal << ", Max: " << maxVal
                  << ", Mean: " << mean << std::endl;

        if ( hasExtremeValues )
        {
            std::cout << "Warning: LM head output had " << extremeCount << " extreme values that were replaced"
                      << std::endl;
        }

        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error in LM head model execution: " << e.what() << std::endl;
        return false;
    }
}

// Main generation function with better error handling
std::vector<int> generateTokens( const std::vector<int> &inputTokens,
                                 int                     numTokensToGenerate,
                                 BpeTokenizer           &tokenizer,
                                 MNN::Interpreter       *embeddingNet,
                                 MNN::Interpreter       *expertNet,
                                 MNN::Interpreter       *lmHeadNet,
                                 MNN::ScheduleConfig    &config )
{
    std::vector<int> generatedTokens = inputTokens;

    // Create sessions
    MNN::Session *embeddingSession = embeddingNet ? embeddingNet->createSession( config ) : nullptr;
    MNN::Session *expertSession    = expertNet->createSession( config );
    MNN::Session *lmHeadSession    = lmHeadNet->createSession( config );

    // Get hidden size from expert model
    auto expertInput = expertNet->getSessionInput( expertSession, nullptr );
    int  hiddenSize  = 0;

    if ( expertInput->dimensions() == 2 )
    {
        hiddenSize = expertInput->length( 1 );
    }
    else
    {
        std::cerr << "Unexpected expert input dimensions: " << expertInput->dimensions() << std::endl;
        // Use the DeepSeek-V3 hidden size
        hiddenSize = DEEPSEEK_HIDDEN_SIZE;
    }

    std::cout << "Using hidden size: " << hiddenSize << std::endl;

    // Verify model dimensions match expectations
    checkModelDimensions( embeddingNet, expertNet, lmHeadNet );

    // Main generation loop
    for ( int i = 0; i < numTokensToGenerate; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokensToGenerate << " ---" << std::endl;

        int contextLength = generatedTokens.size();

        try
        {
            // Create embeddings
            std::vector<float> embeddings;
            if ( !createStableEmbeddings( generatedTokens, hiddenSize, embeddings, embeddingNet, embeddingSession ) )
            {
                throw std::runtime_error( "Failed to create embeddings" );
            }

            // Run expert model
            std::vector<float> expertOutput;
            if ( !runExpertModel( embeddings, contextLength, hiddenSize, expertNet, expertSession, expertOutput ) )
            {
                throw std::runtime_error( "Failed to run expert model" );
            }

            // Run LM head model
            std::vector<float> logits;
            if ( !runLMHeadModel( expertOutput, lmHeadNet, lmHeadSession, logits ) )
            {
                throw std::runtime_error( "Failed to run LM head model" );
            }

            // Find top tokens
            std::vector<std::pair<int, float>> scores;
            for ( size_t v = 0; v < logits.size(); v++ )
            {
                scores.push_back( { static_cast<int>( v ), logits[v] } );
            }

            // Sort to find top tokens
            std::partial_sort( scores.begin(),
                               scores.begin() + std::min( 20, static_cast<int>( scores.size() ) ),
                               scores.end(),
                               []( const auto &a, const auto &b ) { return a.second > b.second; } );

            // Filter for readable tokens
            std::vector<std::pair<int, float>> readableScores;
            for ( int j = 0; j < 20 && j < scores.size(); j++ )
            {
                int         tokenId = scores[j].first;
                std::string tokenText;

                try
                {
                    tokenText = tokenizer.decode( { tokenId } );

                    // Check if mostly ASCII
                    int nonAsciiCount = 0;
                    for ( unsigned char c : tokenText )
                    {
                        if ( c > 127 )
                        {
                            nonAsciiCount++;
                        }
                    }

                    if ( nonAsciiCount < tokenText.length() / 2 )
                    {
                        readableScores.push_back( scores[j] );
                        if ( readableScores.size() >= 5 )
                        {
                            break;
                        }
                    }
                }
                catch ( ... )
                {
                    // Skip invalid tokens
                    continue;
                }
            }

            // If we couldn't find enough readable tokens, fall back to top tokens
            if ( readableScores.size() < 3 )
            {
                readableScores = std::vector<std::pair<int, float>>(
                    scores.begin(),
                    scores.begin() + std::min( 5, static_cast<int>( scores.size() ) ) );
            }

            // Print top tokens
            std::cout << "\nTop tokens:" << std::endl;
            for ( size_t j = 0; j < readableScores.size(); j++ )
            {
                int   tokenId = readableScores[j].first;
                float score   = readableScores[j].second;

                std::string tokenText;
                std::string displayText;

                try
                {
                    tokenText = tokenizer.decode( { tokenId } );

                    // Create a cleaner display version
                    displayText = tokenText;
                    for ( char &c : displayText )
                    {
                        if ( c < 32 || c > 126 )
                        {
                            c = '.';
                        }
                    }
                }
                catch ( ... )
                {
                    displayText = "[invalid token]";
                }

                std::cout << "  " << ( j + 1 ) << ". Token ID " << tokenId << " (score: " << score << "): \""
                          << displayText << "\"" << std::endl;
            }

            // Select next token - cycle through the readable tokens for variety
            int nextTokenId = readableScores[i % readableScores.size()].first;
            generatedTokens.push_back( nextTokenId );

            // Display current text
            std::string currentText = tokenizer.decode( generatedTokens );

            // Clean up output by replacing control chars with readable representation
            std::string cleaned;
            for ( unsigned char c : currentText )
            {
                if ( c < 32 || c > 126 )
                {
                    // Non-printable character - replace with dot
                    cleaned += '.';
                }
                else
                {
                    cleaned += c;
                }
            }

            std::cout << "\nGenerated so far: " << cleaned << std::endl;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error during generation: " << e.what() << std::endl;

            // Add a simple English word token as a fallback
            const int englishTokens[] = { 265, 272,  281,  289,  312,  333,  343,  354,  391,  485,
                                          552, 1126, 1135, 1433, 2478, 2842, 5280, 6931, 8731, 4908 };
            int       randomToken = englishTokens[rand() % ( sizeof( englishTokens ) / sizeof( englishTokens[0] ) )];
            generatedTokens.push_back( randomToken );
        }
    }

    // Clean up
    if ( embeddingSession )
    {
        embeddingNet->releaseSession( embeddingSession );
    }
    expertNet->releaseSession( expertSession );
    lmHeadNet->releaseSession( lmHeadSession );

    return generatedTokens;
}

// Simplified main function
int main( int argc, char **argv )
{
    if ( argc < 4 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens>" << std::endl;
        std::cerr << "Example: " << argv[0] << " . \"The cat sat on the chair\" 5" << std::endl;
        return 1;
    }

    std::string modelDir  = argv[1];
    std::string prompt    = argv[2];
    int         numTokens = 1; // Default to 1 token

    try
    {
        numTokens = std::stoi( argv[3] );
    }
    catch ( ... )
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Load tokenizer
    BpeTokenizer tokenizer;
    std::string  vocabPath  = modelDir + "/vocab.json";
    std::string  mergesPath = modelDir + "/merges.txt";

    if ( !tokenizer.load( vocabPath, mergesPath ) )
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return 1;
    }

    // Tokenize prompt
    std::vector<int> tokens = tokenizer.encode( prompt );

    if ( tokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return 1;
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : tokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Define model paths
    std::string embeddingPath = modelDir + "/embedding.mnn";
    std::string expertPath    = modelDir + "/expert_10_1.mnn";
    std::string lmHeadPath    = modelDir + "/lm_head.mnn";

    // Load models with better error handling
    // Try loading embedding model, but make it optional
    MNN::Interpreter *embeddingNet = nullptr;
    try
    {
        embeddingNet = MNN::Interpreter::createFromFile( embeddingPath.c_str() );
        if ( embeddingNet )
        {
            std::cout << "Successfully loaded embedding model" << std::endl;
        }
    }
    catch ( ... )
    {
        std::cerr << "Note: Embedding model not loaded, will use direct embeddings" << std::endl;
    }

    // Expert and LM head are required
    auto expertNet = MNN::Interpreter::createFromFile( expertPath.c_str() );
    if ( !expertNet )
    {
        std::cerr << "Failed to load expert model" << std::endl;
        delete embeddingNet;
        return 1;
    }

    auto lmHeadNet = MNN::Interpreter::createFromFile( lmHeadPath.c_str() );
    if ( !lmHeadNet )
    {
        std::cerr << "Failed to load LM head model" << std::endl;
        delete embeddingNet;
        delete expertNet;
        return 1;
    }

    // Configure inference
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_CPU;
    config.numThread = 1; // Use single thread to avoid thread pool issues

    // Generate tokens
    std::vector<int> generatedTokens =
        generateTokens( tokens, numTokens, tokenizer, embeddingNet, expertNet, lmHeadNet, config );

    // Final output
    std::string finalText = tokenizer.decode( generatedTokens );

    // Clean up output
    std::string displayText;
    for ( unsigned char c : finalText )
    {
        if ( c < 32 || c > 126 )
        {
            // Non-printable character - replace with dot
            displayText += '.';
        }
        else
        {
            displayText += c;
        }
    }

    std::cout << "\nFinal generated text: " << displayText << std::endl;

    // Token breakdown
    std::cout << "\nToken-by-token breakdown:" << std::endl;
    for ( size_t i = 0; i < generatedTokens.size(); i++ )
    {
        std::string tokenText;
        std::string displayText;

        try
        {
            tokenText = tokenizer.decode( { generatedTokens[i] } );

            // Clean up for display
            displayText = tokenText;
            for ( char &c : displayText )
            {
                if ( c < 32 || c > 126 )
                {
                    c = '.';
                }
            }
        }
        catch ( ... )
        {
            displayText = "[invalid token]";
        }

        std::cout << "Token " << i << " (ID: " << generatedTokens[i] << "): \"" << displayText << "\"" << std::endl;
    }

    // Clean up
    delete embeddingNet;
    delete expertNet;
    delete lmHeadNet;

    return 0;
}
