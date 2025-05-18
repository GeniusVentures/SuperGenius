#include "BpeTokenizer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <memory>
#include <fstream>
#include <sstream>
#include <unordered_map>
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

// Simple JSON parser for loading metadata
class SimpleJsonParser
{
public:
    static std::unordered_map<std::string, std::string> parseJson( const std::string &jsonString )
    {
        std::unordered_map<std::string, std::string> result;

        // Very simple parsing for key-value pairs in JSON
        // This is not a full JSON parser, just enough for our metadata
        std::string::size_type pos = 0;
        while ( pos < jsonString.size() )
        {
            // Find next key
            pos = jsonString.find( "\"", pos );
            if ( pos == std::string::npos )
            {
                break;
            }

            std::string::size_type keyStart = pos + 1;
            pos                             = jsonString.find( "\"", keyStart );
            if ( pos == std::string::npos )
            {
                break;
            }

            std::string key = jsonString.substr( keyStart, pos - keyStart );
            pos++;

            // Find value
            pos = jsonString.find( ":", pos );
            if ( pos == std::string::npos )
            {
                break;
            }
            pos++;

            // Skip whitespace
            while ( pos < jsonString.size() && ( jsonString[pos] == ' ' || jsonString[pos] == '\t' ) )
            {
                pos++;
            }

            std::string value;
            if ( pos < jsonString.size() )
            {
                if ( jsonString[pos] == '\"' )
                {
                    // String value
                    std::string::size_type valueStart = pos + 1;
                    pos                               = jsonString.find( "\"", valueStart );
                    if ( pos == std::string::npos )
                    {
                        break;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart );
                    pos++;
                }
                else if ( jsonString[pos] == '{' || jsonString[pos] == '[' )
                {
                    // Object or array - skip for now
                    int depth = 1;
                    pos++;
                    std::string::size_type valueStart = pos;
                    while ( pos < jsonString.size() && depth > 0 )
                    {
                        if ( jsonString[pos] == '{' || jsonString[pos] == '[' )
                        {
                            depth++;
                        }
                        else if ( jsonString[pos] == '}' || jsonString[pos] == ']' )
                        {
                            depth--;
                        }
                        pos++;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart - 1 );
                }
                else
                {
                    // Number, boolean, or null
                    std::string::size_type valueStart = pos;
                    while ( pos < jsonString.size() && jsonString[pos] != ',' && jsonString[pos] != '}' &&
                            jsonString[pos] != ']' )
                    {
                        pos++;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart );
                    value.erase( 0, value.find_first_not_of( " \t" ) );
                    value.erase( value.find_last_not_of( " \t," ) + 1 );
                }
            }

            result[key] = value;
        }

        return result;
    }

    static std::vector<std::unordered_map<std::string, std::string>> parseJsonArray( const std::string &jsonString,
                                                                                     const std::string &arrayName )
    {
        std::vector<std::unordered_map<std::string, std::string>> result;

        // Find array
        std::string            searchStr = "\"" + arrayName + "\"";
        std::string::size_type pos       = jsonString.find( searchStr );
        if ( pos == std::string::npos )
        {
            return result;
        }

        // Find array start
        pos = jsonString.find( "[", pos );
        if ( pos == std::string::npos )
        {
            return result;
        }
        pos++;

        // Parse array elements
        while ( pos < jsonString.size() )
        {
            // Skip whitespace
            while ( pos < jsonString.size() && ( jsonString[pos] == ' ' || jsonString[pos] == '\t' ||
                                                 jsonString[pos] == '\n' || jsonString[pos] == ',' ) )
            {
                pos++;
            }

            if ( pos >= jsonString.size() || jsonString[pos] == ']' )
            {
                break;
            }

            if ( jsonString[pos] == '{' )
            {
                // Found object
                std::string::size_type objectStart = pos;
                int                    depth       = 1;
                pos++;

                while ( pos < jsonString.size() && depth > 0 )
                {
                    if ( jsonString[pos] == '{' )
                    {
                        depth++;
                    }
                    else if ( jsonString[pos] == '}' )
                    {
                        depth--;
                    }
                    pos++;
                }

                if ( depth == 0 )
                {
                    std::string objectStr = jsonString.substr( objectStart + 1, pos - objectStart - 2 );
                    std::unordered_map<std::string, std::string> obj = parseJson( "{" + objectStr + "}" );
                    result.push_back( obj );
                }
            }
            else
            {
                // Skip non-object
                while ( pos < jsonString.size() && jsonString[pos] != ',' && jsonString[pos] != ']' )
                {
                    pos++;
                }
            }
        }

        return result;
    }

