/**
* Header file for the distributed subtasks queue
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "base/logger.hpp"

namespace sgns::processing
{
/** Distributed queue implementation
*/
class ProcessingSubTaskQueue
{
public:

    using TimestampProvider = std::function<uint64_t()>;

    /** Construct an empty queue
    * @param localNodeId local processing node ID
    * @param timestampProvider get the current timestamp from the manager function
    */
    ProcessingSubTaskQueue(std::string localNodeId, TimestampProvider timestampProvider = nullptr);

    /** Create a subtask queue by splitting the task to subtasks using the processing code
    * @param task - task that should be split into subtasks
    * @param enabledItemIndices - indexes of enabled items. Disabled items are considered as deleted.
    */
    void CreateQueue(SGProcessing::ProcessingQueue* queue, const std::vector<int>& enabledItemIndices);

    /** Asynchronous getting of a subtask from the queue
    * @param onSubTaskGrabbedCallback a callback that is called when a grapped iosubtask is locked by the local node
    * @param timestamp the current timestamp for the queue
    */
    bool GrabItem(size_t& grabbedItemIndex, uint64_t timestamp);

    /** Transfer the queue ownership to another processing node
    * @param nodeId - processing node ID that the ownership should be transferred
    */
    bool MoveOwnershipTo(const std::string& nodeId);

    /** Rollbacks the queue ownership to the previous state
    * @return true if the ownership is successfully rolled back
    */
    bool RollbackOwnership();

    /** Checks id the local processing node owns the queue
    * @return true is the lolca node owns the queue
    */
    bool HasOwnership() const;

    /** Updates the local queue with a snapshot that have the most recent timestamp
    * @param queue - the queue snapshot
    * @param enabledItemIndices - indexes of enabled items. Disabled items are considered as deleted.
    */
    bool UpdateQueue(SGProcessing::ProcessingQueue* queue, const std::vector<int>& enabledItemIndices);

    /** Unlocks expired queue items
    * @param currentTime - the current queue time
    * @return true if at least one item was unlocked
    */
    bool UnlockExpiredItems(uint64_t currentTime);

    /** Returns the most recent item lock timestamp
    */
    [[nodiscard]] uint64_t GetLastLockTimestamp() const;

    /** Adds a new ownership request to the queue
    * @param nodeId - ID of the node requesting ownership
    * @param timestamp - timestamp when the request was created
    * @return true if the request was successfully added, false if it already exists
    */
    bool AddOwnershipRequest(const std::string& nodeId, uint64_t timestamp);

    /** Processes the next ownership request in the queue
    * @return true if an ownership request was processed, false if the queue is empty or the node doesn't have ownership
    */
    bool ProcessNextOwnershipRequest();

private:

    void ChangeOwnershipTo(const std::string& nodeId);

    bool LockItem(size_t& lockedItemIndex, uint64_t timestamp);

    void LogQueue() const;

    std::string m_localNodeId;
    SGProcessing::ProcessingQueue* m_queue;

    std::vector<int> m_enabledItemIndices;

    base::Logger m_logger = base::createLogger("ProcessingSubTaskQueue");

    TimestampProvider m_timestampProvider;
};
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_HPP
