#include "processing_processor_mnn_ml.hpp"
#include <rapidjson/document.h>
#include "processing/processing_imagesplit.hpp"
#include <openssl/sha.h> // For SHA256_DIGEST_LENGTH


//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image.h"
//#include "stb_image_write.h"

namespace sgns::processing
{
    using namespace MNN;

    std::vector<uint8_t> MNN_ML::StartProcessing( SGProcessing::SubTaskResult &result,
                                                       const SGProcessing::Task    &task,
                                                       const SGProcessing::SubTask &subTask, 
                                                       std::vector<char> imageData, 
                                                       std::vector<char> modelFile)
    {
        std::vector<uint8_t> modelFile_bytes;
        modelFile_bytes.assign(modelFile.begin(), modelFile.end());

            //Get stride data
        std::vector<uint8_t> subTaskResultHash(SHA256_DIGEST_LENGTH);
        rapidjson::Document document;
        document.Parse(subTask.json_data().c_str());

            auto          dataindex           = 0;
            auto          basechunk           = subTask.chunkstoprocess( 0 );
            bool          isValidationSubTask = ( subTask.subtaskid() == "subtask_validation" );

            
            for ( int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx )
            {
            }
            return subTaskResultHash;
    }

    std::unique_ptr<MNN::Tensor> MNN_ML::Process(const std::vector<uint8_t>& imgdata, 
                                                         std::vector<uint8_t>& modelFile, 
                                                         const int channels, 
                                                         const int origwidth,
                                                         const int origheight, 
                                                         const std::string filename) 
    {
        auto outputHost = std::make_unique<MNN::Tensor>();
        return outputHost;
    }

}
