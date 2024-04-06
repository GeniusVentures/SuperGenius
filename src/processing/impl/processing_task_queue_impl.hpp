/**
* Header file for creating a processing task queue
* @author ????? Updated by:Justin Church
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_IMPL_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_IMPL_HPP

#include <processing/processing_task_queue.hpp>
#include <crdt/globaldb/globaldb.hpp>

#include <boost/format.hpp>

#include <optional>


namespace sgns::processing
{
    class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        ProcessingTaskQueueImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db)
            : m_db(db)
            , m_processingTimeout(std::chrono::seconds(10))
        {
        }

        void EnqueueTask(
            const SGProcessing::Task& task,
            const std::list<SGProcessing::SubTask>& subTasks) override;

        bool GetSubTasks(
            const std::string& taskId,
            std::list<SGProcessing::SubTask>& subTasks) override;

        bool GrabTask(std::string& grabbedTaskKey, SGProcessing::Task& task) override;

        bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& taskResult) override;

        bool IsTaskLocked(const std::string& taskKey);

        bool LockTask(const std::string& taskKey);

        bool MoveExpiredTaskLock(const std::string& taskKey, SGProcessing::Task& task);

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::chrono::system_clock::duration m_processingTimeout;
        sgns::base::Logger m_logger = sgns::base::createLogger("ProcessingTaskQueueImpl");
    };

}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
