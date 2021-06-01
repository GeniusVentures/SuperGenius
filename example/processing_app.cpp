#include "processing/processing_service.hpp"

#include <iostream>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t nSubtasks, size_t subTaskProcessingTime)
            : m_nSubtasks(nSubtasks)
            , m_subTaskProcessingTime(subTaskProcessingTime)
        {
        }

        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            for (size_t i = 0; i < m_nSubtasks; ++i)
            {
                auto subtask = std::make_unique<SGProcessing::SubTask>();
                subtask->set_ipfsblock(task.ipfs_block_id());
                subtask->set_results_channel((boost::format("%s_subtask_%d") % task.results_channel() % i).str());
                subTasks.push_back(std::move(subtask));
            }
        }

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override 
        {
            std::cout << "SubTask processing started. " << subTask.results_channel() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(m_subTaskProcessingTime));
            std::cout << "SubTask processed. " << subTask.results_channel() << std::endl;
            result.set_ipfs_results_data_id((boost::format("%s_%s") % "RESULT_IPFS" % subTask.results_channel()).str());
        }
        
    private:
        size_t m_nSubtasks;
        size_t m_subTaskProcessingTime;
    };


    class ProcessingTaksQueueImpl : public ProcessingTaksQueue
    {
    public:
        ProcessingTaksQueueImpl(const std::list<SGProcessing::Task>& tasks)
            : m_tasks(tasks)
        {
        }

        bool PopTask(SGProcessing::Task& task) override
        {
            if (m_tasks.empty())
            {
                return false;
            }


            task = std::move(m_tasks.back());
            m_tasks.pop_back();

            return true;
        };

    private:
        std::list<SGProcessing::Task> m_tasks;
    };

    // cmd line options
    struct Options 
    {
        size_t serviceIndex = 0;
        size_t subTaskProcessingTime = 0; // ms
        size_t roomSize = 0;
        size_t disconnect = 0;
        size_t nSubTasks = 5;
        size_t channelListRequestTimeout = 5000;
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
                ("processingtime,p", po::value(&o.subTaskProcessingTime), "subtask processing time (ms)")
                ("roomsize,s", po::value(&o.roomSize), "subtask processing time (ms)")
                ("disconnect,d", po::value(&o.disconnect), "disconnect after (ms)")
                ("nsubtasks,n", po::value(&o.nSubTasks), "number of subtasks that task is split to")
                ("channellisttimeout,t", po::value(&o.channelListRequestTimeout), "chnnel list request timeout (ms)")
                ("serviceindex,i", po::value(&o.serviceIndex), "index of the service in computational grid (has to be a unique value)");

            po::variables_map vm;
            po::store(parse_command_line(argc, argv, desc), vm);
            po::notify(vm);

            if (vm.count("help") != 0 || argc == 1) 
            {
                std::cerr << desc << "\n";
                return boost::none;
            }

            if (o.serviceIndex == 0) 
            {
                std::cerr << "Service index should be > 0\n";
                return boost::none;
            }

            if (o.subTaskProcessingTime == 0)
            {
                std::cerr << "SubTask processing time should be > 0\n";
                return boost::none;
            }

            if (o.roomSize == 0)
            {
                std::cerr << "Processing room size should be > 0\n";
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
    

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();

    if (options->serviceIndex == 1)
    {
        std::string publicKey = "z5b3BTS9wEgJxi9E8NHH6DT8Pj9xTmxBRgTaRUpBVox9a";
        std::string privateKey = "zGRXH26ag4k9jxTGXp2cg8n31CEkR2HN1SbHaKjaHnFTu";

        libp2p::crypto::KeyPair keyPair;
        auto codec = libp2p::multi::MultibaseCodecImpl();
        keyPair.publicKey = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode(publicKey).value() };
        keyPair.privateKey = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode(privateKey).value() };

        pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(keyPair);
    }

    if (options->remote)
    {
        pubs->Start(40001, { *options->remote });
    }
    else
    {
        pubs->Start(40001, {});
    }

    const size_t maximalNodesCount = 1;

    std::list<SGProcessing::Task> tasks;

    // Only a service with index equal to 1 has a locked task (job)
    if (options->serviceIndex == 1)
    {
        SGProcessing::Task task;
        task.set_ipfs_block_id("IPFS_BLOCK_ID_1");
        task.set_block_len(1000);
        task.set_block_line_stride(2);
        task.set_block_stride(4);
        task.set_random_seed(0);
        task.set_results_channel("RESULT_CHANNEL_ID_1");
        tasks.push_back(std::move(task));
    }

    boost::asio::deadline_timer timerToDisconnect(*pubs->GetAsioContext());
    if (options->disconnect > 0)
    {
        timerToDisconnect.expires_from_now(boost::posix_time::milliseconds(options->disconnect));
        timerToDisconnect.async_wait([pubs, &timerToDisconnect](const boost::system::error_code& error)
        {
            timerToDisconnect.expires_at(boost::posix_time::pos_infin);
            pubs->Stop();
        });
    }

    auto taskQueue = std::make_shared<ProcessingTaksQueueImpl>(tasks);
    auto processingCore = std::make_shared<ProcessingCoreImpl>(options->nSubTasks, options->subTaskProcessingTime);
    ProcessingServiceImpl processingService(pubs, maximalNodesCount, options->roomSize, taskQueue, processingCore);

    processingService.Listen(processingGridChannel);
    processingService.SetChannelListRequestTimeout(boost::posix_time::milliseconds(options->channelListRequestTimeout));

    processingService.SendChannelListRequest();

    // Gracefully shutdown on signal
    boost::asio::signal_set signals(*pubs->GetAsioContext(), SIGINT, SIGTERM);
    signals.async_wait(
        [&pubs](const boost::system::error_code&, int)
        {
            pubs->Stop();
        });

    pubs->Wait();

    return 0;
}

