/**
* Header file for the distributed subtasks queue
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SHARED_QUEUE_HPP
#define SUPERGENIUS_PROCESSING_SHARED_QUEUE_HPP

#include "SGProcessing.pb.h"
#include <libp2p/common/logger.hpp>

namespace sgns::processing
{
/** Distributed queue implementation
*/
class SharedQueue
{
public:
    /** Construct an empty queue
    * @param localNodeId local processing node ID
    */
    SharedQueue(const std::string& localNodeId);

    /** Create a subtask queue by splitting the task to subtasks using the processing code
    * @param task - task that should be split into subtasks
    */
    void CreateQueue(SGProcessing::ProcessingQueue* queue);

    /** Asynchronous getting of a subtask from the queue
    * @param onSubTaskGrabbedCallback a callback that is called when a grapped iosubtask is locked by the local node
    */
    bool GrabItem(size_t& grabbedItemIndex);

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
    */
    bool UpdateQueue(SGProcessing::ProcessingQueue* queue);
    
    /** Unlocks expired queue items
    * @param expirationTimeout - timeout applied to detect expired items
    * @return true if at least one item was unlocked
    */
    bool UnlockExpiredItems(std::chrono::system_clock::duration expirationTimeout);

    /** Returns the most recent item lock timestamp
    */
    std::chrono::system_clock::time_point GetLastLockTimestamp() const;

    /** Sets indices of valid queue items.
    * Invalid items are considered as deleted.
    */
    void SetValidItemIndices(std::vector<int>&& indices);

private:
    void ChangeOwnershipTo(const std::string& nodeId);

    bool LockItem(size_t& lockedItemIndex);

    void LogQueue() const;

    std::string m_localNodeId;
    SGProcessing::ProcessingQueue* m_queue;

    std::vector<int> m_validItemIndices;

    libp2p::common::Logger m_logger = libp2p::common::createLogger("ProcessingSharedQueue");
};
}

#endif // SUPERGENIUS_PROCESSING_SHARED_QUEUE_HPP