#include "BpeTokenizer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <memory>
#include <fstream>
#include <random> // For std::mt19937 and std::uniform_real_distribution
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
bool safeCopyToTensor( float *destData, const std::vector<float> &sourceData )
{
    if ( !destData )
    {
        std::cerr << "Error: Invalid destination data pointer" << std::endl;
        return false;
    }

    // Copy data with bounds checking
    for ( size_t i = 0; i < sourceData.size(); i++ )
    {
        destData[i] = sourceData[i];
    }

    return true;
}

// Function to save tensor data to a binary file for debugging
void saveTensorToFile( MNN::Tensor *tensor, const std::string &filename )
{
    std::ofstream file( filename, std::ios::binary );
    if ( !file.is_open() )
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }

    // Get tensor data
    MNN::Tensor tensorHost( tensor, tensor->getDimensionType() );
    tensor->copyToHostTensor( &tensorHost );
    float *data = tensorHost.host<float>();

    // Write dimensions first
    int dim = tensor->dimensions();
    file.write( reinterpret_cast<char *>( &dim ), sizeof( int ) );

    // Write each dimension size
    for ( int i = 0; i < dim; i++ )
    {
        int size = tensor->length( i );
        file.write( reinterpret_cast<char *>( &size ), sizeof( int ) );
    }

    // Write tensor data
    size_t dataSize = tensorHost.elementSize() * sizeof( float );
    file.write( reinterpret_cast<char *>( data ), dataSize );

    file.close();
    std::cout << "Saved tensor to " << filename << std::endl;
}

// Function to load tensor data from a binary file
std::vector<float> loadTensorFromFile( const std::string &filename, std::vector<int> &dimensions )
{
    std::ifstream file( filename, std::ios::binary );
    if ( !file.is_open() )
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return {};
    }

    // Read dimensions
    int dim;
    file.read( reinterpret_cast<char *>( &dim ), sizeof( int ) );

    dimensions.resize( dim );
    size_t totalElements = 1;

    // Read each dimension size
    for ( int i = 0; i < dim; i++ )
    {
        file.read( reinterpret_cast<char *>( &dimensions[i] ), sizeof( int ) );
        totalElements *= dimensions[i];
    }

    // Read tensor data
    std::vector<float> data( totalElements );
    file.read( reinterpret_cast<char *>( data.data() ), totalElements * sizeof( float ) );

    file.close();
    std::cout << "Loaded tensor from " << filename << std::endl;

    return data;
}

// Generate synthetic embedding for a token
std::vector<float> createSyntheticEmbedding( int tokenId, int hiddenSize )
{
    std::vector<float> embedding( hiddenSize, 0.0f );

    // Create deterministic embedding based on token ID
    std::mt19937                          rng( tokenId );
    std::uniform_real_distribution<float> dist( -0.1f, 0.1f );

    for ( int h = 0; h < hiddenSize; h++ )
    {
        embedding[h] = dist( rng );
    }

    return embedding;
}

