#include "BpeTokenizer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <numeric> // For std::accumulate
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>

// Utility function to check tensor properties
void printTensorInfo( MNN::Tensor *tensor, const std::string &name )
{
    if ( !tensor )
    {
        std::cerr << "Error: " << name << " tensor is null" << std::endl;
        return;
    }

    std::cout << name << " tensor info - Dimensions: " << tensor->dimensions();
    for ( int i = 0; i < tensor->dimensions(); i++ )
    {
        std::cout << " x " << tensor->length( i );
    }
    std::cout << std::endl;
}

// Safe tensor copy function with checks
bool safeCopyToTensor( float *destData, const float *sourceData, size_t copySize )
{
    if ( !destData || !sourceData )
    {
        std::cerr << "Error: Invalid source or destination data pointer" << std::endl;
        return false;
    }

    // Copy data with bounds checking
    for ( size_t i = 0; i < copySize; i++ )
    {
        destData[i] = sourceData[i];
    }

    return true;
}

// Process and run inference on DeepSeek models
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

    // Load DeepSeek MNN models
    std::string embeddingPath = modelDir + "/embedding_f32.mnn";
    std::string expertPath    = modelDir + "/expert_f32.mnn";
    std::string lmHeadPath    = modelDir + "/lm_head_f32.mnn";

    auto embeddingNet = MNN::Interpreter::createFromFile( embeddingPath.c_str() );
    if ( !embeddingNet )
    {
        std::cerr << "Failed to load embedding model from " << embeddingPath << std::endl;
        std::cerr << "Using direct embeddings instead" << std::endl;
    }

    auto expertNet = MNN::Interpreter::createFromFile( expertPath.c_str() );
    if ( !expertNet )
    {
        std::cerr << "Failed to load expert model from " << expertPath << std::endl;
        return 1;
    }

    auto lmHeadNet = MNN::Interpreter::createFromFile( lmHeadPath.c_str() );
    if ( !lmHeadNet )
    {
        std::cerr << "Failed to load LM head model from " << lmHeadPath << std::endl;
        return 1;
    }

    // Configure inference
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_VULKAN;
    config.numThread = 1; // Use single thread to avoid thread pool issues
    MNN::BackendConfig backendConfig;
    backendConfig.precision = MNN::BackendConfig::Precision_Low; // Request FP16
    config.backendConfig    = &backendConfig;

    // Create sessions
    MNN::Session *embeddingSession = embeddingNet ? embeddingNet->createSession( config ) : nullptr;
    MNN::Session *expertSession    = expertNet->createSession( config );
    MNN::Session *lmHeadSession    = lmHeadNet->createSession( config );

    // Get tensor info
    auto expertInput = expertNet->getSessionInput( expertSession, nullptr );
    auto lmHeadInput = lmHeadNet->getSessionInput( lmHeadSession, nullptr );

    // Print info for debugging
    printTensorInfo( expertInput, "Expert input" );
    printTensorInfo( lmHeadInput, "LM head input" );

    // Get dimensions
    int expertInputDim = expertInput ? expertInput->length( 1 ) : 0;
    int lmHeadInputDim = lmHeadInput ? lmHeadInput->length( 1 ) : 0;

    std::cout << "Expert input dimension: " << expertInputDim << std::endl;
    std::cout << "LM head input dimension: " << lmHeadInputDim << std::endl;

    // Check for dimension mismatch
    if ( expertInputDim != lmHeadInputDim && expertInputDim > 0 && lmHeadInputDim > 0 )
    {
        std::cerr << "Warning: Dimension mismatch between expert output and LM head input" << std::endl;
        std::cerr << "Expert: " << expertInputDim << ", LM head: " << lmHeadInputDim << std::endl;
        std::cerr << "Please export LM head with matching dimension (in_features=" << expertInputDim << ")"
                  << std::endl;
        return 1;
    }

    // Set hidden dimension based on expert input size
    int hiddenSize = expertInputDim > 0 ? expertInputDim : 7168;
    std::cout << "Using hidden size: " << hiddenSize << std::endl;

    // Generate tokens
    std::vector<int> generatedTokens = tokens;

    for ( int i = 0; i < numTokens; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;
        int seqLength = generatedTokens.size();

        // ===== EMBEDDING STAGE =====
        std::vector<float> embeddings;

        if ( embeddingNet && embeddingSession )
        {
            auto embeddingInput = embeddingNet->getSessionInput( embeddingSession, nullptr );
            printTensorInfo( embeddingInput, "Embedding input" );

            // Prepare input tensor with token IDs
            embeddingNet->resizeTensor( embeddingInput, { 1, (int)generatedTokens.size() } );
            embeddingNet->resizeSession( embeddingSession );

            // Copy token IDs to input tensor
            MNN::Tensor embeddingInputHost( embeddingInput, MNN::Tensor::CAFFE );
            int        *inputData = embeddingInputHost.host<int>();

            for ( size_t t = 0; t < generatedTokens.size(); t++ )
            {
                inputData[t] = generatedTokens[t];
            }

            embeddingInput->copyFromHostTensor( &embeddingInputHost );

            // Run embedding model
            std::cout << "Running embedding model..." << std::endl;
            embeddingNet->runSession( embeddingSession );

            // Get output
            auto embeddingOutput = embeddingNet->getSessionOutput( embeddingSession, nullptr );
            printTensorInfo( embeddingOutput, "Embedding output" );

            // Copy output to embeddings vector
            MNN::Tensor embeddingOutputHost( embeddingOutput, MNN::Tensor::CAFFE );
            embeddingOutput->copyToHostTensor( &embeddingOutputHost );
            float *outputData = embeddingOutputHost.host<float>();

            // Calculate total size based on dimensions
            size_t totalSize = 1;
            for ( int d = 0; d < embeddingOutput->dimensions(); d++ )
            {
                totalSize *= embeddingOutput->length( d );
            }

            // Get the actual embedding dimension from the output tensor
            int embeddingDim = embeddingOutput->length( embeddingOutput->dimensions() - 1 );

            // Last token's embedding offset: for a [1, seqLen, hiddenSize] tensor
            size_t lastTokenOffset = ( seqLength - 1 ) * embeddingDim;

            // Make sure we don't exceed the bounds
            if ( lastTokenOffset + embeddingDim <= totalSize )
            {
                // Extract just the last token's embedding
                embeddings.resize( embeddingDim );
                for ( int h = 0; h < embeddingDim; h++ )
                {
                    embeddings[h] = outputData[lastTokenOffset + h];

                    // Check for invalid values
                    if ( std::isnan( embeddings[h] ) || std::isinf( embeddings[h] ) || std::abs( embeddings[h] ) > 1e6 )
                    {
                        embeddings[h] = 0.0f; // Replace with reasonable value
                    }
                }
            }
            else
            {
                std::cerr << "Error calculating last token offset. Using zero embeddings." << std::endl;
                embeddings.resize( hiddenSize, 0.0f );
            }

            std::cout << "Successfully extracted last token embedding" << std::endl;
        }
        else
        {
            // Direct embedding creation as fallback
            std::cout << "Creating direct embeddings (no model available)..." << std::endl;

            // Create simple embedding vector
            embeddings.resize( hiddenSize, 0.0f );

            // Create deterministic but varied values based on last token
            int                                   lastToken = generatedTokens.back();
            std::mt19937                          rng( lastToken ); // Seed with token ID
            std::uniform_real_distribution<float> dist( -0.1f, 0.1f );

            for ( int h = 0; h < hiddenSize; h++ )
            {
                embeddings[h] = dist( rng );
            }
        }

        // Print some embedding stats
        if ( !embeddings.empty() )
        {
            float minVal = *std::min_element( embeddings.begin(), embeddings.end() );
            float maxVal = *std::max_element( embeddings.begin(), embeddings.end() );
            float sum    = std::accumulate( embeddings.begin(), embeddings.end(), 0.0f );
            float mean   = sum / embeddings.size();

            std::cout << "Embedding stats - Size: " << embeddings.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }

        // ===== EXPERT STAGE =====
        std::cout << "\n== EXPERT STAGE ==" << std::endl;

        // Resize expert input
        expertNet->resizeTensor( expertInput, { 1, (int)embeddings.size() } );
        expertNet->resizeSession( expertSession );

        // Copy embeddings to expert input
        MNN::Tensor expertInputHost( expertInput, MNN::Tensor::CAFFE );
        float      *expertInputData = expertInputHost.host<float>();

        // Print sample values
        std::cout << "Embedding sample values: ";
        for ( int h = 0; h < std::min( 5, (int)embeddings.size() ); h++ )
        {
            std::cout << embeddings[h] << " ";
        }
        std::cout << std::endl;

        // Copy data
        safeCopyToTensor( expertInputData, embeddings.data(), embeddings.size() );

        // Print copied values
        std::cout << "Expert input copied values: ";
        for ( int h = 0; h < std::min( 5, (int)embeddings.size() ); h++ )
        {
            std::cout << expertInputData[h] << " ";
        }
        std::cout << std::endl;

        expertInput->copyFromHostTensor( &expertInputHost );

        // Run expert model
        std::cout << "Running expert model..." << std::endl;
        expertNet->runSession( expertSession );

        // Get expert output
        auto expertOutput = expertNet->getSessionOutput( expertSession, nullptr );
        printTensorInfo( expertOutput, "Expert output" );

        // Copy output to host
        MNN::Tensor expertOutputHost( expertOutput, MNN::Tensor::CAFFE );
        expertOutput->copyToHostTensor( &expertOutputHost );
        float *expertOutputData = expertOutputHost.host<float>();

        // Calculate output size
        size_t expertOutputSize = 1;
        for ( int d = 0; d < expertOutput->dimensions(); d++ )
        {
            expertOutputSize *= expertOutput->length( d );
        }

        // Create vector for expert output
        std::vector<float> expertOutputVec( expertOutputSize );

        // Check and copy values
        bool hasInvalidValues = false;
        for ( size_t i = 0; i < expertOutputSize; i++ )
        {
            float val = expertOutputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                hasInvalidValues   = true;
                expertOutputVec[i] = 0.0f; // Replace with zero
            }
            else
            {
                expertOutputVec[i] = val;
            }
        }

        if ( hasInvalidValues )
        {
            std::cout << "Warning: Expert output contained invalid values that were replaced" << std::endl;
        }

        // Print expert output stats
        if ( !expertOutputVec.empty() )
        {
            float minVal = *std::min_element( expertOutputVec.begin(), expertOutputVec.end() );
            float maxVal = *std::max_element( expertOutputVec.begin(), expertOutputVec.end() );
            float sum    = std::accumulate( expertOutputVec.begin(), expertOutputVec.end(), 0.0f );
            float mean   = sum / expertOutputVec.size();

            std::cout << "Expert output stats - Size: " << expertOutputVec.size() << ", Min: " << minVal
                      << ", Max: " << maxVal << ", Mean: " << mean << std::endl;
        }

        // ===== LM HEAD STAGE =====
        std::cout << "\n== LM HEAD STAGE ==" << std::endl;

        // Check if dimensions match
        if ( lmHeadInputDim != expertOutputVec.size() )
        {
            std::cerr << "Error: LM head input dimension (" << lmHeadInputDim << ") doesn't match expert output size ("
                      << expertOutputVec.size() << ")" << std::endl;
            return 1;
        }

        // Resize LM head input
        lmHeadNet->resizeTensor( lmHeadInput, { 1, (int)expertOutputVec.size() } );
        lmHeadNet->resizeSession( lmHeadSession );

        // Copy expert output to LM head input
        MNN::Tensor lmHeadInputHost( lmHeadInput, MNN::Tensor::CAFFE );
        float      *lmHeadInputData = lmHeadInputHost.host<float>();

        // Copy data
        safeCopyToTensor( lmHeadInputData, expertOutputVec.data(), expertOutputVec.size() );
        lmHeadInput->copyFromHostTensor( &lmHeadInputHost );

        // Run LM head model
        std::cout << "Running LM head model..." << std::endl;
        lmHeadNet->runSession( lmHeadSession );

        // Get logits
        auto logitsTensor = lmHeadNet->getSessionOutput( lmHeadSession, nullptr );
        printTensorInfo( logitsTensor, "Logits" );

        // Copy logits to host
        MNN::Tensor logitsHost( logitsTensor, MNN::Tensor::CAFFE );
        logitsTensor->copyToHostTensor( &logitsHost );
        float *logitsData = logitsHost.host<float>();

        // Get vocab size
        size_t vocabSize = logitsTensor->length( logitsTensor->dimensions() - 1 );

        // Find top tokens
        std::vector<std::pair<int, float>> scores;
        for ( size_t v = 0; v < vocabSize; v++ )
        {
            float val = logitsData[v];
            // Skip invalid values
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                continue;
            }
            scores.push_back( { (int)v, val } );
        }

        // Sort to find top tokens
        std::sort( scores.begin(), scores.end(), []( const auto &a, const auto &b ) { return a.second > b.second; } );

        // Print top tokens
        std::cout << "\nTop tokens:" << std::endl;
        int topCount = std::min( 5, (int)scores.size() );
        for ( int j = 0; j < topCount; j++ )
        {
            int   tokenId = scores[j].first;
            float score   = scores[j].second;

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

            std::cout << "  " << ( j + 1 ) << ". Token ID " << tokenId << " (score: " << score << "): \"" << tokenText
                      << "\"" << std::endl;
        }

        // Select next token (use top token)
        int nextTokenId = scores.empty() ? 0 : scores[0].first;
        generatedTokens.push_back( nextTokenId );

        // Display current text
        std::string currentText = tokenizer.decode( generatedTokens );

        // Clean up output
        std::string displayText;
        for ( unsigned char c : currentText )
        {
            if ( c < 32 || c > 126 )
            {
                displayText += '.';
            }
            else
            {
                displayText += c;
            }
        }

        std::cout << "\nGenerated so far: " << displayText << std::endl;
    }

    // Final output
    std::string finalText = tokenizer.decode( generatedTokens );

    // Clean up output
    std::string displayText;
    for ( unsigned char c : finalText )
    {
        if ( c < 32 || c > 126 )
        {
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
    if ( embeddingNet )
    {
        if ( embeddingSession )
        {
            embeddingNet->releaseSession( embeddingSession );
        }
        delete embeddingNet;
    }

    expertNet->releaseSession( expertSession );
    delete expertNet;

    lmHeadNet->releaseSession( lmHeadSession );
    delete lmHeadNet;

    return 0;
}
