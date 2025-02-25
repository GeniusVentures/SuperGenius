#include "processing_processor_mnn_image.hpp"
#include <rapidjson/document.h>
#include "processing/processing_imagesplit.hpp"
#include <functional>
#include <thread>
#include <openssl/sha.h> // For SHA256_DIGEST_LENGTH
#include "crypto/sha/sha256.hpp"

//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image.h"
//#include "stb_image_write.h"

namespace sgns::processing
{
    using namespace MNN;

    std::vector<uint8_t> MNN_Image::StartProcessing( SGProcessing::SubTaskResult &result,
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
        auto block_len = document["block_len"].GetUint64();
        auto block_line_stride = document["block_line_stride"].GetUint64();
        auto block_stride = document["block_stride"].GetUint64();
        auto chunk_line_stride = document["chunk_line_stride"].GetUint64();
        auto chunk_offset = document["chunk_offset"].GetUint64();
        auto chunk_stride = document["chunk_stride"].GetUint64();
        auto chunk_subchunk_height = document["chunk_subchunk_height"].GetUint64();
        auto chunk_subchunk_width = document["chunk_subchunk_width"].GetUint64();
        auto chunk_count = document["chunk_count"].GetUint64();
        auto channels = document["channels"].GetUint64();

        //for ( auto image : *imageData_ )
        //{
            std::vector<uint8_t> output(imageData.size());
            std::transform(imageData.begin(), imageData.end(), output.begin(),
                            []( char c ) { return static_cast<uint8_t>( c ); } );
            //ImageSplitter animageSplit( output, task.block_line_stride(), task.block_stride(), task.block_len() );
            ImageSplitter animageSplit(output, block_line_stride, block_stride, block_len, channels);
            auto          dataindex           = 0;
            auto          basechunk           = subTask.chunkstoprocess( 0 );
            bool          isValidationSubTask = ( subTask.subtaskid() == "subtask_validation" );
            ImageSplitter ChunkSplit( animageSplit.GetPart( dataindex ), chunk_line_stride, chunk_stride,
                                      animageSplit.GetPartHeightActual( dataindex ) / chunk_subchunk_height *
                                            chunk_line_stride, channels);
            
            for ( int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx )
            {
                std::cout << "Chunk IDX:  " << chunkIdx << "Total: " << subTask.chunkstoprocess_size() << std::endl;
                const auto          &chunk = subTask.chunkstoprocess( chunkIdx );
                std::vector<uint8_t> shahash( SHA256_DIGEST_LENGTH );

                // Chunk result hash should be calculated
                size_t chunkHash = 0;
                if ( isValidationSubTask )
                {
                    //chunkHash = ((size_t)chunkIdx < m_validationChunkHashes.size()) ?
                    //    m_validationChunkHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
                }
                else
                {
                    auto procresults =
                        Process( ChunkSplit.GetPart( chunkIdx ), modelFile_bytes, channels, ChunkSplit.GetPartWidthActual( chunkIdx ),
                                    ChunkSplit.GetPartHeightActual( chunkIdx ) );

                    const float *data     = procresults->host<float>();
                    size_t       dataSize = procresults->elementSize() * sizeof( float );
                    shahash = sgns::crypto::sha256(data, dataSize);

                }
                std::string hashString( shahash.begin(), shahash.end() );
                result.add_chunk_hashes( hashString );

                std::string combinedHash = std::string(subTaskResultHash.begin(), subTaskResultHash.end()) + hashString;
                subTaskResultHash = sgns::crypto::sha256(combinedHash.c_str(), combinedHash.length());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return subTaskResultHash;
        //}
        //return subTaskResultHash;
    }

    std::unique_ptr<MNN::Tensor> MNN_Image::Process(const std::vector<uint8_t>& imgdata, 
                                                         std::vector<uint8_t>& modelFile, 
                                                         const int channels, 
                                                         const int origwidth,
                                                         const int origheight, 
                                                         const std::string filename) 
    {
        std::vector<uint8_t> ret_vect(imgdata);

        // Get Target Width
        const int targetWidth = static_cast<int>((float)origwidth / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;
        const int targetHeight = static_cast<int>((float)origheight / (float)OUTPUT_STRIDE) * OUTPUT_STRIDE + 1;

        // Scale
        CV::Point scale;
        scale.fX = (float)origwidth / (float)targetWidth;
        scale.fY = (float)origheight / (float)targetHeight;

        // Create net and session
        const void* buffer = static_cast<const void*>( modelFile.data() );
        auto mnnNet = std::shared_ptr<MNN::Interpreter>( MNN::Interpreter::createFromBuffer( buffer, modelFile.size() ) );

        MNN::ScheduleConfig netConfig;
        netConfig.type      = MNN_FORWARD_VULKAN;
        netConfig.numThread = 4;
        netConfig.mode = 0;
        auto session        = mnnNet->createSession( netConfig );

        auto input = mnnNet->getSessionInput( session, nullptr );

        if ( input->elementSize() <= 4 )
        {
            mnnNet->resizeTensor( input, { 1, 3, targetHeight, targetWidth } );
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
            trans.postScale( 1.0 / targetWidth, 1.0 / targetHeight );
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
        auto outputHost   = std::make_unique<MNN::Tensor>( outputTensor, MNN::Tensor::CAFFE );
        outputTensor->copyToHostTensor( outputHost.get() );

        return outputHost;
    }

}
