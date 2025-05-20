#ifndef LLM_UTILITY_HPP
#define LLM_UTILITY_HPP
#include <MNN/Interpreter.hpp>
#include "BpeTokenizer.hpp"
#include <memory>
#include <iostream>
#include <filesystem>
#include <regex> 
#include <random> 
#include <fstream>
#include <sstream>

class LLMUtility
{
private:
public:
    // Utility function to check tensor properties
    static void printTensorInfo( MNN::Tensor *tensor, const std::string &name );

    

    // Safe tensor copy function with checks
    static bool safeCopyToTensor( float *destData, const std::vector<float> &sourceData );

    // Function to save tensor data to a binary file for debugging
    static void saveTensorToFile( MNN::Tensor *tensor, const std::string &filename );

    // Function to load tensor data from a binary file
    static std::vector<float> loadTensorFromFile( const std::string &filename, std::vector<int> &dimensions );

    // Generate synthetic embedding for a token
    static std::vector<float> createSyntheticEmbedding( int tokenId, int hiddenSize );

    // Legacy function to run LM head model
    static int runLmHeadModelLegacy( MNN::Interpreter         *lmHeadNet,
                                     const std::vector<float> &expertOutput,
                                     BpeTokenizer             *tokenizer );
};
#endif
