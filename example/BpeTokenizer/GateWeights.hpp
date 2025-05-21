#ifndef GATEWEIGHTS_HPP
#define GATEWEIGHTS_HPP

#include <MNN/Interpreter.hpp>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include "jsonparser.hpp"

class GateWeightsHandler
{
private:
    struct GateModelInfo
    {
        int                               layerId;
        std::string                       modelPath;
        std::unique_ptr<MNN::Interpreter> interpreter;
        MNN::Session                     *session;
        std::vector<int>                  recommendedExperts;

        GateModelInfo() : layerId( -1 ), session( nullptr ) {}

        // Move constructor and assignment
        GateModelInfo( GateModelInfo &&other ) noexcept :
            layerId( other.layerId ),
            modelPath( std::move( other.modelPath ) ),
            interpreter( std::move( other.interpreter ) ),
            session( other.session ),
            recommendedExperts( std::move( other.recommendedExperts ) )
        {
            other.session = nullptr;
        }

        GateModelInfo &operator=( GateModelInfo &&other ) noexcept
        {
            if ( this != &other )
            {
                layerId            = other.layerId;
                modelPath          = std::move( other.modelPath );
                interpreter        = std::move( other.interpreter );
                session            = other.session;
                recommendedExperts = std::move( other.recommendedExperts );
                other.session      = nullptr;
            }
            return *this;
        }

        // Delete copy constructor and assignment
        GateModelInfo( const GateModelInfo & )            = delete;
        GateModelInfo &operator=( const GateModelInfo & ) = delete;
    };

    std::unordered_map<int, GateModelInfo> gateModels; // Layer ID -> Gate model
    std::string                            modelDir;
    bool                                   initialized;
    MNN::ScheduleConfig                    config;
    bool                                   debugMode;

    // Load recommended experts from JSON analysis
    bool loadRecommendedExperts( int layerId, const std::string &analysisPath );

public:
    GateWeightsHandler();
    ~GateWeightsHandler();

    // Initialize by loading gate models and recommendations
    bool initialize( const std::string &modelDir );

    // Add a specific gate model
    bool addGateModel( int layerId, const std::string &gatePath, const std::string &analysisPath );

    // Get recommended experts for a layer
    std::vector<int> getRecommendedExperts( int layerId ) const;

    // Run gate model to select experts dynamically
    std::vector<int> selectExperts( int layerId, const std::vector<float> &embedding, int topK = 2 );

    // Set/get debug mode
    void setDebugMode( bool debug );
    bool getDebugMode() const;

    // Check if handler has gate model for a specific layer
    bool hasLayerGate( int layerId ) const;

    // Print information about loaded gate models
    void printGateInfo() const;
};

#endif