    static int getIntValue( const std::unordered_map<std::string, std::string> &json,
                            const std::string                                  &key,
                            int                                                 defaultValue = 0 )
    {
        auto it = json.find( key );
        if ( it == json.end() )
        {
            return defaultValue;
        }

        try
        {
            return std::stoi( it->second );
        }
        catch ( ... )
        {
            return defaultValue;
        }
    }

    static std::string getStringValue( const std::unordered_map<std::string, std::string> &json,
                                       const std::string                                  &key,
                                       const std::string                                  &defaultValue = "" )
    {
        auto it = json.find( key );
        if ( it == json.end() )
        {
            return defaultValue;
        }
        return it->second;
    }

    static bool getBoolValue( const std::unordered_map<std::string, std::string> &json,
                              const std::string                                  &key,
                              bool                                                defaultValue = false )
    {
        auto it = json.find( key );
        if ( it == json.end() )
        {
            return defaultValue;
        }

        std::string value = it->second;
        return value == "true" || value == "1";
    }
};

// SplitEmbeddingLoader class
class SplitEmbeddingLoader
{
private:
    struct ChunkInfo
    {
        int                               chunkIndex;
        int                               startTokenId;
        int                               endTokenId;
        std::string                       modelPath;
        std::unique_ptr<MNN::Interpreter> interpreter;
        MNN::Session                     *session;

        ChunkInfo() : chunkIndex( -1 ), startTokenId( -1 ), endTokenId( -1 ), session( nullptr ) {}

        // Explicitly define move constructor and assignment
        ChunkInfo( ChunkInfo &&other ) noexcept :
            chunkIndex( other.chunkIndex ),
            startTokenId( other.startTokenId ),
            endTokenId( other.endTokenId ),
            modelPath( std::move( other.modelPath ) ),
            interpreter( std::move( other.interpreter ) ),
            session( other.session )
        {
            other.session = nullptr;
        }

        ChunkInfo &operator=( ChunkInfo &&other ) noexcept
        {
            if ( this != &other )
            {
                chunkIndex    = other.chunkIndex;
                startTokenId  = other.startTokenId;
                endTokenId    = other.endTokenId;
                modelPath     = std::move( other.modelPath );
                interpreter   = std::move( other.interpreter );
                session       = other.session;
                other.session = nullptr;
            }
            return *this;
        }

        // Delete copy constructor and assignment
        ChunkInfo( const ChunkInfo & )            = delete;
        ChunkInfo &operator=( const ChunkInfo & ) = delete;
    };

    std::vector<ChunkInfo> chunks;
    std::string            modelDir;
    int                    embeddingDim;
    bool                   initialized;
    MNN::ScheduleConfig    config;

public:
    SplitEmbeddingLoader() : initialized( false ), embeddingDim( 0 )
    {
        // Default config
        config.type      = MNN_FORWARD_VULKAN; // Use VULKAN by default, change as needed
        config.numThread = 1;
    }

    ~SplitEmbeddingLoader()
    {
        // Clean up sessions
        for ( auto &chunk : chunks )
        {
            if ( chunk.interpreter && chunk.session )
            {
                chunk.interpreter->releaseSession( chunk.session );
                chunk.session = nullptr;
            }
        }
    }

