#ifndef SUPERGENIUS_PROCESSING_TASK_QUEUE_IMPL_HPP
#define SUPERGENIUS_PROCESSING_TASK_QUEUE_IMPL_HPP

#include <processing/processing_task_queue.hpp>
#include <crdt/globaldb/globaldb.hpp>

namespace sgns::processing
{
    class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        ProcessingTaskQueueImpl(std::shared_ptr<sgns::crdt::GlobalDB> db);

        void EnqueueTask(
            const SGProcessing::Task& task,
            const std::list<SGProcessing::SubTask>& subTasks) override;

        bool GetSubTasks(
            const std::string& taskId,
            std::list<SGProcessing::SubTask>& subTasks) override;

        bool GrabTask(std::string& grabbedTaskKey, SGProcessing::Task& task) override;

        bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& taskResult) override;

    private:
        bool IsTaskLocked(const std::string& taskKey);
        bool LockTask(const std::string& taskKey);
        bool MoveExpiredTaskLock(const std::string& taskKey, SGProcessing::Task& task);

        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::chrono::system_clock::duration m_processingTimeout;
        sgns::base::Logger m_logger = sgns::base::createLogger("ProcessingTaskQueueImpl");
    };

}

#endif // SUPERGENIUS_PROCESSING_TASK_QUEUE_IMPL_HPP
