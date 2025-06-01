#include "BpeTokenizer.hpp"
#include "jsonparser.hpp"
#include "SplitEmbedding.hpp"
#include "SplitLMHead.hpp"
#include "MultiExpert.hpp"
#include "GateWeights.hpp"
#include "Utility.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <memory>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <filesystem>
#include "LayerProcessor.hpp"
#include "MoeModelRunner.hpp"

// Static members
std::shared_ptr<MultiExpertHandler> LayerProcessor::sharedExpertHandler = nullptr;
std::shared_ptr<GateWeightsHandler> LayerProcessor::sharedGateHandler   = nullptr;
std::mutex                          LayerProcessor::expertHandlerMutex;
std::mutex                          LayerProcessor::gateHandlerMutex;

// Main function
// Updated main function with expert testing capability
int main( int argc, char **argv )
{
    if ( argc < 4 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens> [debug|test_expert <layer>] [fp16]"
                  << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " . \"Hello world\" 20" << std::endl;
        std::cerr << "  " << argv[0] << " . \"Hello world\" 20 debug" << std::endl;
        std::cerr << "  " << argv[0] << " . \"Hello world\" 20 debug fp16" << std::endl;
        std::cerr << "  " << argv[0] << " . \"Hello world\" 20 test_expert 3 fp16" << std::endl;
        return 1;
    }

    std::string modelDir    = argv[1];
    std::string prompt      = argv[2];
    int         numTokens   = 1;
    bool        runDebug    = false;
    bool        testExpert  = false;
    bool        useFp16     = false;  // NEW
    int         expertLayer = 3;

    try
    {
        numTokens = std::stoi( argv[3] );
    }
    catch ( ... )
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Parse command line arguments
    for ( int i = 4; i < argc; i++ )
    {
        std::string arg = argv[i];
        if ( arg == "debug" )
        {
            runDebug = true;
        }
        else if ( arg == "test_expert" && i + 1 < argc )
        {
            testExpert = true;
            try
            {
                expertLayer = std::stoi( argv[i + 1] );
                i++; // Skip the next argument since we consumed it
            }
            catch ( ... )
            {
                std::cerr << "Invalid expert layer, using default: 3" << std::endl;
                expertLayer = 3;
            }
        }
        else if ( arg == "fp16" )
        {
            useFp16 = true;
        }
    }

    if ( useFp16 )
    {
        std::cout << "Using FP16 precision mode" << std::endl;
    }

    // Create and initialize the model runner with FP16 flag
    MoEModelRunner runner( modelDir, true, useFp16 );  // MODIFIED

    if ( !runner.initialize() )
    {
        std::cerr << "Failed to initialize model runner" << std::endl;
        return 1;
    }

    if ( testExpert )
    {
        // Test specific expert models directly
        std::cout << "=== EXPERT MODEL TESTING MODE ===" << std::endl;

        // Test the requested layer
        std::string expertPath = modelDir + "/expert_layer" + std::to_string( expertLayer ) + "_0.mnn";
        if ( std::filesystem::exists( expertPath ) )
        {
            std::cout << "Testing expert for layer " << expertLayer << std::endl;
            runner.testExpertModelDirectly( expertPath, expertLayer, 0 );
        }
        else
        {
            std::cerr << "Expert model not found: " << expertPath << std::endl;
        }

        // Also test a layer 10 expert for comparison (if different from requested)
        if ( expertLayer != 10 )
        {
            std::string layer10ExpertPath = modelDir + "/expert_layer10_4.mnn";
            if ( std::filesystem::exists( layer10ExpertPath ) )
            {
                std::cout << "Testing layer 10 expert for comparison:" << std::endl;
                runner.testExpertModelDirectly( layer10ExpertPath, 10, 4 );
            }
        }

        // Test a standard layer for comparison
        std::string standardLayerPath = modelDir + "/layer_0_mlp.mnn";
        if ( std::filesystem::exists( standardLayerPath ) )
        {
            std::cout << "Testing standard layer 0 for comparison:" << std::endl;
            runner.testStandardLayerDirectly( standardLayerPath, 0 );
        }

        return 0;
    }

    if ( runDebug )
    {
        // Run debugging instead of generation
        runner.debugModelPipeline( prompt );
        return 0;
    }

    // Generate text normally
    std::string generatedText = runner.generateText( prompt, numTokens );

    std::cout << "\n=== Final Generated Text ===\n" << generatedText << std::endl;

    return 0;
}
