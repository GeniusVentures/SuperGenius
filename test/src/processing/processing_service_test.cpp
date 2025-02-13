#include "processing/processing_service.hpp"
#include "processing/processing_subtask_enqueuer_impl.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <thread>

using namespace sgns::processing;

namespace
{
class SubTaskStateStorageMock: public SubTaskStateStorage
{
public:
    void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) override {}
    std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) override
    {
        return std::nullopt;
    }
};

class SubTaskResultStorageMock : public SubTaskResultStorage
{
public:
    void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) override {}
    void RemoveSubTaskResult(const std::string& subTaskId) override {}
    std::vector<SGProcessing::SubTaskResult> GetSubTaskResults(
        const std::set<std::string>& subTaskIds) override { return {};}
};

class ProcessingCoreImpl : public ProcessingCore
{
public:
    outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) override { return SGProcessing::SubTaskResult{};};
    bool SetProcessingTypeFromJson(std::string jsondata) override
    {
        return true; //TODO - This is wrong - Update this tests to the actual ProcessingCoreImpl on src/processing/impl
    }
    std::shared_ptr<std::pair<std::vector<char>, std::vector<char>>> GetCidForProc(std::string json_data, std::string base_json) override
    {
        return nullptr;
    }

    void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc,std::string url, std::vector<char>& results) override
    {
        return;
    }
};

class ProcessingTaskQueueImpl : public ProcessingTaskQueue
{
public:
    outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                       const std::list<SGProcessing::SubTask> &subTasks ) override
    {
        return outcome::success();
    }

    bool GetSubTasks(
        const std::string& taskId,
        std::list<SGProcessing::SubTask>& subTasks) override 
    {
        return false;
    }

    outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override
    {
        return outcome::failure(boost::system::error_code{});
    }
    
    bool IsTaskCompleted( const std::string &taskId ) override
    {
        return true;
    }
    
    bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& task) override
    {
        return false;
    }
};
}

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_service_test
    sink: console
    level: off
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingServiceTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_service_test", soralog::Level::OFF);
    }
};
/**
 * @given Empty queue list
 * @when A queue channel received
 * @then A processing node is created
 */
TEST_F(ProcessingServiceTest, ProcessingSlotsAreAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>(taskQueue);

    ProcessingServiceImpl processingService(
        pubs,
        1,
        enqueuer,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        processingCore);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel(pubs, "GRID_CHANNEL_ID");
    gridChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService.StartProcessing("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    SGProcessing::GridChannelMessage gridMessage;
    auto channelResponse = gridMessage.mutable_processing_channel_response();
    channelResponse->set_channel_id("PROCESSING_QUEUE_ID");
    gridChannel.Publish(gridMessage.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    pubs->Stop();

    EXPECT_EQ(processingService.GetProcessingNodesCount(), 1);
}

/**
 * @given Empty queue list
 * @when No queue channel received
 * @then No new processing node is created
 */
// The test disabled due to processing room handling removed
// No room capacity is checked
TEST_F(ProcessingServiceTest, DISABLED_NoProcessingSlotsAvailable)
{
    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs->Start(40001, {});

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer = std::make_shared<SubTaskEnqueuerImpl>(taskQueue);

    ProcessingServiceImpl processingService(
        pubs,
        1,
        enqueuer,
        std::make_shared<SubTaskStateStorageMock>(),
        std::make_shared<SubTaskResultStorageMock>(),
        processingCore);


    sgns::ipfs_pubsub::GossipPubSubTopic gridChannel(pubs, "GRID_CHANNEL_ID");
    gridChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    processingService.StartProcessing("GRID_CHANNEL_ID");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // No queue channel message sent

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    pubs->Stop();

    EXPECT_EQ(processingService.GetProcessingNodesCount(), 0);
}
