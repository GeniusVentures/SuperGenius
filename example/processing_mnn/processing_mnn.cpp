#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "processing_task_queue_impl.hpp"

#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include "stb_image.h"
#include "stb_image_write.h"
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>

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

        void SplitTask(const SGProcessing::Task& task, std::list<SGProcessing::SubTask>& subTasks)
        {
            std::optional<SGProcessing::SubTask> validationSubtask;
            if (m_addValidationSubtask)
            {
                validationSubtask = SGProcessing::SubTask();
            }

            size_t chunkId = 0;
            for (size_t i = 0; i < m_nSubTasks; ++i)
            {
                auto subtaskId = (boost::format("subtask_%d") % i).str();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock(task.ipfs_block_id());
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
}

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

int main(int argc, char* argv[])
{
    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();

    libp2p::log::setLoggingSystem(logging_system);
    libp2p::log::setLevelOfGroup("gossip_pubsub_test", soralog::Level::ERROR_);
    auto loggerProcessingEngine = sgns::base::createLogger("ProcessingEngine");
    loggerProcessingEngine->set_level(spdlog::level::trace);

    auto loggerProcessingService = sgns::base::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::trace);

    auto loggerProcessingQueueManager = sgns::base::createLogger("ProcessingSubTaskQueueManager");
    loggerProcessingQueueManager->set_level(spdlog::level::debug);

    auto loggerGlobalDB = sgns::base::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::debug);

    auto loggerDAGSyncer = sgns::base::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = sgns::base::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::debug);


    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Make Pubsubs
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());
    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());
    //Start Pubsubs, add peers of other addresses.
    pubs->Start(40001, { pubs2->GetLocalAddress() });

    pubs2->Start(40002, { pubs->GetLocalAddress() });

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;
    size_t nTasks = 10;
    // Put tasks to Global DB
    for (size_t taskIdx = 0; taskIdx < nTasks; ++taskIdx)
    {
        // And wait for its processing
        SGProcessing::Task task;
        task.set_ipfs_block_id((boost::format("IPFS_BLOCK_ID_%1%") % (taskIdx + 1)).str());
        task.set_block_len(1000);
        task.set_block_line_stride(2);
        task.set_block_stride(4);
        task.set_random_seed(0);
        task.set_results_channel((boost::format("RESULT_CHANNEL_ID_%1%") % (taskIdx + 1)).str());
        tasks.push_back(std::move(task));
    }
    auto io = std::make_shared<boost::asio::io_context>();
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init(crdtOptions);
    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);
    size_t nSubTasks = 5;
    size_t nChunks = 5;
    TaskSplitter taskSplitter(
        nSubTasks,
        nChunks,
        false);

    for (auto& task : tasks)
    {
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter.SplitTask(task, subTasks);
        taskQueue->EnqueueTask(task, subTasks);
    }

    // Gracefully shutdown on signal
    boost::asio::signal_set signals(*pubs->GetAsioContext(), SIGINT, SIGTERM);
    signals.async_wait(
        [&pubs, &io](const boost::system::error_code&, int)
        {
            pubs->Stop();
            io->stop();
        });

    pubs->Wait();
    iothread.join();

}