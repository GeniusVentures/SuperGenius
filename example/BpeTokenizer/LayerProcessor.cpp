#include "LayerProcessor.hpp"


LayerProcessor::LayerProcessor( int layerId, const std::string &modelDir, bool debug ) :
    layerId( layerId ), modelDir( modelDir ), debugMode( debug )
{
}

std::vector<int> LayerProcessor::scanExpertsFromFilesystem() const
{
    std::vector<int> availableExperts;

    try
    {
        // Scan the model directory for expert files for this specific layer
        std::string layerPattern = "expert_layer" + std::to_string( layerId ) + "_(\\d+)\\.mnn";
        std::regex  expertRegex( layerPattern );

        if ( std::filesystem::exists( modelDir ) )
        {
            for ( const auto &entry : std::filesystem::directory_iterator( modelDir ) )
            {
                if ( entry.is_regular_file() )
                {
                    std::string filename = entry.path().filename().string();
                    std::smatch match;

                    if ( std::regex_search( filename, match, expertRegex ) && match.size() > 1 )
                    {
                        int expertId = std::stoi( match[1].str() );
                        availableExperts.push_back( expertId );

                        if ( debugMode )
                        {
                            std::cout << "Found expert file for layer " << layerId << ", expert " << expertId << ": "
                                      << filename << std::endl;
                        }
                    }
                }
            }
        }
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error scanning for expert files: " << e.what() << std::endl;
    }

    // Sort the expert IDs
    std::sort( availableExperts.begin(), availableExperts.end() );

    return availableExperts;
}

void LayerProcessor::scanAvailableExperts()
{
    // Use filesystem-based scanning instead of relying on the shared expert handler
    availableExpertIds = scanExpertsFromFilesystem();

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " filesystem scan found " << availableExpertIds.size() << " experts"
                  << std::endl;
    }

    // If no experts found via filesystem, this might indicate a problem
    if ( availableExpertIds.empty() )
    {
        std::cerr << "Warning: No expert files found for layer " << layerId << " in directory " << modelDir
                  << std::endl;
    }
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

            // Use the main model directory (experts are stored as expert_layerX_Y.mnn files)
            if ( !sharedExpertHandler->initialize( modelDir ) )
            {
                std::cerr << "Warning: Failed to initialize shared expert handler" << std::endl;
                return false;
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

    // Scan for available experts using filesystem scan
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

std::vector<float> LayerProcessor::process( const std::vector<float> &input, int tokenPosition )
{
    if ( !sharedExpertHandler )
    {
        std::cerr << "No expert handler available for layer " << layerId << std::endl;
        return input; // Pass through input as fallback
    }

    // Use the pre-scanned available experts for this layer
    if ( availableExpertIds.empty() )
    {
        std::cerr << "No experts available for layer " << layerId << "! Skipping layer." << std::endl;
        return input; // Pass through unchanged
    }

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " processing with " << availableExpertIds.size() << " available experts"
                  << std::endl;
    }

    std::vector<int> selectedExperts;

    // Use gate to select experts if available
    if ( sharedGateHandler && sharedGateHandler->hasLayerGate( layerId ) )
    {
        // Use the new method that handles availability filtering
        selectedExperts = sharedGateHandler->selectAvailableExperts( layerId, input, availableExpertIds, 2 );

        if ( debugMode )
        {
            std::cout << "Gate for layer " << layerId << " selected available experts: ";
            for ( int id : selectedExperts )
            {
                std::cout << id << " ";
            }
            std::cout << std::endl;
        }
    }
    else
    {
        // No gate - use the first available expert (typically expert 0)
        selectedExperts.push_back( availableExpertIds[0] );

        if ( debugMode )
        {
            std::cout << "No gate for layer " << layerId << ", using first available expert " << availableExpertIds[0]
                      << std::endl;
        }
    }

    // Ensure we have at least one expert to run
    if ( selectedExperts.empty() )
    {
        selectedExperts.push_back( availableExpertIds[0] );

        if ( debugMode )
        {
            std::cout << "Fallback: using first available expert " << availableExpertIds[0] << " for layer " << layerId
                      << std::endl;
        }
    }

    // Run selected experts with proper layer context
    std::vector<float> resultOutput;
    int                successfulExperts = 0;

    for ( int expertId : selectedExperts )
    {
        std::vector<float> expertOutput = sharedExpertHandler->runExpertForLayer( layerId, expertId, input );

        // Add residual connection
        if ( !expertOutput.empty() && expertOutput.size() == input.size() )
        {
            for ( size_t i = 0; i < expertOutput.size(); i++ )
            {
                expertOutput[i] += input[i]; // Add input back (residual connection)
            }
        }

        if ( !expertOutput.empty() )
        {
            successfulExperts++;

            if ( resultOutput.empty() )
            {
                resultOutput = expertOutput;
            }
            else
            {
                // Average with previous results
                for ( size_t j = 0; j < resultOutput.size() && j < expertOutput.size(); ++j )
                {
                    resultOutput[j] += expertOutput[j];
                }
            }

            if ( debugMode )
            {
                std::cout << "Successfully ran expert " << expertId << " for layer " << layerId << std::endl;
            }
        }
        else if ( debugMode )
        {
            std::cout << "Failed to run expert " << expertId << " for layer " << layerId << std::endl;
        }
    }

    // Average the results if we ran multiple experts
    if ( successfulExperts > 1 )
    {
        for ( size_t j = 0; j < resultOutput.size(); ++j )
        {
            resultOutput[j] /= successfulExperts;
        }

        if ( debugMode )
        {
            std::cout << "Averaged results from " << successfulExperts << " experts for layer " << layerId << std::endl;
        }
    }

    // If no experts ran successfully, return input as fallback
    if ( successfulExperts == 0 )
    {
        std::cerr << "Failed to run any experts for layer " << layerId << ", passing through input" << std::endl;
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
