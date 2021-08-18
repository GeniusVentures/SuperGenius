#include "processing_engine.hpp"

namespace sgns::processing
{
ProcessingEngine::ProcessingEngine(
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
    std::string nodeId,
    std::shared_ptr<ProcessingCore> processingCore,
    std::function<void(const SGProcessing::TaskResult&)> taskResultProcessingSink)
    : m_gossipPubSub(gossipPubSub)
    , m_nodeId(std::move(nodeId))
    , m_processingCore(processingCore)
    , m_taskResultProcessingSink(taskResultProcessingSink)
{
}

void ProcessingEngine::StartQueueProcessing(
    std::shared_ptr<ProcessingSubTaskQueue> subTaskQueue)
{
    std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
    m_subTaskQueue = subTaskQueue;

    auto queue = m_subTaskQueue->GetQueueSnapshot();
    if (queue->subtasks_size() > 0)
    {
        for (size_t subTaskIdx = 0; subTaskIdx < (size_t)queue->subtasks_size(); ++subTaskIdx)
        {
            const auto& subTask = queue->subtasks(subTaskIdx);
            AddResultChannel(subTask.results_channel());
        }

        m_subTaskQueue->GrabSubTask(std::bind(&ProcessingEngine::OnSubTaskGrabbed, this, std::placeholders::_1));
    }
}

void ProcessingEngine::StopQueueProcessing()
{
    std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
    m_subTaskQueue.reset();
}

bool ProcessingEngine::IsQueueProcessingStarted() const
{
    std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
    return (m_subTaskQueue.get() != nullptr);
}

void ProcessingEngine::OnSubTaskGrabbed(boost::optional<const SGProcessing::SubTask&> subTask)
{
    if (subTask)
    {
        m_logger->debug("[GRABBED]. ({}).", subTask->results_channel());
        ProcessSubTask(*subTask);
    }
}

void ProcessingEngine::ProcessSubTask(SGProcessing::SubTask subTask)
{
    m_logger->debug("[PROCESSING_STARTED]. ({}).", subTask.results_channel());
    auto resultChannel = m_resultChannels[subTask.results_channel()];
    std::thread thread(
        [subTask(std::move(subTask)), resultChannel, this]()
    {
        SGProcessing::SubTaskResult result;
        // @todo set initial hash code that depends on node id
        m_processingCore->ProcessSubTask(subTask, result, std::hash<std::string>{}(m_nodeId));
        m_logger->debug("[PROCESSED]. ({}).", subTask.results_channel());
        resultChannel->Publish(result.SerializeAsString());
        m_logger->debug("[RESULT_SENT]. ({}).", subTask.results_channel());

        std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
        if (m_subTaskQueue)
        {
            // @todo Should a new subtask be grabbed once the perivious one is processed?
            m_subTaskQueue->GrabSubTask(std::bind(&ProcessingEngine::OnSubTaskGrabbed, this, std::placeholders::_1));
        }
    });
    thread.detach();
}

std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> ProcessingEngine::GetResults() const
{
    std::lock_guard<std::mutex> guard(m_mutexResults);
    std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> results;
    for (auto item : m_results)
    {
        results.push_back({item.first, item.second});
    }
    std::sort(results.begin(), results.end(),
        [](const std::tuple<std::string, SGProcessing::SubTaskResult>& v1,
            const std::tuple<std::string, SGProcessing::SubTaskResult>& v2) { return std::get<0>(v1) < std::get<0>(v2); });

    return results;
}

void ProcessingEngine::OnResultChannelMessage(
    boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message)
{
    if (message)
    {
        SGProcessing::SubTaskResult result;
        if (result.ParseFromArray(message->data.data(), static_cast<int>(message->data.size())))
        {
            m_logger->debug("[RESULT_RECEIVED]. ({}).", result.ipfs_results_data_id());

            std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
            if (m_subTaskQueue)
            {
                m_subTaskQueue->AddSubTaskResult(message->topics[0], result);

                // Task processing finished
                if (m_subTaskQueue->IsProcessed()) 
                {
                    bool valid = m_subTaskQueue->ValidateResults();
                    m_logger->debug("RESULTS_VALIDATED: {}", valid ? "VALID" : "INVALID");
                    if (valid)
                    {
                        if (m_subTaskQueue->HasOwnership())
                        {
                            SGProcessing::TaskResult taskResult;
                            auto results = taskResult.mutable_subtask_results();
                            for (const auto& r : m_results)
                            {
                                auto result = results->Add();
                                result->CopyFrom(r.second);
                            }
                            m_taskResultProcessingSink(taskResult);
                            // @todo Notify other nodes that the task is finalized
                        }
                        else
                        {
                            // @todo Process task finalization expiration
                        }
                    }
                    else
                    {
                        m_subTaskQueue->GrabSubTask(std::bind(&ProcessingEngine::OnSubTaskGrabbed, this, std::placeholders::_1));
                    }
                }
            }
            // Results accumulation
            {
                std::lock_guard<std::mutex> guard(m_mutexResults);
                m_results.insert({ message->topics[0], std::move(result) });
            }
        }
    }
}

std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> ProcessingEngine::AddResultChannel(
    const std::string& resultChannelId)
{
    using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

    auto itChannel = m_resultChannels.find(resultChannelId);
    if (itChannel != m_resultChannels.end())
    {
        return itChannel->second;
    }

    auto resultChannel = std::make_shared<GossipPubSubTopic>(m_gossipPubSub, resultChannelId);
    resultChannel->Subscribe(std::bind(&ProcessingEngine::OnResultChannelMessage, this, std::placeholders::_1));
    m_resultChannels[resultChannelId] = resultChannel;
    return resultChannel;
}
}
