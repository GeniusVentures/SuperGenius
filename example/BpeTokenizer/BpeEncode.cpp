#include "BpeTokenizer.hpp"
#include "jsonparser.hpp"
#include "SplitEmbedding.hpp"
#include "SplitLMHead.hpp"
#include "MultiExpert.hpp"
#include "GateWeights.hpp"  // Include the new header
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
#include <random> 
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <filesystem>
#include <regex> 

// Function to run expert models with gate selection
std::vector<float> runExpertWithGateSelection(
    MultiExpertHandler &expertHandler,
    GateWeightsHandler &gateHandler,
    int tokenPosition,
    const std::vector<float> &embedding,
    int layerId = 10,
    int numExperts = 2
);

// Main function
int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens> [expert_strategy]" << std::endl;
        std::cerr << "Example: " << argv[0] << " . \"The cat sat on the chair\" 20 1" << std::endl;
        std::cerr << "Expert strategies: 0 = round robin, 1 = average all, 2 = gate based (default: 0)" << std::endl;
        return 1;
    }

    std::string modelDir       = argv[1];
    std::string prompt         = argv[2];
    int         numTokens      = 1; // Default to 1 token
    int         expertStrategy = 0; // Default to round robin

    try
    {
        numTokens = std::stoi(argv[3]);
    }
    catch (...)
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Parse expert strategy if provided
    if (argc >= 5)
    {
        try
        {
            expertStrategy = std::stoi(argv[4]);
            if (expertStrategy < 0 || expertStrategy > 2)
            {
                std::cerr << "Invalid expert strategy, using default: 0 (round robin)" << std::endl;
                expertStrategy = 0;
            }
        }
        catch (...)
        {
            std::cerr << "Invalid expert strategy, using default: 0 (round robin)" << std::endl;
        }
    }

    // Load tokenizer
    BpeTokenizer tokenizer;
    std::string  vocabPath  = modelDir + "/vocab.json";
    std::string  mergesPath = modelDir + "/merges.txt";

    if (!tokenizer.load(vocabPath, mergesPath))
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return 1;
    }

    // Tokenize prompt
    std::vector<int> promptTokens = tokenizer.encode(prompt);

    if (promptTokens.empty())
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return 1;
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for (int token : promptTokens)
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // Set hidden dimension
    int hiddenSize = 7168;

    // Create a vector to hold generated tokens
    std::vector<int> generatedTokens = promptTokens;

    // Initialize the split embedding loader
    SplitEmbeddingLoader embeddingLoader;
    if (!embeddingLoader.initialize(modelDir))
    {
        std::cerr << "Failed to initialize embedding loader" << std::endl;
        return 1;
    }
    embeddingLoader.printChunkInfo();

    // Initialize the split LM head loader
    SplitLmHeadLoader lmHeadLoader;
    bool              useSplitLmHead = lmHeadLoader.initialize(modelDir);
    if (useSplitLmHead)
    {
        lmHeadLoader.printChunkInfo();
    }
    else
    {
        std::cerr << "Warning: Failed to initialize LM head loader. Will try legacy approach." << std::endl;
    }

    // Initialize multi-expert handler
    MultiExpertHandler expertHandler;
    if (!expertHandler.initialize(modelDir))
    {
        std::cerr << "Failed to initialize expert handler" << std::endl;
        return 1;
    }

    // If using gate-based strategy, initialize gate weights handler
    GateWeightsHandler gateHandler;
    bool useGateSelection = (expertStrategy == 2);
    
    if (useGateSelection)
    {
        if (!gateHandler.initialize(modelDir))
        {
            std::cerr << "Failed to initialize gate handler, falling back to round robin" << std::endl;
            useGateSelection = false;
            expertStrategy = 0;
        }
        else
        {
            std::cout << "Gate-based expert selection enabled" << std::endl;
            gateHandler.printGateInfo();
        }
    }

    // Set expert strategy for non-gate methods
    if (!useGateSelection)
    {
        expertHandler.setStrategy(expertStrategy);
        std::cout << "Using expert strategy: " 
                  << (expertStrategy == 0 ? "Round Robin" : "Average All") << std::endl;
    }

    // Enable debug mode for first few tokens
    expertHandler.setDebugMode(true);
    if (useGateSelection)
    {
        gateHandler.setDebugMode(true);
    }

    // Optionally load legacy LM head model as fallback
    std::unique_ptr<MNN::Interpreter> legacyLmHeadNet;
    if (!useSplitLmHead)
    {
        std::string lmHeadPath = modelDir + "/lm_head_f32.mnn";
        legacyLmHeadNet.reset(MNN::Interpreter::createFromFile(lmHeadPath.c_str()));
        if (!legacyLmHeadNet)
        {
            std::cerr << "Failed to load legacy LM head model from " << lmHeadPath << std::endl;
            return 1;
        }
        std::cout << "Using legacy LM head model as fallback" << std::endl;
    }

    // Generate tokens one by one
    for (int i = 0; i < numTokens; i++)
    {
        std::cout << "\n--- Generating token " << (i + 1) << "/" << numTokens << " ---" << std::endl;

        // Disable debug mode after a few tokens
        if (i >= 3)
        {
            expertHandler.setDebugMode(false);
            if (useGateSelection)
            {
                gateHandler.setDebugMode(false);
            }
        }

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding using split loader
        std::cout << "Computing embedding for token ID " << lastToken << std::endl;
        std::vector<float> embedding = embeddingLoader.extractEmbedding(lastToken);

        // Print embedding stats
        if (!embedding.empty())
        {
            float minVal = *std::min_element(embedding.begin(), embedding.end());
            float maxVal = *std::max_element(embedding.begin(), embedding.end());
            float sum    = std::accumulate(embedding.begin(), embedding.end(), 0.0f);
            float mean   = sum / embedding.size();

            std::cout << "Embedding stats - Size: " << embedding.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }
        else
        {
            std::cerr << "Failed to get embedding for token " << lastToken << std::endl;
            std::cout << "Using synthetic embedding as fallback" << std::endl;
            embedding = LLMUtility::createSyntheticEmbedding(lastToken, hiddenSize);
        }

        // Run expert models using the appropriate strategy
        std::vector<float> expertOutput;
        
        if (useGateSelection && gateHandler.hasLayerGate(10))
        {
            // Use gate-based selection
            std::cout << "Using gate-based expert selection for token " << i << std::endl;
            expertOutput = runExpertWithGateSelection(
                expertHandler, gateHandler, i, embedding, 10, 2);
        }
        else
        {
            // Use standard expert selection
            expertOutput = expertHandler.runExpertModel(i, embedding);
        }

        // Print expert output stats
        if (!expertOutput.empty())
        {
            float minVal = *std::min_element(expertOutput.begin(), expertOutput.end());
            float maxVal = *std::max_element(expertOutput.begin(), expertOutput.end());
            float sum    = std::accumulate(expertOutput.begin(), expertOutput.end(), 0.0f);
            float mean   = sum / expertOutput.size();

            std::cout << "Expert output stats - Size: " << expertOutput.size() << ", Min: " << minVal
                      << ", Max: " << maxVal << ", Mean: " << mean << std::endl;
        }
        else
        {
            std::cerr << "Failed to get expert output" << std::endl;
            break;
        }

        // Run LM head model (using split or legacy approach)
        int nextTokenId = 0;

        if (useSplitLmHead)
        {
            // Run split LM head
            std::cout << "Running split LM head model..." << std::endl;
            std::vector<float> logits = lmHeadLoader.runPrediction(expertOutput);

            if (!logits.empty())
            {
                // Get top tokens
                std::vector<std::pair<int, float>> topTokens = lmHeadLoader.getTopK(logits, 5);

                // Print top tokens
                std::cout << "\nTop tokens:" << std::endl;
                for (size_t j = 0; j < topTokens.size(); j++)
                {
                    int   tokenId = topTokens[j].first;
                    float score   = topTokens[j].second;

                    std::string tokenText;
                    try
                    {
                        tokenText = tokenizer.decode({tokenId});
                        // Replace non-printable chars with dots
                        for (char &c : tokenText)
                        {
                            if (c < 32 || c > 126)
                            {
                                c = '.';
                            }
                        }
                    }
                    catch (...)
                    {
                        tokenText = "[invalid token]";
                    }

                    std::cout << "  " << (j + 1) << ". Token ID " << tokenId << " (score: " << score << "): \""
                              << tokenText << "\"" << std::endl;
                }

                // Get top token
                nextTokenId = topTokens.empty() ? 0 : topTokens[0].first;
            }
            else
            {
                std::cerr << "Failed to get logits from split LM head" << std::endl;
                if (legacyLmHeadNet)
                {
                    std::cout << "Falling back to legacy LM head" << std::endl;
                    nextTokenId = LLMUtility::runLmHeadModelLegacy(legacyLmHeadNet.get(), expertOutput, &tokenizer);
                }
            }
        }
        else
        {
            // Use legacy approach
            nextTokenId = LLMUtility::runLmHeadModelLegacy(legacyLmHeadNet.get(), expertOutput, &tokenizer);
        }

        // Add next token
        generatedTokens.push_back(nextTokenId);

        // Display current text
        std::string currentText = tokenizer.decode(generatedTokens);

        // Clean up output for display
        std::string displayText;
        for (unsigned char c : currentText)
        {
            displayText += (c < 32 || c > 126) ? '.' : c; 
        }
        std::cout << "\nGenerated so far: " << displayText << std::endl;
    }

    return 0;
}

