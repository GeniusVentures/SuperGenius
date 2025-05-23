#include "LayerProcessor.hpp"

LayerProcessor::LayerProcessor( int layerId, const std::string &modelDir, bool debug ) :
    layerId( layerId ), modelDir( modelDir ), debugMode( debug )
{
    // No longer store attention layer in memory
}

LayerProcessor::~LayerProcessor()
{
    // No cleanup needed since we're not storing sessions
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

std::vector<float> LayerProcessor::runAttentionLayerTemporary( const std::vector<float> &input )
{
    // Load attention layer temporarily
    std::string attentionPath = modelDir + "/layer_" + std::to_string( layerId ) + "_attention.mnn";

    if ( !std::filesystem::exists( attentionPath ) )
    {
        if ( debugMode )
        {
            std::cout << "No attention layer found for layer " << layerId << ", skipping attention" << std::endl;
        }
        return input; // Pass through if no attention layer
    }

    try
    {
        // Create temporary interpreter
        std::unique_ptr<MNN::Interpreter> tempAttentionLayer(
            MNN::Interpreter::createFromFile( attentionPath.c_str() ) );

        if ( !tempAttentionLayer )
        {
            std::cerr << "Failed to load attention layer " << layerId << " from " << attentionPath << std::endl;
            return input; // Pass through on failure
        }

        // Create temporary session with CPU to save Vulkan instances
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_CPU;
        config.numThread = 1;

        MNN::Session *tempSession = tempAttentionLayer->createSession( config );
        if ( !tempSession )
        {
            std::cerr << "Failed to create session for attention layer " << layerId << std::endl;
            return input; // Pass through on failure
        }

        // Get input tensor
        auto layerInput = tempAttentionLayer->getSessionInput( tempSession, nullptr );
        if ( !layerInput )
        {
            tempAttentionLayer->releaseSession( tempSession );
            std::cerr << "Failed to get input tensor for attention layer " << layerId << std::endl;
            return input;
        }

        // Resize input tensor
        tempAttentionLayer->resizeTensor( layerInput, { 1, (int)input.size() } );
        tempAttentionLayer->resizeSession( tempSession );

        // Copy input data
        MNN::Tensor inputHost( layerInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        layerInput->copyFromHostTensor( &inputHost );

        // Run attention layer
        tempAttentionLayer->runSession( tempSession );

        // Get output
        auto output = tempAttentionLayer->getSessionOutput( tempSession, nullptr );
        if ( !output )
        {
            tempAttentionLayer->releaseSession( tempSession );
            std::cerr << "Failed to get output tensor for attention layer " << layerId << std::endl;
            return input;
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

        // Copy output data with validation
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

        // Add residual connection for attention
        if ( layerOutput.size() == input.size() )
        {
            for ( size_t i = 0; i < layerOutput.size(); i++ )
            {
                layerOutput[i] += input[i]; // Add input back (residual connection)
            }
        }

        // Clean up temporary session
        tempAttentionLayer->releaseSession( tempSession );

        if ( debugMode )
        {
            std::cout << "Temporary attention layer " << layerId << " processed successfully" << std::endl;
        }

        return layerOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running temporary attention layer " << layerId << ": " << e.what() << std::endl;
        return input; // Return input as fallback
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
    std::vector<float> currentOutput = input;

    // Step 1: Run attention layer temporarily (loads and unloads immediately)
    currentOutput = runAttentionLayerTemporary( currentOutput );

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " attention processed (temporary)" << std::endl;
    }

    // Step 2: Run MLP/Expert layer (using existing shared handlers)
    if ( !sharedExpertHandler )
    {
        std::cerr << "No expert handler available for layer " << layerId << std::endl;
        return currentOutput; // Pass through current output as fallback
    }

    // Use the pre-scanned available experts for this layer
    if ( availableExpertIds.empty() )
    {
        std::cerr << "No experts available for layer " << layerId << "! Skipping MLP layer." << std::endl;
        return currentOutput; // Pass through current output unchanged
    }

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " MLP processing with " << availableExpertIds.size() << " available experts"
                  << std::endl;
    }

    std::vector<int> selectedExperts;

    // Use gate to select experts if available
    if ( sharedGateHandler && sharedGateHandler->hasLayerGate( layerId ) )
    {
        // Use the new method that handles availability filtering
        selectedExperts = sharedGateHandler->selectAvailableExperts( layerId, currentOutput, availableExpertIds, 2 );

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
    std::vector<float> mlpOutput;
    int                successfulExperts = 0;

    for ( int expertId : selectedExperts )
    {
        std::vector<float> expertOutput = sharedExpertHandler->runExpertForLayer( layerId, expertId, currentOutput );

        if ( !expertOutput.empty() )
        {
            successfulExperts++;

            if ( mlpOutput.empty() )
            {
                mlpOutput = expertOutput;
            }
            else
            {
                // Average with previous results
                for ( size_t j = 0; j < mlpOutput.size() && j < expertOutput.size(); ++j )
                {
                    mlpOutput[j] += expertOutput[j];
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
        for ( size_t j = 0; j < mlpOutput.size(); ++j )
        {
            mlpOutput[j] /= successfulExperts;
        }

        if ( debugMode )
        {
            std::cout << "Averaged results from " << successfulExperts << " experts for layer " << layerId << std::endl;
        }
    }

    // If no experts ran successfully, return current output as fallback
    if ( successfulExperts == 0 )
    {
        std::cerr << "Failed to run any experts for layer " << layerId << ", passing through current output"
                  << std::endl;
        return currentOutput;
    }

    // Add residual connection for MLP part
    if ( mlpOutput.size() == currentOutput.size() )
    {
        for ( size_t i = 0; i < mlpOutput.size(); i++ )
        {
            mlpOutput[i] += currentOutput[i]; // Add post-attention output back (residual connection)
        }
    }

    return mlpOutput;
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
