
#include "processing_mock.hpp"
#include "processing_service_test.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <thread>
#include <boost/chrono/duration.hpp>

#include "testutil/wait_condition.hpp"

#include "base/logger.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

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

class ProcessingSubTaskChannelPubSubTest : public ProcessingServiceTest
{
public:
    void SetUp() override
    {
        ProcessingServiceTest::SetUp();
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
    auto start1Future = pubs1->Start(40001, {});
    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( (
                                   [&start1Future]()
                                   {
                                       start1Future.get();
                                       return true;
                                   } ),
                               std::chrono::milliseconds( 2000 ),
                               "Pubsub nodes initialization failed",
                               &resultTime );

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");

    std::atomic<size_t> requestCount1{0};
    std::vector<std::string> requestedNodeIds1;
    std::mutex mutex1;
    queueChannel1->SetQueueRequestSink([&requestCount1, &requestedNodeIds1, &mutex1](const SGProcessing::SubTaskQueueRequest& request) {
            std::lock_guard<std::mutex> lock(mutex1);
            requestedNodeIds1.push_back(request.node_id());
            requestCount1++;
            return true;
        });

    std::atomic<size_t> requestCount2{0};
    std::vector<std::string> requestedNodeIds2;
    std::mutex mutex2;
    queueChannel2->SetQueueRequestSink([&requestCount2, &requestedNodeIds2, &mutex2](const SGProcessing::SubTaskQueueRequest& request) {
        std::lock_guard<std::mutex> lock(mutex2);
        requestedNodeIds2.push_back(request.node_id());
        requestCount2++;
        return true;
        });

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 1 Subscription established after ", wait_time.count(), " ms");
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 2 Subscription established after ", wait_time.count(), " ms");
    }

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership(nodeId1);
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION(
        ([&requestCount1, &requestCount2]() {
            return requestCount1 >= 1 && requestCount2 >= 1;
        }),
        std::chrono::milliseconds(2000),
        "First request not received by both channels",
        &waitTime1
    );

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership(nodeId2);
    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION(
        ([&requestCount1, &requestCount2]() {
            return requestCount1 >= 2 && requestCount2 >= 2;
        }),
        std::chrono::milliseconds(2000),
        "Second request not received by both channels",
        &waitTime2
    );

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
    auto start1Future = pubs1->Start( 40001, {} );

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    auto start2Future = pubs2->Start( 40002, { pubs1->GetLocalAddress() } );
    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( (
                                   [&start1Future, &start2Future]()
                                   {
                                       start1Future.get();
                                       start2Future.get();
                                       return true;
                                   } ),
                               std::chrono::milliseconds( 2000 ),
                               "Pubsub nodes initialization failed",
                               &resultTime );

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs2, "PROCESSING_CHANNEL_ID");

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // if there are multiple threads sending the QueueRequest, the counter should be wrapped in a mutex
    std::atomic<size_t> requestCount1{0};
    std::set<std::string> requestedNodeIds1;
    queueChannel1->SetQueueRequestSink([&requestCount1, &requestedNodeIds1](const SGProcessing::SubTaskQueueRequest& request) {
        requestedNodeIds1.insert(request.node_id());
        // Properly update the atomic
        requestCount1++;
        return true;
    });

    // if there are multiple threads sending the QueueRequest, the counter should be wrapped in a mutex
    std::atomic<size_t> requestCount2{0};
    std::set<std::string> requestedNodeIds2;
    queueChannel2->SetQueueRequestSink([&requestCount2, &requestedNodeIds2](const SGProcessing::SubTaskQueueRequest& request) {
        requestedNodeIds2.insert(request.node_id());
        requestCount2++;
        return true;
    });

        // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 1 Subscription established after ", wait_time.count(), " ms" );
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 2 Subscription established after ", wait_time.count(), " ms" );
    }

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership(nodeId1);
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION(
        ([&requestCount1, &requestCount2]() {
            return requestCount1 >= 1 && requestCount2 >= 1;
        }),
        std::chrono::milliseconds(2000),
        "First request not received by both channels",
        &waitTime1
    );

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership(nodeId2);

    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION(
        ([&requestCount1, &requestCount2]() {
            return requestCount1 >= 2 && requestCount2 >= 2;
        }),
        std::chrono::milliseconds(2000),
        "Second request not received by both channels",
        &waitTime2
    );

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
    auto start1Future = pubs1->Start(40001, {});
    std::chrono::milliseconds resultTime;
    ASSERT_WAIT_FOR_CONDITION( (
                                   [&start1Future]()
                                   {
                                       start1Future.get();
                                       return true;
                                   } ),
                               std::chrono::milliseconds( 2000 ),
                               "Pubsub nodes initialization failed",
                               &resultTime );

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>(pubs1, "PROCESSING_CHANNEL_ID");

    std::atomic<size_t> queueCount1{0};
    std::mutex mutex1;
    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet1;
    queueChannel1->SetQueueUpdateSink([&queueSnapshotSet1, &queueCount1, &mutex1](SGProcessing::SubTaskQueue* queue) {
        std::lock_guard<std::mutex> lock(mutex1);
        auto queueCopy = std::make_shared<SGProcessing::SubTaskQueue>();
        queueCopy->CopyFrom(*queue);
        queueSnapshotSet1.push_back(queueCopy);
        queueCount1++;
        return true;
    });

    std::atomic<size_t> queueCount2{0};
    std::mutex mutex2;
    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet2;
    queueChannel2->SetQueueUpdateSink([&queueSnapshotSet2, &queueCount2, &mutex2](SGProcessing::SubTaskQueue* queue) {
        std::lock_guard<std::mutex> lock(mutex2);
        auto queueCopy = std::make_shared<SGProcessing::SubTaskQueue>();
        queueCopy->CopyFrom(*queue);
        queueSnapshotSet2.push_back(queueCopy);
        queueCount2++;
        return true;
    });

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 1 Subscription established after ", wait_time.count(), " ms");
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE(listen_result) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if (listen_result && std::holds_alternative<std::chrono::milliseconds>(listen_result.value())) {
        auto wait_time = std::get<std::chrono::milliseconds>(listen_result.value());
        Color::PrintInfo("Channel 2 Subscription established after ", wait_time.count(), " ms");
    }

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
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION(
        ([&queueCount1, &queueCount2]() {
            return queueCount1 >= 1 && queueCount2 >= 1;
        }),
        std::chrono::milliseconds(2000),
        "First queue not received by both channels",
        &waitTime1
    );

    std::string nodeId2 = "NODE2_ID";
    queue->mutable_processing_queue()->set_owner_node_id(nodeId2);
    queueChannel2->PublishQueue(queue);
    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION(
        ([&queueCount1, &queueCount2]() {
            return queueCount1 >= 2 && queueCount2 >= 2;
        }),
        std::chrono::milliseconds(2000),
        "Second queue not received by both channels",
        &waitTime2
    );

    pubs1->Stop();

    ASSERT_EQ(2, queueCount1.load());
    EXPECT_EQ(nodeId1, queueSnapshotSet1[0]->processing_queue().owner_node_id());
    EXPECT_EQ(nodeId2, queueSnapshotSet1[1]->processing_queue().owner_node_id());

    ASSERT_EQ(2, queueCount2.load());
    EXPECT_EQ(nodeId1, queueSnapshotSet2[0]->processing_queue().owner_node_id());
    EXPECT_EQ(nodeId2, queueSnapshotSet2[1]->processing_queue().owner_node_id());
}