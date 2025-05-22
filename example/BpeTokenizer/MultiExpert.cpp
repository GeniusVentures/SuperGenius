#include "MultiExpert.hpp"

MultiExpertHandler::MultiExpertHandler() : initialized( false ), expertStrategy( 0 ), debugMode( false )
{
    // Default config
    config.type      = MNN_FORWARD_VULKAN; // Use VULKAN by default
    config.numThread = 1;
}

MultiExpertHandler::~MultiExpertHandler()
{
    // Clean up sessions
    for ( auto &expert : experts )
    {
        if ( expert.interpreter && expert.session )
        {
            expert.interpreter->releaseSession( expert.session );
            expert.session = nullptr;
        }
    }
}

// Set strategy - 0 for round robin, 1 for average all
void MultiExpertHandler::setStrategy( int strategy )
{
    if ( strategy >= 0 && strategy <= 1 )
    {
        expertStrategy = strategy;
        std::cout << "Expert strategy set to: " << ( expertStrategy == 0 ? "Round Robin" : "Average All" ) << std::endl;
    }
}

void MultiExpertHandler::setDebugMode( bool debug )
{
    debugMode = debug;
    std::cout << "Debug mode " << ( debug ? "enabled" : "disabled" ) << std::endl;
}

// Initialize by scanning modelDir for expert model files
bool MultiExpertHandler::initialize( const std::string &modelDir )
{
    this->modelDir = modelDir;

    try
    {
        std::vector<std::string> expertFiles;

        // First, try to find layer-specific experts (expert_layer{N}_{ID}.mnn)
        std::string layerSpecificPattern = "expert_layer(\\d+)_(\\d+)\\.mnn";
        std::regex  layerRegex( layerSpecificPattern );

        for ( const auto &entry : std::filesystem::directory_iterator( modelDir ) )
        {
            if ( entry.is_regular_file() )
            {
                std::string filename = entry.path().filename().string();
                std::smatch match;
                if ( std::regex_search( filename, match, layerRegex ) )
                {
                    expertFiles.push_back( filename );
                    //std::cout << "Found layer-specific expert: " << filename << std::endl;
                }
            }
        }

        if ( expertFiles.empty() )
        {
            std::cerr << "No expert models found in directory: " << modelDir << std::endl;
            return false;
        }

        std::cout << "Found " << expertFiles.size() << " expert model files" << std::endl;

        // Process each expert file
        for ( const auto &filename : expertFiles )
        {
            ExpertModel expert;
            expert.modelPath = modelDir + "/" + filename;

            // Extract layer and expert IDs
            std::regex  idRegex( "expert_layer(\\d+)_(\\d+)" );
            std::smatch match;
            if ( std::regex_search( filename, match, idRegex ) && match.size() > 2 )
            {
                expert.layerId  = std::stoi( match[1].str() );
                expert.expertId = std::stoi( match[2].str() );

                //std::cout << "Parsed expert: Layer " << expert.layerId << ", Expert " << expert.expertId << " -> "
                //          << expert.modelPath << std::endl;
            }
            else
            {
                std::cerr << "Could not parse layer/expert ID from filename: " << filename << std::endl;
                continue;
            }

            experts.push_back( std::move( expert ) );
        }

        // Sort by layer ID, then expert ID for consistent ordering
        std::sort( experts.begin(),
                   experts.end(),
                   []( const ExpertModel &a, const ExpertModel &b )
                   {
                       if ( a.layerId != b.layerId )
                       {
                           return a.layerId < b.layerId;
                       }
                       return a.expertId < b.expertId;
                   } );

        std::cout << "Loaded " << experts.size() << " expert models:" << std::endl;
        for ( size_t i = 0; i < experts.size(); ++i )
        {
            std::cout << "  Index " << i << ": Layer " << experts[i].layerId << ", Expert " << experts[i].expertId
                      << " - " << experts[i].modelPath << std::endl;
        }

        buildExpertIdMapping();
        initialized = true;
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error initializing experts: " << e.what() << std::endl;
        return false;
    }
}