    bool initialize( const std::string &modelDir )
    {
        this->modelDir = modelDir;

        // Load metadata file
        std::string   metadataPath = modelDir + "/embedding_chunks_metadata.json";
        std::ifstream metadataFile( metadataPath );
        if ( !metadataFile.is_open() )
        {
            std::cerr << "Failed to open metadata file: " << metadataPath << std::endl;
            return false;
        }

        // Read entire file
        std::stringstream buffer;
        buffer << metadataFile.rdbuf();
        metadataFile.close();
        std::string jsonString = buffer.str();

        try
        {
            // Parse basic metadata
            auto metadata = SimpleJsonParser::parseJson( jsonString );
            embeddingDim  = SimpleJsonParser::getIntValue( metadata, "embedding_dim" );

            // Parse chunks array
            auto chunksData = SimpleJsonParser::parseJsonArray( jsonString, "token_ranges" );

            // Reserve space
            chunks.resize( chunksData.size() );

            // Load chunk information
            for ( const auto &chunkData : chunksData )
            {
                int idx = SimpleJsonParser::getIntValue( chunkData, "chunk_index" );

                // Create temp info and set properties
                ChunkInfo info;
                info.chunkIndex   = idx;
                info.startTokenId = SimpleJsonParser::getIntValue( chunkData, "start_token_id" );
                info.endTokenId   = SimpleJsonParser::getIntValue( chunkData, "end_token_id" );
                info.modelPath    = modelDir + "/" + SimpleJsonParser::getStringValue( chunkData, "model_path" );

                // Replace .onnx with .mnn if needed
                if ( info.modelPath.size() > 5 && info.modelPath.substr( info.modelPath.size() - 5 ) == ".onnx" )
                {
                    info.modelPath = info.modelPath.substr( 0, info.modelPath.size() - 5 ) + ".mnn";
                }

                // Store in proper index using move assignment
                if ( idx >= 0 && idx < (int)chunks.size() )
                {
                    chunks[idx] = std::move( info );
                }
            }
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error parsing metadata: " << e.what() << std::endl;
            return false;
        }

        std::cout << "Loaded embedding metadata with " << chunks.size() << " chunks" << std::endl;
        std::cout << "Embedding dimension: " << embeddingDim << std::endl;

        initialized = true;
        return true;
    }

    // Load a specific chunk model when needed
    bool loadChunkModel( int chunkIndex )
    {
        if ( !initialized )
        {
            return false;
        }
        if ( chunkIndex < 0 || chunkIndex >= (int)chunks.size() )
        {
            return false;
        }

        // Skip if already loaded with active session
        if ( chunks[chunkIndex].interpreter && chunks[chunkIndex].session )
        {
            return true;
        }

        try
        {
            // Create interpreter if needed
            if ( !chunks[chunkIndex].interpreter )
            {
                chunks[chunkIndex].interpreter.reset(
                    MNN::Interpreter::createFromFile( chunks[chunkIndex].modelPath.c_str() ) );
                if ( !chunks[chunkIndex].interpreter )
                {
                    std::cerr << "Failed to load chunk model: " << chunks[chunkIndex].modelPath << std::endl;
                    return false;
                }
            }

            // Create session
            chunks[chunkIndex].session = chunks[chunkIndex].interpreter->createSession( config );
            if ( !chunks[chunkIndex].session )
            {
                std::cerr << "Failed to create session for chunk " << chunkIndex << std::endl;
                return false;
            }

            std::cout << "Loaded chunk model " << chunkIndex << " from " << chunks[chunkIndex].modelPath << std::endl;
            return true;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error loading chunk model: " << e.what() << std::endl;
            return false;
        }
    }