std::vector<float> runExpertWithGateSelection( MultiExpertHandler       &expertHandler,
                                               GateWeightsHandler       &gateHandler,
                                               int                       tokenPosition,
                                               const std::vector<float> &embedding,
                                               int                       layerId,
                                               int                       numExperts )
{
    if ( !gateHandler.hasLayerGate( layerId ) )
    {
        // Fallback to existing strategy if no gate model for this layer
        std::cout << "No gate model available for layer " << layerId << ", using standard expert selection"
                  << std::endl;
        return expertHandler.runExpertModel( tokenPosition, embedding );
    }

    // Get expert selections from gate model
    std::vector<int> selectedExperts = gateHandler.selectExperts( layerId, embedding, numExperts );

    if ( selectedExperts.empty() )
    {
        // Fallback if no experts were selected
        std::cout << "No experts selected by gate model, using standard selection" << std::endl;
        return expertHandler.runExpertModel( tokenPosition, embedding );
    }

    // Convert selected expert IDs to valid model indices
    std::vector<int> validExperts;
    for ( int expertId : selectedExperts )
    {
        if ( expertHandler.isValidExpertId( expertId ) )
        {
            validExperts.push_back( expertId );
        }
        else
        {
            std::cout << "Skipping invalid expert ID " << expertId << std::endl;
        }
    }

    // If no valid experts, fall back to recommended experts
    if ( validExperts.empty() )
    {
        std::vector<int> recommendedExperts = gateHandler.getRecommendedExperts( layerId );
        for ( int expertId : recommendedExperts )
        {
            if ( expertHandler.isValidExpertId( expertId ) )
            {
                validExperts.push_back( expertId );
                if ( validExperts.size() >= 2 )
                {
                    break; // Get at least 2 valid experts
                }
            }
        }
    }

    // If still no valid experts, fall back to standard selection
    if ( validExperts.empty() )
    {
        std::cout << "No valid experts available, using standard selection" << std::endl;
        return expertHandler.runExpertModel( tokenPosition, embedding );
    }

    // Run selected experts and average their outputs
    std::vector<float> resultOutput;
    int                successfulExperts = 0;

    for ( int expertId : validExperts )
    {
        std::vector<float> expertOutput = expertHandler.runSingleExpert( expertId, embedding );

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
        std::cerr << "Failed to run any selected experts, falling back to standard selection" << std::endl;
        return expertHandler.runExpertModel( tokenPosition, embedding );
    }

    std::cout << "Successfully used " << successfulExperts << " experts with gate-based selection" << std::endl;
    return resultOutput;
}
