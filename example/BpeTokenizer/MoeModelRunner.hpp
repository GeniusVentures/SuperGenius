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

    // Standard layers (0-2)
    std::vector<std::unique_ptr<MNN::Interpreter>> standardLayers;
    std::vector<MNN::Session *>                    standardSessions;

    // Expert layers (3-61)
    std::vector<std::unique_ptr<LayerProcessor>> expertLayers;

    // Utility methods
    bool               loadStandardLayers();
    bool               loadExpertLayers();
    std::vector<float> runStandardLayer( int layerId, const std::vector<float> &input );

public:
    MoEModelRunner( const std::string &modelDir, bool debug = false );
    ~MoEModelRunner();

    bool        initialize();
    std::string generateText( const std::string &prompt, int numTokens );

    void setDebugMode( bool debug )
    {
        debugMode = debug;
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