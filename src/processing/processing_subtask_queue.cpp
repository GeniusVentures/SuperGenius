#include "processing_subtask_queue.hpp"

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingSubTaskQueue::ProcessingSubTaskQueue(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> queueChannel,
    std::shared_ptr<boost::asio::io_context> context,
    const std::string& localNodeId,
    std::shared_ptr<ProcessingCore> processingCore)
    : m_queueChannel(queueChannel)
    , m_context(std::move(context))
    , m_localNodeId(localNodeId)
    , m_processingCore(processingCore)
    , m_dltQueueResponseTimeout(*m_context.get())
    , m_queueResponseTimeout(boost::posix_time::seconds(5))
    , m_dltGrabSubTaskTimeout(*m_context.get())
    , m_sharedQueue(localNodeId)
    , m_processingTimeout(std::chrono::seconds(10))
{
}

void ProcessingSubTaskQueue::CreateQueue(const SGProcessing::Task& task)
{
    using SubTaskList = ProcessingCore::SubTaskList;

    auto timestamp = std::chrono::system_clock::now();

    SubTaskList subTasks;
    m_processingCore->SplitTask(task, subTasks);

    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_queue = std::make_shared<SGProcessing::SubTaskQueue>();
    auto queuSubTasks = m_queue->mutable_subtasks();
    auto processingQueue = m_queue->mutable_processing_queue();
    for (auto itSubTask = subTasks.begin(); itSubTask != subTasks.end(); ++itSubTask)
    {
        queuSubTasks->AddAllocated(itSubTask->release());
        processingQueue->add_items();
    }

    m_sharedQueue.CreateQueue(processingQueue);
    PublishSubTaskQueue();
}

bool ProcessingSubTaskQueue::UpdateQueue(SGProcessing::SubTaskQueue* pQueue)
{
    if (pQueue)
    {
        std::shared_ptr<SGProcessing::SubTaskQueue> queue(pQueue);
        if (m_sharedQueue.UpdateQueue(queue->mutable_processing_queue()))
        {
            m_queue.swap(queue);
            std::vector<int> validItemIndices;
            for (int subTaskIdx = 0; subTaskIdx < m_queue->subtasks_size(); ++subTaskIdx)
            {
                const auto& subTask = m_queue->subtasks(subTaskIdx);
                if (m_results.find(subTask.results_channel()) == m_results.end())
                {
                    validItemIndices.push_back(subTaskIdx);
                }
            }
            m_sharedQueue.SetValidItemIndices(std::move(validItemIndices));
            LogQueue();
            return true;
        }
    }
    return false;
}

void ProcessingSubTaskQueue::ProcessPendingSubTaskGrabbing()
{
    // The method has to be called in scoped lock of queue mutex
    m_dltGrabSubTaskTimeout.expires_at(boost::posix_time::pos_infin);
    while (!m_onSubTaskGrabbedCallbacks.empty())
    {
        size_t itemIdx;
        if (m_sharedQueue.GrabItem(itemIdx))
        {
            LogQueue();
            PublishSubTaskQueue();

            m_onSubTaskGrabbedCallbacks.front()({ m_queue->subtasks(itemIdx) });
            m_onSubTaskGrabbedCallbacks.pop_front();
        }
        else
        {
            // No available subtasks found
            auto unlocked = m_sharedQueue.UnlockExpiredItems(m_processingTimeout);
            if (!unlocked)
            {
                break;
            }
        }
    }

    if (!m_onSubTaskGrabbedCallbacks.empty())
    {
        if (m_results.size() <(size_t)m_queue->processing_queue().items_size())
        {
            // Wait for subtasks are processed
            auto timestamp = std::chrono::system_clock::now();
            auto lastLockTimestamp = m_sharedQueue.GetLastLockTimestamp();
            auto lastExpirationTime = lastLockTimestamp + m_processingTimeout;

            std::chrono::milliseconds grabSubTaskTimeout =
                (lastExpirationTime > timestamp)
                ? std::chrono::duration_cast<std::chrono::milliseconds>(lastExpirationTime - timestamp)
                : std::chrono::milliseconds(1);

            m_logger->debug("GRAB_TIMEOUT {}ms", grabSubTaskTimeout.count());

            m_dltGrabSubTaskTimeout.expires_from_now(
                boost::posix_time::milliseconds(grabSubTaskTimeout.count()));

            m_dltGrabSubTaskTimeout.async_wait(std::bind(
                &ProcessingSubTaskQueue::HandleGrabSubTaskTimeout, this, std::placeholders::_1));
        }
        else
        {
            while (!m_onSubTaskGrabbedCallbacks.empty())
            {
                // Let the requester know that there are no available subtasks
                m_onSubTaskGrabbedCallbacks.front()({});
                // Reset the callback
                m_onSubTaskGrabbedCallbacks.pop_front();
            }
        }
    }
}

void ProcessingSubTaskQueue::HandleGrabSubTaskTimeout(const boost::system::error_code& ec)
{
    if (ec != boost::asio::error::operation_aborted)
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        m_dltGrabSubTaskTimeout.expires_at(boost::posix_time::pos_infin);
        m_logger->debug("HANDLE_GRAB_TIMEOUT");
        if (!m_onSubTaskGrabbedCallbacks.empty()
            && (m_results.size() < (size_t)m_queue->processing_queue().items_size()))
        {
            GrabSubTasks();
        }
    }
}

