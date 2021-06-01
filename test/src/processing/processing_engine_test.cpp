#include "processing/processing_engine.hpp"

#include <gtest/gtest.h>

#include <iostream>

using namespace sgns::processing;

namespace
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t processingMillisec)
            : m_processingMillisec(processingMillisec)
        {

        }

        void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) override
        {
            auto subtask = std::make_unique<SGProcessing::SubTask>();
            subTasks.push_back(std::move(subtask));
        }

        void  ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override 
        {
            if (m_processingMillisec > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_processingMillisec));
            }
            m_processedSubTasks.push_back(subTask);
            m_initialHashes.push_back(initialHashCode);
        };

        std::vector<SGProcessing::SubTask> m_processedSubTasks;
        std::vector<uint32_t> m_initialHashes;

    private:
        size_t m_processingMillisec;
    };
}
/**
 * @given A node is subscribed to result channed 
 * @when A result is published to the channel
 * @then The node receives the result
 */
TEST(ProcessingEngineTest, SubscribtionToResultChannel)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs2->Start(40001, {pubs1->GetLocalAddress()});

    sgns::ipfs_pubsub::GossipPubSubTopic resultChannel(pubs1, "RESULT_CHANNEL_ID");
    resultChannel.Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
    {     
    });

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    ProcessingEngine engine(pubs1, nodeId, processingCore);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id("DIFFERENT_NODE_ID");

    auto item = queue->mutable_processing_queue()->add_items();
    auto subTask = queue->add_subtasks();
    subTask->set_results_channel("RESULT_CHANNEL_ID");

    auto processingQueue = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId, processingCore);
    // The local queue wrapper doesn't own the queue
    processingQueue->ProcessSubTaskQueueMessage(queue.release());

    engine.StartQueueProcessing(processingQueue);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    SGProcessing::SubTaskResult result;    
    resultChannel.Publish(result.SerializeAsString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();
    pubs2->Stop();

    ASSERT_EQ(1, engine.GetResults().size());
    EXPECT_EQ("RESULT_CHANNEL_ID", std::get<0>(engine.GetResults()[0]));
}

/**
 * @given A queue containing subtasks
 * @when Processing is started
 * @then ProcessingCore::ProcessSubTask is called for each subtask.
 */
TEST(ProcessingEngineTest, SubTaskProcessing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(0);

    auto nodeId = "NODE_1";
    ProcessingEngine engine(pubs1, nodeId, processingCore);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
    }

    auto processingQueue = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId, processingCore);
    processingQueue->ProcessSubTaskQueueMessage(queue.release());

    engine.StartQueueProcessing(processingQueue);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    pubs1->Stop();

    ASSERT_EQ(2, processingCore->m_processedSubTasks.size());
    EXPECT_EQ("RESULT_CHANNEL_ID1", processingCore->m_processedSubTasks[0].results_channel());
    EXPECT_EQ("RESULT_CHANNEL_ID2", processingCore->m_processedSubTasks[1].results_channel());
}

/**
 * @given A queue containing 2 subtasks
 * @when 2 engined sequentually start the queue processing
 * @then Each of them processes only 1 subtask from the queue.
 */
TEST(ProcessingEngineTest, SharedSubTaskProcessing)
{
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();;
    pubs1->Start(40001, {});

    auto queueChannel = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubs1, "QUEUE_CHANNEL_ID");
    queueChannel->Subscribe([](boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message) {});


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto processingCore = std::make_shared<ProcessingCoreImpl>(500);

    auto nodeId1 = "NODE_1";
    auto nodeId2 = "NODE_2";

    ProcessingEngine engine1(pubs1, nodeId1, processingCore);
    ProcessingEngine engine2(pubs1, nodeId2, processingCore);

    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();
    // Local queue wrapped owns the queue
    queue->mutable_processing_queue()->set_owner_node_id(nodeId1);
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID1");
    }
    {
        auto item = queue->mutable_processing_queue()->add_items();
        auto subTask = queue->add_subtasks();
        subTask->set_results_channel("RESULT_CHANNEL_ID2");
    }

    auto processingQueue1 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId1, processingCore);
    processingQueue1->ProcessSubTaskQueueMessage(queue.release());

    engine1.StartQueueProcessing(processingQueue1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto processingQueue2 = std::make_shared<ProcessingSubTaskQueue>(
        queueChannel, pubs1->GetAsioContext(), nodeId2, processingCore);

    // Change queue owner
    SGProcessing::SubTaskQueueRequest queueRequest;
    queueRequest.set_node_id(nodeId2);
    auto updatedQueue = processingQueue1->ProcessSubTaskQueueRequestMessage(queueRequest);

    // Synchronize the queues
    processingQueue2->ProcessSubTaskQueueMessage(processingQueue1->GetQueueSnapshot().release());
    processingQueue1->ProcessSubTaskQueueMessage(processingQueue2->GetQueueSnapshot().release());

    engine2.StartQueueProcessing(processingQueue2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pubs1->Stop();

    ASSERT_EQ(2, processingCore->m_initialHashes.size());
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId1)), processingCore->m_initialHashes[0]);
    EXPECT_EQ(static_cast<uint32_t>(std::hash<std::string>{}(nodeId2)), processingCore->m_initialHashes[1]);
}





