#include "processing/proto/SGProcessing.pb.h"

#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>

#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <iostream>

namespace
{  
    // cmd line options
    struct Options 
    {
        // optional remote peer to connect to
        std::optional<std::string> remote;
    };

    boost::optional<Options> parseCommandLine(int argc, char** argv) {
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

            if (vm.count("help") != 0 || argc == 1) 
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

}

int main(int argc, char* argv[])
{
    auto options = parseCommandLine(argc, argv);
    if (!options)
    {
        return 1;
    }

    auto loggerPubSub = libp2p::common::createLogger("GossipPubSub");
    //loggerPubSub->set_level(spdlog::level::trace);

    auto loggerProcessingEngine = libp2p::common::createLogger("ProcessingEngine");
    loggerProcessingEngine->set_level(spdlog::level::trace);

    auto loggerProcessingService = libp2p::common::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::trace);

    auto loggerProcessingQueue = libp2p::common::createLogger("ProcessingSubTaskQueue");
    loggerProcessingQueue->set_level(spdlog::level::debug);
    
    auto loggerGlobalDB = libp2p::common::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::debug);

    auto loggerDAGSyncer = libp2p::common::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = libp2p::common::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::debug);

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());

    std::vector<std::string> pubsubBootstrapPeers;
    if (options->remote)
    {
        pubsubBootstrapPeers = std::move(std::vector({ *options->remote }));
    }
    pubs->Start(40001, pubsubBootstrapPeers);

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;

    // Put a single task to Global DB
    // And wait for its processing
    SGProcessing::Task task;
    task.set_ipfs_block_id("IPFS_BLOCK_ID_1");
    task.set_block_len(1000);
    task.set_block_line_stride(2);
    task.set_block_stride(4);
    task.set_random_seed(0);
    task.set_results_channel("RESULT_CHANNEL_ID_1");
    tasks.push_back(std::move(task));

    auto io = std::make_shared<boost::asio::io_context>();
    sgns::crdt::GlobalDB globalDB(
        io, "CRDT.Datastore.TEST", 40000,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));


    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB.Init(crdtOptions);

    std::thread iothread([io]() { io->run(); });

    size_t taskIdx = 0;
    for (auto& task : tasks)
    {
        auto taskKey = (boost::format("tasks/TASK_%d") % taskIdx).str();
        sgns::base::Buffer valueBuffer;
        valueBuffer.put(task.SerializeAsString());
        auto setKeyResult = globalDB.Put(sgns::crdt::HierarchicalKey(taskKey), valueBuffer);
        if (setKeyResult.has_failure())
        {
            std::cout << "Unable to put key-value to CRDT datastore." << std::endl;
        }

        // Check if data put
        auto getKeyResult = globalDB.Get(sgns::crdt::HierarchicalKey(taskKey));
        if (getKeyResult.has_failure())
        {
            std::cout << "Unable to find key in CRDT datastore"<< std::endl;
        }
        else
        {
            std::cout << "[" << "taskKey" << "] placed to GlobalDB " << std::endl;
            // getKeyResult.value().toString()
        }
        ++taskIdx;
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

    return 0;
}

