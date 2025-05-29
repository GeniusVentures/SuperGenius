#ifndef LAYERPROCESSOR_HPP
#define LAYERPROCESSOR_HPP
#include <string>
#include <mutex>
#include <memory>
#include <MNN/Interpreter.hpp>
#include "GateWeights.hpp"
#include "MultiExpert.hpp"
#include "Utility.hpp"

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

    // Process different types of layers
    std::vector<float> processStandardMLP( const std::vector<float> &input );
    std::vector<float> processMoEExperts( const std::vector<float> &input, int tokenPosition );

    // Run individual components temporarily (load, run, cleanup)
    std::vector<float> runPreAttentionLayerNorm( const std::vector<float> &input );
    std::vector<float> runPostAttentionLayerNorm( const std::vector<float> &input );
    std::vector<float> runAttentionLayerTemporary( const std::vector<float> &input );
    std::vector<float> runSharedExpert( const std::vector<float> &input );

public:
    LayerProcessor( int layerId, const std::string &modelDir, bool debug = false );
    ~LayerProcessor();

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

    // Check if attention layer exists (but don't load it)
    bool hasAttentionLayer() const
    {
        std::string attentionPath = modelDir + "/layer_" + std::to_string( layerId ) + "_attention.mnn";
        return std::filesystem::exists( attentionPath );
    }

    // Check if this is a standard layer (0-2) or MoE layer (3-61)
    bool isStandardLayer() const
    {
        return layerId < 3;
    }

    bool isMoELayer() const
    {
        return layerId >= 3;
    }
};

#endif
