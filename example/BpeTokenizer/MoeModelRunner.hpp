#ifndef MOEMODELRUN_HPP
#define MOEMODELRUN_HPP
#include <string>
#include <mutex>
#include "LayerProcessor.hpp"
#include "SplitEmbedding.hpp"
#include "SplitLMHead.hpp"

class MoEModelRunner
{
private:
    std::string modelDir;
    bool        debugMode;
    int         hiddenSize;

    // Components
    BpeTokenizer                      tokenizer;
    SplitEmbeddingLoader              embeddingLoader;
    SplitLmHeadLoader                 lmHeadLoader;
    std::unique_ptr<MNN::Interpreter> legacyLmHeadNet;
    bool                              useSplitLmHead;

    // All layers (0-61) using unified LayerProcessor
    std::vector<std::unique_ptr<LayerProcessor>> allLayers;

    // Final model layernorm
    std::unique_ptr<MNN::Interpreter> finalLayerNorm;
    MNN::Session                     *finalLayerNormSession;

    // Utility methods
    bool               loadAllLayers();
    bool               loadFinalLayerNorm();
    std::vector<float> runFinalLayerNorm( const std::vector<float> &input );

public:
    MoEModelRunner( const std::string &modelDir, bool debug = false );
    ~MoEModelRunner();

    bool        initialize();
    std::string generateText( const std::string &prompt, int numTokens );

    void setDebugMode( bool debug )
    {
        debugMode = debug;
        // Update debug mode for all layers
        for ( auto &layer : allLayers )
        {
            layer->setDebugMode( debug );
        }
    }

    bool getDebugMode() const
    {
        return debugMode;
    }

    void debugModelPipeline( const std::string &prompt );
    void debugModelPipelineDetailed( const std::string &prompt );
    void testExpertModelDirectly( const std::string &expertPath, int layerId, int expertId );
    void testStandardLayerDirectly( const std::string &layerPath, int layerId );
};

#endif
