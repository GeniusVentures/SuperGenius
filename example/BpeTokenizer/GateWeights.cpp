#include "GateWeights.hpp"
#include "Utility.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

GateWeightsHandler::GateWeightsHandler() : initialized( false ), debugMode( false )
{
    // Default config
    config.type      = MNN_FORWARD_CPU; // Use CPU by default for gate models (they're smaller)
    config.numThread = 1;
}

GateWeightsHandler::~GateWeightsHandler()
{
    // Clean up sessions
    for ( auto &pair : gateModels )
    {
        if ( pair.second.interpreter && pair.second.session )
        {
            pair.second.interpreter->releaseSession( pair.second.session );
            pair.second.session = nullptr;
        }
    }
}

bool GateWeightsHandler::initialize( const std::string &modelDir )
{
    this->modelDir = modelDir;

    try
    {
        // Look for gate models
        bool foundAny = false;

        // Look for all layers with gate models
        for ( const auto &entry : std::filesystem::directory_iterator( modelDir ) )
        {
            if ( entry.is_regular_file() )
            {
                std::string filename = entry.path().filename().string();

                // Check for gate model files following pattern "layer_X_gate.mnn"
                std::regex  gatePattern( "layer_(\\d+)_gate\\.mnn" );
                std::smatch match;

                if ( std::regex_search( filename, match, gatePattern ) && match.size() > 1 )
                {
                    int layerId = std::stoi( match[1].str() );

                    // Add the gate model (no need for analysis files anymore)
                    if ( addGateModel( layerId, entry.path().string() ) )
                    {
                        foundAny = true;
                        //std::cout << "Added gate model for layer " << layerId << std::endl;
                    }
                }
            }
        }

        if ( !foundAny )
        {
            std::cerr << "No valid gate models found in directory: " << modelDir << std::endl;
            return false;
        }

        initialized = true;
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error initializing gate weights handler: " << e.what() << std::endl;
        return false;
    }
}