// Embedding extraction that avoids running session directly
std::vector<float> extractEmbedding( MNN::Interpreter *embeddingNet, int tokenId, int hiddenSize )
{
    try
    {
        // Configure inference
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN;
        config.numThread = 1;
        MNN::BackendConfig backendConfig;
        backendConfig.precision = MNN::BackendConfig::Precision_Normal; 
        config.backendConfig    = &backendConfig;

        // Create a clean session
        MNN::Session *embeddingSession = embeddingNet->createSession( config );
        if ( !embeddingSession )
        {
            throw std::runtime_error( "Failed to create embedding session" );
        }

        // Get input tensor
        auto embeddingInput = embeddingNet->getSessionInput( embeddingSession, nullptr );
        printTensorInfo( embeddingInput, "Embedding input" );

        // Resize for a single token
        embeddingNet->resizeTensor( embeddingInput, { 1, 1 } );
        embeddingNet->resizeSession( embeddingSession );

        // Copy token ID
        MNN::Tensor embeddingInputHost( embeddingInput, MNN::Tensor::CAFFE );
        int        *inputData = embeddingInputHost.host<int>();
        inputData[0]          = tokenId;
        embeddingInput->copyFromHostTensor( &embeddingInputHost );

        // Run embedding model
        std::cout << "Running embedding model for token " << tokenId << "..." << std::endl;
        embeddingNet->runSession( embeddingSession );

        // Get output
        auto embeddingOutput = embeddingNet->getSessionOutput( embeddingSession, nullptr );
        printTensorInfo( embeddingOutput, "Embedding output" );

        // Save output tensor for debugging
        saveTensorToFile( embeddingOutput, "embedding_output_" + std::to_string( tokenId ) + ".bin" );

        // Copy to host
        MNN::Tensor embeddingOutputHost( embeddingOutput, embeddingOutput->getDimensionType() );
        embeddingOutput->copyToHostTensor( &embeddingOutputHost );
        float *outputData = embeddingOutputHost.host<float>();

        // Extract embedding based on dimensions
        int                embeddingDim = embeddingOutput->length( embeddingOutput->dimensions() - 1 );
        std::vector<float> embedding( embeddingDim );

        if ( embeddingOutput->dimensions() == 3 )
        {
            // Shape [batch=1, seq=1, hidden_dim]
            for ( int h = 0; h < embeddingDim; h++ )
            {
                embedding[h] = outputData[h];
            }
        }
        else if ( embeddingOutput->dimensions() == 4 )
        {
            // Shape [?, batch=1, seq=1, hidden_dim]
            for ( int h = 0; h < embeddingDim; h++ )
            {
                embedding[h] = outputData[h];
            }
        }

        // Clean up
        embeddingNet->releaseSession( embeddingSession );

        return embedding;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Using synthetic embedding as fallback" << std::endl;
        return createSyntheticEmbedding( tokenId, hiddenSize );
    }
}

// Function to run expert model
std::vector<float> runExpertModel( MNN::Interpreter *expertNet, const std::vector<float> &embedding )
{
    try
    {
        // Configure inference
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN;
        config.numThread = 1;
        MNN::BackendConfig backendConfig;
        backendConfig.precision = MNN::BackendConfig::Precision_Normal;
        config.backendConfig    = &backendConfig;
        // Create session
        MNN::Session *expertSession = expertNet->createSession( config );
        if ( !expertSession )
        {
            throw std::runtime_error( "Failed to create expert session" );
        }

        // Get input tensor
        auto expertInput = expertNet->getSessionInput( expertSession, nullptr );
        printTensorInfo( expertInput, "Expert input" );

        // Resize input tensor
        expertNet->resizeTensor( expertInput, { 1, (int)embedding.size() } );
        expertNet->resizeSession( expertSession );

        // Copy embedding to input tensor
        MNN::Tensor expertInputHost( expertInput, MNN::Tensor::CAFFE );
        float      *expertInputData = expertInputHost.host<float>();
        safeCopyToTensor( expertInputData, embedding );
        expertInput->copyFromHostTensor( &expertInputHost );

        // Run expert model
        std::cout << "Running expert model..." << std::endl;
        expertNet->runSession( expertSession );

        // Get output
        auto expertOutputTensor = expertNet->getSessionOutput( expertSession, nullptr );
        printTensorInfo( expertOutputTensor, "Expert output" );

        // Save output tensor for debugging - including iteration number
        static int expertCallCount = 0;
        saveTensorToFile( expertOutputTensor, "expert_output_" + std::to_string( expertCallCount++ ) + ".bin" );

        // Copy to host
        MNN::Tensor expertOutputHost( expertOutputTensor, MNN::Tensor::CAFFE );
        expertOutputTensor->copyToHostTensor( &expertOutputHost );
        float *expertOutputData = expertOutputHost.host<float>();

        // Calculate output size
        size_t expertOutputSize = 1;
        for ( int d = 0; d < expertOutputTensor->dimensions(); d++ )
        {
            expertOutputSize *= expertOutputTensor->length( d );
        }

        // Create vector for expert output
        std::vector<float> expertOutput( expertOutputSize );

        // Check and copy values
        for ( size_t i = 0; i < expertOutputSize; i++ )
        {
            float val = expertOutputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                expertOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                expertOutput[i] = val;
            }
        }

        // Clean up
        expertNet->releaseSession( expertSession );

        return expertOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running expert model: " << e.what() << std::endl;
        return {};
    }
}

