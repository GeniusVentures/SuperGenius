#ifndef MULTIEXPERT_HPP
#define MULTIEXPERT_HPP
#include <MNN/Interpreter.hpp>
#include "Utility.hpp"
#include <memory>
#include <iostream>
#include <filesystem>
#include <regex> 

class MultiExpertHandler
{
private:
    struct ExpertModel
    {
        int                               expertId;
        int                               layerId;
        std::string                       modelPath;
        std::unique_ptr<MNN::Interpreter> interpreter;
        MNN::Session                     *session;

        ExpertModel() : expertId( -1 ), layerId( -1 ), session( nullptr ) {}

        // Move constructor and assignment
        ExpertModel( ExpertModel &&other ) noexcept :
            expertId( other.expertId ),
            layerId( other.layerId ),
            modelPath( std::move( other.modelPath ) ),
            interpreter( std::move( other.interpreter ) ),
            session( other.session )
        {
            other.session = nullptr;
        }

        ExpertModel &operator=( ExpertModel &&other ) noexcept
        {
            if ( this != &other )
            {
                expertId      = other.expertId;
                layerId       = other.layerId;
                modelPath     = std::move( other.modelPath );
                interpreter   = std::move( other.interpreter );
                session       = other.session;
                other.session = nullptr;
            }
            return *this;
        }

        // Delete copy constructor and assignment
        ExpertModel( const ExpertModel & )            = delete;
        ExpertModel &operator=( const ExpertModel & ) = delete;
    };

    std::vector<ExpertModel> experts;
    std::string              modelDir;
    bool                     initialized;
    MNN::ScheduleConfig      config;
    int                      expertStrategy; // 0 = round robin, 1 = average all experts
    bool                     debugMode;

public:
    MultiExpertHandler();

    ~MultiExpertHandler();

    // Set strategy - 0 for round robin, 1 for average all
    void setStrategy( int strategy );

    void setDebugMode( bool debug );

    // Initialize by scanning modelDir for expert model files
    bool initialize( const std::string &modelDir );

    // Load a specific expert model when needed
    bool loadExpertModel( int index );

    // Get the number of experts
    int getExpertCount() const;

    // Run single expert model
    std::vector<float> runSingleExpert( int expertIndex, const std::vector<float> &embedding );

    // Run all experts and average their outputs
    std::vector<float> runAverageAllExperts( const std::vector<float> &embedding );

    // Run expert based on token position (round robin)
    std::vector<float> runExpertForToken( int tokenPosition, const std::vector<float> &embedding );

    // Main run function that uses the selected strategy
    std::vector<float> runExpertModel( int tokenPosition, const std::vector<float> &embedding );
};
#endif