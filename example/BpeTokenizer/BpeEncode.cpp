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

// Forward declaration
class LayerProcessor;

// Main class to handle model execution
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
};

// Class to handle processing of a single expert layer
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

    // List of available expert IDs for this layer
    std::vector<int> availableExpertIds;

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

// Static members
std::shared_ptr<MultiExpertHandler> LayerProcessor::sharedExpertHandler = nullptr;
std::shared_ptr<GateWeightsHandler> LayerProcessor::sharedGateHandler   = nullptr;
std::mutex                          LayerProcessor::expertHandlerMutex;
std::mutex                          LayerProcessor::gateHandlerMutex;

// LayerProcessor implementation
LayerProcessor::LayerProcessor( int layerId, const std::string &modelDir, bool debug ) :
    layerId( layerId ), modelDir( modelDir ), debugMode( debug )
{
}

bool LayerProcessor::initialize()
{
    // Initialize shared gate handler once
    {
        std::lock_guard<std::mutex> lock( gateHandlerMutex );
        if ( !sharedGateHandler )
        {
            sharedGateHandler = std::make_shared<GateWeightsHandler>();
            if ( !sharedGateHandler->initialize( modelDir ) )
            {
                std::cerr << "Warning: Failed to initialize shared gate handler" << std::endl;
            }
            else
            {
                std::cout << "Shared gate handler initialized" << std::endl;
            }
        }
    }

    // Initialize shared expert handler once
    {
        std::lock_guard<std::mutex> lock( expertHandlerMutex );
        if ( !sharedExpertHandler )
        {
            sharedExpertHandler = std::make_shared<MultiExpertHandler>();

            // Try layer-specific expert directory first
            std::string expertDir = modelDir + "/layer_" + std::to_string( layerId ) + "_experts";
            if ( std::filesystem::exists( expertDir ) )
            {
                if ( !sharedExpertHandler->initialize( expertDir ) )
                {
                    std::cerr << "Warning: Failed to initialize experts from layer directory" << std::endl;
                }
            }
            else
            {
                // Fall back to using the main model directory
                if ( !sharedExpertHandler->initialize( modelDir ) )
                {
                    std::cerr << "Warning: Failed to initialize shared expert handler" << std::endl;
                    return false;
                }
            }

            std::cout << "Shared expert handler initialized" << std::endl;
        }
    }

    // Set debug mode
    if ( sharedExpertHandler )
    {
        sharedExpertHandler->setDebugMode( debugMode );
    }
    if ( sharedGateHandler )
    {
        sharedGateHandler->setDebugMode( debugMode );
    }

    // Scan for available experts
    scanAvailableExperts();

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " initialized with " << availableExpertIds.size() << " available experts"
                  << std::endl;
        if ( !availableExpertIds.empty() )
        {
            std::cout << "Available expert IDs: ";
            for ( int id : availableExpertIds )
            {
                std::cout << id << " ";
            }
            std::cout << std::endl;
        }
    }

    return true;
}

void LayerProcessor::scanAvailableExperts()
{
    availableExpertIds.clear();

    if ( !sharedExpertHandler )
    {
        return;
    }

    // Get total expert count from handler
    int expertCount = sharedExpertHandler->getExpertCount();

    // Check each potential expert ID
    for ( int i = 0; i < expertCount; i++ )
    {
        int expertId = sharedExpertHandler->getExpertIdForIndex( i );
        if ( expertId >= 0 )
        {
            availableExpertIds.push_back( expertId );
        }
    }

    // Sort IDs for easier lookup
    std::sort( availableExpertIds.begin(), availableExpertIds.end() );
}

