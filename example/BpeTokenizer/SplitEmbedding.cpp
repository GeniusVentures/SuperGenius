#include "SplitEmbedding.hpp"
#include "jsonparser.hpp"

SplitEmbeddingLoader::SplitEmbeddingLoader( bool fp16 ) :
    initialized( false ), embeddingDim( 0 ), useFp16( fp16 ) // NEW
{
    // Default config
    config.type      = MNN_FORWARD_VULKAN;
    config.numThread = 1;
}

SplitEmbeddingLoader::~SplitEmbeddingLoader()
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

bool SplitEmbeddingLoader::initialize( const std::string &modelDir )
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
bool SplitEmbeddingLoader::loadChunkModel( int chunkIndex )
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
        std::string modelPath;
        if ( !chunks[chunkIndex].interpreter )
        {
            modelPath = LLMUtility::getFp16Path( chunks[chunkIndex].modelPath, useFp16 ); 
            chunks[chunkIndex].interpreter.reset( MNN::Interpreter::createFromFile( modelPath.c_str() ) );
            if ( !chunks[chunkIndex].interpreter )
            {
                std::cerr << "Failed to load chunk model: " << modelPath << std::endl;
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

        std::cout << "Loaded chunk model " << chunkIndex << " from " << modelPath << std::endl;
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error loading chunk model: " << e.what() << std::endl;
        return false;
    }
}

// Extract embedding for a token
std::vector<float> SplitEmbeddingLoader::extractEmbedding( int tokenId )
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
void SplitEmbeddingLoader::printChunkInfo()
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