bool GateWeightsHandler::addGateModel( int layerId, const std::string &gatePath )
{
    try
    {
        // Create a new gate model info entry
        GateModelInfo gateInfo;
        gateInfo.layerId   = layerId;
        gateInfo.modelPath = gatePath;

        // Load the MNN model (defer session creation until needed)
        gateInfo.interpreter.reset( MNN::Interpreter::createFromFile( gatePath.c_str() ) );
        if ( !gateInfo.interpreter )
        {
            std::cerr << "Failed to load gate model: " << gatePath << std::endl;
            return false;
        }

        // Add to map
        gateModels[layerId] = std::move( gateInfo );

        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error adding gate model: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> GateWeightsHandler::selectExperts( int layerId, const std::vector<float> &embedding, int topK )
{
    if ( !initialized )
    {
        std::cerr << "Gate weights handler not initialized" << std::endl;
        return { 0 }; // Fallback to expert 0
    }

    auto it = gateModels.find( layerId );
    if ( it == gateModels.end() )
    {
        if ( debugMode )
        {
            std::cout << "No gate model found for layer " << layerId << ", using expert 0" << std::endl;
        }
        return { 0 }; // Fallback to expert 0
    }

    auto &gateInfo = it->second;

    // Create session if needed
    if ( !gateInfo.session )
    {
        gateInfo.session = gateInfo.interpreter->createSession( config );
        if ( !gateInfo.session )
        {
            std::cerr << "Failed to create session for gate model layer " << layerId << std::endl;
            return { 0 }; // Fallback to expert 0
        }
    }

    try
    {
        // Get input tensor
        auto input = gateInfo.interpreter->getSessionInput( gateInfo.session, nullptr );
        if ( !input )
        {
            throw std::runtime_error( "Failed to get input tensor for gate model" );
        }

        // Resize for batch size 1
        gateInfo.interpreter->resizeTensor( input, { 1, (int)embedding.size() } );
        gateInfo.interpreter->resizeSession( gateInfo.session );

        // Copy embedding to input tensor
        MNN::Tensor inputHost( input, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, embedding );
        input->copyFromHostTensor( &inputHost );

        // Run the gate model
        gateInfo.interpreter->runSession( gateInfo.session );

        // Get the output (gate scores)
        auto output = gateInfo.interpreter->getSessionOutput( gateInfo.session, nullptr );
        if ( !output )
        {
            throw std::runtime_error( "Failed to get output tensor for gate model" );
        }

        // Copy to host
        MNN::Tensor outputHost( output, output->getDimensionType() );
        output->copyToHostTensor( &outputHost );
        float *outputData = outputHost.host<float>();

        // Get the number of experts
        int numExperts = output->length( output->dimensions() - 1 );

        // Create vector of (expert id, score) pairs
        std::vector<std::pair<int, float>> expertScores;
        expertScores.reserve( numExperts );

        for ( int i = 0; i < numExperts; i++ )
        {
            expertScores.push_back( { i, outputData[i] } );
        }

        // Sort by score (descending)
        std::sort( expertScores.begin(),
                   expertScores.end(),
                   []( const auto &a, const auto &b ) { return a.second > b.second; } );

        if ( debugMode )
        {
            std::cout << "Gate model for layer " << layerId << " raw scores:" << std::endl;
            for ( int i = 0; i < std::min( 10, (int)expertScores.size() ); i++ )
            {
                std::cout << "  Expert " << expertScores[i].first << ": " << expertScores[i].second << std::endl;
            }
        }

        // Return all experts ranked by score (caller will handle availability filtering)
        std::vector<int> rankedExperts;
        rankedExperts.reserve( numExperts );
        for ( const auto &pair : expertScores )
        {
            rankedExperts.push_back( pair.first );
        }

        return rankedExperts;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running gate model for layer " << layerId << ": " << e.what() << std::endl;
        return { 0 }; // Fallback to expert 0
    }
}

std::vector<int> GateWeightsHandler::selectAvailableExperts( int                       layerId,
                                                             const std::vector<float> &embedding,
                                                             const std::vector<int>   &availableExperts,
                                                             int                       topK )
{
    // Get all experts ranked by the gate model
    std::vector<int> rankedExperts = selectExperts( layerId, embedding, -1 ); // Get all experts

    if ( rankedExperts.empty() )
    {
        // If gate failed, try expert 0 if available
        if ( std::find( availableExperts.begin(), availableExperts.end(), 0 ) != availableExperts.end() )
        {
            return { 0 };
        }
        // Otherwise return the first available expert
        return availableExperts.empty() ? std::vector<int>{} : std::vector<int>{ availableExperts[0] };
    }

    // Filter to only include available experts, maintaining gate's ranking
    std::vector<int> selectedExperts;
    selectedExperts.reserve( topK );

    for ( int expertId : rankedExperts )
    {
        // Check if this expert is available
        if ( std::find( availableExperts.begin(), availableExperts.end(), expertId ) != availableExperts.end() )
        {
            selectedExperts.push_back( expertId );

            if ( debugMode )
            {
                std::cout << "Selected available expert " << expertId << " for layer " << layerId << std::endl;
            }

            // Stop when we have enough experts
            if ( selectedExperts.size() >= (size_t)topK )
            {
                break;
            }
        }
        else if ( debugMode )
        {
            //std::cout << "Expert " << expertId << " recommended by gate but not available for layer " << layerId
            //          << std::endl;
        }
    }

    // If we didn't find enough available experts, add more if possible
    if ( selectedExperts.size() < (size_t)topK )
    {
        for ( int expertId : availableExperts )
        {
            // Add if not already selected
            if ( std::find( selectedExperts.begin(), selectedExperts.end(), expertId ) == selectedExperts.end() )
            {
                selectedExperts.push_back( expertId );

                if ( debugMode )
                {
                    std::cout << "Added additional available expert " << expertId << " for layer " << layerId
                              << std::endl;
                }

                if ( selectedExperts.size() >= (size_t)topK )
                {
                    break;
                }
            }
        }
    }

    // If still no experts, ensure we return at least one if any are available
    if ( selectedExperts.empty() && !availableExperts.empty() )
    {
        selectedExperts.push_back( availableExperts[0] );

        if ( debugMode )
        {
            std::cout << "Fallback: using first available expert " << availableExperts[0] << " for layer " << layerId
                      << std::endl;
        }
    }

    return selectedExperts;
}

void GateWeightsHandler::setDebugMode( bool debug )
{
    debugMode = debug;
}

bool GateWeightsHandler::getDebugMode() const
{
    return debugMode;
}

bool GateWeightsHandler::hasLayerGate( int layerId ) const
{
    return gateModels.find( layerId ) != gateModels.end();
}

void GateWeightsHandler::printGateInfo() const
{
    if ( !initialized || gateModels.empty() )
    {
        std::cout << "No gate models loaded" << std::endl;
        return;
    }

    std::cout << "Loaded Gate Models:" << std::endl;
    for ( const auto &pair : gateModels )
    {
        std::cout << "  Layer " << pair.first << ": " << pair.second.modelPath << std::endl;
    }
}
