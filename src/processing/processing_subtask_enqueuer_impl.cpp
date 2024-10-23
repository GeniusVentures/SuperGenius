#include "processing_subtask_enqueuer_impl.hpp"

#include <utility>

namespace sgns::processing
{
    SubTaskEnqueuerImpl::SubTaskEnqueuerImpl( std::shared_ptr<ProcessingTaskQueue> taskQueue ) :
        m_taskQueue( std::move( taskQueue ) )
    {
    }

    bool SubTaskEnqueuerImpl::EnqueueSubTasks( std::string &subTaskQueueId, std::list<SGProcessing::SubTask> &subTasks )
    {
        if ( auto maybe_grabbed = m_taskQueue->GrabTask() )
        {
            std::string        taskKey = maybe_grabbed.value().first;
            //SGProcessing::Task task    = maybe_grabbed.value().second; //TODO - Not used for anything. Rewrite this code

            subTaskQueueId = taskKey;

            m_taskQueue->GetSubTasks( taskKey, subTasks );

            m_logger->debug( "ENQUEUE_SUBTASKS: {}", subTasks.size() );
            return true;
        }
        return false;
    }

}
