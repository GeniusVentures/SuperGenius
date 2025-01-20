/**
* Header file for processing posenet using MNN
* @author Justin Church
*/
#pragma once
#include <cmath>
#include <memory>
#include <vector>

#include <MNN/ImageProcess.hpp>
#include <MNN/Interpreter.hpp>
#include "processing/processing_processor.hpp"
#define MNN_OPEN_TIME_TRACE
#include <MNN/AutoTime.hpp>

namespace sgns::processing
{
    using namespace MNN;

    class MNN_Audio : public ProcessingProcessor
    {
    public:
        /** Create a posenet processor
        */
        MNN_Audio() 
            //imageData_( std::make_unique<std::vector<std::vector<char>>>() ),
            //modelFile_( std::make_unique<std::vector<uint8_t>>() )
        {
        }

        ~MNN_Audio() override{
            //stbi_image_free(imageData_);
        };
        /** Start processing data
        * @param result - Reference to result item to set hashes to
        * @param task - Reference to task to get image split data
        * @param subTask - Reference to subtask to get chunk data from
        */
        std::vector<uint8_t> StartProcessing( SGProcessing::SubTaskResult &result, 
                                              const SGProcessing::Task &task,
                                              const SGProcessing::SubTask &subTask, 
                                              std::vector<char> audioData, 
                                              std::vector<char> modelFile ) override;

        /** Set data for processor
        * @param buffers - Data containing file name and data pair lists.
        */
        //void SetData(
        //    std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers ) override;

    private:
        /** Run MNN processing on image
        * @param imgdata - RGBA image bytes
        * @param origwidth - Width of image
        * @param origheight - Height of image
        */
        std::unique_ptr<MNN::Tensor> Process( const std::vector<uint8_t> &audiodata, 
                                                std::vector<uint8_t> &modelFile, 
                                                const int channels, 
                                                const int origwidth, 
                                                const int origheight,
                                                const std::string filename = "" );

        //std::unique_ptr<std::vector<std::vector<char>>> imageData_;
        //std::unique_ptr<std::vector<uint8_t>>           modelFile_;
        //std::string                                     fileName_;
    };

}
