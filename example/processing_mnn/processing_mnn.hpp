#pragma once
#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "processing_task_queue_impl.hpp"
#include "processing_subtask_result_storage_impl.hpp"
#include <processing/processing_service.hpp>
#include <processing/processing_subtask_enqueuer_impl.hpp>
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <crdt/globaldb/proto/broadcast.pb.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <openssl/sha.h>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "mnn_posenet.hpp"
//Async IO Manager Stuff
#include "Singleton.hpp"
#include "FileManager.hpp"
#include "URLStringUtil.h"
#include <libp2p/injector/host_injector.hpp>
#include "libp2p/injector/kademlia_injector.hpp"

namespace
{
    class ImageSplitter
    {
    public:
        ImageSplitter() {

        }
        ImageSplitter(
            const char* filename,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen
        ) : blockstride_(blockstride), blocklinestride_(blocklinestride), blocklen_(blocklen) {
            int originalWidth;
            int originalHeight;
            int originChannel;
            inputImage = stbi_load(filename, &originalWidth, &originalHeight, &originChannel, 4);
            imageSize = originalWidth * originalHeight * 4;
            //std::cout << " Image Size : " << imageSize << std::endl;
            // Check if imageSize is evenly divisible by blocklen_
            SplitImageData();
        }
        ImageSplitter(const std::vector<char>& buffer, uint32_t blockstride, uint32_t blocklinestride, uint32_t blocklen)
            : blockstride_(blockstride), blocklinestride_(blocklinestride), blocklen_(blocklen) {
            // Set inputImage and imageSize from the provided buffer
            //inputImage = reinterpret_cast<const unsigned char*>(buffer.data());
            int originalWidth;
            int originalHeight;
            int originChannel;
            inputImage = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(buffer.data()), buffer.size(), &originalWidth, &originalHeight, &originChannel, STBI_rgb_alpha);
            imageSize = originalWidth * originalHeight * 4;

            SplitImageData();
        }
        
        std::vector<uint8_t> GetPart(int part)
        {
            return splitparts_.at(part);
        }

        size_t GetPartByCid(libp2p::multi::ContentIdentifier cid)
        {
            //Find the index of cid in cids_
            auto it = std::find(cids_.begin(), cids_.end(), cid);
            if (it == cids_.end()) {
                //CID not found
                return -1;
            }

            //Find index in splitparts_ corresponding to cid
            size_t index = std::distance(cids_.begin(), it);

            //Return the data
            if (index < splitparts_.size()) {
                return index;
            }
            else {
                //Index out of range
                return -1; 
            }
        }

        uint32_t GetPartSize(int part)
        {
            return splitparts_.at(part).size();
        }

        uint32_t GetPartStride(int part)
        {
            return chunkWidthActual_.at(part);
        }

        int GetPartWidthActual(int part)
        {
            return chunkWidthActual_.at(part);
        }

        int GetPartHeightActual(int part)
        {
            return chunkHeightActual_.at(part);
        }

        size_t GetPartCount()
        {
            return splitparts_.size();
        }

        size_t GetImageSize()
        {
            return imageSize;
        }

        libp2p::multi::ContentIdentifier GetPartCID(int part)
        {
            return cids_.at(part);
        }

    private:
        std::vector<std::vector<uint8_t>> splitparts_;
        int partwidth_ = 32;
        int partheight_ = 32;
        uint32_t blockstride_;
        uint32_t blocklinestride_;
        uint32_t blocklen_;
        const unsigned char* inputImage;
        size_t imageSize;
        std::vector<int> chunkWidthActual_;
        std::vector<int> chunkHeightActual_;
        std::vector<libp2p::multi::ContentIdentifier> cids_;