// Load a specific expert model when needed
bool MultiExpertHandler::loadExpertModel( int index )
{
    if ( !initialized )
    {
        return false;
    }
    if ( index < 0 || index >= (int)experts.size() )
    {
        return false;
    }

    // Skip if already loaded with active session
    if ( experts[index].interpreter && experts[index].session )
    {
        return true;
    }

    try
    {
        // Create interpreter if needed
        if ( !experts[index].interpreter )
        {
            experts[index].interpreter.reset( MNN::Interpreter::createFromFile( experts[index].modelPath.c_str() ) );
            if ( !experts[index].interpreter )
            {
                std::cerr << "Failed to load expert model: " << experts[index].modelPath << std::endl;
                return false;
            }
        }

        // Create session
        experts[index].session = experts[index].interpreter->createSession( config );
        if ( !experts[index].session )
        {
            std::cerr << "Failed to create session for expert " << index << std::endl;
            return false;
        }

        std::cout << "Loaded expert model " << index << " from " << experts[index].modelPath << std::endl;
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error loading expert model: " << e.what() << std::endl;
        return false;
    }
}

// Get the number of experts
int MultiExpertHandler::getExpertCount() const
{
    return experts.size();
}

// Run single expert model
std::vector<float> MultiExpertHandler::runSingleExpert( int expertId, const std::vector<float> &embedding )
{
    // Convert expert ID to index if needed
    int expertIndex = expertId;

    // If the ID is greater than our expert count, it might be a global expert ID
    if ( expertId >= (int)experts.size() )
    {
        expertIndex = expertIdToModelIndex( expertId );

        if ( expertIndex == -1 )
        {
            std::cerr << "Invalid expert ID: " << expertId << std::endl;
            return {};
        }
    }

    if ( !initialized || expertIndex < 0 || expertIndex >= (int)experts.size() )
    {
        std::cerr << "Invalid expert index: " << expertIndex << std::endl;
        return {};
    }

    // Load the model if needed
    if ( !loadExpertModel( expertIndex ) )
    {
        std::cerr << "Failed to load expert model " << expertIndex << std::endl;
        return {};
    }

    try
    {
        // Get input tensor
        auto expertInput = experts[expertIndex].interpreter->getSessionInput( experts[expertIndex].session, nullptr );
        if ( !expertInput )
        {
            throw std::runtime_error( "Failed to get input tensor" );
        }

        // Resize input tensor
        experts[expertIndex].interpreter->resizeTensor( expertInput, { 1, (int)embedding.size() } );
        experts[expertIndex].interpreter->resizeSession( experts[expertIndex].session );

        // Copy embedding to input tensor
        MNN::Tensor expertInputHost( expertInput, MNN::Tensor::CAFFE );
        float      *expertInputData = expertInputHost.host<float>();
        LLMUtility::safeCopyToTensor( expertInputData, embedding );
        expertInput->copyFromHostTensor( &expertInputHost );

        // Run expert model
        if ( debugMode )
        {
            std::cout << "Running expert model " << expertIndex << "..." << std::endl;
        }
        experts[expertIndex].interpreter->runSession( experts[expertIndex].session );

        // Get output
        auto expertOutputTensor = experts[expertIndex].interpreter->getSessionOutput( experts[expertIndex].session,
                                                                                      nullptr );
        if ( !expertOutputTensor )
        {
            throw std::runtime_error( "Failed to get output tensor" );
        }

        // Save output tensor for debugging
        if ( debugMode )
        {
            LLMUtility::saveTensorToFile( expertOutputTensor,
                                          "expert" + std::to_string( expertIndex ) + "_output.bin" );
        }

        // Copy to host
        MNN::Tensor expertOutputHost( expertOutputTensor, MNN::Tensor::CAFFE );
        expertOutputTensor->copyToHostTensor( &expertOutputHost );
        float *expertOutputData = expertOutputHost.host<float>();

        // Calculate output size
        size_t expertOutputSize = 1;
        for ( int d = 0; d < expertOutputTensor->dimensions(); d++ )
        {
            expertOutputSize *= expertOutputTensor->length( d );
        }

        // Create vector for expert output
        std::vector<float> expertOutput( expertOutputSize );

        // Check and copy values
        for ( size_t i = 0; i < expertOutputSize; i++ )
        {
            float val = expertOutputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                expertOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                expertOutput[i] = val;
            }
        }

        return expertOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running expert model " << expertIndex << ": " << e.what() << std::endl;
        return {};
    }
}

