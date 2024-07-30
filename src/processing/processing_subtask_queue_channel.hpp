/**
* Header file for subtask queue channel interface
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_HPP

#include "processing/proto/SGProcessing.pb.h"

#include <string>
#include <memory>

namespace sgns::processing
{
/** Subtask queue channel interface which is used for in-memory queue synchronization
*/
class ProcessingSubTaskQueueChannel
{
public:
    virtual ~ProcessingSubTaskQueueChannel() = default;

    /** Sends a request for queue ownership
    * nodeId - requestor node id
    */
    virtual void RequestQueueOwnership(const std::string& nodeId) = 0;

    /** Publishes queue to all queue consumers
    * queue = subtask queue
    */
    virtual void PublishQueue(std::shared_ptr<SGProcessing::SubTaskQueue> queue) = 0;
};
}
#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_CHANNEL_HPP