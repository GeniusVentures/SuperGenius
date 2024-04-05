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
#include "processing/processors/processing_processor_mnn_posenet.hpp"
#include <processing/impl/processing_core_impl.hpp>

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

}

