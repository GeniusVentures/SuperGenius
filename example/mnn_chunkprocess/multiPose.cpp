//
//  multiPose.cpp
//  MNN
//
//  Created by MNN on 2018/09/26.
//  Copyright © 2018, Alibaba Group Holding Limited
//
#include <filesystem>
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <MNN/ImageProcess.hpp>
#include <MNN/Interpreter.hpp>
#include "PoseNames.hpp"

#define MNN_OPEN_TIME_TRACE
#include <MNN/AutoTime.hpp>
using namespace MNN;

#define MODEL_IMAGE_SIZE 513
#define OUTPUT_STRIDE 16

#define MAX_POSE_DETECTIONS 10
#define NUM_KEYPOINTS 17
#define SCORE_THRESHOLD 0.5
#define MIN_POSE_SCORE 0.25
#define NMS_RADIUS 20
#define LOCAL_MAXIMUM_RADIUS 1

#define OFFSET_NODE_NAME "offset_2"
#define DISPLACE_FWD_NODE_NAME "displacement_fwd_2"
#define DISPLACE_BWD_NODE_NAME "displacement_bwd_2"
#define HEATMAPS "heatmap"

#define CIRCLE_RADIUS 3


    void MNNProcess(const std::vector<uint8_t>& imgdata, 
                                                         std::vector<uint8_t>& modelFile, 
                                                         const int channels, 
                                                         const int origwidth,
                                                         const int origheight) 
    {
        std::vector<uint8_t> ret_vect(imgdata);

        // Get Target Width
        //const int targetWidth = static_cast<int>((float)origwidth / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;
        //const int targetHeight = static_cast<int>((float)origheight / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;

        // Scale
        //CV::Point scale;
        //scale.fX = (float)origwidth / (float)targetWidth;
        //scale.fY = (float)origheight / (float)targetHeight;

        // Create net and session
        const void* buffer = static_cast<const void*>( modelFile.data() );
        auto mnnNet = std::shared_ptr<MNN::Interpreter>( MNN::Interpreter::createFromBuffer( buffer, modelFile.size() ) );

        MNN::ScheduleConfig netConfig;
        netConfig.type      = MNN_FORWARD_CPU;
        netConfig.numThread = 4;
        auto session        = mnnNet->createSession( netConfig );

        auto input = mnnNet->getSessionInput( session, nullptr );

        if ( input->elementSize() <= 4 )
        {
            mnnNet->resizeTensor( input, { 1, 3, origwidth, origheight } );
            mnnNet->resizeSession( session );
        }

        // Preprocess input image
        {
            const float              means[3] = { 127.5f, 127.5f, 127.5f };
            const float              norms[3] = { 2.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f };
            CV::ImageProcess::Config preProcessConfig;
            ::memcpy( preProcessConfig.mean, means, sizeof( means ) );
            ::memcpy( preProcessConfig.normal, norms, sizeof( norms ) );
            preProcessConfig.sourceFormat = CV::RGBA;

            if (channels == 3)
            {
                preProcessConfig.sourceFormat = CV::RGB;
            }
            preProcessConfig.destFormat = CV::RGB;
            preProcessConfig.filterType = CV::BILINEAR;

            auto       pretreat = std::shared_ptr<CV::ImageProcess>( CV::ImageProcess::create( preProcessConfig ) );
            CV::Matrix trans;

            // Dst -> [0, 1]
            //trans.postScale( 1.0 / targetWidth, 1.0 / targetHeight );
            //[0, 1] -> Src
            trans.postScale( origwidth, origheight );

            pretreat->setMatrix( trans );
            pretreat->convert( ret_vect.data(), origwidth, origheight, 0, input );
        }

        // Log preprocessed input tensor data hash
        {
            const float *inputData     = input->host<float>();
            size_t       inputDataSize = input->elementSize() * sizeof( float );
        }

        {
            AUTOTIME;
            mnnNet->runSession( session );
        }

        auto outputTensor = mnnNet->getSessionOutput( session, nullptr );
        int outputWidth = outputTensor->width();
        int outputHeight = outputTensor->height();
        int outputChannels = outputTensor->channel();
        auto outputHost   = std::make_unique<MNN::Tensor>( outputTensor, MNN::Tensor::CAFFE );
        outputTensor->copyToHostTensor( outputHost.get() );

        //return outputHost;
            // Write output data to an image file
        std::vector<uint8_t> outputData(outputWidth * outputHeight * outputChannels);
        float* tensorData = outputHost->host<float>();

        // Convert from float to uint8 for image saving
        for (int i = 0; i < outputWidth * outputHeight * outputChannels; i++) {
            outputData[i] = static_cast<uint8_t>(tensorData[i] * 255.0f);
        }

        // Save as PNG (supports RGB, RGB with channels = 3)
        stbi_write_png("output_image.png", outputWidth, outputHeight, outputChannels, outputData.data(), outputWidth * outputChannels);
    }

    std::vector<uint8_t> loadFileToByteArray(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);  // Open file at end to get size
    if (!file) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return {};
    }

    std::streamsize fileSize = file.tellg();  // Get file size
    file.seekg(0, std::ios::beg);  // Go back to the beginning of the file

    std::vector<uint8_t> buffer(fileSize);  // Create a vector of appropriate size
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        std::cerr << "Failed to read file: " << filePath << std::endl;
        return {};
    }

    return buffer;
}


int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: ./multiPose.out model.mnn input.jpg pose.jpg" << std::endl;
    }
    
    const auto poseModel = argv[1];
    const auto inputImageFileName = argv[2];
    const auto outputImageFileName = argv[3];

    int originalWidth;
    int originalHeight;
    int originChannel;
    auto inputImage = stbi_load(inputImageFileName, &originalWidth, &originalHeight, &originChannel, 4);
    if (nullptr == inputImage) {
        MNN_ERROR("Invalid path: %s\n", inputImageFileName);
        return 0;
    }
    std::vector<uint8_t> imgdata(inputImage, inputImage + (originalWidth * originalHeight * 4));
    std::vector<uint8_t> modelFile = loadFileToByteArray(poseModel);
    MNNProcess(imgdata, modelFile ,originChannel, originalWidth, originalHeight);

    return 0;
}