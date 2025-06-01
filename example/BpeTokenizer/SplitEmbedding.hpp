#ifndef SPLITEMBEDDING_HPP
#define SPLITEMBEDDING_HPP
#include <MNN/Interpreter.hpp>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include "Utility.hpp"

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

        // Provide copy constructor and assignment (they will copy everything except the unique_ptr)
        ChunkInfo( const ChunkInfo &other ) :
            chunkIndex( other.chunkIndex ),
            startTokenId( other.startTokenId ),
            endTokenId( other.endTokenId ),
            modelPath( other.modelPath ),
            interpreter( nullptr ), // Can't copy unique_ptr, leave null
            session( nullptr )      // Session will be recreated when needed
        {
        }

        ChunkInfo &operator=( const ChunkInfo &other )
        {
            if ( this != &other )
            {
                chunkIndex   = other.chunkIndex;
                startTokenId = other.startTokenId;
                endTokenId   = other.endTokenId;
                modelPath    = other.modelPath;
                // Don't copy interpreter or session - they'll be recreated when needed
                interpreter.reset();
                session = nullptr;
            }
            return *this;
        }
    };

    std::vector<ChunkInfo> chunks;
    std::string            modelDir;
    int                    embeddingDim;
    bool                   initialized;
    MNN::ScheduleConfig    config;
    bool                   useFp16;

public:
    SplitEmbeddingLoader( bool fp16 = false );

    ~SplitEmbeddingLoader();

    bool initialize( const std::string &modelDir );

    // Load a specific chunk model when needed
    bool loadChunkModel( int chunkIndex );

    // Extract embedding for a token
    std::vector<float> extractEmbedding( int tokenId );

    // Utility to print chunk information
    void printChunkInfo();
};
#endif
