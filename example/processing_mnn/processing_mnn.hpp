#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "imageHelper/stb_image.h"
#include "imageHelper/stb_image_write.h"
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

namespace
{
    class ImageSplitter
    {
    public:
        ImageSplitter(
            const char* filename,
            int width,
            int height
        ) : partwidth_(width), partheight_(height) {
            int originalWidth;
            int originalHeight;
            int originChannel;
            inputImage = stbi_load(filename, &originalWidth, &originalHeight, &originChannel, 4);
            imageSize = originalWidth * originalHeight * 4;
            for (int y = 0; y < originalHeight; y += partheight_) {
                for (int x = 0; x < originalWidth; x += partwidth_) {
                    //Extract actual size
                    auto chunkWidthActual = std::min(partwidth_, originalWidth - x);
                    auto chunkHeightActual = std::min(partheight_, originalHeight - y);

                    //Create Buffer
                    //uint8_t* chunkBuffer = new uint8_t[4 * chunkWidthActual * chunkHeightActual];
                    std::vector<uint8_t> chunkBuffer(4 * chunkWidthActual * chunkHeightActual);
                    for (int i = 0; i < chunkHeightActual; i++)
                    {
                        auto chunkOffset = (y + i) * originalWidth * 4 + x * 4;
                        auto chunkData = inputImage + chunkOffset;
                        std::memcpy(chunkBuffer.data() + (i * 4 * chunkWidthActual), chunkData, 4 * chunkWidthActual);
                    }
                    splitparts_.push_back(chunkBuffer);
                    chunkWidthActual_.push_back(chunkWidthActual);
                    chunkHeightActual_.push_back(chunkHeightActual);
                }
            }
        }

        std::vector<uint8_t> GetPart(int part)
        {
            return splitparts_.at(part);
        }

        uint32_t GetPartSize(int part)
        {
            return splitparts_.at(part).size();
        }

        uint32_t GetPartStride(int part)
        {
            return chunkWidthActual_.at(part);
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
            gsl::span<const uint8_t> byte_span(splitparts_.at(part));
            std::vector<uint8_t> shahash(SHA256_DIGEST_LENGTH);
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, splitparts_.at(part).data(), splitparts_.at(part).size());
            SHA256_Final(shahash.data(), &sha256);

            auto hash = libp2p::multi::Multihash::create(libp2p::multi::HashType::sha256, shahash);
            return libp2p::multi::ContentIdentifier(
                libp2p::multi::ContentIdentifier::Version::V0,
                libp2p::multi::MulticodecType::Code::DAG_PB,
                hash.value()
            );
        }

    private:
        std::vector<std::vector<uint8_t>> splitparts_;
        int partwidth_ = 32;
        int partheight_ = 32;
        stbi_uc* inputImage;
        size_t imageSize;
        std::vector<int> chunkWidthActual_;
        std::vector<int> chunkHeightActual_;
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
                std::cout << "Base56:    " << base58.value() << std::endl;
                std::cout << "Task BlockID  : " << task.ipfs_block_id() << std::endl;

                auto subtaskId = (boost::format("subtask_%d") % i).str();

                //auto subtaskId = base58.value();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock(task.ipfs_block_id());
                //std::cout << "BLOCK ID CHECK: " << task.ipfs_block_id() << std::endl;
                std::cout << "Subtask ID :: " << subtaskId << std::endl;
                subtask.set_subtaskid(subtaskId);

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

            std::this_thread::sleep_for(std::chrono::milliseconds(m_subTaskProcessingTime));
            result.set_ipfs_results_data_id((boost::format("%s_%s") % "RESULT_IPFS" % subTask.subtaskid()).str());

            bool isValidationSubTask = (subTask.subtaskid() == "subtask_validation");
            size_t subTaskResultHash = initialHashCode;
            for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
            {
                const auto& chunk = subTask.chunkstoprocess(chunkIdx);

                // Chunk result hash should be calculated
                // Chunk data hash is calculated just as a stub
                size_t chunkHash = 0;
                if (isValidationSubTask)
                {
                    chunkHash = ((size_t)chunkIdx < m_validationChunkHashes.size()) ?
                        m_validationChunkHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
                }
                else
                {
                    chunkHash = ((size_t)chunkIdx < m_chunkResulHashes.size()) ?
                        m_chunkResulHashes[chunkIdx] : std::hash<std::string>{}(chunk.SerializeAsString());
                }

                result.add_chunk_hashes(chunkHash);
                boost::hash_combine(subTaskResultHash, chunkHash);
            }

            result.set_result_hash(subTaskResultHash);
        }

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        size_t m_subTaskProcessingTime;
        size_t m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t m_processingSubTaskCount;
    };


    //class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    //{
    //public:
    //    ProcessingTaskQueueImpl()
    //    {
    //    }

    //    void EnqueueTask(
    //        const SGProcessing::Task& task,
    //        const std::list<SGProcessing::SubTask>& subTasks)
    //    {
    //        m_tasks.push_back(task);
    //        m_subTasks.emplace(task.ipfs_block_id(), subTasks);
    //    }

    //    bool GetSubTasks(
    //        const std::string& taskId,
    //        std::list<SGProcessing::SubTask>& subTasks)
    //    {
    //        auto it = m_subTasks.find(taskId);
    //        if (it != m_subTasks.end())
    //        {
    //            subTasks = it->second;
    //            return true;
    //        }

    //        return false;
    //    }

    //    bool GrabTask(std::string& taskKey, SGProcessing::Task& task) override
    //    {
    //        if (m_tasks.empty())
    //        {
    //            return false;
    //        }


    //        task = std::move(m_tasks.back());
    //        m_tasks.pop_back();
    //        taskKey = (boost::format("TASK_%d") % m_tasks.size()).str();

    //        return true;
    //    };

    //    bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& task) override
    //    {
    //        return false;
    //    }

    //private:
    //    std::list<SGProcessing::Task> m_tasks;
    //    std::map<std::string, std::list<SGProcessing::SubTask>> m_subTasks;
    //};

}