#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "processing_task_queue_impl.hpp"

#include <math.h>
#include <fstream>
#include <memory>
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <openssl/sha.h>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "imageHelper/stb_image.h"
#include "imageHelper/stb_image_write.h"
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>


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
                }
            }
        }

        std::vector<uint8_t> GetPart(int part)
        {
            return splitparts_.at(part);
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
                libp2p::multi::MulticodecType::Code::SHA2_256,
                hash.value()
            );
        }

    private:
        std::vector<std::vector<uint8_t>> splitparts_;
        int partwidth_ = 32;
        int partheight_ = 32;
        stbi_uc* inputImage;
        size_t imageSize;
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
                std::cout << "split check: " << i << std::endl;
                auto cidcheck = SplitImage.GetPartCID(i);
                
                std::cout << "Split CID: " << cidcheck.toPrettyString("") << std::endl;;
                auto subtaskId = (boost::format("subtask_%d") % i).str();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock(task.ipfs_block_id());
                std::cout << "BLOCK ID CHECK: " << task.ipfs_block_id() << std::endl;
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

    //Load Image
    const auto poseModel = argv[1];
    const auto inputImageFileName = argv[2];

    ImageSplitter imagesplit(inputImageFileName, 128, 128);
    std::cout << "Image Split Size: " << imagesplit.GetPartCount() << std::endl;
    //return 1;
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    //Make Pubsubs
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());
    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        );
    //Start Pubsubs, add peers of other addresses.
    pubs->Start(40001, { pubs2->GetLocalAddress() });

    pubs2->Start(40002, { pubs->GetLocalAddress() });

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;
    size_t nTasks = 1;
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
    //Asio Context
    auto io = std::make_shared<boost::asio::io_context>();
    //Create Pubsub Topic
    //auto topic = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel");
    //topic->Subscribe([&](boost::optional<const GossipPubSub::Message&> message)
    //    {
    //        if (message)
    //        {
    //            std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
    //            std::cout << "Pubs 1 Got message: " << message << std::endl;;
    //        }
    //    });

    //Add to GlobalDB
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));

    auto pubsTopic2 = pubs2->Subscribe("CRDT.Datastore.TEST.Channel", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                std::cout << "Pubs 2 Got message: " << message << std::endl;
                //receivedMessages.push_back(std::move(message));
            }
        });

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init(crdtOptions);
    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);
    size_t nSubTasks = 16;
    size_t nChunks = 1;
    TaskSplitter taskSplitter(
        nSubTasks,
        nChunks,
        false);

    for (auto& task : tasks)
    {
        std::list<SGProcessing::SubTask> subTasks;
        taskSplitter.SplitTask(task, subTasks, imagesplit);
        taskQueue->EnqueueTask(task, subTasks);
    }
    return 1;
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