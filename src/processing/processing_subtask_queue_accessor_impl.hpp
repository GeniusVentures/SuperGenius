/**
* Header file for subtask queue accessor implementation
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_IMPL_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_IMPL_HPP

#include "processing/processing_subtask_queue_accessor.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_subtask_state_storage.hpp"
#include "processing/processing_subtask_result_storage.hpp"
#include "processing/processing_validation_core.hpp"

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <list>

namespace sgns::processing
{
/** Subtask storage implementation
*/
class SubTaskQueueAccessorImpl : public SubTaskQueueAccessor,
    public std::enable_shared_from_this<SubTaskQueueAccessorImpl>
{
public:
    /** Creates subtask queue accessor implementation object
    * @param gossipPubSub pubsub host which is used to create subscriptions to result channel
    * @param subTaskQueueManager - in-memory queue manager
    * @param subTaskStateStorage - storage of subtask states
    * @param subTaskResultStorage - processing results storage
    * @param taskResultProcessingSink - a callback which is called when a task processing is completed
    */
    SubTaskQueueAccessorImpl(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> gossipPubSub,
        std::shared_ptr<ProcessingSubTaskQueueManager> subTaskQueueManager,
        std::shared_ptr<SubTaskStateStorage> subTaskStateStorage,
        std::shared_ptr<SubTaskResultStorage> subTaskResultStorage,
        std::function<void(const SGProcessing::TaskResult&)> taskResultProcessingSink);
    ~SubTaskQueueAccessorImpl() override;

    /** SubTaskQueueAccessor overrides
    */
    void ConnectToSubTaskQueue(std::function<void()> onSubTaskQueueConnectedEventSink) override;
    void AssignSubTasks(std::list<SGProcessing::SubTask>& subTasks) override;
    void GrabSubTask(SubTaskGrabbedCallback onSubTaskGrabbedCallback) override;
    void CompleteSubTask(const std::string& subTaskId, const SGProcessing::SubTaskResult& subTaskResult) override;

    /** Returns available results of subtask queue
    * @return a vector of subtask id->results pairs
    */
    std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> GetResults() const;

private:
    void OnResultReceived(SGProcessing::SubTaskResult&& subTaskResult);
    void OnSubTaskQueueAssigned(
        const std::vector<std::string>& subTaskIds,
        std::function<void()> onSubTaskQueueConnectedEventSink);
    void UpdateResultsFromStorage(const std::set<std::string>& subTaskIds);
    bool FinalizeQueueProcessing(
        const SGProcessing::SubTaskCollection& subTasks,
        std::set<std::string>& invalidSubTaskIds);
    
    static void OnResultChannelMessage(
        std::weak_ptr<SubTaskQueueAccessorImpl> weakThis,
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message&> message);

    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> m_gossipPubSub;
    std::shared_ptr<ProcessingSubTaskQueueManager> m_subTaskQueueManager;
    std::shared_ptr<SubTaskStateStorage> m_subTaskStateStorage;
    std::shared_ptr<SubTaskResultStorage> m_subTaskResultStorage;
    std::function<void(const SGProcessing::TaskResult&)> m_taskResultProcessingSink;

    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_resultChannel;

    mutable std::mutex m_mutexResults;
    std::map<std::string, SGProcessing::SubTaskResult> m_results;
    ProcessingValidationCore m_validationCore;

    base::Logger m_logger = base::createLogger("ProcessingSubTaskQueueAccessorImpl");
};
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_IMPL_HPP