    // Extract embedding for a token
    std::vector<float> extractEmbedding( int tokenId )
    {
        if ( !initialized )
        {
            std::cerr << "Embedding loader not initialized" << std::endl;
            return {};
        }

        // Find the right chunk for this token
        int chunkIndex   = -1;
        int localTokenId = -1;

        for ( size_t i = 0; i < chunks.size(); ++i )
        {
            if ( tokenId >= chunks[i].startTokenId && tokenId <= chunks[i].endTokenId )
            {
                chunkIndex   = i;
                localTokenId = tokenId - chunks[i].startTokenId;
                break;
            }
        }

        if ( chunkIndex == -1 )
        {
            std::cerr << "Token ID " << tokenId << " not found in any chunk" << std::endl;
            return {};
        }

        // Load the model if needed
        if ( !loadChunkModel( chunkIndex ) )
        {
            std::cerr << "Failed to load model for chunk " << chunkIndex << std::endl;
            return {};
        }

        try
        {
            // Get input tensor
            auto input = chunks[chunkIndex].interpreter->getSessionInput( chunks[chunkIndex].session, nullptr );
            if ( !input )
            {
                throw std::runtime_error( "Failed to get input tensor" );
            }

            // Resize for a single token
            chunks[chunkIndex].interpreter->resizeTensor( input, { 1, 1 } );
            chunks[chunkIndex].interpreter->resizeSession( chunks[chunkIndex].session );

            // Copy token ID (using local ID for this chunk)
            MNN::Tensor inputHost( input, MNN::Tensor::CAFFE );
            int        *inputData = inputHost.host<int>();
            inputData[0]          = localTokenId;
            input->copyFromHostTensor( &inputHost );

            // Run model
            chunks[chunkIndex].interpreter->runSession( chunks[chunkIndex].session );

            // Get output
            auto output = chunks[chunkIndex].interpreter->getSessionOutput( chunks[chunkIndex].session, nullptr );
            if ( !output )
            {
                throw std::runtime_error( "Failed to get output tensor" );
            }

            // Copy to host
            MNN::Tensor outputHost( output, output->getDimensionType() );
            output->copyToHostTensor( &outputHost );
            float *outputData = outputHost.host<float>();

            // Extract embedding
            std::vector<float> embedding( embeddingDim );
            for ( int i = 0; i < embeddingDim; ++i )
            {
                embedding[i] = outputData[i];
            }

            return embedding;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error extracting embedding: " << e.what() << std::endl;
            return {};
        }
    }

    // Utility to print chunk information
    void printChunkInfo()
    {
        if ( !initialized )
        {
            std::cerr << "Embedding loader not initialized" << std::endl;
            return;
        }

        std::cout << "Embedding Loader Chunks:" << std::endl;
        for ( const auto &chunk : chunks )
        {
            std::cout << "  Chunk " << chunk.chunkIndex << ": Token IDs " << chunk.startTokenId << " to "
                      << chunk.endTokenId << " (" << ( chunk.endTokenId - chunk.startTokenId + 1 ) << " tokens)"
                      << " - " << chunk.modelPath << std::endl;
        }
    }
};

// SplitLmHeadLoader class
class SplitLmHeadLoader
{
private:
    struct ChunkInfo
    {
        int                               chunkIndex;
        int                               startOutputId;
        int                               endOutputId;
        std::string                       modelPath;
        std::unique_ptr<MNN::Interpreter> interpreter;
        MNN::Session                     *session;

        ChunkInfo() : chunkIndex( -1 ), startOutputId( -1 ), endOutputId( -1 ), session( nullptr ) {}

        // Explicitly define move constructor and assignment
        ChunkInfo( ChunkInfo &&other ) noexcept :
            chunkIndex( other.chunkIndex ),
            startOutputId( other.startOutputId ),
            endOutputId( other.endOutputId ),
            modelPath( std::move( other.modelPath ) ),
            interpreter( std::move( other.interpreter ) ),
            session( other.session )
        {
            other.session = nullptr;
        }

        ChunkInfo &operator=( ChunkInfo &&other ) noexcept
        {
            if ( this != &other )
            {
                chunkIndex    = other.chunkIndex;
                startOutputId = other.startOutputId;
                endOutputId   = other.endOutputId;
                modelPath     = std::move( other.modelPath );
                interpreter   = std::move( other.interpreter );
                session       = other.session;
                other.session = nullptr;
            }
            return *this;
        }

        // Delete copy constructor and assignment
        ChunkInfo( const ChunkInfo & )            = delete;
        ChunkInfo &operator=( const ChunkInfo & ) = delete;
    };

