/**
* Header file for subtask queue accessor interface
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_HPP

#include "processing/proto/SGProcessing.pb.h"
#include <boost/optional.hpp>
#include <list>

namespace sgns::processing
{
    /** Subtask queue accessor interface
*/
    class SubTaskQueueAccessor
    {
    public:
        using SubTaskGrabbedCallback = std::function<void( boost::optional<const SGProcessing::SubTask &> )>;

        virtual ~SubTaskQueueAccessor() = default;

        /** Starts a waiting for subtasks queue 
        * @param onSubTaskQueueConnectedEventSink hadnler which is called when subtask queue is connected
        */
        virtual bool ConnectToSubTaskQueue( std::function<void()> onSubTaskQueueConnectedEventSink ) = 0;

        /** Assigns a subtask list to processing queue
        * @param subTasks - a list of enqueued subtasks
        */
        virtual bool AssignSubTasks( std::list<SGProcessing::SubTask> &subTasks ) = 0;

        /** Asynchronous getting of a subtask from the queue
        * @param onSubTaskGrabbedCallback a callback that is called when a subtask is grabbed by the local node
        */
        virtual void GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback ) = 0;

        /** Finalizes subtask execution
        * @param subTaskId - id of processed subtask
        * @param subTaskResult - result of subtask processing
        */
        virtual void CompleteSubTask( const std::string                 &subTaskId,
                                      const SGProcessing::SubTaskResult &subTaskResult ) = 0;

        virtual bool CreateResultsChannel( const std::string &task_id ) = 0;

        // @todo Add SetErrorsHandler method
    };
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_HPP
