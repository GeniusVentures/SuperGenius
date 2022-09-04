#include "infura_ipfs.hpp"

#include <processing/processing_task_queue_impl.hpp>
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <boost/format.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

int main(int argc, const char* argv)
{

    const std::string logger_config(R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: processing_dapp
        sink: console
        level: info
        children:
          - name: libp2p
          - name: Gossip
    # ----------------
    )");

    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();

    libp2p::log::setLoggingSystem(logging_system);

    // Task data preparation
    std::ifstream spvFile("array_multiplication.comp.spv", std::ios::binary);

    std::vector<char> spvData;
    spvData.insert(spvData.begin(), std::istreambuf_iterator<char>(spvFile), {});

    spvFile.close();

    std::stringstream taskData;

    // Add signature
    char signature[] = {'t', 's', 'k', '1'};
    taskData.write(signature, sizeof(signature) / sizeof(char));

    // Add spirv module
    uint64_t spvSize = spvData.size();
    taskData.write((char*)&spvSize, sizeof(uint64_t));
    taskData.write(spvData.data(), spvSize);

    // Add data to process
    std::vector<float> data(
        { 1.0, 3.0, 5.0, 7.0, 2.0, 4.0, 6.0, 8.0, 9.0 }
    );
    uint64_t dataSize = data.size();
    taskData.write((char*)&dataSize, sizeof(uint64_t));

    for (auto& val : data)
    {
        taskData.write((char*)&val, sizeof(float));
    }

    // Add task data to IPFS
    std::ifstream autorizationKeyFile("authorizationKey.txt");

    if (!autorizationKeyFile.is_open())
    {
        std::cout << "Infura authorization key file not found" << std::endl;
        return EXIT_FAILURE;
    }
    std::string autorizationKey;
    std::getline(autorizationKeyFile, autorizationKey);

    InfuraIPFS ipfs(autorizationKey);
    auto cid = ipfs.AddIPFSFile("sg_task_data.bin", taskData.str());

    std::cout << cid << std::endl;

    // Create IPFS job
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());

    // @todo Init bootstrap peers
    std::vector<std::string> pubsubBootstrapPeers;
    pubs->Start(40001, pubsubBootstrapPeers);

    auto io = std::make_shared<boost::asio::io_context>();
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));


    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init(crdtOptions);

    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);

    SGProcessing::Task task;
    task.set_ipfs_block_id(cid);
    task.set_block_len(data.size());
    task.set_block_line_stride(4);
    task.set_block_stride(1);
    task.set_random_seed(0);
    task.set_results_channel((boost::format("RESULT_CHANNEL_ID_%1%") % cid).str());

    std::list<SGProcessing::SubTask> subTasks;

    const size_t nSubTasks = 3;
    const size_t nChunks = 9;
    std::vector<SGProcessing::ProcessingChunk> chunks;

    for (size_t chunkIdx = 0; chunkIdx < nChunks; ++chunkIdx)
    {
        SGProcessing::ProcessingChunk chunk;
        chunk.set_chunkid((boost::format("chunk_%s_%d") % cid % chunkIdx).str());
        chunk.set_n_subchunks(1);
        chunk.set_line_stride(task.block_line_stride());
        chunk.set_offset(chunkIdx * 2);
        chunk.set_stride(2);
        chunk.set_subchunk_height(1);
        chunk.set_subchunk_width(2);

        chunks.push_back(std::move(chunk));
    }


    for (size_t subTaskIdx = 0; subTaskIdx < nSubTasks; ++subTaskIdx)
    {
        auto subtaskId = (boost::format("subtask_%s_%d") % cid % subTaskIdx).str();
        SGProcessing::SubTask subtask;
        subtask.set_ipfsblock(task.ipfs_block_id());
        subtask.set_subtaskid(subtaskId);

        subtask.add_chunkstoprocess()->CopyFrom(chunks[subTaskIdx * 3 + 0]);
        subtask.add_chunkstoprocess()->CopyFrom(chunks[subTaskIdx * 3 + 1]);
        subtask.add_chunkstoprocess()->CopyFrom(chunks[subTaskIdx * 3 + 2]);

        subTasks.push_back(std::move(subtask));
    }

    // Validation subtask
    auto subtaskId = (boost::format("subtask_%s_%d") % cid % 1000).str();
    SGProcessing::SubTask subtask;
    subtask.set_ipfsblock(task.ipfs_block_id());
    subtask.set_subtaskid(subtaskId);

    subtask.add_chunkstoprocess()->CopyFrom(chunks[0]);
    subtask.add_chunkstoprocess()->CopyFrom(chunks[3]);
    subtask.add_chunkstoprocess()->CopyFrom(chunks[6]);

    subTasks.push_back(std::move(subtask));

    taskQueue->EnqueueTask(task, subTasks);

    std::cout << "TASK equeued" << std::endl;

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

    return 0;
}
