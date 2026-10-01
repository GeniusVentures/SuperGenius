#ifndef PROCESSING_SERVICE_TEST_HPP
#define PROCESSING_SERVICE_TEST_HPP

#include <gtest/gtest.h>

#include "processing_mock.hpp"
#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_service.hpp"

class ProcessingServiceTest : public ::testing::Test
{
public:
    void SetUp() override;
    void TearDown() override;
    virtual void Initialize(uint64_t numNodes, size_t processingTime);
    virtual void SetUp(std::string name, std::string loggerConfig);

    /**
     * public variables to share between test fixtures
     **/
    std::vector<std::shared_ptr<GossipPubSub>> m_pubsub_nodes;
    std::vector<std::future<std::error_code>> m_pubsub_futures;

    std::vector<std::shared_ptr<sgns::test::ProcessingCoreImpl>> m_processing_cores;
    std::vector<std::shared_ptr<ProcessingSubTaskQueueChannelPubSub>> m_processing_queues_channel_pub_subs;

    std::vector<std::shared_ptr<ProcessingSubTaskQueueManager>> m_processing_queues_managers;

    std::vector<std::shared_ptr<ProcessingEngine>> m_processing_engines;

    std::vector<std::shared_ptr<SubTaskQueueAccessorImpl>> m_processing_queues_accessors;

    // Shared (not unique) so completion callbacks can keep their flag alive:
    // SubTaskQueueAccessorImpl's broadcast handler holds a strong self reference
    // while it runs, so a late FinalizeQueueProcessing can invoke the sink after
    // TearDown cleared the fixture vectors (CI crash: null unique_ptr deref).
    std::vector<std::shared_ptr<std::atomic<bool>>> m_IsTaskFinalized;

    // Track ProcessingServiceImpl instances to properly stop them in TearDown
    std::vector<std::shared_ptr<ProcessingServiceImpl>> m_processing_services;

    std::shared_ptr<soralog::Logger> m_Logger;

};

#endif
