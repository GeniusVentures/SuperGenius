#include "Utility.hpp"

void LLMUtility::printTensorInfo( MNN::Tensor *tensor, const std::string &name )
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
bool LLMUtility::safeCopyToTensor( float *destData, const std::vector<float> &sourceData )
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
void LLMUtility::saveTensorToFile( MNN::Tensor *tensor, const std::string &filename )
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
std::vector<float> LLMUtility::loadTensorFromFile( const std::string &filename, std::vector<int> &dimensions )
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
std::vector<float> LLMUtility::createSyntheticEmbedding( int tokenId, int hiddenSize )
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

// Legacy function to run LM head model
int LLMUtility::runLmHeadModelLegacy( MNN::Interpreter         *lmHeadNet,
                                 const std::vector<float> &expertOutput,
                                 BpeTokenizer             *tokenizer )
{
    try
    {
        // Configure inference
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN;
        config.numThread = 1;

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

std::string LLMUtility::getFp16Path( const std::string &originalPath, bool useFp16 )
{
    if ( !useFp16 )
    {
        return originalPath;
    }

    // Find the last dot for file extension
    size_t lastDot = originalPath.find_last_of( '.' );
    if ( lastDot == std::string::npos )
    {
        // No extension found, just append _fp16
        return originalPath + "_fp16";
    }

    // Insert _fp16 before the extension
    std::string basePath  = originalPath.substr( 0, lastDot );
    std::string extension = originalPath.substr( lastDot );
    return basePath + "_fp16" + extension;
}
