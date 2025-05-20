#ifndef LMHEAD_HPP
#define LMHEAD_HPP
#include <MNN/Interpreter.hpp>
#include <memory>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>

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
    SplitLmHeadLoader();

    ~SplitLmHeadLoader();

    bool initialize( const std::string &modelDir );

    // Load a specific chunk model when needed
    bool loadChunkModel( int chunkIndex );

    // Run full LM head prediction (combines all chunks)
    std::vector<float> runPrediction( const std::vector<float> &expertOutput );

    // Find top K tokens from logits
    std::vector<std::pair<int, float>> getTopK( const std::vector<float> &logits, int k );

    // Utility to print chunk information
    void printChunkInfo();
};
#endif