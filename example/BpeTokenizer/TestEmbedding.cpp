#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <numeric> // For std::accumulate
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>

// Simple test to check embedding model
void testEmbedding(const std::string& modelPath) {
    // Load embedding model
    auto embeddingNet = MNN::Interpreter::createFromFile(modelPath.c_str());
    if (!embeddingNet) {
        std::cerr << "Failed to load embedding model" << std::endl;
        return;
    }
    
    // Create session
    MNN::ScheduleConfig config;
    config.type = MNN_FORWARD_VULKAN;
    config.numThread = 1;
    MNN::BackendConfig backendConfig;
    backendConfig.precision = MNN::BackendConfig::Precision_Low; // Request FP16
    backendConfig.memory    = MNN::BackendConfig::Memory_Low;
    config.backendConfig    = &backendConfig;
    auto session = embeddingNet->createSession(config);
    
    
    // Get input tensor
    auto input = embeddingNet->getSessionInput(session, nullptr);

    // Resize for single token
    embeddingNet->resizeTensor(input, {1, 1});
    embeddingNet->resizeSession(session);
    
    // Create input data - try with token ID 1
    MNN::Tensor inputHost(input, MNN::Tensor::CAFFE);
    int* inputData = inputHost.host<int>();
    inputData[0] = 1; // Use token ID 1 as a test
    
    // Copy to input tensor
    input->copyFromHostTensor(&inputHost);
    
    // Run model
    embeddingNet->runSession(session);
    
    // Get output
    auto output = embeddingNet->getSessionOutput(session, nullptr);
    MNN::Tensor outputHost(output, MNN::Tensor::CAFFE);
    output->copyToHostTensor(&outputHost);
    
    // Check output
    float* data = outputHost.host<float>();
    int size = output->elementSize();
    
    // Print some stats
    float sum = 0.0f;
    float minVal = data[0];
    float maxVal = data[0];
    
    for (int i = 0; i < size; i++) {
        sum += data[i];
        minVal = std::min(minVal, data[i]);
        maxVal = std::max(maxVal, data[i]);
    }
    
    std::cout << "Embedding test - Size: " << size
              << ", Min: " << minVal
              << ", Max: " << maxVal
              << ", Mean: " << (sum / size)
              << std::endl;
    
    // Check if all values are zero
    bool allZeros = true;
    for (int i = 0; i < std::min(100, size); i++) {
        if (data[i] != 0.0f) {
            allZeros = false;
            break;
        }
    }
    
    if (allZeros) {
        std::cout << "Warning: All embedding values are zero!" << std::endl;
    } else {
        std::cout << "Sample values: ";
        for (int i = 0; i < std::min(5, size); i++) {
            std::cout << data[i] << " ";
        }
        std::cout << std::endl;
    }
    
    // Clean up
    embeddingNet->releaseSession(session);
    delete embeddingNet;
}

int main( int argc, char **argv )
{
    if ( argc < 2 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> " << std::endl;
        return 1;
    }

    std::string modelDir = argv[1];

    testEmbedding( modelDir );
}