        void SplitImageData()
        {
            // Check if imageSize is evenly divisible by blocklen_
            if (imageSize % blocklen_ != 0) {
                throw std::invalid_argument("Image size is not evenly divisible by block length");
            }

            for (uint32_t i = 0; i < imageSize; i += blocklen_)
            {
                std::vector<uint8_t> chunkBuffer(blocklen_);
                int rowsdone = (i / (blocklen_ *
                    ((blockstride_ + blocklinestride_) / blockstride_)));
                uint32_t bufferoffset = 0 + (i / blocklen_ * blockstride_);
                bufferoffset -= (blockstride_ + blocklinestride_) * rowsdone;
                bufferoffset +=
                    rowsdone
                    * (blocklen_ *
                        ((blockstride_ + blocklinestride_) / blockstride_));
                //std::cout << "buffer offset:  " << bufferoffset << std::endl;
                for (uint32_t size = 0; size < blocklen_; size += blockstride_)
                {
                    auto chunkData = inputImage + bufferoffset;
                    std::memcpy(chunkBuffer.data() + (size), chunkData, blockstride_);
                    bufferoffset += blockstride_ + blocklinestride_;
                }
                //std::string filename = "chunk_" + std::to_string(i) + ".png";
                //int result = stbi_write_png(filename.c_str(), blockstride_ / 4, blocklen_ / blockstride_, 4, chunkBuffer.data(), blockstride_);
                //if (!result) {
                //    std::cerr << "Error writing PNG file: " << filename << "\n";
                //}
                splitparts_.push_back(chunkBuffer);
                chunkWidthActual_.push_back(blockstride_ / 4);
                chunkHeightActual_.push_back(blocklen_ / blockstride_);
                gsl::span<const uint8_t> byte_span(chunkBuffer);
                std::vector<uint8_t> shahash(SHA256_DIGEST_LENGTH);
                SHA256_CTX sha256;
                SHA256_Init(&sha256);
                SHA256_Update(&sha256, chunkBuffer.data(), chunkBuffer.size());
                SHA256_Final(shahash.data(), &sha256);
                auto hash = libp2p::multi::Multihash::create(libp2p::multi::HashType::sha256, shahash);
                cids_.push_back(libp2p::multi::ContentIdentifier(
                    libp2p::multi::ContentIdentifier::Version::V0,
                    libp2p::multi::MulticodecType::Code::DAG_PB,
                    hash.value()
                ));
            }
        }
    };

    class TaskSplitter
    {
    public:
        TaskSplitter(
            size_t nSubTasks,
            size_t nChunks,
            bool addValidationSubtask)
            : m_nSubTasks(nSubTasks)
            , m_nChunks(nChunks)
            , m_addValidationSubtask(addValidationSubtask)
        {
        }

        void SplitTask(const SGProcessing::Task& task, std::list<SGProcessing::SubTask>& subTasks, ImageSplitter& SplitImage)
        {
            std::optional<SGProcessing::SubTask> validationSubtask;
            if (m_addValidationSubtask)
            {
                validationSubtask = SGProcessing::SubTask();
            }

            size_t chunkId = 0;
            for (size_t i = 0; i < m_nSubTasks; ++i)
            {
                auto cidcheck = SplitImage.GetPartCID(i);
                auto base58 = libp2p::multi::ContentIdentifierCodec::toString(cidcheck);
                //std::cout << "Base56:    " << base58.value() << std::endl;
                //std::cout << "Task BlockID  : " << task.ipfs_block_id() << std::endl;

                auto subtaskId = (boost::format("subtask_%d") % i).str();

                //auto subtaskId = base58.value();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock(task.ipfs_block_id());
                //subtask.set_ipfsblock(base58.value());
                //std::cout << "BLOCK ID CHECK: " << task.ipfs_block_id() << std::endl;
                std::cout << "Subtask ID :: " << subtaskId << std::endl;
                std::cout << "Subtask CID :: " << task.ipfs_block_id() << std::endl;
                subtask.set_subtaskid(base58.value());
                //subtask.set_subtaskid(subtaskId);

                for (size_t chunkIdx = 0; chunkIdx < m_nChunks; ++chunkIdx)
                {
                    SGProcessing::ProcessingChunk chunk;
                    chunk.set_chunkid((boost::format("CHUNK_%d_%d") % i % chunkId).str());
                    chunk.set_n_subchunks(1);
                    chunk.set_line_stride(1);
                    chunk.set_offset(0);
                    chunk.set_stride(1);
                    chunk.set_subchunk_height(10);
                    chunk.set_subchunk_width(10);

                    auto chunkToProcess = subtask.add_chunkstoprocess();
                    chunkToProcess->CopyFrom(chunk);

                    if (validationSubtask)
                    {
                        if (chunkIdx == 0)
                        {
                            // Add the first chunk of a processing subtask into the validation subtask
                            auto chunkToValidate = validationSubtask->add_chunkstoprocess();
                            chunkToValidate->CopyFrom(chunk);
                        }
                    }

                    ++chunkId;
                }
                subTasks.push_back(std::move(subtask));
            }

            if (validationSubtask)
            {
                auto subtaskId = (boost::format("subtask_validation")).str();
                validationSubtask->set_ipfsblock(task.ipfs_block_id());
                validationSubtask->set_subtaskid(subtaskId);
                subTasks.push_back(std::move(*validationSubtask));
            }
        }
    private:
        size_t m_nSubTasks;
        size_t m_nChunks;
        bool m_addValidationSubtask;
    };

    using namespace sgns::processing;
    class SubTaskStateStorageImpl : public SubTaskStateStorage
    {
    public:
        void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) override {}
        std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) override
        {
            return std::nullopt;
        }
    };

    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db,
            size_t subTaskProcessingTime,
            size_t maximalProcessingSubTaskCount)
            : m_db(db)
            , m_subTaskProcessingTime(subTaskProcessingTime)
            , m_maximalProcessingSubTaskCount(maximalProcessingSubTaskCount)
            , m_processingSubTaskCount(0)
        {
        }

        void setImageSplitter(const ImageSplitter& imagesplit) {
            imagesplit_ = imagesplit;
        }
        void setModelFile(const char* modelFile) {
            modelFile_ = modelFile;
        }
        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override
        {
            {
                std::scoped_lock<std::mutex> subTaskCountLock(m_subTaskCountMutex);
                ++m_processingSubTaskCount;
                if ((m_maximalProcessingSubTaskCount > 0)
                    && (m_processingSubTaskCount > m_maximalProcessingSubTaskCount))
                {
                    // Reset the counter to allow processing restart
                    m_processingSubTaskCount = 0;
                    throw std::runtime_error("Maximal number of processed subtasks exceeded");
                }
            }
            //std::cout << "Test CID:       " << imagesplit_.GetPartCID(1).toPrettyString("") << std::endl;
            std::cout << "Process subtask " << subTask.subtaskid() << std::endl;
            std::cout << "IPFS BLOCK:           " << subTask.ipfsblock() << std::endl;
            
            //auto hash = libp2p::multi::Multihash::create()
            if (cidData_.find(subTask.ipfsblock()) == cidData_.end())
            {
                //ASIO for Async, should probably be made to use the main IO but this class doesn't have it 
                libp2p::protocol::kademlia::Config kademlia_config;
                kademlia_config.randomWalk.enabled = true;
                kademlia_config.randomWalk.interval = std::chrono::seconds(300);
                kademlia_config.requestConcurency = 20;
                auto injector = libp2p::injector::makeHostInjector(
                    libp2p::injector::makeKademliaInjector(
                        libp2p::injector::useKademliaConfig(kademlia_config)));
                auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

                boost::asio::io_context::executor_type executor = ioc->get_executor();
                boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
                //Get Image Async
                FileManager::GetInstance().InitializeSingletons();
                string fileURL = "ipfs://" + subTask.ipfsblock() + "/test.png";
                auto data = FileManager::GetInstance().LoadASync(fileURL, false, false, ioc, [](const int& status)
                    {
                        std::cout << "status: " << status << std::endl;
                    }, [subTask, &result, initialHashCode, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                        {
                            std::cout << "Final Callback" << std::endl;
                            //this->cidData_.first.push_back(subTask.ipfsblock());
                            //this->cidData_.second.push_back(buffers->second.at(0));
                            this->cidData_.insert({ subTask.ipfsblock(), buffers->second.at(0) });
                            this->ProcessSubTask2(subTask, result, initialHashCode, buffers->second.at(0));
                        }, "file");
                ioc->reset();
                ioc->run();
            }
            else {
                auto it = cidData_.find(subTask.ipfsblock());
                ProcessSubTask2(subTask, result, initialHashCode, it->second);
            }
        }
        void  ProcessSubTask2(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode, std::vector<char> buffer)
        {
            //std::cout << "Process Subtask 2" << std::endl;
            //Splite image
            ImageSplitter animageSplit(buffer, 540, 4860, 48600);
            //Get Part Data 
            //std::cout << "ID : " << subTask.subtaskid() << std::endl;
            auto dataindex = animageSplit.GetPartByCid(sgns::CID::fromString(subTask.subtaskid()).value());
            auto data = animageSplit.GetPart(dataindex);
            auto width = animageSplit.GetPartWidthActual(dataindex);
            auto height = animageSplit.GetPartHeightActual(dataindex);
            auto mnnproc = sgns::mnn::MNN_PoseNet(&data, modelFile_, width, height, (boost::format("%s_%s") % "RESULT_IPFS" % subTask.subtaskid()).str() + ".png");

            auto procresults = mnnproc.StartProcessing();


            gsl::span<const uint8_t> byte_span(procresults);
            std::vector<uint8_t> shahash(SHA256_DIGEST_LENGTH);
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, &procresults, sizeof(procresults));
            SHA256_Final(shahash.data(), &sha256);
            auto hash = libp2p::multi::Multihash::create(libp2p::multi::HashType::sha256, shahash);
            //auto cid = libp2p::multi::ContentIdentifier(
            //    libp2p::multi::ContentIdentifier::Version::V0,
            //    libp2p::multi::MulticodecType::Code::DAG_PB,
            //    hash.value()
            //);
            sgns::CID cid(libp2p::multi::ContentIdentifier(
                libp2p::multi::ContentIdentifier::Version::V0,
                libp2p::multi::MulticodecType::Code::DAG_PB,
                hash.value()));

            result.set_ipfs_results_data_id(cid.toString().value());
            //std::this_thread::sleep_for(std::chrono::milliseconds(m_subTaskProcessingTime));
            //result.set_ipfs_results_data_id((boost::format("%s_%s") % "RESULT_IPFS" % subTask.subtaskid()).str());

            //bool isValidationSubTask = (subTask.subtaskid() == "subtask_validation");
            //size_t subTaskResultHash = initialHashCode;
            //for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
            //{
            //    const auto& chunk = subTask.chunkstoprocess(chunkIdx);

            //    // Chunk result hash should be calculated
            //    // Chunk data hash is calculated just as a stub
            //    size_t chunkHash = 0;
            //    if (isValidationSubTask)
            //    {
            //        chunkHash = ((size_t)chunkIdx < m_validationChunkHashes.size()) ?
            //            m_validationChunkHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
            //    }
            //    else
            //    {
            //        chunkHash = ((size_t)chunkIdx < m_chunkResulHashes.size()) ?
            //            m_chunkResulHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
            //    }

            //    result.add_chunk_hashes(chunkHash);
            //    boost::hash_combine(subTaskResultHash, chunkHash);
            //}
            //uint32_t temphash = 0;
            //if (shahash.size() >= 4)
            //{
            //    for (int i = 0; i < 4; ++i) {
            //        temphash |= static_cast<uint32_t>(shahash[i]) << (8 * i);
            //    }
            //}
            std::string hashString(shahash.begin(), shahash.end());
            result.set_result_hash(hashString.data());
            std::cout << "end processing" << std::endl;
        }
        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        size_t m_subTaskProcessingTime;
        size_t m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t m_processingSubTaskCount;
        ImageSplitter imagesplit_;
        const char* modelFile_;
        std::map<std::string, std::vector<char>> cidData_;
    };

}

