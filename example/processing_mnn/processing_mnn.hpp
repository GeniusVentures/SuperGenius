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
#include <processing/impl/processing_core_impl.hpp>
#include <processing/processing_imagesplit.hpp>
//Async IO Manager Stuff
//#include "Singleton.hpp"
//#include "FileManager.hpp"
//#include "URLStringUtil.h"
//#include <libp2p/injector/host_injector.hpp>
//#include "libp2p/injector/kademlia_injector.hpp"

namespace
{
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

        void SplitTask(const SGProcessing::Task& task, std::list<SGProcessing::SubTask>& subTasks, sgns::processing::ImageSplitter& SplitImage, std::vector<std::vector<uint32_t>> chunkOptions)
        {
            std::optional<SGProcessing::SubTask> validationSubtask;
            if (m_addValidationSubtask)
            {
                validationSubtask = SGProcessing::SubTask();
            }

            size_t chunkId = 0;
            for (size_t i = 0; i < m_nSubTasks; ++i)
            {
                auto cidcheck = SplitImage.GetPartCID(0);
                auto base58 = libp2p::multi::ContentIdentifierCodec::toString(cidcheck);
                //std::cout << "Base56:    " << base58.value() << std::endl;
                //std::cout << "Task BlockID  : " << task.ipfs_block_id() << std::endl;

                auto subtaskId = (boost::format("subtask_%d") % i).str();

                //auto subtaskId = base58.value();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock(task.ipfs_block_id());
                //subtask.set_ipfsblock(base58.value());
                //std::cout << "BLOCK ID CHECK: " << task.ipfs_block_id() << std::endl;
                std::cout << "Subtask ID :: " << base58.value() + "_" + std::to_string(i) << std::endl;
                std::cout << "Subtask CID :: " << task.ipfs_block_id() << std::endl;

                subtask.set_subtaskid(base58.value() + "_" + std::to_string(i));
                for (size_t chunkIdx = 0; chunkIdx < chunkOptions.at(i).at(5); ++chunkIdx)
                {
                    //std::cout << "AddChunk : " << chunkIdx << std::endl;
                    SGProcessing::ProcessingChunk chunk;
                    chunk.set_chunkid((boost::format("CHUNK_%d_%d") % i % chunkId).str());
                    chunk.set_n_subchunks(1);
                    chunk.set_line_stride(chunkOptions.at(i).at(0));
                    chunk.set_offset(chunkOptions.at(i).at(1));
                    chunk.set_stride(chunkOptions.at(i).at(2));
                    chunk.set_subchunk_height(chunkOptions.at(i).at(3));
                    chunk.set_subchunk_width(chunkOptions.at(i).at(4));

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
                //std::cout << "Subtask? " << subtask.chunkstoprocess_size() << std::endl;
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

    //class ProcessingCoreImpl : public ProcessingCore
    //{
    //public:
    //    ProcessingCoreImpl(
    //        std::shared_ptr<sgns::crdt::GlobalDB> db,
    //        size_t subTaskProcessingTime,
    //        size_t maximalProcessingSubTaskCount)
    //        : m_db(db)
    //        , m_subTaskProcessingTime(subTaskProcessingTime)
    //        , m_maximalProcessingSubTaskCount(maximalProcessingSubTaskCount)
    //        , m_processingSubTaskCount(0)
    //        , modelFile_()
    //    {
    //    }

    //    void setImageSplitter(const ImageSplitter& imagesplit) {
    //        imagesplit_ = imagesplit;
    //    }
    //    void setModelFile(const char* modelFile) {
    //        modelFile_ = modelFile;
    //    }
    //    void  ProcessSubTask(
    //        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
    //        uint32_t initialHashCode) override
    //    {
    //        {
    //            std::scoped_lock<std::mutex> subTaskCountLock(m_subTaskCountMutex);
    //            ++m_processingSubTaskCount;
    //            if ((m_maximalProcessingSubTaskCount > 0)
    //                && (m_processingSubTaskCount > m_maximalProcessingSubTaskCount))
    //            {
    //                // Reset the counter to allow processing restart
    //                m_processingSubTaskCount = 0;
    //                throw std::runtime_error("Maximal number of processed subtasks exceeded");
    //            }
    //        }
    //        //std::cout << "Test CID:       " << imagesplit_.GetPartCID(1).toPrettyString("") << std::endl;
    //        std::cout << "Process subtask " << subTask.subtaskid() << std::endl;
    //        std::cout << "IPFS BLOCK:           " << subTask.ipfsblock() << std::endl;
    //        
    //        //auto hash = libp2p::multi::Multihash::create()
    //        if (cidData_.find(subTask.ipfsblock()) == cidData_.end())
    //        {
    //            //ASIO for Async, should probably be made to use the main IO but this class doesn't have it 
    //            libp2p::protocol::kademlia::Config kademlia_config;
    //            kademlia_config.randomWalk.enabled = true;
    //            kademlia_config.randomWalk.interval = std::chrono::seconds(300);
    //            kademlia_config.requestConcurency = 20;
    //            auto injector = libp2p::injector::makeHostInjector(
    //                libp2p::injector::makeKademliaInjector(
    //                    libp2p::injector::useKademliaConfig(kademlia_config)));
    //            auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

    //            boost::asio::io_context::executor_type executor = ioc->get_executor();
    //            boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
    //            //Get Image Async
    //            FileManager::GetInstance().InitializeSingletons();
    //            string fileURL = "ipfs://" + subTask.ipfsblock() + "/test.png";
    //            auto data = FileManager::GetInstance().LoadASync(fileURL, false, false, ioc, [](const int& status)
    //                {
    //                    std::cout << "status: " << status << std::endl;
    //                }, [subTask, &result, initialHashCode, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
    //                    {
    //                        std::cout << "Final Callback" << std::endl;
    //                        //this->cidData_.first.push_back(subTask.ipfsblock());
    //                        //this->cidData_.second.push_back(buffers->second.at(0));
    //                        
    //                        if (!buffers || (buffers->first.empty() && buffers->second.empty()))
    //                        {
    //                            std::cout << "Buffer from AsyncIO is 0" << std::endl;
    //                            return;
    //                        }
    //                        else {
    //                            this->cidData_.insert({ subTask.ipfsblock(), buffers->second.at(0) });
    //                            this->ProcessSubTask2(subTask, result, initialHashCode, buffers->second.at(0));
    //                        }
    //                    }, "file");
    //            ioc->reset();
    //            ioc->run();
    //        }
    //        else {
    //            auto it = cidData_.find(subTask.ipfsblock());
    //            ProcessSubTask2(subTask, result, initialHashCode, it->second);
    //        }
    //    }
    //    void  ProcessSubTask2(
    //        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
    //        uint32_t initialHashCode, std::vector<char> buffer)
    //    {
    //        if (buffer.size() == 0)
    //        {
    //            std::cout << "No file obtained from IPFS" << std::endl;
    //            return;
    //        }
    //        //Splite image
    //        SGProcessing::Task task;
    //        auto queryTasks = m_db->Get("tasks/TASK_" + subTask.ipfsblock());
    //        if (queryTasks.has_value())
    //        {
    //            auto element = queryTasks.value();
    //            
    //            task.ParseFromArray(element.data(), element.size());
    //            //task.ParseFromArray(element, element.second.size());
    //        }
    //        ImageSplitter animageSplit(buffer, task.block_line_stride(), task.block_stride(), task.block_len());
    //        //Get Part Data 
    //        //auto dataindex = animageSplit.GetPartByCid(sgns::CID::fromString(subTask.subtaskid()).value());
    //        auto dataindex = 0;

    //        bool isValidationSubTask = (subTask.subtaskid() == "subtask_validation");
    //        auto basechunk = subTask.chunkstoprocess(0);
    //        //std::cout << "Check Nums:" << basechunk.line_stride() << std::endl;
    //        ImageSplitter ChunkSplit(animageSplit.GetPart(dataindex), basechunk.line_stride(), basechunk.stride(), animageSplit.GetPartHeightActual(dataindex) / basechunk.subchunk_height() * basechunk.line_stride());
    //        //std::string subTaskResultHash = "";
    //        std::vector<uint8_t> subTaskResultHash(SHA256_DIGEST_LENGTH);
    //        for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
    //        {
    //            std::cout << "Chunk IDX:  " << chunkIdx << "Total: " << subTask.chunkstoprocess_size() << std::endl;
    //            const auto& chunk = subTask.chunkstoprocess(chunkIdx);
    //            std::vector<uint8_t> shahash(SHA256_DIGEST_LENGTH);
    //            // Chunk result hash should be calculated
    //            size_t chunkHash = 0;
    //            if (isValidationSubTask)
    //            {
    //                //chunkHash = ((size_t)chunkIdx < m_validationChunkHashes.size()) ?
    //                //    m_validationChunkHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
    //            }
    //            else
    //            {
    //                
    //                auto data = ChunkSplit.GetPart(chunkIdx);
    //                auto width = ChunkSplit.GetPartWidthActual(chunkIdx);
    //                auto height = ChunkSplit.GetPartHeightActual(chunkIdx);
    //                auto mnnproc = sgns::mnn::MNN_PoseNet(&data, modelFile_, width, height, (boost::format("%s_%s") % "RESULT_IPFS" % std::to_string(chunkIdx)).str() + ".png");
    //                auto procresults = mnnproc.StartProcessing();

    //                gsl::span<const uint8_t> byte_span(procresults);
    //                
    //                SHA256_CTX sha256;
    //                SHA256_Init(&sha256);
    //                SHA256_Update(&sha256, &procresults, sizeof(procresults));
    //                SHA256_Final(shahash.data(), &sha256);

    //            }
    //            std::string hashString(shahash.begin(), shahash.end());
    //            result.add_chunk_hashes(hashString);


    //            SHA256_CTX sha256;
    //            SHA256_Init(&sha256);
    //            std::string combinedHash = std::string(subTaskResultHash.begin(), subTaskResultHash.end()) + hashString;
    //            SHA256_Update(&sha256, combinedHash.c_str(), sizeof(combinedHash));
    //            SHA256_Final(subTaskResultHash.data(), &sha256);
    //        }
    //        std::string hashString(subTaskResultHash.begin(), subTaskResultHash.end());
    //        result.set_result_hash(hashString);
    //        std::cout << "end processing " << std::endl;
    //    }
    //    std::vector<size_t> m_chunkResulHashes;
    //    std::vector<size_t> m_validationChunkHashes;

    //private:
    //    std::shared_ptr<sgns::crdt::GlobalDB> m_db;
    //    size_t m_subTaskProcessingTime;
    //    size_t m_maximalProcessingSubTaskCount;

    //    std::mutex m_subTaskCountMutex;
    //    size_t m_processingSubTaskCount;
    //    ImageSplitter imagesplit_;
    //    const char* modelFile_;
    //    std::map<std::string, std::vector<char>> cidData_;
    //};

}

