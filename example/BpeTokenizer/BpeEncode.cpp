#include "BpeTokenizer.hpp"
#include "jsonparser.hpp"
#include "SplitEmbedding.hpp"
#include "SplitLMHead.hpp"
#include "MultiExpert.hpp"
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


// Main function
int main( int argc, char **argv )
{
    if ( argc < 4 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens> [expert_strategy]" << std::endl;
        std::cerr << "Example: " << argv[0] << " . \"The cat sat on the chair\" 20 1" << std::endl;
        std::cerr << "Expert strategies: 0 = round robin, 1 = average all (default: 0)" << std::endl;
        return 1;
    }

    std::string modelDir       = argv[1];
    std::string prompt         = argv[2];
    int         numTokens      = 1; // Default to 1 token
    int         expertStrategy = 0; // Default to round robin

    try
    {
        numTokens = std::stoi( argv[3] );
    }
    catch ( ... )
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Parse expert strategy if provided
    if ( argc >= 5 )
    {
        try
        {
            expertStrategy = std::stoi( argv[4] );
            if ( expertStrategy < 0 || expertStrategy > 1 )
            {
                std::cerr << "Invalid expert strategy, using default: 0 (round robin)" << std::endl;
                expertStrategy = 0;
            }
        }
        catch ( ... )
        {
            std::cerr << "Invalid expert strategy, using default: 0 (round robin)" << std::endl;
        }
    }

    // Load tokenizer
    BpeTokenizer tokenizer;
    std::string  vocabPath  = modelDir + "/vocab.json";
    std::string  mergesPath = modelDir + "/merges.txt";

    if ( !tokenizer.load( vocabPath, mergesPath ) )
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return 1;
    }

    // Tokenize prompt
    std::vector<int> promptTokens = tokenizer.encode( prompt );

    if ( promptTokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return 1;
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : promptTokens )
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
    if ( !embeddingLoader.initialize( modelDir ) )
    {
        std::cerr << "Failed to initialize embedding loader" << std::endl;
        return 1;
    }
    embeddingLoader.printChunkInfo();

    // Initialize the split LM head loader
    SplitLmHeadLoader lmHeadLoader;
    bool              useSplitLmHead = lmHeadLoader.initialize( modelDir );
    if ( useSplitLmHead )
    {
        lmHeadLoader.printChunkInfo();
    }
    else
    {
        std::cerr << "Warning: Failed to initialize LM head loader. Will try legacy approach." << std::endl;
    }

    // Initialize multi-expert handler (NEW)
    MultiExpertHandler expertHandler;
    if ( !expertHandler.initialize( modelDir ) )
    {
        std::cerr << "Failed to initialize expert handler" << std::endl;
        return 1;
    }

    // Set expert strategy
    expertHandler.setStrategy( expertStrategy );
    std::cout << "Using expert strategy: " << ( expertStrategy == 0 ? "Round Robin" : "Average All" ) << std::endl;

    // Enable debug mode for first few tokens
    expertHandler.setDebugMode( true );

    // Optionally load legacy LM head model as fallback
    std::unique_ptr<MNN::Interpreter> legacyLmHeadNet;
    if ( !useSplitLmHead )
    {
        std::string lmHeadPath = modelDir + "/lm_head_f32.mnn";
        legacyLmHeadNet.reset( MNN::Interpreter::createFromFile( lmHeadPath.c_str() ) );
        if ( !legacyLmHeadNet )
        {
            std::cerr << "Failed to load legacy LM head model from " << lmHeadPath << std::endl;
            return 1;
        }
        std::cout << "Using legacy LM head model as fallback" << std::endl;
    }

    // Generate tokens one by one
    for ( int i = 0; i < numTokens; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        // Disable debug mode after a few tokens
        if ( i >= 3 )
        {
            expertHandler.setDebugMode( false );
        }

        // Get last token
        int lastToken = generatedTokens.back();

        // Get embedding using split loader
        std::cout << "Computing embedding for token ID " << lastToken << std::endl;
        std::vector<float> embedding = embeddingLoader.extractEmbedding( lastToken );

        // Print embedding stats
        if ( !embedding.empty() )
        {
            float minVal = *std::min_element( embedding.begin(), embedding.end() );
            float maxVal = *std::max_element( embedding.begin(), embedding.end() );
            float sum    = std::accumulate( embedding.begin(), embedding.end(), 0.0f );
            float mean   = sum / embedding.size();

            std::cout << "Embedding stats - Size: " << embedding.size() << ", Min: " << minVal << ", Max: " << maxVal
                      << ", Mean: " << mean << std::endl;
        }
        else
        {
            std::cerr << "Failed to get embedding for token " << lastToken << std::endl;
            std::cout << "Using synthetic embedding as fallback" << std::endl;
            embedding = LLMUtility::createSyntheticEmbedding( lastToken, hiddenSize );
        }

        // Run expert models using the multi-expert handler (NEW)
        std::vector<float> expertOutput = expertHandler.runExpertModel( i, embedding );

        // Print expert output stats
        if ( !expertOutput.empty() )
        {
            float minVal = *std::min_element( expertOutput.begin(), expertOutput.end() );
            float maxVal = *std::max_element( expertOutput.begin(), expertOutput.end() );
            float sum    = std::accumulate( expertOutput.begin(), expertOutput.end(), 0.0f );
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

        if ( useSplitLmHead )
        {
            // Run split LM head
            std::cout << "Running split LM head model..." << std::endl;
            std::vector<float> logits = lmHeadLoader.runPrediction( expertOutput );

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
                    nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), expertOutput, &tokenizer );
                }
            }
        }
        else
        {
            // Use legacy approach
            nextTokenId = LLMUtility::runLmHeadModelLegacy( legacyLmHeadNet.get(), expertOutput, &tokenizer );
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

    return 0;
}
