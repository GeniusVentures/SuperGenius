#include "processing/processing_subtask_queue_channel_pubsub.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <thread>

using namespace sgns::processing;

const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_subtask_queue_channel_pubsub_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class ProcessingSubTaskChannelPubSubTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup("processing_subtask_queue_channel_pubsub_test", soralog::Level::DEBUG);
    }
};

/**
 * @given 2 channels connected to a single pubsub host
 * @when A queue ownership request sent
 * @then Both channels receive the request.
 */
TEST_F(ProcessingSubTaskChannelPubSubTest, RequestTransmittingOnSinglePubSubHost)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");

    std::vector<std::string> requestedNodeIds1;
    queueChannel1->SetQueueRequestSink([&requestedNodeIds1](const SGProcessing::SubTaskQueueRequest& request) {
            requestedNodeIds1.push_back(request.node_id());
            return true;
        });

    std::vector<std::string> requestedNodeIds2;
    queueChannel2->SetQueueRequestSink([&requestedNodeIds2](const SGProcessing::SubTaskQueueRequest& request) {
        requestedNodeIds2.push_back(request.node_id());
        return true;
        });

    queueChannel1->Listen(100);
    queueChannel2->Listen(100);

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership(nodeId1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership(nodeId2);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    pubs1->Stop();

    ASSERT_EQ(2, requestedNodeIds1.size());
    EXPECT_EQ(nodeId1, requestedNodeIds1[0]);
    EXPECT_EQ(nodeId2, requestedNodeIds1[1]);

    ASSERT_EQ(2, requestedNodeIds2.size());
    EXPECT_EQ(nodeId1, requestedNodeIds2[0]);
    EXPECT_EQ(nodeId2, requestedNodeIds2[1]);
}

/**
 * @given 2 channels connected to a different pubsub hosts
 * @when A queue ownership request sent
 * @then Both channels receive the request.
 */
TEST_F(ProcessingSubTaskChannelPubSubTest, RequestTransmittingOnDifferentPubSubHosts)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, { pubs1->GetLocalAddress() });

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs2, "PROCESSING_CHANNEL_ID");

    std::set<std::string> requestedNodeIds1;
    queueChannel1->SetQueueRequestSink([&requestedNodeIds1](const SGProcessing::SubTaskQueueRequest& request) {
        requestedNodeIds1.insert(request.node_id());
        return true;
        });

    std::set<std::string> requestedNodeIds2;
    queueChannel2->SetQueueRequestSink([&requestedNodeIds2](const SGProcessing::SubTaskQueueRequest& request) {
        requestedNodeIds2.insert(request.node_id());
        return true;
        });

    queueChannel1->Listen(100);
    queueChannel2->Listen(100);

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership(nodeId1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership(nodeId2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    pubs1->Stop();

    // Requests are received by both channel endpoints.
    ASSERT_EQ(2, requestedNodeIds1.size());
    ASSERT_EQ(2, requestedNodeIds2.size());
}

/**
 * @given 2 channels connected to a single pubsub host
 * @when A queue published to a channel
 * @then Both channels receive the queue.
 */
TEST_F(ProcessingSubTaskChannelPubSubTest, QueueTransmittingOnSinglePubSubHost)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");

    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet1;
    queueChannel1->SetQueueUpdateSink([&queueSnapshotSet1](SGProcessing::SubTaskQueue* queue) {
        std::shared_ptr<SGProcessing::SubTaskQueue> pQueue;
        pQueue.reset(queue);
        queueSnapshotSet1.push_back(pQueue);
        return true;
        });

    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet2;
    queueChannel2->SetQueueUpdateSink([&queueSnapshotSet2](SGProcessing::SubTaskQueue* queue) {
        std::shared_ptr<SGProcessing::SubTaskQueue> pQueue;
        pQueue.reset(queue);
        queueSnapshotSet2.push_back(pQueue);
        return true;
        });

    queueChannel1->Listen(100);
    queueChannel2->Listen(100);

    std::string nodeId1 = "NODE1_ID";
    auto queue = std::make_shared<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto subtask = queue->mutable_subtasks()->add_items();
        subtask->set_subtaskid("SUBTASK_1");
    }
    {
        auto subtask = queue->mutable_subtasks()->add_items();
        subtask->set_subtaskid("SUBTASK_2");
    }

    queueChannel1->PublishQueue(queue);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string nodeId2 = "NODE2_ID";
    queue->mutable_processing_queue()->set_owner_node_id(nodeId2);
    queueChannel2->PublishQueue(queue);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    pubs1->Stop();

    ASSERT_EQ(2, queueSnapshotSet1.size());
    EXPECT_EQ(nodeId1, queueSnapshotSet1[0]->processing_queue().owner_node_id());
    EXPECT_EQ(nodeId2, queueSnapshotSet1[1]->processing_queue().owner_node_id());

    ASSERT_EQ(2, queueSnapshotSet2.size());
    EXPECT_EQ(nodeId1, queueSnapshotSet2[0]->processing_queue().owner_node_id());
    EXPECT_EQ(nodeId2, queueSnapshotSet2[1]->processing_queue().owner_node_id());
}