std::vector<float> LayerProcessor::process( const std::vector<float> &input, int tokenPosition )
{
    if ( availableExpertIds.empty() || !sharedExpertHandler )
    {
        std::cerr << "No experts available for layer " << layerId << std::endl;
        return input; // Pass through input as fallback
    }

    std::vector<int> selectedExperts;

    // Use gate to select experts if available
    if ( sharedGateHandler && sharedGateHandler->hasLayerGate( layerId ) )
    {
        selectedExperts = sharedGateHandler->selectExperts( layerId, input, 2 );

        if ( debugMode )
        {
            std::cout << "Gate for layer " << layerId << " selected experts: ";
            for ( int id : selectedExperts )
            {
                std::cout << id << " ";
            }
            std::cout << std::endl;
        }

        // Filter to only use available experts
        std::vector<int> filteredExperts;
        for ( int expertId : selectedExperts )
        {
            if ( std::binary_search( availableExpertIds.begin(), availableExpertIds.end(), expertId ) )
            {
                filteredExperts.push_back( expertId );
            }
        }

        // If none of the selected experts are available, use the first available expert
        if ( filteredExperts.empty() && !availableExpertIds.empty() )
        {
            filteredExperts.push_back( availableExpertIds[0] );
            if ( debugMode )
            {
                std::cout << "Falling back to available expert " << availableExpertIds[0] << " for layer " << layerId
                          << std::endl;
            }
        }

        selectedExperts = filteredExperts;
    }
    else
    {
        // No gate, use round-robin from available experts
        int expertIndex = tokenPosition % availableExpertIds.size();
        selectedExperts.push_back( availableExpertIds[expertIndex] );

        if ( debugMode )
        {
            std::cout << "No gate for layer " << layerId << ", using expert " << availableExpertIds[expertIndex]
                      << std::endl;
        }
    }

    // Process through selected experts and average their outputs
    std::vector<float> resultOutput;
    int                successfulExperts = 0;

    for ( int expertId : selectedExperts )
    {
        std::vector<float> expertOutput = sharedExpertHandler->runSingleExpert( expertId, input );

        if ( !expertOutput.empty() )
        {
            successfulExperts++;

            // Initialize result with first expert's output
            if ( resultOutput.empty() )
            {
                resultOutput = expertOutput;
            }
            else
            {
                // Accumulate outputs (for averaging later)
                for ( size_t j = 0; j < resultOutput.size() && j < expertOutput.size(); ++j )
                {
                    resultOutput[j] += expertOutput[j];
                }
            }
        }
    }

    // Average the results
    if ( successfulExperts > 1 )
    {
        for ( size_t j = 0; j < resultOutput.size(); ++j )
        {
            resultOutput[j] /= successfulExperts;
        }
    }

    // If no experts ran successfully, return input as fallback
    if ( successfulExperts == 0 )
    {
        std::cerr << "Failed to run any experts for layer " << layerId << std::endl;
        return input;
    }

    return resultOutput;
}

void LayerProcessor::setDebugMode( bool debug )
{
    debugMode = debug;
    if ( sharedExpertHandler )
    {
        sharedExpertHandler->setDebugMode( debug );
    }
    if ( sharedGateHandler )
    {
        sharedGateHandler->setDebugMode( debug );
    }
}

// MoEModelRunner implementation
MoEModelRunner::MoEModelRunner( const std::string &modelDir, bool debug ) :
    modelDir( modelDir ), debugMode( debug ), hiddenSize( 7168 ), useSplitLmHead( false )
{
}

MoEModelRunner::~MoEModelRunner()
{
    // Clean up standard layer sessions
    for ( size_t i = 0; i < standardSessions.size(); i++ )
    {
        if ( standardLayers[i] && standardSessions[i] )
        {
            standardLayers[i]->releaseSession( standardSessions[i] );
            standardSessions[i] = nullptr;
        }
    }
}

bool MoEModelRunner::initialize()
{
    // Load tokenizer
    std::string vocabPath  = modelDir + "/vocab.json";
    std::string mergesPath = modelDir + "/merges.txt";

    if ( !tokenizer.load( vocabPath, mergesPath ) )
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return false;
    }
    std::cout << "Tokenizer loaded successfully" << std::endl;

    // Initialize embedding loader
    if ( !embeddingLoader.initialize( modelDir ) )
    {
        std::cerr << "Failed to initialize embedding loader" << std::endl;
        return false;
    }

    if ( debugMode )
    {
        embeddingLoader.printChunkInfo();
    }

    // Initialize LM head loader
    useSplitLmHead = lmHeadLoader.initialize( modelDir );
    if ( useSplitLmHead )
    {
        if ( debugMode )
        {
            lmHeadLoader.printChunkInfo();
        }
    }
    else
    {
        std::cerr << "Warning: Failed to initialize split LM head loader. Will try legacy approach." << std::endl;

        // Load legacy LM head model as fallback
        std::string lmHeadPath = modelDir + "/lm_head_f32.mnn";
        legacyLmHeadNet.reset( MNN::Interpreter::createFromFile( lmHeadPath.c_str() ) );
        if ( !legacyLmHeadNet )
        {
            std::cerr << "Failed to load legacy LM head model from " << lmHeadPath << std::endl;
            return false;
        }
        std::cout << "Using legacy LM head model as fallback" << std::endl;
    }

    // Load standard layers (0-2)
    if ( !loadStandardLayers() )
    {
        std::cerr << "Warning: Failed to load some standard layers" << std::endl;
    }

    // Load expert layers (3-61)
    if ( !loadExpertLayers() )
    {
        std::cerr << "Warning: Failed to load some expert layers" << std::endl;
    }

    return true;
}

