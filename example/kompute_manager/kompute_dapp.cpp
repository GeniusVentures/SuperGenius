#include "infura_ipfs.hpp"

#include <processing/processing_task_queue_impl.hpp>
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
// cmd line options
struct Options
{
    // optional remote peer to connect to
    std::optional<std::string> remote;
};

boost::optional<Options> parseCommandLine(int argc, char** argv)
{
    namespace po = boost::program_options;
    try
    {
        Options o;
        std::string remote;

        po::options_description desc("processing service options");
        desc.add_options()("help,h", "print usage message")
            ("remote,r", po::value(&remote), "remote service multiaddress to connect to")
            ;

        po::variables_map vm;
        po::store(parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help") != 0)
        {
            std::cerr << desc << "\n";
            return boost::none;
        }

        if (!remote.empty())
        {
            o.remote = remote;
        }

        return o;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return boost::none;
}
} // namespace

int main(int argc, char** argv)
{
    auto options = parseCommandLine(argc, argv);
    if (!options)
    {
        return 1;
    }

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
    std::string signature = "sg01";
    taskData.write(signature.c_str(), signature.size());

    // Add spirv module
    uint64_t spvSize = spvData.size();
    taskData.write((char*)&spvSize, sizeof(uint64_t));
    taskData.write(spvData.data(), spvSize);

    // Add data to process
    // 3x3 matrix
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
    if (options->remote)
    {
        pubsubBootstrapPeers = std::move(std::vector({ *options->remote }));
    }
    pubs->Start(40001, pubsubBootstrapPeers);

    auto io = std::make_shared<boost::asio::io_context>();
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));


    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init(crdtOptions);

    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<sgns::processing::ProcessingTaskQueueImpl>(globalDB);

    // Create a task
    SGProcessing::Task task;
    task.set_ipfs_block_id(cid);
    task.set_block_len(data.size());
    task.set_block_line_stride(4);
    task.set_block_stride(1);
    task.set_random_seed(0);
    task.set_results_channel((boost::format("RESULT_CHANNEL_ID_%1%") % cid).str());

    std::list<SGProcessing::SubTask> subTasks;

    // Split task to subtasks
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

    // Input data is 3x3 matrix
    // Each subtask includes 3 chunks where each chunk includes a processing of 1 value from the matrix
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

    // Validation subtask includes 1st chunk from each of main subtasks
    auto subtaskId = (boost::format("subtask_%s_%d") % cid % 1000).str();
    SGProcessing::SubTask subtask;
    subtask.set_ipfsblock(task.ipfs_block_id());
    subtask.set_subtaskid(subtaskId);

    subtask.add_chunkstoprocess()->CopyFrom(chunks[0]);
    subtask.add_chunkstoprocess()->CopyFrom(chunks[3]);
    subtask.add_chunkstoprocess()->CopyFrom(chunks[6]);

    subTasks.push_back(std::move(subtask));

    // Place task into queue
    taskQueue->EnqueueTask(task, subTasks);

    std::cout << "TASK enqueued" << std::endl;

    auto taskCompleteChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, 
        (boost::format("TASK_STATUS_%s") % task.ipfs_block_id()).str());

    taskCompleteChannel->Subscribe([channelId(taskCompleteChannel->GetTopic())](
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {
        if (message)
        {
            if (message->topic == channelId)
            {
                std::string status(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                std::cout << "TASK_STATUS: " << status << std::endl;
            }
        }
    });


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