    std::vector<ChunkInfo> chunks;
    std::string            modelDir;
    int                    inputFeatures;
    int                    totalOutputs;
    bool                   initialized;
    MNN::ScheduleConfig    config;

public:
    SplitLmHeadLoader() : initialized( false ), inputFeatures( 0 ), totalOutputs( 0 )
    {
        // Default config
        config.type      = MNN_FORWARD_VULKAN; // Use VULKAN by default, change as needed
        config.numThread = 1;
    }

    ~SplitLmHeadLoader()
    {
        // Clean up sessions
        for ( auto &chunk : chunks )
        {
            if ( chunk.interpreter && chunk.session )
            {
                chunk.interpreter->releaseSession( chunk.session );
                chunk.session = nullptr;
            }
        }
    }

    bool initialize( const std::string &modelDir )
    {
        this->modelDir = modelDir;

        // Load metadata file
        std::string   metadataPath = modelDir + "/lm_head_chunks_metadata.json";
        std::ifstream metadataFile( metadataPath );
        if ( !metadataFile.is_open() )
        {
            std::cerr << "Failed to open LM head metadata file: " << metadataPath << std::endl;
            return false;
        }

        // Read entire file
        std::stringstream buffer;
        buffer << metadataFile.rdbuf();
        metadataFile.close();
        std::string jsonString = buffer.str();

        try
        {
            // Parse basic metadata
            auto metadata = SimpleJsonParser::parseJson( jsonString );
            inputFeatures = SimpleJsonParser::getIntValue( metadata, "input_features" );
            totalOutputs  = SimpleJsonParser::getIntValue( metadata, "total_outputs" );

            // Parse chunks array
            auto chunksData = SimpleJsonParser::parseJsonArray( jsonString, "output_ranges" );

            // Reserve space
            chunks.resize( chunksData.size() );

            // Load chunk information
            for ( const auto &chunkData : chunksData )
            {
                int idx = SimpleJsonParser::getIntValue( chunkData, "chunk_index" );

                // Create temp info and set properties
                ChunkInfo info;
                info.chunkIndex    = idx;
                info.startOutputId = SimpleJsonParser::getIntValue( chunkData, "start_output_id" );
                info.endOutputId   = SimpleJsonParser::getIntValue( chunkData, "end_output_id" );
                info.modelPath     = modelDir + "/" + SimpleJsonParser::getStringValue( chunkData, "model_path" );

                // Replace .onnx with .mnn if needed
                if ( info.modelPath.size() > 5 && info.modelPath.substr( info.modelPath.size() - 5 ) == ".onnx" )
                {
                    info.modelPath = info.modelPath.substr( 0, info.modelPath.size() - 5 ) + ".mnn";
                }

                // Store in proper index using move assignment
                if ( idx >= 0 && idx < (int)chunks.size() )
                {
                    chunks[idx] = std::move( info );
                }
            }
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error parsing LM head metadata: " << e.what() << std::endl;
            return false;
        }

        std::cout << "Loaded LM head metadata with " << chunks.size() << " chunks" << std::endl;
        std::cout << "Input features: " << inputFeatures << ", Total outputs: " << totalOutputs << std::endl;

        initialized = true;
        return true;
    }

    // Load a specific chunk model when needed
    bool loadChunkModel( int chunkIndex )
    {
        if ( !initialized )
        {
            return false;
        }
        if ( chunkIndex < 0 || chunkIndex >= (int)chunks.size() )
        {
            return false;
        }

        // Skip if already loaded with active session
        if ( chunks[chunkIndex].interpreter && chunks[chunkIndex].session )
        {
            return true;
        }

        try
        {
            // Create interpreter if needed
            if ( !chunks[chunkIndex].interpreter )
            {
                chunks[chunkIndex].interpreter.reset(
                    MNN::Interpreter::createFromFile( chunks[chunkIndex].modelPath.c_str() ) );
                if ( !chunks[chunkIndex].interpreter )
                {
                    std::cerr << "Failed to load LM head chunk model: " << chunks[chunkIndex].modelPath << std::endl;
                    return false;
                }
            }

            // Create session
            chunks[chunkIndex].session = chunks[chunkIndex].interpreter->createSession( config );
            if ( !chunks[chunkIndex].session )
            {
                std::cerr << "Failed to create session for LM head chunk " << chunkIndex << std::endl;
                return false;
            }

            std::cout << "Loaded LM head chunk " << chunkIndex << " from " << chunks[chunkIndex].modelPath << std::endl;
            return true;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error loading LM head chunk model: " << e.what() << std::endl;
            return false;
        }
    }

