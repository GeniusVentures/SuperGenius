#include "processing_subtask_enqueuer_impl.hpp"

namespace sgns::processing
{
    SubTaskEnqueuerImpl::SubTaskEnqueuerImpl( std::shared_ptr<ProcessingTaskQueue> taskQueue ) :
        m_taskQueue( taskQueue )
    {
    }

    bool SubTaskEnqueuerImpl::EnqueueSubTasks( std::string &subTaskQueueId, std::list<SGProcessing::SubTask> &subTasks )
    {
        SGProcessing::Task task;
        std::string        taskKey;
        if ( m_taskQueue->GrabTask( taskKey, task ) )
        {
            subTaskQueueId = taskKey;

            m_taskQueue->GetSubTasks( taskKey, subTasks );

            m_logger->debug( "ENQUEUE_SUBTASKS: {}", subTasks.size() );
            return true;
        }
        return false;
    }

}
