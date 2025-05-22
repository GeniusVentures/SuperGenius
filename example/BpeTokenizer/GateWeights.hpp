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
#include <regex>
#include <algorithm>

class GateWeightsHandler
{
private:
    struct GateModelInfo
    {
        int                               layerId;
        std::string                       modelPath;
        std::unique_ptr<MNN::Interpreter> interpreter;
        MNN::Session                     *session;

        GateModelInfo() : layerId( -1 ), session( nullptr ) {}

        // Move constructor and assignment
        GateModelInfo( GateModelInfo &&other ) noexcept :
            layerId( other.layerId ),
            modelPath( std::move( other.modelPath ) ),
            interpreter( std::move( other.interpreter ) ),
            session( other.session )
        {
            other.session = nullptr;
        }

        GateModelInfo &operator=( GateModelInfo &&other ) noexcept
        {
            if ( this != &other )
            {
                layerId       = other.layerId;
                modelPath     = std::move( other.modelPath );
                interpreter   = std::move( other.interpreter );
                session       = other.session;
                other.session = nullptr;
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

public:
    GateWeightsHandler();
    ~GateWeightsHandler();

    // Initialize by loading gate models
    bool initialize( const std::string &modelDir );

    // Add a specific gate model (no longer needs analysis file)
    bool addGateModel( int layerId, const std::string &gatePath );

    // Run gate model to select experts dynamically (returns all experts ranked by score)
    std::vector<int> selectExperts( int layerId, const std::vector<float> &embedding, int topK = -1 );

    // Run gate model and filter to only available experts (main method to use)
    std::vector<int> selectAvailableExperts( int                       layerId,
                                             const std::vector<float> &embedding,
                                             const std::vector<int>   &availableExperts,
                                             int                       topK = 2 );

    // Set/get debug mode
    void setDebugMode( bool debug );
    bool getDebugMode() const;

    // Check if handler has gate model for a specific layer
    bool hasLayerGate( int layerId ) const;

    // Print information about loaded gate models
    void printGateInfo() const;
};

#endif
