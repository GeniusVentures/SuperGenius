#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <numeric> // For std::accumulate
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>

// Test expert model
void testExpert(const std::string& modelPath) {
    // Load expert model
    auto expertNet = MNN::Interpreter::createFromFile(modelPath.c_str());
    if (!expertNet) {
        std::cerr << "Failed to load expert model" << std::endl;
        return;
    }
    
    // Create session
    MNN::ScheduleConfig config;
    config.type = MNN_FORWARD_CPU;
    config.numThread = 1;
    auto session = expertNet->createSession(config);
    
    // Get input tensor
    auto input = expertNet->getSessionInput(session, nullptr);
    
    // Resize for single hidden state
    expertNet->resizeTensor(input, {1, input->length(1)});
    expertNet->resizeSession(session);
    
    // Create input data - use small random values
    MNN::Tensor inputHost(input, MNN::Tensor::CAFFE);
    float* inputData = inputHost.host<float>();
    
    // Fill with some small non-zero values
    for (int i = 0; i < input->elementSize(); i++) {
        inputData[i] = 0.01f * (i % 10);  // Small non-zero values
    }
    
    // Copy to input tensor
    input->copyFromHostTensor(&inputHost);
    
    // Run model
    expertNet->runSession(session);
    
    // Get output
    auto output = expertNet->getSessionOutput(session, nullptr);
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
    
    std::cout << "Expert test - Size: " << size
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
        std::cout << "Warning: All expert output values are zero!" << std::endl;
    } else {
        std::cout << "Sample values: ";
        for (int i = 0; i < std::min(5, size); i++) {
            std::cout << data[i] << " ";
        }
        std::cout << std::endl;
    }
    
    // Clean up
    expertNet->releaseSession(session);
    delete expertNet;
}

int main( int argc, char **argv )
{
    if ( argc < 2 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> " << std::endl;
        return 1;
    }

    std::string modelDir  = argv[1];

    testExpert( modelDir );
}
