#ifndef LMHEAD_HPP
#define LMHEAD_HPP
#include <MNN/Interpreter.hpp>
#include <memory>
#include <iostream>
#include <algorithm>

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
#endif