void ProcessingSubTaskQueue::GrabSubTask(SubTaskGrabbedCallback onSubTaskGrabbedCallback)
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_onSubTaskGrabbedCallbacks.push_back(std::move(onSubTaskGrabbedCallback));
    GrabSubTasks();
}

void ProcessingSubTaskQueue::GrabSubTasks()
{
    if (m_sharedQueue.HasOwnership())
    {
        ProcessPendingSubTaskGrabbing();
    }
    else
    {
        // Send a request to grab a subtask queue
        SGProcessing::ProcessingChannelMessage message;
        auto request = message.mutable_subtask_queue_request();
        request->set_node_id(m_localNodeId);
        m_queueChannel->Publish(message.SerializeAsString());
    }
}

bool ProcessingSubTaskQueue::MoveOwnershipTo(const std::string& nodeId)
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    if (m_sharedQueue.MoveOwnershipTo(nodeId))
    {
        LogQueue();
        PublishSubTaskQueue();
        return true;
    }
    return false;
}

bool ProcessingSubTaskQueue::HasOwnership() const
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    return m_sharedQueue.HasOwnership();
}

void ProcessingSubTaskQueue::PublishSubTaskQueue() const
{
    SGProcessing::ProcessingChannelMessage message;
    message.set_allocated_subtask_queue(m_queue.get());
    m_queueChannel->Publish(message.SerializeAsString());
    message.release_subtask_queue();
    m_logger->debug("QUEUE_PUBLISHED");
}

bool ProcessingSubTaskQueue::ProcessSubTaskQueueMessage(SGProcessing::SubTaskQueue* queue)
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_dltQueueResponseTimeout.expires_at(boost::posix_time::pos_infin);

    bool queueChanged = UpdateQueue(queue);
    if (queueChanged && m_sharedQueue.HasOwnership())
    {
        ProcessPendingSubTaskGrabbing();
    }
    return queueChanged;
}

bool ProcessingSubTaskQueue::ProcessSubTaskQueueRequestMessage(
    const SGProcessing::SubTaskQueueRequest& request)
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    if (m_sharedQueue.MoveOwnershipTo(request.node_id()))
    {
        LogQueue();
        PublishSubTaskQueue();
        return true;
    }

    m_logger->debug("QUEUE_REQUEST_RECEIVED");
    m_dltQueueResponseTimeout.expires_from_now(m_queueResponseTimeout);
    m_dltQueueResponseTimeout.async_wait(std::bind(
        &ProcessingSubTaskQueue::HandleQueueRequestTimeout, this, std::placeholders::_1));

    return false;
}

void ProcessingSubTaskQueue::HandleQueueRequestTimeout(const boost::system::error_code& ec)
{
    if (ec != boost::asio::error::operation_aborted)
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        m_logger->debug("QUEUE_REQUEST_TIMEOUT");
        m_dltQueueResponseTimeout.expires_at(boost::posix_time::pos_infin);
        if (m_sharedQueue.RollbackOwnership())
        {
            LogQueue();
            PublishSubTaskQueue();

            if (m_sharedQueue.HasOwnership())
            {
                ProcessPendingSubTaskGrabbing();
            }
        }
    }
}

std::unique_ptr<SGProcessing::SubTaskQueue> ProcessingSubTaskQueue::GetQueueSnapshot() const
{
    auto queue = std::make_unique<SGProcessing::SubTaskQueue>();

    std::lock_guard<std::mutex> guard(m_queueMutex);
    if (m_queue)
    {
        queue->CopyFrom(*m_queue.get());
    }
    return std::move(queue);
}

void ProcessingSubTaskQueue::AddSubTaskResult(
    const std::string& resultChannel, const SGProcessing::SubTaskResult& subTaskResult)
{
    std::lock_guard<std::mutex> guard(m_queueMutex);
    SGProcessing::SubTaskResult result;
    result.CopyFrom(subTaskResult);
    m_results.insert(std::make_pair(resultChannel, std::move(result)));

    std::vector<int> validItemIndices;
    for (int subTaskIdx = 0; subTaskIdx < m_queue->subtasks_size(); ++subTaskIdx)
    {
        const auto& subTask = m_queue->subtasks(subTaskIdx);
        if (m_results.find(subTask.results_channel()) == m_results.end())
        {
            validItemIndices.push_back(subTaskIdx);
        }
    }
    m_sharedQueue.SetValidItemIndices(std::move(validItemIndices));
}

void ProcessingSubTaskQueue::LogQueue() const
{
    if (m_logger->level() <= spdlog::level::trace)
    {
        std::stringstream ss;
        ss << "{";
        ss << "\"owner_node_id\":\"" << m_queue->processing_queue().owner_node_id() << "\"";
        ss << "," << "\"last_update_timestamp\":" << m_queue->processing_queue().last_update_timestamp();
        ss << "," << "\"items\":[";
        for (int itemIdx = 0; itemIdx < m_queue->processing_queue().items_size(); ++itemIdx)
        {
            auto item = m_queue->processing_queue().items(itemIdx);
            ss << "{\"lock_node_id\":\"" << item.lock_node_id() << "\"";
            ss << ",\"lock_timestamp\":" << item.lock_timestamp() << "},";
        }
        ss << "]}";

        m_logger->trace(ss.str());
    }
}
}

////////////////////////////////////////////////////////////////////////////////
