#include <processing/processing_service.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <crdt/globaldb/globaldb.hpp>

#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <iostream>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db,
            size_t nSubtasks,
            size_t subTaskProcessingTime,
            size_t nChunks,
            bool addValidationSubtask)
            : m_db(db)
            , m_nSubtasks(nSubtasks)
            , m_subTaskProcessingTime(subTaskProcessingTime)
            , m_nChunks(nChunks)
            , m_addValidationSubtask(addValidationSubtask)
        {
        }

        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            std::unique_ptr<SGProcessing::SubTask> validationSubtask = m_addValidationSubtask ?
                std::make_unique<SGProcessing::SubTask>() : nullptr;
            
            size_t chunkId = 0;
            for (size_t i = 0; i < m_nSubtasks; ++i)
            {
                auto subtaskId = (boost::format("subtask_%d") % i).str();
                auto subtask = std::make_unique<SGProcessing::SubTask>();
                subtask->set_ipfsblock(task.ipfs_block_id());
                subtask->set_results_channel(subtaskId);

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

                    auto chunkToProcess = subtask->add_chunkstoprocess();
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
                validationSubtask->set_results_channel(subtaskId);
                subTasks.push_back(std::move(validationSubtask));
            }
        }

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_subTaskProcessingTime));
            result.set_ipfs_results_data_id((boost::format("%s_%s") % "RESULT_IPFS" % subTask.results_channel()).str());

            bool isValidationSubTask = (subTask.results_channel() == "subtask_validation");
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
            sgns::crdt::GlobalDB::Buffer data;
            data.put(result.SerializeAsString());

            auto taskId = 
            m_db->Put(
                sgns::crdt::HierarchicalKey((boost::format("results/%s") % subTask.results_channel()).str().c_str()),
                data);
        }

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        size_t m_nSubtasks;
        size_t m_subTaskProcessingTime;
        size_t m_nChunks;
        bool m_addValidationSubtask;
    };


    class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        ProcessingTaskQueueImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db)
            : m_db(db)
            , m_processingTimeout(std::chrono::seconds(10))
        {
        }

        bool GrabTask(std::string& grabbedTaskKey, SGProcessing::Task& task) override
        {
            m_logger->info("GRAB_TASK");

            auto queryTasks = m_db->QueryKeyValues("tasks");
            if (queryTasks.has_failure())
            {
                m_logger->info("Unable list tasks from CRDT datastore");
                return false;
            }

            std::set<std::string> lockedTasks;
            if (queryTasks.has_value())
            {
                m_logger->info("TASK_QUEUE_SIZE: {}", queryTasks.value().size());
                bool isTaskGrabbed = false;
                for (auto element : queryTasks.value())
                {
                    auto taskKey = m_db->KeyToString(element.first);
                    if (taskKey.has_value())
                    {
                        bool isTaskLocked = IsTaskLocked(taskKey.value());
                        m_logger->debug("TASK_QUEUE_ITEM: {}, LOCKED: {}", taskKey.value(), isTaskLocked);

                        if (!isTaskLocked)
                        {
                            if (task.ParseFromArray(element.second.data(), element.second.size()))
                            {
                                if (LockTask(taskKey.value()))
                                {
                                    m_logger->debug("TASK_LOCKED {}", taskKey.value());
                                    grabbedTaskKey = taskKey.value();
                                    return true;
                                }
                            }
                        }
                        else
                        {
                            m_logger->debug("TASK_PREVIOUSLY_LOCKED {}", taskKey.value());
                            lockedTasks.insert(taskKey.value());
                        }
                    }
                    else
                    {
                        m_logger->debug("Undable to convert a key to string");
                    }
                }

                // No task was grabbed so far
                for (auto lockedTask : lockedTasks)
                {
                    if (MoveExpiredTaskLock(lockedTask, task))
                    {
                        grabbedTaskKey = lockedTask;
                        return true;
                    }
                }

            }
            return false;
        }

        bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& taskResult) override
        {
            sgns::base::Buffer data;
            data.put(taskResult.SerializeAsString());

            auto transaction = m_db->BeginTransaction();
            transaction->AddToDelta(sgns::crdt::HierarchicalKey("task_results/" + taskKey), data);
            transaction->RemoveFromDelta(sgns::crdt::HierarchicalKey("lock_" + taskKey));
            transaction->RemoveFromDelta(sgns::crdt::HierarchicalKey(taskKey));

            auto res = transaction->PublishDelta();
            m_logger->debug("TASK_COMPLETED: {}", taskKey);
            return !res.has_failure();
        }

        bool IsTaskLocked(const std::string& taskKey)
        {
            auto lockData = m_db->Get(sgns::crdt::HierarchicalKey("lock_" + taskKey));
            return (!lockData.has_failure() && lockData.has_value());
        }

        bool LockTask(const std::string& taskKey)
        {
            auto timestamp = std::chrono::system_clock::now();

            SGProcessing::TaskLock lock;
            lock.set_task_id(taskKey);
            lock.set_lock_timestamp(timestamp.time_since_epoch().count());

            sgns::base::Buffer lockData;
            lockData.put(lock.SerializeAsString());

            auto res = m_db->Put(sgns::crdt::HierarchicalKey("lock_" + taskKey), lockData);
            return !res.has_failure();
        }

        bool MoveExpiredTaskLock(const std::string& taskKey, SGProcessing::Task& task)
        {
            auto timestamp = std::chrono::system_clock::now();

            auto lockData = m_db->Get(sgns::crdt::HierarchicalKey("lock_" + taskKey));
            if (!lockData.has_failure() && lockData.has_value())
            {
                // Check task expiration
                SGProcessing::TaskLock lock;
                if (lock.ParseFromArray(lockData.value().data(), lockData.value().size()))
                {
                    auto expirationTime =
                        std::chrono::system_clock::time_point(
                            std::chrono::system_clock::duration(lock.lock_timestamp())) + m_processingTimeout;
                    if (timestamp > expirationTime)
                    {
                        auto taskData = m_db->Get(taskKey);

                        if (!taskData.has_failure())
                        {
                            if (task.ParseFromArray(taskData.value().data(), taskData.value().size()))
                            {
                                if (LockTask(taskKey))
                                {
                                    return true;
                                }
                            }
                        }
                        else
                        {
                            m_logger->debug("Unable to find a task {}", taskKey);
                        }
                    }
                }
            }
            return false;
        }

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::chrono::system_clock::duration m_processingTimeout;
        sgns::base::Logger m_logger = sgns::base::createLogger("ProcessingTaskQueueImpl");
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
        size_t nChunks = 1;
        std::vector<size_t> chunkResultHashes;
        bool addValidationSubtask = false;
        std::vector<size_t> validationChunkHashes;
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
                ("serviceindex,i", po::value(&o.serviceIndex), "index of the service in computational grid (has to be a unique value)")
                ("addvalidationsubtask,v", po::value(&o.addValidationSubtask),
                    "add a subtask that contains a randon (actually first) chunk of each of processing subtasks")
                ("nchunks,c", po::value(&o.nChunks), "number of chunks in each processing subtask")
                ("chunkresulthashes,h", po::value<std::vector<size_t>>()->multitoken(), "chunk result hashes")
                ("validationchunkhashes", po::value<std::vector<size_t>>()->multitoken(), "validation chunk result hashes");

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

            if (!vm["chunkresulthashes"].empty()) 
            {
                o.chunkResultHashes = vm["chunkresulthashes"].as<std::vector<size_t>>();
                if (o.chunkResultHashes.size() != o.nChunks)
                {
                    std::cerr << 
                        (boost::format("Number of chunks (%d) doesn't match the number of result hashes (%d)") 
                        % o.nChunks
                        % o.chunkResultHashes.size()).str() << std::endl;

                    for (auto v : o.chunkResultHashes)
                    {
                        std::cerr << v << ";";
                    }

                    std::cerr << std::endl;

                    return boost::none;
                }
            }

            if (!vm["validationchunkhashes"].empty())
            {
                o.validationChunkHashes = vm["validationchunkhashes"].as<std::vector<size_t>>();
                if (o.validationChunkHashes.size() != o.nSubTasks)
                {
                    std::cerr <<
                        (boost::format("Number of subtasks (%d) doesn't match the number of validation result hashes (%d)")
                            % o.nSubTasks
                            % o.validationChunkHashes.size()).str() << std::endl;

                    for (auto v : o.validationChunkHashes)
                    {
                        std::cerr << v << ";";
                    }

                    std::cerr << std::endl;

                    return boost::none;
                }
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

    const std::string logger_config(R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: processing_dapp_processor
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

    auto loggerPubSub = libp2p::log::createLogger("GossipPubSub");
    //loggerPubSub->set_level(spdlog::level::trace);

    auto loggerProcessingEngine = sgns::base::createLogger("ProcessingEngine");
    loggerProcessingEngine->set_level(spdlog::level::trace);

    auto loggerProcessingService = sgns::base::createLogger("ProcessingService");
    loggerProcessingService->set_level(spdlog::level::trace);

    auto loggerProcessingTaskQueue = sgns::base::createLogger("ProcessingTaskQueueImpl");
    loggerProcessingTaskQueue->set_level(spdlog::level::debug);

    auto loggerProcessingSubTaskQueue = sgns::base::createLogger("ProcessingSubTaskQueue");
    loggerProcessingSubTaskQueue->set_level(spdlog::level::debug);
    
    auto loggerProcessingSharedQueue = sgns::base::createLogger("ProcessingSharedQueue");
    loggerProcessingSharedQueue->set_level(spdlog::level::debug);

    auto loggerGlobalDB = sgns::base::createLogger("GlobalDB");
    loggerGlobalDB->set_level(spdlog::level::debug);

    auto loggerDAGSyncer = sgns::base::createLogger("GraphsyncDAGSyncer");
    loggerDAGSyncer->set_level(spdlog::level::trace);

    auto loggerBroadcaster = sgns::base::createLogger("PubSubBroadcasterExt");
    loggerBroadcaster->set_level(spdlog::level::debug);
    
    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto pubsubKeyPath = (boost::format("CRDT.Datastore.TEST.%d/pubs_processor") % options->serviceIndex).str();
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage(pubsubKeyPath).GetKeyPair().value());

    std::vector<std::string> pubsubBootstrapPeers;
    if (options->remote)
    {
        pubsubBootstrapPeers = std::move(std::vector({ *options->remote }));
    }
    pubs->Start(40001, pubsubBootstrapPeers);

    const size_t maximalNodesCount = 1;

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

    auto io = std::make_shared<boost::asio::io_context>();
    auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
        io, 
        (boost::format("CRDT.Datastore.TEST.%d") %  options->serviceIndex).str(), 
        40010 + options->serviceIndex,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs, "CRDT.Datastore.TEST.Channel"));

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    auto initRes = globalDB->Init(crdtOptions);

    std::thread iothread([io]() { io->run(); });

    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>(globalDB);

    auto processingCore = std::make_shared<ProcessingCoreImpl>(
        globalDB,
        options->nSubTasks, 
        options->subTaskProcessingTime,
        options->nChunks,
        options->addValidationSubtask);
    processingCore->m_chunkResulHashes = options->chunkResultHashes;
    processingCore->m_validationChunkHashes = options->validationChunkHashes;
    
    ProcessingServiceImpl processingService(pubs, maximalNodesCount, options->roomSize, taskQueue, processingCore);

    processingService.Listen(processingGridChannel);
    processingService.SetChannelListRequestTimeout(boost::posix_time::milliseconds(options->channelListRequestTimeout));

    processingService.SendChannelListRequest();

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
