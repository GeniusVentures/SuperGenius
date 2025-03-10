#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <iostream>
#include <boost/functional/hash.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/deadline_timer.hpp>

#include <functional>
#include <thread>

using namespace sgns::processing;

namespace
{
    class SubTaskQueueAccessorMock : public SubTaskQueueAccessor
    {
    public:
        SubTaskQueueAccessorMock(boost::asio::io_context& context)
            : m_context(context)
            , m_timerToKeepContext(m_context)
        {
            m_timerToKeepContext.expires_from_now(boost::posix_time::seconds(5));
            m_timerToKeepContext.async_wait(
                std::bind(&SubTaskQueueAccessorMock::OnTimerEvent, this, std::placeholders::_1));
        }

        bool ConnectToSubTaskQueue(std::function<void()> onSubTaskQueueConnectedEventSink) override
        {
            m_onSubTaskQueueConnectedEventSink = onSubTaskQueueConnectedEventSink;
            return true;
        }

        bool AssignSubTasks(std::list<SGProcessing::SubTask>& subTasks) override
        {
            m_subTasks.swap(subTasks);
            if (m_onSubTaskQueueConnectedEventSink)
            {
                m_onSubTaskQueueConnectedEventSink();
            }
            return true;
        }

        void GrabSubTask(SubTaskGrabbedCallback onSubTaskGrabbedCallback) override
        {
            if (!m_subTasks.empty())
            {
                m_context.post([this, onSubTaskGrabbedCallback]() {
                    onSubTaskGrabbedCallback(m_subTasks.front());
                    m_subTasks.pop_front();
                });
            }
        }

        void CompleteSubTask(const std::string& subTaskId, const SGProcessing::SubTaskResult& subTaskResult) override
        {
            // Do nothing
        }
        bool CreateResultsChannel( const std::string &task_id ) override
        {
            return true;
        }

    private:
        std::list<SGProcessing::SubTask> m_subTasks;

        void OnTimerEvent(const boost::system::error_code& error)
        {
            m_timerToKeepContext.expires_from_now(boost::posix_time::seconds(5));
            m_timerToKeepContext.async_wait(
                std::bind(&SubTaskQueueAccessorMock::OnTimerEvent, this, std::placeholders::_1));
        }

        boost::asio::io_context& m_context;
        boost::asio::deadline_timer m_timerToKeepContext;

        std::function<void()> m_onSubTaskQueueConnectedEventSink;
    };

    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t processingMillisec)
            : m_processingMillisec(processingMillisec)
        {
        }
        bool SetProcessingTypeFromJson(std::string jsondata) override
        {
            return true; //TODO - This is wrong - Update this tests to the actual ProcessingCoreImpl on src/processing/impl
        }
        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>  GetCidForProc(std::string json_data, std::string base_json) override
        {
            return nullptr;
        }

        void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc,std::string url,std::shared_ptr<std::vector<char>> resultss) override
        {
            return;
        }

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) override 
        {
            SGProcessing::SubTaskResult result;
            if (m_processingMillisec > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_processingMillisec));
            }

            auto itResultHashes = m_chunkResultHashes.find(subTask.subtaskid());

            size_t subTaskResultHash = initialHashCode;
            for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
            {
                size_t chunkHash = 0;
                if (itResultHashes != m_chunkResultHashes.end())
                {
                    chunkHash = itResultHashes->second[chunkIdx];
                }
                else
                {
                    const auto& chunk = subTask.chunkstoprocess(chunkIdx);
                    // Chunk result hash should be calculated
                    // Chunk data hash is calculated just as a stub
                    chunkHash = std::hash<std::string>{}(chunk.SerializeAsString());
                }

                std::string chunkHashString(reinterpret_cast<const char*>(&chunkHash), sizeof(chunkHash));
                result.add_chunk_hashes(chunkHashString);
                boost::hash_combine(subTaskResultHash, chunkHash);
            }

            std::string hashString(reinterpret_cast<const char*>(&subTaskResultHash), sizeof(subTaskResultHash));
            result.set_result_hash(hashString);

            m_processedSubTasks.push_back(subTask);
            m_initialHashes.push_back(initialHashCode);
            return result;
        };

        std::vector<SGProcessing::SubTask> m_processedSubTasks;
        std::vector<uint32_t> m_initialHashes;

        std::map<std::string, std::vector<size_t>> m_chunkResultHashes;
    private:
        size_t m_processingMillisec;
    };
}

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_engine_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingEngineTest : public ::testing::Test
{
public:
    virtual void SetUp() override
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
        libp2p::log::setLevelOfGroup("processing_engine_test", soralog::Level::DEBUG);
    }
};
/**
 * @given A queue containing subtasks
 * @when Processing is started
 * @then ProcessingCore::ProcessSubTask is called for each subtask.
 */
TEST_F(ProcessingEngineTest, DISABLED_SubTaskProcessing)
{
    boost::asio::io_context context;

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    auto engine = std::make_shared<ProcessingEngine>(nodeId, processingCore);

    std::list<SGProcessing::SubTask> subTasks;
    auto subTaskQueueAccessor = std::make_shared<SubTaskQueueAccessorMock>(context);
    {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid("SUBTASK_ID1");
        subTasks.push_back(std::move(subTask));
    }
    {
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid("SUBTASK_ID2");
        subTasks.push_back(std::move(subTask));
    }
    subTaskQueueAccessor->AssignSubTasks(subTasks);

    std::thread contextThread([&context]() { context.run(); });
    engine->StartQueueProcessing(subTaskQueueAccessor);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    context.stop();
    contextThread.join();


    ASSERT_EQ(2, processingCore->m_processedSubTasks.size());
    EXPECT_EQ("SUBTASK_ID1", processingCore->m_processedSubTasks[0].subtaskid());
    EXPECT_EQ("SUBTASK_ID2", processingCore->m_processedSubTasks[1].subtaskid());
}

/**
 * @given A queue containing 2 subtasks
 * @when 2 engines sequentually start the queue processing
 * @then Each of them processes only 1 subtask from the queue.
 */
TEST_F(ProcessingEngineTest, DISABLED_SharedSubTaskProcessing)
{
    boost::asio::io_context context;

    auto processingCore = std::make_shared<ProcessingCoreImpl>(500);

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    auto engine1 = std::make_shared<ProcessingEngine>(nodeId1, processingCore);
    auto engine2 = std::make_shared<ProcessingEngine>(nodeId2, processingCore);

    auto subTaskQueueAccessor1 = std::make_shared<SubTaskQueueAccessorMock>(context);
    {
        std::list<SGProcessing::SubTask> subTasks;
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid("SUBTASK_ID1");
        subTasks.push_back(std::move(subTask));
        subTaskQueueAccessor1->AssignSubTasks(subTasks);
    }

    auto subTaskQueueAccessor2 = std::make_shared<SubTaskQueueAccessorMock>(context);
    {
        std::list<SGProcessing::SubTask> subTasks;
        SGProcessing::SubTask subTask;
        subTask.set_subtaskid("SUBTASK_ID2");
        subTasks.push_back(std::move(subTask));
        subTaskQueueAccessor2->AssignSubTasks(subTasks);
    }

    std::thread contextThread([&context]() { context.run(); });

    engine1->StartQueueProcessing(subTaskQueueAccessor1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine2->StartQueueProcessing(subTaskQueueAccessor2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    context.stop();
    contextThread.join();

    ASSERT_EQ(2, processingCore->m_initialHashes.size());
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId1)), processingCore->m_initialHashes[0]);
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId2)), processingCore->m_initialHashes[1]);
}