bool MoEModelRunner::loadStandardLayers()
{
    // Reserve space for layers
    standardLayers.resize( 3 );
    standardSessions.resize( 3, nullptr );

    // Load each standard layer
    for ( int i = 0; i < 3; i++ )
    {
        std::string layerPath = modelDir + "/layer_" + std::to_string( i ) + "_mlp.mnn";

        if ( std::filesystem::exists( layerPath ) )
        {
            standardLayers[i].reset( MNN::Interpreter::createFromFile( layerPath.c_str() ) );

            if ( !standardLayers[i] )
            {
                std::cerr << "Failed to load standard layer " << i << " from " << layerPath << std::endl;
                continue;
            }

            // Create a session for this layer
            MNN::ScheduleConfig config;
            config.type      = MNN_FORWARD_VULKAN;
            config.numThread = 1;

            standardSessions[i] = standardLayers[i]->createSession( config );

            if ( !standardSessions[i] )
            {
                std::cerr << "Failed to create session for standard layer " << i << std::endl;
                continue;
            }

            std::cout << "Loaded standard layer " << i << " from " << layerPath << std::endl;
        }
        else
        {
            std::cerr << "Standard layer " << i << " not found at " << layerPath << std::endl;
        }
    }

    return true;
}

bool MoEModelRunner::loadExpertLayers()
{
    // Start from layer 3, go up to layer 61
    for ( int i = 3; i <= 61; i++ )
    {
        auto processor = std::make_unique<LayerProcessor>( i, modelDir, debugMode );

        if ( processor->initialize() )
        {
            expertLayers.push_back( std::move( processor ) );
            std::cout << "Initialized expert layer " << i << std::endl;
        }
        else
        {
            std::cerr << "Failed to initialize expert layer " << i << std::endl;
        }
    }

    return !expertLayers.empty();
}

std::vector<float> MoEModelRunner::runStandardLayer( int layerId, const std::vector<float> &input )
{
    if ( layerId < 0 || layerId >= (int)standardLayers.size() || !standardLayers[layerId] ||
         !standardSessions[layerId] )
    {
        // If layer doesn't exist or failed to load, return input as passthrough
        return input;
    }

    try
    {
        // Get input tensor
        auto layerInput = standardLayers[layerId]->getSessionInput( standardSessions[layerId], nullptr );
        if ( !layerInput )
        {
            throw std::runtime_error( "Failed to get input tensor for standard layer " + std::to_string( layerId ) );
        }

        // Resize input tensor
        standardLayers[layerId]->resizeTensor( layerInput, { 1, (int)input.size() } );
        standardLayers[layerId]->resizeSession( standardSessions[layerId] );

        // Copy input data
        MNN::Tensor inputHost( layerInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        layerInput->copyFromHostTensor( &inputHost );

        // Run layer
        standardLayers[layerId]->runSession( standardSessions[layerId] );

        // Get output
        auto output = standardLayers[layerId]->getSessionOutput( standardSessions[layerId], nullptr );
        if ( !output )
        {
            throw std::runtime_error( "Failed to get output tensor for standard layer " + std::to_string( layerId ) );
        }

        // Copy to host
        MNN::Tensor outputHost( output, output->getDimensionType() );
        output->copyToHostTensor( &outputHost );
        float *outputData = outputHost.host<float>();

        // Get output size
        size_t outputSize = 1;
        for ( int d = 0; d < output->dimensions(); d++ )
        {
            outputSize *= output->length( d );
        }

        // Create vector for output
        std::vector<float> layerOutput( outputSize );

        // Copy output data
        for ( size_t i = 0; i < outputSize; i++ )
        {
            float val = outputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                layerOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                layerOutput[i] = val;
            }
        }

        return layerOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running standard layer " << layerId << ": " << e.what() << std::endl;
        return input; // Return input as fallback
    }
}

