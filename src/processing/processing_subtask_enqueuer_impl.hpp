#ifndef SUPERGENIUS_PROCESSING_SUBTASK_ENQUEUER_IMPL_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_ENQUEUER_IMPL_HPP

#include <list>
#include <string>

#include "base/logger.hpp"
#include "processing/processing_subtask_enqueuer.hpp"
#include "processing/processing_task_queue.hpp"

namespace sgns::processing
{
    // Encapsulates subtask queue construction algorithm
    class SubTaskEnqueuerImpl : public SubTaskEnqueuer
    {
    public:
        SubTaskEnqueuerImpl( std::shared_ptr<ProcessingTaskQueue> taskQueue );

        outcome::result<SGProcessing::Task>  EnqueueSubTasks( std::string &subTaskQueueId, std::list<SGProcessing::SubTask> &subTasks ) override;

    private:
        std::shared_ptr<ProcessingTaskQueue> m_taskQueue;
        sgns::base::Logger                   m_logger = sgns::base::createLogger( "SubTaskEnqueuerImpl" );
    };
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_ENQUEUER_IMPL_HPP