    // Run full LM head prediction (combines all chunks)
    std::vector<float> runPrediction( const std::vector<float> &expertOutput )
    {
        if ( !initialized )
        {
            std::cerr << "LM head loader not initialized" << std::endl;
            return {};
        }

        if ( (int)expertOutput.size() != inputFeatures )
        {
            std::cerr << "Expert output size mismatch: expected " << inputFeatures << ", got " << expertOutput.size()
                      << std::endl;
            // Try to proceed anyway
        }

        // Prepare result vector for all logits
        std::vector<float> allLogits( totalOutputs, 0.0f );

        // Process each chunk
        for ( size_t i = 0; i < chunks.size(); ++i )
        {
            // Load the model if needed
            if ( !loadChunkModel( i ) )
            {
                std::cerr << "Failed to load model for LM head chunk " << i << std::endl;
                continue;
            }

            try
            {
                // Get input tensor
                auto input = chunks[i].interpreter->getSessionInput( chunks[i].session, nullptr );
                if ( !input )
                {
                    throw std::runtime_error( "Failed to get input tensor" );
                }

                // Resize for batch size 1
                chunks[i].interpreter->resizeTensor( input, { 1, (int)expertOutput.size() } );
                chunks[i].interpreter->resizeSession( chunks[i].session );

                // Copy expert output
                MNN::Tensor inputHost( input, MNN::Tensor::CAFFE );
                float      *inputData = inputHost.host<float>();
                memcpy( inputData, expertOutput.data(), expertOutput.size() * sizeof( float ) );
                input->copyFromHostTensor( &inputHost );

                // Run model
                chunks[i].interpreter->runSession( chunks[i].session );

                // Get output
                auto output = chunks[i].interpreter->getSessionOutput( chunks[i].session, nullptr );
                if ( !output )
                {
                    throw std::runtime_error( "Failed to get output tensor" );
                }

                // Copy to host
                MNN::Tensor outputHost( output, output->getDimensionType() );
                output->copyToHostTensor( &outputHost );
                float *outputData = outputHost.host<float>();

                // Get chunk output size
                int chunkOutputs = chunks[i].endOutputId - chunks[i].startOutputId + 1;

                // Copy to the appropriate section of the result vector
                for ( int j = 0; j < chunkOutputs; ++j )
                {
                    int globalIdx = chunks[i].startOutputId + j;
                    if ( globalIdx < totalOutputs )
                    {
                        allLogits[globalIdx] = outputData[j];
                    }
                }
            }
            catch ( const std::exception &e )
            {
                std::cerr << "Error running LM head chunk " << i << ": " << e.what() << std::endl;
            }
        }

        return allLogits;
    }

    // Find top K tokens from logits
    std::vector<std::pair<int, float>> getTopK( const std::vector<float> &logits, int k )
    {
        if ( logits.empty() )
        {
            return {};
        }

        // Create vector of (index, value) pairs
        std::vector<std::pair<int, float>> indexedLogits;
        indexedLogits.reserve( logits.size() );

        for ( size_t i = 0; i < logits.size(); ++i )
        {
            if ( !std::isnan( logits[i] ) && !std::isinf( logits[i] ) && std::abs( logits[i] ) < 1e6 )
            {
                indexedLogits.push_back( { (int)i, logits[i] } );
            }
        }

        // Sort by value (descending)
        std::partial_sort( indexedLogits.begin(),
                           indexedLogits.begin() + std::min( k, (int)indexedLogits.size() ),
                           indexedLogits.end(),
                           []( const auto &a, const auto &b ) { return a.second > b.second; } );

        // Truncate to top K
        if ( indexedLogits.size() > (size_t)k )
        {
            indexedLogits.resize( k );
        }

        return indexedLogits;
    }