std::string MoEModelRunner::generateText( const std::string &prompt, int numTokens )
{
    // Tokenize prompt
    std::vector<int> promptTokens = tokenizer.encode( prompt );

    if ( promptTokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return "";
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : promptTokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Create a vector to hold generated tokens
    std::vector<int> generatedTokens = promptTokens;

    // Generate tokens one by one
    for ( int i = 0; i < numTokens; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        // Disable debug mode after a few tokens
        if ( i >= 3 )
        {
            setDebugMode( false );
            for ( auto &layer : expertLayers )
            {
                layer->setDebugMode( false );
            }
        }

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding using split loader
        std::cout << "Computing embedding for token ID " << lastToken << std::endl;
        std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

        // Print embedding stats if in debug mode
        if ( debugMode && !embedding.empty() )
        {
            float minVal = *std::min_element( embedding.begin(), embedding.end() );
            float maxVal = *std::max_element( embedding.begin(), embedding.end() );
            float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
            float mean   = sum / embedding.size();

            std::cout << "Embedding stats - Size: " << embedding.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }

        if ( embedding.empty() )
        {
            std::cerr << "Failed to get embedding for token " << lastToken << std::endl;
            std::cout << "Using synthetic embedding as fallback" << std::endl;
            embedding = LLMUtility::createSyntheticEmbedding( lastToken, hiddenSize );
        }

        // Process through standard layers (0-2)
        std::vector<float> layerOutput = embedding;
        for ( int layerId = 0; layerId < 3; layerId++ )
        {
            layerOutput = runStandardLayer( layerId, layerOutput );

            if ( debugMode )
            {
                std::cout << "Processed through standard layer " << layerId << std::endl;
            }
        }

        // Process through expert layers (3-61)
        for ( size_t j = 0; j < expertLayers.size(); j++ )
        {
            int layerId = expertLayers[j]->getLayerId();
            layerOutput = expertLayers[j]->process( layerOutput, i );

            if ( debugMode )
            {
                std::cout << "Processed through expert layer " << layerId << std::endl;
            }
        }

        // Run LM head model (using split or legacy approach)
        int nextTokenId = 0;

        if ( useSplitLmHead )
        {
            // Run split LM head
            std::cout << "Running split LM head model..." << std::endl;
            std::vector<float> logits = lmHeadLoader.runPrediction( layerOutput );

            if ( !logits.empty() )
            {
                // Get top tokens
                std::vector<std::pair<int, float>> topTokens = lmHeadLoader.getTopK( logits, 5 );

                // Print top tokens
                std::cout << "\nTop tokens:" << std::endl;
                for ( size_t j = 0; j < topTokens.size(); j++ )
                {
                    int   tokenId = topTokens[j].first;
                    float score   = topTokens[j].second;

                    std::string tokenText;
                    try
                    {
                        tokenText = tokenizer.decode( { tokenId } );
                        // Replace non-printable chars with dots
                        for ( char &c : tokenText )
                        {
                            if ( c < 32 || c > 126 )
                            {
                                c = '.';
                            }
                        }
                    }
                    catch ( ... )
                    {
                        tokenText = "[invalid token]";
                    }

                    std::cout << "  " << ( j + 1 ) << ". Token ID " << tokenId << " (score: " << score << "): \""
                              << tokenText << "\"" << std::endl;
                }

                // Get top token
                nextTokenId = topTokens.empty() ? 0 : topTokens[0].first;
            }
            else
            {
                std::cerr << "Failed to get logits from split LM head" << std::endl;
                if ( legacyLmHeadNet )
                {
                    std::cout << "Falling back to legacy LM head" << std::endl;
                    nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), layerOutput, &tokenizer );
                }
            }
        }
        else if ( legacyLmHeadNet )
        {
            // Use legacy approach
            nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), layerOutput, &tokenizer );
        }

        // Add next token
        generatedTokens.push_back( nextTokenId );

        // Display current text
        std::string currentText = tokenizer.decode( generatedTokens );

        // Clean up output for display
        std::string displayText;
        for ( unsigned char c : currentText )
        {
            displayText += ( c < 32 || c > 126 ) ? '.' : c;
        }
        std::cout << "\nGenerated so far: " << displayText << std::endl;
    }

    // Return the final generated text
    return tokenizer.decode( generatedTokens );
}

// Main function
int main( int argc, char **argv )
{
    if ( argc < 4 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens>" << std::endl;
        std::cerr << "Example: " << argv[0] << " . \"The cat sat on the chair\" 20" << std::endl;
        return 1;
    }

    std::string modelDir  = argv[1];
    std::string prompt    = argv[2];
    int         numTokens = 1; // Default to 1 token

    try
    {
        numTokens = std::stoi( argv[3] );
    }
    catch ( ... )
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Create and initialize the model runner
    MoEModelRunner runner( modelDir, true ); // Start with debug mode on

    if ( !runner.initialize() )
    {
        std::cerr << "Failed to initialize model runner" << std::endl;
        return 1;
    }

    // Generate text
    std::string generatedText = runner.generateText( prompt, numTokens );

    std::cout << "\n=== Final Generated Text ===\n" << generatedText << std::endl;

    return 0;
}