std::vector<float> MultiExpertHandler::runSingleExpertWithDiagnostics( int                       expertId,
                                                                       const std::vector<float> &embedding )
{
    // Convert expert ID to index if needed
    int expertIndex = expertId;

    // If the ID is greater than our expert count, it might be a global expert ID
    if ( expertId >= (int)experts.size() )
    {
        expertIndex = expertIdToModelIndex( expertId );

        if ( expertIndex == -1 )
        {
            std::cerr << "Invalid expert ID: " << expertId << std::endl;
            return {};
        }
    }

    if ( !initialized || expertIndex < 0 || expertIndex >= (int)experts.size() )
    {
        std::cerr << "Invalid expert index: " << expertIndex << std::endl;
        return {};
    }

    // Load the model if needed
    if ( !loadExpertModel( expertIndex ) )
    {
        std::cerr << "Failed to load expert model " << expertIndex << std::endl;
        return {};
    }

    try
    {
        std::cout << "\n=== EXPERT MODEL DIAGNOSTICS ===" << std::endl;
        std::cout << "Expert Index: " << expertIndex << ", Expert ID: " << expertId << std::endl;
        std::cout << "Layer: " << experts[expertIndex].layerId << std::endl;
        std::cout << "Model Path: " << experts[expertIndex].modelPath << std::endl;

        // Analyze input embedding
        if ( !embedding.empty() )
        {
            float minVal = *std::min_element( embedding.begin(), embedding.end() );
            float maxVal = *std::max_element( embedding.begin(), embedding.end() );
            float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
            float mean   = sum / embedding.size();

            std::cout << "Input Embedding Stats:" << std::endl;
            std::cout << "  Size: " << embedding.size() << std::endl;
            std::cout << "  Range: [" << minVal << ", " << maxVal << "]" << std::endl;
            std::cout << "  Mean: " << mean << std::endl;
            std::cout << "  First 5 values: ";
            for ( int i = 0; i < std::min( 5, (int)embedding.size() ); i++ )
            {
                std::cout << embedding[i] << " ";
            }
            std::cout << std::endl;
        }

        // Get input tensor
        auto expertInput = experts[expertIndex].interpreter->getSessionInput( experts[expertIndex].session, nullptr );
        if ( !expertInput )
        {
            throw std::runtime_error( "Failed to get input tensor" );
        }

        std::cout << "Input Tensor Info:" << std::endl;
        LLMUtility::printTensorInfo( expertInput, "  Expert Input" );

        // Resize input tensor
        experts[expertIndex].interpreter->resizeTensor( expertInput, { 1, (int)embedding.size() } );
        experts[expertIndex].interpreter->resizeSession( experts[expertIndex].session );

        std::cout << "After Resize:" << std::endl;
        LLMUtility::printTensorInfo( expertInput, "  Expert Input" );

        // Copy embedding to input tensor
        MNN::Tensor expertInputHost( expertInput, MNN::Tensor::CAFFE );
        float      *expertInputData = expertInputHost.host<float>();

        std::cout << "Copying " << embedding.size() << " values to input tensor..." << std::endl;
        LLMUtility::safeCopyToTensor( expertInputData, embedding );
        expertInput->copyFromHostTensor( &expertInputHost );

        // Verify the copy worked
        std::cout << "Input tensor after copy - first 5 values: ";
        for ( int i = 0; i < std::min( 5, (int)embedding.size() ); i++ )
        {
            std::cout << expertInputData[i] << " ";
        }
        std::cout << std::endl;

        // Run expert model
        std::cout << "Running expert model..." << std::endl;
        experts[expertIndex].interpreter->runSession( experts[expertIndex].session );
        std::cout << "Expert model execution completed." << std::endl;

        // Get output
        auto expertOutputTensor = experts[expertIndex].interpreter->getSessionOutput( experts[expertIndex].session,
                                                                                      nullptr );
        if ( !expertOutputTensor )
        {
            throw std::runtime_error( "Failed to get output tensor" );
        }

        std::cout << "Output Tensor Info:" << std::endl;
        LLMUtility::printTensorInfo( expertOutputTensor, "  Expert Output" );

        // Copy to host
        MNN::Tensor expertOutputHost( expertOutputTensor, MNN::Tensor::CAFFE );
        expertOutputTensor->copyToHostTensor( &expertOutputHost );
        float *expertOutputData = expertOutputHost.host<float>();

        // Calculate output size
        size_t expertOutputSize = 1;
        for ( int d = 0; d < expertOutputTensor->dimensions(); d++ )
        {
            expertOutputSize *= expertOutputTensor->length( d );
        }

        std::cout << "Raw output tensor data (first 10 values):" << std::endl;
        for ( int i = 0; i < std::min( 10, (int)expertOutputSize ); i++ )
        {
            std::cout << "  [" << i << "]: " << expertOutputData[i] << std::endl;
        }

        // Analyze raw output before any processing
        if ( expertOutputSize > 0 )
        {
            float minVal     = expertOutputData[0];
            float maxVal     = expertOutputData[0];
            float sum        = 0.0f;
            int   validCount = 0;
            int   nanCount   = 0;
            int   infCount   = 0;
            int   zeroCount  = 0;

            for ( size_t i = 0; i < expertOutputSize; i++ )
            {
                float val = expertOutputData[i];

                if ( std::isnan( val ) )
                {
                    nanCount++;
                }
                else if ( std::isinf( val ) )
                {
                    infCount++;
                }
                else if ( std::abs( val ) < 1e-30 )
                {
                    zeroCount++;
                }
                else
                {
                    validCount++;
                    sum    += val;
                    minVal  = std::min( minVal, val );
                    maxVal  = std::max( maxVal, val );
                }
            }

            std::cout << "Raw Output Analysis:" << std::endl;
            std::cout << "  Total values: " << expertOutputSize << std::endl;
            std::cout << "  Valid values: " << validCount << std::endl;
            std::cout << "  Near-zero values: " << zeroCount << std::endl;
            std::cout << "  NaN values: " << nanCount << std::endl;
            std::cout << "  Inf values: " << infCount << std::endl;

            if ( validCount > 0 )
            {
                std::cout << "  Valid range: [" << minVal << ", " << maxVal << "]" << std::endl;
                std::cout << "  Valid mean: " << ( sum / validCount ) << std::endl;
            }
        }

        // Create vector for expert output
        std::vector<float> expertOutput( expertOutputSize );

        // Check and copy values
        for ( size_t i = 0; i < expertOutputSize; i++ )
        {
            float val = expertOutputData[i];
            if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
            {
                expertOutput[i] = 0.0f; // Replace invalid values
            }
            else
            {
                expertOutput[i] = val;
            }
        }

        std::cout << "=== END EXPERT DIAGNOSTICS ===\n" << std::endl;

        return expertOutput;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Error running expert model " << expertIndex << ": " << e.what() << std::endl;
        return {};
    }
}