    // Utility to print chunk information
    void printChunkInfo()
    {
        if ( !initialized )
        {
            std::cerr << "LM head loader not initialized" << std::endl;
            return;
        }

        std::cout << "LM Head Loader Chunks:" << std::endl;
        for ( const auto &chunk : chunks )
        {
            std::cout << "  Chunk " << chunk.chunkIndex << ": Output IDs " << chunk.startOutputId << " to "
                      << chunk.endOutputId << " (" << ( chunk.endOutputId - chunk.startOutputId + 1 ) << " outputs)"
                      << " - " << chunk.modelPath << std::endl;
        }
    }
};

// Function to run expert model
std::vector<float> runExpertModel( MNN::Interpreter *expertNet, const std::vector<float> &embedding )
{
    try
    {
        // Configure inference
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN;
        config.numThread = 1;

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

// Legacy function to run LM head model
int runLmHeadModelLegacy( MNN::Interpreter *lmHeadNet, const std::vector<float> &expertOutput, BpeTokenizer *tokenizer )
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

// Main function
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

    // Initialize the split embedding loader
    SplitEmbeddingLoader embeddingLoader;
    if ( !embeddingLoader.initialize( modelDir ) )
    {
        std::cerr << "Failed to initialize embedding loader" << std::endl;
        return 1;
    }
    embeddingLoader.printChunkInfo();

    // Initialize the split LM head loader
    SplitLmHeadLoader lmHeadLoader;
    bool              useSplitLmHead = lmHeadLoader.initialize( modelDir );
    if ( useSplitLmHead )
    {
        lmHeadLoader.printChunkInfo();
    }
    else
    {
        std::cerr << "Warning: Failed to initialize LM head loader. Will try legacy approach." << std::endl;
    }

    // Load expert model (still a single model, not split)
    std::string                       expertPath = modelDir + "/expert_f32.mnn";
    std::unique_ptr<MNN::Interpreter> expertNet( MNN::Interpreter::createFromFile( expertPath.c_str() ) );
    if ( !expertNet )
    {
        std::cerr << "Failed to load expert model from " << expertPath << std::endl;
        return 1;
    }

    // Optionally load legacy LM head model as fallback
    std::unique_ptr<MNN::Interpreter> legacyLmHeadNet;
    if ( !useSplitLmHead )
    {
        std::string lmHeadPath = modelDir + "/lm_head_f32.mnn";
        legacyLmHeadNet.reset( MNN::Interpreter::createFromFile( lmHeadPath.c_str() ) );
        if ( !legacyLmHeadNet )
        {
            std::cerr << "Failed to load legacy LM head model from " << lmHeadPath << std::endl;
            return 1;
        }
        std::cout << "Using legacy LM head model as fallback" << std::endl;
    }

    // Generate tokens one by one
    for ( int i = 0; i < std::min( numTokens, 20 ); i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding using split loader
        std::cout << "Computing embedding for token ID " << lastToken << std::endl;
        std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

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
        else
        {
            std::cerr << "Failed to get embedding for token " << lastToken << std::endl;
            std::cout << "Using synthetic embedding as fallback" << std::endl;
            embedding = createSyntheticEmbedding( lastToken, hiddenSize );
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
        else
        {
            std::cerr << "Failed to get expert output" << std::endl;
            break;
        }

        // Run LM head model (using split or legacy approach)
        int nextTokenId = 0;

        if ( useSplitLmHead )
        {
            // Run split LM head
            std::cout << "Running split LM head model..." << std::endl;
            std::vector<float> logits = lmHeadLoader.runPrediction( expertOutput );

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
                    nextTokenId = runLmHeadModelLegacy( legacyLmHeadNet.get(), expertOutput, &tokenizer );
                }
            }
        }
        else
        {
            // Use legacy approach
            nextTokenId = runLmHeadModelLegacy( legacyLmHeadNet.get(), expertOutput, &tokenizer );
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

    return 0;
}