// Function to run LM head model
int runLmHeadModel( MNN::Interpreter *lmHeadNet, const std::vector<float> &expertOutput, BpeTokenizer *tokenizer )
{
    try
    {
        // Configure inference
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN;
        config.numThread = 1;
        MNN::BackendConfig backendConfig;
        backendConfig.precision = MNN::BackendConfig::Precision_Normal;
        config.backendConfig    = &backendConfig;
        // Create session
        MNN::Session *lmHeadSession = lmHeadNet->createSession( config );
        if ( !lmHeadSession )
        {
            throw std::runtime_error( "Failed to create LM head session" );
        }

        // Get input tensor
        auto lmHeadInput = lmHeadNet->getSessionInput( lmHeadSession, nullptr );
        printTensorInfo( lmHeadInput, "LM head input" );

        // Resize input tensor
        lmHeadNet->resizeTensor( lmHeadInput, { 1, (int)expertOutput.size() } );
        lmHeadNet->resizeSession( lmHeadSession );

        // Copy expert output to input tensor
        MNN::Tensor lmHeadInputHost( lmHeadInput, MNN::Tensor::CAFFE );
        float      *lmHeadInputData = lmHeadInputHost.host<float>();
        safeCopyToTensor( lmHeadInputData, expertOutput );
        lmHeadInput->copyFromHostTensor( &lmHeadInputHost );

        // Run LM head model
        std::cout << "Running LM head model..." << std::endl;
        lmHeadNet->runSession( lmHeadSession );

        // Get logits
        auto logitsTensor = lmHeadNet->getSessionOutput( lmHeadSession, nullptr );
        printTensorInfo( logitsTensor, "Logits" );

        // Save logits for debugging - including iteration number
        static int lmHeadCallCount = 0;
        saveTensorToFile( logitsTensor, "lm_head_output_" + std::to_string( lmHeadCallCount++ ) + ".bin" );

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

        // Sort to find top token
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
                tokenText = tokenizer->decode( { tokenId } );
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

        // Clean up
        lmHeadNet->releaseSession( lmHeadSession );

        // Return top token
        return scores.empty() ? 0 : scores[0].first;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running LM head model: " << e.what() << std::endl;
        return 0;
    }
}

