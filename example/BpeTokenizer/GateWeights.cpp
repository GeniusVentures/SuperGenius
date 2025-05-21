#include "GateWeights.hpp"
#include "Utility.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

GateWeightsHandler::GateWeightsHandler() : initialized(false), debugMode(false)
{
    // Default config
    config.type = MNN_FORWARD_VULKAN; // Use VULKAN by default
    config.numThread = 1;
}

GateWeightsHandler::~GateWeightsHandler()
{
    // Clean up sessions
    for (auto &pair : gateModels)
    {
        if (pair.second.interpreter && pair.second.session)
        {
            pair.second.interpreter->releaseSession(pair.second.session);
            pair.second.session = nullptr;
        }
    }
}

bool GateWeightsHandler::loadRecommendedExperts(int layerId, const std::string &analysisPath)
{
    // Check if file exists
    if (!std::filesystem::exists(analysisPath))
    {
        std::cerr << "Analysis file not found: " << analysisPath << std::endl;
        return false;
    }

    try
    {
        // Read the analysis JSON file
        std::ifstream file(analysisPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonContent = buffer.str();
        
        // Parse the JSON content
        auto json = SimpleJsonParser::parseJson(jsonContent);
        
        // Try to find recommended experts array
        std::vector<int> recommendedExperts;
        
        // Check if this is a recommendations file
        if (json.find("recommended_experts") != json.end())
        {
            // Direct recommendations file format
            auto recommendationsArray = SimpleJsonParser::parseJsonArray(jsonContent, "recommended_experts");
            for (const auto &item : recommendationsArray)
            {
                int expertId = SimpleJsonParser::getIntValue(item, "expert_id", -1);
                if (expertId >= 0)
                {
                    recommendedExperts.push_back(expertId);
                }
            }
        }
        else if (json.find("expert_stats") != json.end())
        {
            // Analysis file format - extract from expert_stats
            auto expertStatsArray = SimpleJsonParser::parseJsonArray(jsonContent, "expert_stats");
            
            // Convert to vector of pairs for sorting
            std::vector<std::pair<int, float>> expertScores;
            for (const auto &expert : expertStatsArray)
            {
                int expertId = SimpleJsonParser::getIntValue(expert, "expert_id", -1);
                float weightNorm = std::stof(SimpleJsonParser::getStringValue(expert, "weight_norm", "0"));
                if (expertId >= 0)
                {
                    expertScores.push_back({expertId, weightNorm});
                }
            }
            
            // Sort by weight norm (descending)
            std::sort(expertScores.begin(), expertScores.end(), 
                    [](const auto &a, const auto &b) { return a.second > b.second; });
            
            // Take top 20 experts
            int count = std::min(20, static_cast<int>(expertScores.size()));
            for (int i = 0; i < count; i++)
            {
                recommendedExperts.push_back(expertScores[i].first);
            }
        }
        
        if (recommendedExperts.empty())
        {
            std::cerr << "No recommended experts found in analysis file" << std::endl;
            return false;
        }
        
        // Store the recommended experts in the gate model info
        gateModels[layerId].recommendedExperts = recommendedExperts;
        
        std::cout << "Loaded " << recommendedExperts.size() << " recommended experts for layer " 
                  << layerId << std::endl;
        
        if (debugMode)
        {
            std::cout << "Recommended experts: ";
            for (int expertId : recommendedExperts)
            {
                std::cout << expertId << " ";
            }
            std::cout << std::endl;
        }
        
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading recommended experts: " << e.what() << std::endl;
        return false;
    }
}

bool GateWeightsHandler::initialize(const std::string &modelDir)
{
    this->modelDir = modelDir;
    
    try
    {
        // Look for gate models and analysis files
        bool foundAny = false;
        
        // Check for layer 10 gate model (our initial focus)
        std::string gateModelPath = modelDir + "/layer_10_gate.mnn";
        std::string analysisPath = modelDir + "/layer_10_gate_analysis.json";
        std::string recommendationsPath = modelDir + "/layer_10_recommended_experts.json";
        
        // Try to add the gate model
        if (std::filesystem::exists(gateModelPath))
        {
            // Prefer recommendations file if it exists
            if (std::filesystem::exists(recommendationsPath))
            {
                if (addGateModel(10, gateModelPath, recommendationsPath))
                {
                    foundAny = true;
                    std::cout << "Added gate model for layer 10 using recommendations file" << std::endl;
                }
            }
            // Fall back to analysis file
            else if (std::filesystem::exists(analysisPath))
            {
                if (addGateModel(10, gateModelPath, analysisPath))
                {
                    foundAny = true;
                    std::cout << "Added gate model for layer 10 using analysis file" << std::endl;
                }
            }
            else
            {
                std::cerr << "Gate model found for layer 10, but no analysis or recommendations file" << std::endl;
            }
        }
        
        // Look for other layers (for future extensibility)
        for (const auto &entry : std::filesystem::directory_iterator(modelDir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                
                // Check for gate model files following pattern "layer_X_gate.mnn"
                std::regex gatePattern("layer_(\\d+)_gate\\.mnn");
                std::smatch match;
                
                if (std::regex_search(filename, match, gatePattern) && match.size() > 1)
                {
                    int layerId = std::stoi(match[1].str());
                    
                    // Skip layer 10 as we've already handled it
                    if (layerId == 10)
                        continue;
                    
                    // Look for corresponding analysis or recommendations files
                    std::string layerGatePath = entry.path().string();
                    std::string layerAnalysisPath = modelDir + "/layer_" + std::to_string(layerId) + "_gate_analysis.json";
                    std::string layerRecommendationsPath = modelDir + "/layer_" + std::to_string(layerId) + "_recommended_experts.json";
                    
                    if (std::filesystem::exists(layerRecommendationsPath))
                    {
                        if (addGateModel(layerId, layerGatePath, layerRecommendationsPath))
                        {
                            foundAny = true;
                            std::cout << "Added gate model for layer " << layerId << " using recommendations file" << std::endl;
                        }
                    }
                    else if (std::filesystem::exists(layerAnalysisPath))
                    {
                        if (addGateModel(layerId, layerGatePath, layerAnalysisPath))
                        {
                            foundAny = true;
                            std::cout << "Added gate model for layer " << layerId << " using analysis file" << std::endl;
                        }
                    }
                    else
                    {
                        std::cerr << "Gate model found for layer " << layerId 
                                  << ", but no analysis or recommendations file" << std::endl;
                    }
                }
            }
        }
        
        if (!foundAny)
        {
            std::cerr << "No valid gate models found in directory: " << modelDir << std::endl;
            return false;
        }
        
        initialized = true;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing gate weights handler: " << e.what() << std::endl;
        return false;
    }
}

bool GateWeightsHandler::addGateModel(int layerId, const std::string &gatePath, const std::string &analysisPath)
{
    try
    {
        // Create a new gate model info entry
        GateModelInfo gateInfo;
        gateInfo.layerId = layerId;
        gateInfo.modelPath = gatePath;
        
        // Load the MNN model (defer session creation until needed)
        gateInfo.interpreter.reset(MNN::Interpreter::createFromFile(gatePath.c_str()));
        if (!gateInfo.interpreter)
        {
            std::cerr << "Failed to load gate model: " << gatePath << std::endl;
            return false;
        }
        
        // Load recommended experts from analysis file
        if (!loadRecommendedExperts(layerId, analysisPath))
        {
            std::cerr << "Failed to load recommended experts for layer " << layerId << std::endl;
            return false;
        }
        
        // Add to map
        gateModels[layerId] = std::move(gateInfo);
        
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error adding gate model: " << e.what() << std::endl;
        return false;
    }
}

std::vector<int> GateWeightsHandler::getRecommendedExperts(int layerId) const
{
    auto it = gateModels.find(layerId);
    if (it != gateModels.end())
    {
        return it->second.recommendedExperts;
    }
    return {};
}

std::vector<int> GateWeightsHandler::selectExperts(int layerId, const std::vector<float> &embedding, int topK)
{
    if (!initialized)
    {
        std::cerr << "Gate weights handler not initialized" << std::endl;
        return getRecommendedExperts(layerId);
    }
    
    auto it = gateModels.find(layerId);
    if (it == gateModels.end())
    {
        std::cerr << "No gate model found for layer " << layerId << std::endl;
        return getRecommendedExperts(layerId);
    }
    
    auto &gateInfo = it->second;
    
    // Create session if needed
    if (!gateInfo.session)
    {
        gateInfo.session = gateInfo.interpreter->createSession(config);
        if (!gateInfo.session)
        {
            std::cerr << "Failed to create session for gate model layer " << layerId << std::endl;
            return gateInfo.recommendedExperts;
        }
    }
    
    try
    {
        // Get input tensor
        auto input = gateInfo.interpreter->getSessionInput(gateInfo.session, nullptr);
        if (!input)
        {
            throw std::runtime_error("Failed to get input tensor for gate model");
        }
        
        // Resize for batch size 1
        gateInfo.interpreter->resizeTensor(input, {1, (int)embedding.size()});
        gateInfo.interpreter->resizeSession(gateInfo.session);
        
        // Copy embedding to input tensor
        MNN::Tensor inputHost(input, MNN::Tensor::CAFFE);
        float *inputData = inputHost.host<float>();
        LLMUtility::safeCopyToTensor(inputData, embedding);
        input->copyFromHostTensor(&inputHost);
        
        // Run the gate model
        gateInfo.interpreter->runSession(gateInfo.session);
        
        // Get the output (gate scores)
        auto output = gateInfo.interpreter->getSessionOutput(gateInfo.session, nullptr);
        if (!output)
        {
            throw std::runtime_error("Failed to get output tensor for gate model");
        }
        
        // Copy to host
        MNN::Tensor outputHost(output, output->getDimensionType());
        output->copyToHostTensor(&outputHost);
        float *outputData = outputHost.host<float>();
        
        // Get the number of experts
        int numExperts = output->length(output->dimensions() - 1);
        
        // Create vector of (expert id, score) pairs
        std::vector<std::pair<int, float>> expertScores;
        expertScores.reserve(numExperts);
        
        for (int i = 0; i < numExperts; i++)
        {
            expertScores.push_back({i, outputData[i]});
        }
        
        // Sort by score (descending)
        std::sort(expertScores.begin(), expertScores.end(), 
                [](const auto &a, const auto &b) { return a.second > b.second; });
        
        // Get top K experts
        std::vector<int> selectedExperts;
        selectedExperts.reserve(topK);
        
        int count = std::min(topK, numExperts);
        for (int i = 0; i < count; i++)
        {
            selectedExperts.push_back(expertScores[i].first);
            
            if (debugMode)
            {
                std::cout << "Selected expert " << expertScores[i].first 
                          << " with score " << expertScores[i].second << std::endl;
            }
        }
        
        return selectedExperts;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error running gate model for layer " << layerId << ": " << e.what() << std::endl;
        
        // Fallback to recommended experts
        return gateInfo.recommendedExperts;
    }
}

void GateWeightsHandler::setDebugMode(bool debug)
{
    debugMode = debug;
}

bool GateWeightsHandler::getDebugMode() const
{
    return debugMode;
}

bool GateWeightsHandler::hasLayerGate(int layerId) const
{
    return gateModels.find(layerId) != gateModels.end();
}

void GateWeightsHandler::printGateInfo() const
{
    if (!initialized || gateModels.empty())
    {
        std::cout << "No gate models loaded" << std::endl;
        return;
    }
    
    std::cout << "Loaded Gate Models:" << std::endl;
    for (const auto &pair : gateModels)
    {
        std::cout << "  Layer " << pair.first << ": " << pair.second.modelPath << std::endl;
        std::cout << "    Recommended experts: " << pair.second.recommendedExperts.size() << std::endl;
        
        if (debugMode)
        {
            std::cout << "    Expert IDs: ";
            for (int expertId : pair.second.recommendedExperts)
            {
                std::cout << expertId << " ";
            }
            std::cout << std::endl;
        }
    }
}