// Run all experts and average their outputs
std::vector<float> MultiExpertHandler::runAverageAllExperts( const std::vector<float> &embedding )
{
    if ( !initialized || experts.empty() )
    {
        std::cerr << "No expert models available" << std::endl;
        return {};
    }

    std::vector<float> resultOutput;
    int                successfulExperts = 0;

    // Run all experts
    for ( size_t i = 0; i < experts.size(); ++i )
    {
        std::vector<float> expertOutput = runSingleExpert( i, embedding );

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
                for ( size_t j = 0; j < resultOutput.size(); ++j )
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

    if ( successfulExperts == 0 )
    {
        std::cerr << "Failed to run any expert models" << std::endl;
        return {};
    }

    return resultOutput;
}

// Run expert based on token position (round robin)
std::vector<float> MultiExpertHandler::runExpertForToken( int tokenPosition, const std::vector<float> &embedding )
{
    if ( !initialized || experts.empty() )
    {
        std::cerr << "No expert models available" << std::endl;
        return {};
    }

    // Select expert based on token position (round robin)
    int expertIndex = tokenPosition % experts.size();

    if ( debugMode )
    {
        std::cout << "Token position " << tokenPosition << " using expert " << expertIndex << std::endl;
    }

    return runSingleExpert( expertIndex, embedding );
}

// Main run function that uses the selected strategy
std::vector<float> MultiExpertHandler::runExpertModel( int tokenPosition, const std::vector<float> &embedding )
{
    if ( expertStrategy == 0 )
    {
        // Round robin strategy
        return runExpertForToken( tokenPosition, embedding );
    }
    else
    {
        // Average all experts
        return runAverageAllExperts( embedding );
    }
}

void MultiExpertHandler::buildExpertIdMapping()
{
    // Clear existing mapping
    expertIdToIndex.clear();

    // Build mapping from expert ID to index
    for ( int i = 0; i < (int)experts.size(); i++ )
    {
        // Extract expert ID from filename or use stored ID
        int expertId              = experts[i].expertId;
        expertIdToIndex[expertId] = i;

        if ( debugMode )
        {
            std::cout << "Mapped expert ID " << expertId << " to index " << i << std::endl;
        }
    }

    std::cout << "Built mapping for " << expertIdToIndex.size() << " experts" << std::endl;
}

bool MultiExpertHandler::isValidExpertId( int expertId ) const
{
    return expertIdToIndex.find( expertId ) != expertIdToIndex.end();
}

int MultiExpertHandler::expertIdToModelIndex( int expertId ) const
{
    auto it = expertIdToIndex.find( expertId );
    if ( it != expertIdToIndex.end() )
    {
        return it->second;
    }
    return -1; // Invalid ID
}

int MultiExpertHandler::getExpertIdForIndex( int index ) const
{
    if ( index >= 0 && index < (int)experts.size() )
    {
        return experts[index].expertId;
    }
    return -1; // Invalid index
}

int MultiExpertHandler::getLayerIdForExpert( int expertId ) const
{
    for ( const auto &expert : experts )
    {
        if ( expert.expertId == expertId )
        {
            return expert.layerId;
        }
    }
    return -1; // Not found
}

bool MultiExpertHandler::expertExistsForLayer( int layerId, int expertId ) const
{
    for ( const auto &expert : experts )
    {
        if ( expert.layerId == layerId && expert.expertId == expertId )
        {
            return true;
        }
    }
    return false;
}

std::vector<float> MultiExpertHandler::runExpertForLayer( int                       layerId,
                                                          int                       expertId,
                                                          const std::vector<float> &embedding )
{
    // Find the specific expert model for this layer and expert ID
    int expertIndex = -1;

    for ( size_t i = 0; i < experts.size(); i++ )
    {
        if ( experts[i].layerId == layerId && experts[i].expertId == expertId )
        {
            expertIndex = i;
            break;
        }
    }

    if ( expertIndex == -1 )
    {
        std::cerr << "Expert " << expertId << " not found for layer " << layerId << std::endl;
        return {};
    }

    if ( debugMode )
    {
        std::cout << "Running expert " << expertId << " for layer " << layerId << " (model index " << expertIndex << ")"
                  << std::endl;
        std::cout << "Model path: " << experts[expertIndex].modelPath << std::endl;
    }

    // Use the existing runSingleExpert but with the correct index
    return runSingleExpert( expertIndex, embedding );
}
