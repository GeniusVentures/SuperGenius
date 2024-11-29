#include "processing_processor_mnn_audio.hpp"
#include <rapidjson/document.h>
#include "processing/processing_imagesplit.hpp"

//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb_image.h"
//#include "stb_image_write.h"

namespace sgns::processing
{
    using namespace MNN;

    std::vector<uint8_t> MNN_Audio::StartProcessing( SGProcessing::SubTaskResult &result,
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
                // std::cout << "Chunk IDX:  " << chunkIdx << "Total: " << subTask.chunkstoprocess_size() << std::endl;
                // const auto          &chunk = subTask.chunkstoprocess( chunkIdx );
                // std::vector<uint8_t> shahash( SHA256_DIGEST_LENGTH );

                // // Chunk result hash should be calculated
                // size_t chunkHash = 0;
                // if ( isValidationSubTask )
                // {
                //     //chunkHash = ((size_t)chunkIdx < m_validationChunkHashes.size()) ?
                //     //    m_validationChunkHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
                // }
                // else
                // {
                //     auto procresults =
                //         Process( ChunkSplit.GetPart( chunkIdx ), modelFile_bytes, channels, ChunkSplit.GetPartWidthActual( chunkIdx ),
                //                     ChunkSplit.GetPartHeightActual( chunkIdx ) );

                //     const float *data     = procresults->host<float>();
                //     size_t       dataSize = procresults->elementSize() * sizeof( float );
                //     SHA256_CTX sha256;
                //     SHA256_Init( &sha256 );
                //     SHA256_Update( &sha256, data, dataSize );
                //     SHA256_Final( shahash.data(), &sha256 );
                // }
                // std::string hashString( shahash.begin(), shahash.end() );
                // result.add_chunk_hashes( hashString );

                // SHA256_CTX sha256;
                // SHA256_Init( &sha256 );
                // std::string combinedHash =
                //     std::string( subTaskResultHash.begin(), subTaskResultHash.end() ) + hashString;
                // SHA256_Update( &sha256, combinedHash.c_str(), sizeof( combinedHash ) );
                // SHA256_Final( subTaskResultHash.data(), &sha256 );
            }
            return subTaskResultHash;
    }

    std::unique_ptr<MNN::Tensor> MNN_Audio::Process(const std::vector<uint8_t>& imgdata, 
                                                         std::vector<uint8_t>& modelFile, 
                                                         const int channels, 
                                                         const int origwidth,
                                                         const int origheight, 
                                                         const std::string filename) 
    {

    }

}
