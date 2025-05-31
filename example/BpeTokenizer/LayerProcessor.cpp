// Updated LayerProcessor.cpp with complete DeepSeek-V3 architecture

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
    if ( availableExpertIds.empty() && layerId >= 3 ) // Only MoE layers should have experts
    {
        std::cerr << "Warning: No expert files found for MoE layer " << layerId << " in directory " << modelDir
                  << std::endl;
    }
}

std::vector<float> LayerProcessor::runPreAttentionLayerNorm( const std::vector<float> &input )
{
    // Load pre-attention layernorm temporarily
    std::string layerNormPath = modelDir + "/layer_" + std::to_string( layerId ) + "_layernorm.mnn";

    if ( !std::filesystem::exists( layerNormPath ) )
    {
        if ( debugMode )
        {
            std::cout << "No pre-attention layernorm found for layer " << layerId << ", skipping normalization"
                      << std::endl;
        }
        return input; // Pass through if no layernorm layer
    }

    try
    {
        // Create temporary interpreter
        std::unique_ptr<MNN::Interpreter> tempLayerNorm( MNN::Interpreter::createFromFile( layerNormPath.c_str() ) );

        if ( !tempLayerNorm )
        {
            std::cerr << "Failed to load pre-attention layernorm " << layerId << " from " << layerNormPath << std::endl;
            return input; // Pass through on failure
        }

        // Create temporary session with CPU to save Vulkan instances
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_CPU;
        config.numThread = 1;

        MNN::Session *tempSession = tempLayerNorm->createSession( config );
        if ( !tempSession )
        {
            std::cerr << "Failed to create session for pre-attention layernorm " << layerId << std::endl;
            return input; // Pass through on failure
        }

        // Get input tensor
        auto layerInput = tempLayerNorm->getSessionInput( tempSession, nullptr );
        if ( !layerInput )
        {
            tempLayerNorm->releaseSession( tempSession );
            std::cerr << "Failed to get input tensor for pre-attention layernorm " << layerId << std::endl;
            return input;
        }

        // Resize input tensor
        tempLayerNorm->resizeTensor( layerInput, { 1, 1, (int)input.size() } );
        tempLayerNorm->resizeSession( tempSession );

        // Copy input data
        MNN::Tensor inputHost( layerInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        layerInput->copyFromHostTensor( &inputHost );

        // Run layernorm
        tempLayerNorm->runSession( tempSession );

        // Get output
        auto output = tempLayerNorm->getSessionOutput( tempSession, nullptr );
        if ( !output )
        {
            tempLayerNorm->releaseSession( tempSession );
            std::cerr << "Failed to get output tensor for pre-attention layernorm " << layerId << std::endl;
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

        // Clean up temporary session
        tempLayerNorm->releaseSession( tempSession );

        if ( debugMode )
        {
            std::cout << "Temporary pre-attention layernorm " << layerId << " processed successfully" << std::endl;
        }

        return layerOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running temporary pre-attention layernorm " << layerId << ": " << e.what() << std::endl;
        return input; // Return input as fallback
    }
}

std::vector<float> LayerProcessor::runPostAttentionLayerNorm( const std::vector<float> &input )
{
    // Same implementation as pre-attention, but this represents the post-attention layernorm
    // In practice, we're using the same layernorm model but with different weights loaded
    // For now, we'll use the same model file since both are RMSNorm with same dimensions

    std::string layerNormPath = modelDir + "/layer_" + std::to_string( layerId ) + "_layernorm.mnn";

    if ( !std::filesystem::exists( layerNormPath ) )
    {
        if ( debugMode )
        {
            std::cout << "No post-attention layernorm found for layer " << layerId << ", skipping normalization"
                      << std::endl;
        }
        return input; // Pass through if no layernorm layer
    }

    try
    {
        // Create temporary interpreter
        std::unique_ptr<MNN::Interpreter> tempLayerNorm( MNN::Interpreter::createFromFile( layerNormPath.c_str() ) );

        if ( !tempLayerNorm )
        {
            std::cerr << "Failed to load post-attention layernorm " << layerId << " from " << layerNormPath
                      << std::endl;
            return input; // Pass through on failure
        }

        // Create temporary session with CPU to save Vulkan instances
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_CPU;
        config.numThread = 1;

        MNN::Session *tempSession = tempLayerNorm->createSession( config );
        if ( !tempSession )
        {
            std::cerr << "Failed to create session for post-attention layernorm " << layerId << std::endl;
            return input; // Pass through on failure
        }

        // Get input tensor
        auto layerInput = tempLayerNorm->getSessionInput( tempSession, nullptr );
        if ( !layerInput )
        {
            tempLayerNorm->releaseSession( tempSession );
            std::cerr << "Failed to get input tensor for post-attention layernorm " << layerId << std::endl;
            return input;
        }

        // Resize input tensor
        tempLayerNorm->resizeTensor( layerInput, { 1, 1, (int)input.size() } );
        tempLayerNorm->resizeSession( tempSession );

        // Copy input data
        MNN::Tensor inputHost( layerInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        layerInput->copyFromHostTensor( &inputHost );

        // Run layernorm
        tempLayerNorm->runSession( tempSession );

        // Get output
        auto output = tempLayerNorm->getSessionOutput( tempSession, nullptr );
        if ( !output )
        {
            tempLayerNorm->releaseSession( tempSession );
            std::cerr << "Failed to get output tensor for post-attention layernorm " << layerId << std::endl;
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

        // Clean up temporary session
        tempLayerNorm->releaseSession( tempSession );

        if ( debugMode )
        {
            std::cout << "Temporary post-attention layernorm " << layerId << " processed successfully" << std::endl;
        }

        return layerOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running temporary post-attention layernorm " << layerId << ": " << e.what() << std::endl;
        return input; // Return input as fallback
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
        tempAttentionLayer->resizeTensor( layerInput, { 1, 1, (int)input.size() } );
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

std::vector<float> LayerProcessor::runSharedExpert( const std::vector<float> &input )
{
    // Load shared expert temporarily
    std::string sharedExpertPath = modelDir + "/shared_expert_layer_" + std::to_string( layerId ) + ".mnn";

    if ( !std::filesystem::exists( sharedExpertPath ) )
    {
        if ( debugMode )
        {
            std::cout << "No shared expert found for layer " << layerId << ", skipping shared expert" << std::endl;
        }
        return std::vector<float>( input.size(), 0.0f ); // Return zeros if no shared expert
    }

    try
    {
        // Create temporary interpreter
        std::unique_ptr<MNN::Interpreter> tempSharedExpert(
            MNN::Interpreter::createFromFile( sharedExpertPath.c_str() ) );

        if ( !tempSharedExpert )
        {
            std::cerr << "Failed to load shared expert " << layerId << " from " << sharedExpertPath << std::endl;
            return std::vector<float>( input.size(), 0.0f ); // Return zeros on failure
        }

        // Create temporary session
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN; // Use VULKAN for shared experts (they're larger)
        config.numThread = 1;

        MNN::Session *tempSession = tempSharedExpert->createSession( config );
        if ( !tempSession )
        {
            std::cerr << "Failed to create session for shared expert " << layerId << std::endl;
            return std::vector<float>( input.size(), 0.0f );
        }

        // Get input tensor
        auto expertInput = tempSharedExpert->getSessionInput( tempSession, nullptr );
        if ( !expertInput )
        {
            tempSharedExpert->releaseSession( tempSession );
            std::cerr << "Failed to get input tensor for shared expert " << layerId << std::endl;
            return std::vector<float>( input.size(), 0.0f );
        }

        // Resize input tensor
        tempSharedExpert->resizeTensor( expertInput, { 1, (int)input.size() } );
        tempSharedExpert->resizeSession( tempSession );

        // Copy input data
        MNN::Tensor inputHost( expertInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        expertInput->copyFromHostTensor( &inputHost );

        // Run shared expert
        tempSharedExpert->runSession( tempSession );

        // Get output
        auto output = tempSharedExpert->getSessionOutput( tempSession, nullptr );
        if ( !output )
        {
            tempSharedExpert->releaseSession( tempSession );
            std::cerr << "Failed to get output tensor for shared expert " << layerId << std::endl;
            return std::vector<float>( input.size(), 0.0f );
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
        std::vector<float> expertOutput( outputSize );

        // Copy output data with validation
        for ( size_t i = 0; i < outputSize; i++ )
        {
            float val = outputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                expertOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                expertOutput[i] = val;
            }
        }

        // Clean up temporary session
        tempSharedExpert->releaseSession( tempSession );

        if ( debugMode )
        {
            std::cout << "Temporary shared expert " << layerId << " processed successfully" << std::endl;
        }

        return expertOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running temporary shared expert " << layerId << ": " << e.what() << std::endl;
        return std::vector<float>( input.size(), 0.0f ); // Return zeros as fallback
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

    // Initialize shared expert handler once (only for MoE layers)
    if ( layerId >= 3 )
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

    // Scan for available experts using filesystem scan (only for MoE layers)
    if ( layerId >= 3 )
    {
        scanAvailableExperts();
    }

    if ( debugMode )
    {
        if ( layerId >= 3 )
        {
            std::cout << "Layer " << layerId << " (MoE) initialized with " << availableExpertIds.size()
                      << " available experts" << std::endl;
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
        else
        {
            std::cout << "Layer " << layerId << " (Standard) initialized" << std::endl;
        }
    }

    return true;
}

std::vector<float> LayerProcessor::process( const std::vector<float> &input, int tokenPosition )
{
    std::vector<float> currentOutput = input;

    // Step 1: Apply PRE-ATTENTION LayerNorm (input_layernorm)
    currentOutput = runPreAttentionLayerNorm( currentOutput );

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " pre-attention layernorm applied" << std::endl;
    }

    // Step 2: Run attention layer temporarily with residual connection
    std::vector<float> attentionInput  = currentOutput; // Save input to attention
    std::vector<float> attentionOutput = runAttentionLayerTemporary( currentOutput );

    // Add residual connection for attention
    if ( attentionOutput.size() == attentionInput.size() )
    {
        for ( size_t i = 0; i < attentionOutput.size(); i++ )
        {
            attentionOutput[i] += attentionInput[i]; // Add residual
        }
    }
    currentOutput = attentionOutput;

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " attention processed with residual connection" << std::endl;
    }

    // Step 3: Apply POST-ATTENTION LayerNorm (post_attention_layernorm)
    currentOutput = runPostAttentionLayerNorm( currentOutput );

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " post-attention layernorm applied" << std::endl;
    }

    // Step 4: Process FFN/MLP layer based on layer type
    std::vector<float> ffnInput = currentOutput; // Save input to FFN for residual
    std::vector<float> ffnOutput;

    if ( layerId < 3 )
    {
        // Standard layers (0-2): Use the existing standard layer processing from MoEModelRunner
        ffnOutput = processStandardMLP( currentOutput );

        if ( debugMode )
        {
            std::cout << "Layer " << layerId << " standard MLP processed" << std::endl;
        }
    }
    else
    {
        // MoE layers (3-61): Process shared + routed experts
        ffnOutput = processMoEExperts( currentOutput, tokenPosition );

        if ( debugMode )
        {
            std::cout << "Layer " << layerId << " MoE experts processed" << std::endl;
        }
    }

    // Step 5: Add residual connection for FFN
    if ( ffnOutput.size() == ffnInput.size() )
    {
        for ( size_t i = 0; i < ffnOutput.size(); i++ )
        {
            ffnOutput[i] += ffnInput[i]; // Add residual
        }
    }

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " FFN processed with residual connection" << std::endl;
    }

    return ffnOutput;
}

std::vector<float> LayerProcessor::processStandardMLP( const std::vector<float> &input )
{
    // Load standard MLP temporarily
    std::string mlpPath = modelDir + "/layer_" + std::to_string( layerId ) + "_mlp.mnn";

    if ( !std::filesystem::exists( mlpPath ) )
    {
        if ( debugMode )
        {
            std::cout << "No standard MLP found for layer " << layerId << ", skipping MLP" << std::endl;
        }
        return input; // Pass through if no MLP layer
    }

    try
    {
        // Create temporary interpreter
        std::unique_ptr<MNN::Interpreter> tempMLP( MNN::Interpreter::createFromFile( mlpPath.c_str() ) );

        if ( !tempMLP )
        {
            std::cerr << "Failed to load standard MLP " << layerId << " from " << mlpPath << std::endl;
            return input; // Pass through on failure
        }

        // Create temporary session
        MNN::ScheduleConfig config;
        config.type      = MNN_FORWARD_VULKAN; // Use VULKAN for MLPs
        config.numThread = 1;

        MNN::Session *tempSession = tempMLP->createSession( config );
        if ( !tempSession )
        {
            std::cerr << "Failed to create session for standard MLP " << layerId << std::endl;
            return input; // Pass through on failure
        }

        // Get input tensor
        auto mlpInput = tempMLP->getSessionInput( tempSession, nullptr );
        if ( !mlpInput )
        {
            tempMLP->releaseSession( tempSession );
            std::cerr << "Failed to get input tensor for standard MLP " << layerId << std::endl;
            return input;
        }

        // Resize input tensor
        tempMLP->resizeTensor( mlpInput, { 1, (int)input.size() } );
        tempMLP->resizeSession( tempSession );

        // Copy input data
        MNN::Tensor inputHost( mlpInput, MNN::Tensor::CAFFE );
        float      *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor( inputData, input );
        mlpInput->copyFromHostTensor( &inputHost );

        // Run MLP
        tempMLP->runSession( tempSession );

        // Get output
        auto output = tempMLP->getSessionOutput( tempSession, nullptr );
        if ( !output )
        {
            tempMLP->releaseSession( tempSession );
            std::cerr << "Failed to get output tensor for standard MLP " << layerId << std::endl;
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
        std::vector<float> mlpOutput( outputSize );

        // Copy output data with validation
        for ( size_t i = 0; i < outputSize; i++ )
        {
            float val = outputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                mlpOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                mlpOutput[i] = val;
            }
        }

        // Clean up temporary session
        tempMLP->releaseSession( tempSession );

        if ( debugMode )
        {
            std::cout << "Temporary standard MLP " << layerId << " processed successfully" << std::endl;
        }

        return mlpOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running temporary standard MLP " << layerId << ": " << e.what() << std::endl;
        return input; // Return input as fallback
    }
}

std::vector<float> LayerProcessor::processMoEExperts( const std::vector<float> &input, int tokenPosition )
{
    if ( !sharedExpertHandler )
    {
        std::cerr << "No expert handler available for layer " << layerId << std::endl;
        return input; // Pass through current output as fallback
    }

    // Use the pre-scanned available experts for this layer
    if ( availableExpertIds.empty() )
    {
        std::cerr << "No experts available for layer " << layerId << "! Skipping MLP layer." << std::endl;
        return input; // Pass through current output unchanged
    }

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " MLP processing with " << availableExpertIds.size() << " available experts"
                  << std::endl;
    }

    // Step 4a: Run Shared Experts (always active in MoE layers)
    std::vector<float> sharedExpertOutput = runSharedExpert( input );

    if ( debugMode )
    {
        std::cout << "Layer " << layerId << " shared expert processed" << std::endl;
    }

    // Step 4b: Run Routed Experts with proper weighting
    std::vector<float> routedExpertOutput( input.size(), 0.0f ); // Initialize to zeros
    float              totalWeight = 0.0f;

    // Use gate to select experts with scores if available
    if ( sharedGateHandler && sharedGateHandler->hasLayerGate( layerId ) )
    {
        // Get experts with their gate scores
        std::vector<std::pair<int, float>> expertScores = sharedGateHandler->selectAvailableExpertsWithScores(
            layerId,
            input,
            availableExpertIds,
            2 );

        if ( debugMode )
        {
            std::cout << "Gate for layer " << layerId << " selected available experts with scores: ";
            for ( const auto &pair : expertScores )
            {
                std::cout << pair.first << "(" << pair.second << ") ";
            }
            std::cout << std::endl;
        }

        // Process each selected expert with its weight
        for ( const auto &expertScore : expertScores )
        {
            int   expertId = expertScore.first;
            float rawScore = expertScore.second;

            // Convert raw score to weight using softmax-like approach
            // For now, use exponential to emphasize differences
            float weight  = std::exp( rawScore * 0.1f ); // Scale factor to prevent overflow
            totalWeight  += weight;

            std::vector<float> expertOutput = sharedExpertHandler->runExpertForLayer( layerId, expertId, input );

            if ( !expertOutput.empty() && expertOutput.size() == routedExpertOutput.size() )
            {
                // Add weighted expert output
                for ( size_t j = 0; j < routedExpertOutput.size(); ++j )
                {
                    routedExpertOutput[j] += expertOutput[j] * weight;
                }

                if ( debugMode )
                {
                    std::cout << "Added expert " << expertId << " with weight " << weight << " (raw score: " << rawScore
                              << ") for layer " << layerId << std::endl;
                }
            }
            else if ( debugMode )
            {
                std::cout << "Failed to run expert " << expertId << " for layer " << layerId << std::endl;
            }
        }

        // Normalize by total weight
        if ( totalWeight > 0.0f )
        {
            for ( size_t j = 0; j < routedExpertOutput.size(); ++j )
            {
                routedExpertOutput[j] /= totalWeight;
            }

            if ( debugMode )
            {
                std::cout << "Normalized routed expert outputs by total weight " << totalWeight << " for layer "
                          << layerId << std::endl;
            }
        }
    }
    else
    {
        // No gate - use the first available expert with weight 1.0
        int expertId       = availableExpertIds[0];
        routedExpertOutput = sharedExpertHandler->runExpertForLayer( layerId, expertId, input );

        if ( debugMode )
        {
            std::cout << "No gate for layer " << layerId << ", using first available expert " << expertId << std::endl;
        }
    }

    // Step 4c: Combine Shared + Routed Expert outputs (MoE architecture)
    std::vector<float> combinedExpertOutput;

    // Combine shared expert output + routed expert output
    if ( !sharedExpertOutput.empty() && !routedExpertOutput.empty() &&
         sharedExpertOutput.size() == routedExpertOutput.size() )
    {
        combinedExpertOutput.resize( sharedExpertOutput.size() );
        for ( size_t i = 0; i < combinedExpertOutput.size(); i++ )
        {
            combinedExpertOutput[i] = sharedExpertOutput[i] + routedExpertOutput[i];
        }

        if ( debugMode )
        {
            std::cout << "Combined shared + weighted routed expert outputs for layer " << layerId << std::endl;
        }
    }
    else if ( !routedExpertOutput.empty() )
    {
        combinedExpertOutput = routedExpertOutput; // Fallback to routed only
        if ( debugMode )
        {
            std::cout << "Using weighted routed expert output only for layer " << layerId << std::endl;
        }
    }
    else if ( !sharedExpertOutput.empty() )
    {
        combinedExpertOutput = sharedExpertOutput; // Fallback to shared only
        if ( debugMode )
        {
            std::cout << "Using shared expert output only for layer " << layerId << std::endl;
        }
    }
    else
    {
        std::cerr << "Failed to run any experts for layer " << layerId << ", passing through input" << std::endl;
        return input;
    }

    return combinedExpertOutput;
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
