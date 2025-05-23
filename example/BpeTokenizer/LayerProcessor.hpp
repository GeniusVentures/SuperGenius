#ifndef LAYERPROCESSOR_HPP
#define LAYERPROCESSOR_HPP
#include <string>
#include <mutex>
#include "GateWeights.hpp"
#include "MultiExpert.hpp"

class LayerProcessor
{
private:
    int         layerId;
    std::string modelDir;
    bool        debugMode;

    // Gate model - shared across all layers for resource efficiency
    static std::shared_ptr<GateWeightsHandler> sharedGateHandler;
    static std::mutex                          gateHandlerMutex;

    // Expert models - shared across all layers for resource efficiency
    static std::shared_ptr<MultiExpertHandler> sharedExpertHandler;
    static std::mutex                          expertHandlerMutex;

    // List of available expert IDs for this layer (determined by filesystem scan)
    std::vector<int> availableExpertIds;

    // Scan filesystem for available experts for this layer
    std::vector<int> scanExpertsFromFilesystem() const;

    // Scan for available experts for this layer
    void scanAvailableExperts();

public:
    LayerProcessor( int layerId, const std::string &modelDir, bool debug = false );
    ~LayerProcessor() = default;

    bool               initialize();
    std::vector<float> process( const std::vector<float> &input, int tokenPosition );

    void setDebugMode( bool debug );

    bool getDebugMode() const
    {
        return debugMode;
    }

    int getLayerId() const
    {
        return layerId;
    }

    const std::vector<int> &getAvailableExpertIds() const
    {
        return availableExpertIds;
    }
};

#endif