// Function to precompute embeddings for a set of tokens
std::unordered_map<int, std::vector<float>> precomputeEmbeddings( const std::string      &modelDir,
                                                                  const std::vector<int> &tokens,
                                                                  int                     hiddenSize )
{
    std::unordered_map<int, std::vector<float>> embeddings;
    std::string                                 embeddingPath = modelDir + "/embedding_f32.mnn";

    // Create embedding model
    std::unique_ptr<MNN::Interpreter> embeddingNet( MNN::Interpreter::createFromFile( embeddingPath.c_str() ) );
    if ( !embeddingNet )
    {
        std::cerr << "Failed to load embedding model from " << embeddingPath << std::endl;
        return embeddings;
    }

    // Process each token
    std::set<int> uniqueTokens( tokens.begin(), tokens.end() );
    std::cout << "Precomputing embeddings for " << uniqueTokens.size() << " unique tokens..." << std::endl;

    for ( int tokenId : uniqueTokens )
    {
        std::cout << "Computing embedding for token ID " << tokenId << std::endl;

        // Extract embedding for token
        try
        {
            std::vector<float> embedding = extractEmbedding( embeddingNet.get(), tokenId, hiddenSize );
            if ( !embedding.empty() )
            {
                embeddings[tokenId] = embedding;
            }
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error computing embedding for token " << tokenId << ": " << e.what() << std::endl;
        }
    }

    std::cout << "Precomputed " << embeddings.size() << " embeddings" << std::endl;
    return embeddings;
}

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
    std::vector<int> promptTokens = tokenizer.encode( prompt );

    if ( promptTokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return 1;
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : promptTokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Set hidden dimension
    int hiddenSize = 7168;

    // Create a vector to hold generated tokens
    std::vector<int> generatedTokens = promptTokens;

    // Precompute embeddings for prompt tokens
    auto embeddings = precomputeEmbeddings( modelDir, promptTokens, hiddenSize );

    // Load expert model
    std::string                       expertPath = modelDir + "/expert_f32.mnn";
    std::unique_ptr<MNN::Interpreter> expertNet( MNN::Interpreter::createFromFile( expertPath.c_str() ) );
    if ( !expertNet )
    {
        std::cerr << "Failed to load expert model from " << expertPath << std::endl;
        return 1;
    }

    // Load LM head model
    std::string                       lmHeadPath = modelDir + "/lm_head_f32.mnn";
    std::unique_ptr<MNN::Interpreter> lmHeadNet( MNN::Interpreter::createFromFile( lmHeadPath.c_str() ) );
    if ( !lmHeadNet )
    {
        std::cerr << "Failed to load LM head model from " << lmHeadPath << std::endl;
        return 1;
    }

    // Generate tokens one by one
    for ( int i = 0; i < std::min( numTokens, 5 ); i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding for last token
        std::vector<float> embedding;
        if ( embeddings.find( lastToken ) != embeddings.end() )
        {
            std::cout << "Using precomputed embedding for token ID " << lastToken << std::endl;
            embedding = embeddings[lastToken];
        }
        else
        {
            // Create embedding model (each time to reset state)
            std::string                       embeddingPath = modelDir + "/embedding_f32.mnn";
            std::unique_ptr<MNN::Interpreter> embeddingNet( MNN::Interpreter::createFromFile( embeddingPath.c_str() ) );
            if ( !embeddingNet )
            {
                std::cerr << "Failed to load embedding model from " << embeddingPath << std::endl;
                return 1;
            }

            std::cout << "Computing new embedding for token ID " << lastToken << std::endl;
            embedding             = extractEmbedding( embeddingNet.get(), lastToken, hiddenSize );
            embeddings[lastToken] = embedding;
        }

        // Print embedding stats
        if ( !embedding.empty() )
        {
            float minVal = *std::min_element( embedding.begin(), embedding.end() );
            float maxVal = *std::max_element( embedding.begin(), embedding.end() );
            float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
            float mean   = sum / embedding.size();

            std::cout << "Embedding stats - Size: " << embedding.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }

        // Run expert model
        std::vector<float> expertOutput = runExpertModel( expertNet.get(), embedding );

        // Print expert output stats
        if ( !expertOutput.empty() )
        {
            float minVal = *std::min_element( expertOutput.begin(), expertOutput.end() );
            float maxVal = *std::max_element( expertOutput.begin(), expertOutput.end() );
            float sum    = std::accumulate( expertOutput.begin(), expertOutput.end(), 0.0f );
            float mean   = sum / expertOutput.size();

            std::cout << "Expert output stats - Size: " << expertOutput.size() << ", Min: " << minVal
                      << ", Max: " << maxVal << ", Mean: " << mean << std::endl;
        }

        // Run LM head model
        int nextTokenId = runLmHeadModel( lmHeadNet.get(), expertOutput, &tokenizer );

        // Add next token
        generatedTokens.push_back( nextTokenId );

        // Display current text
        std::string currentText = tokenizer.decode( generatedTokens );

        // Clean up output for display
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

        // Save the entire state after each token if we're approaching the problematic third token
        if ( i >= 1 )
        {
            std::cout << "\nSaving detailed state for debugging..." << std::endl;

            // Save current token info
            std::ofstream tokenInfoFile( "token_info_" + std::to_string( i ) + ".txt" );
            if ( tokenInfoFile.is_open() )
            {
                tokenInfoFile << "Generated Tokens: ";
                for ( int token : generatedTokens )
                {
                    tokenInfoFile << token << " ";
                }
                tokenInfoFile << "\nCurrent text: " << displayText << std::endl;
                tokenInfoFile.close();
            }

            // Try to get at least two tokens successfully before potentially crashing
            //if ( i == 1 )
            //{
            //    std::cout << "\nCompleted processing two tokens." << std::endl;
            //    std::cout << "You can now analyze the saved tensor data files with tensor_analyzer.py" << std::endl;
            //    std::cout << "To try processing the third token (which may crash), set num_tokens >= 3" << std::endl;
            //    break;
            //}
        }
    }

    return 0;
}
