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
        /** Create a task queue
        * @param db - CRDT globaldb to use
        */
        ProcessingTaskQueueImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db)
            : m_db(db)
            , m_processingTimeout(std::chrono::seconds(10))
        {
        }

        /** Enqueue a task and subtasks
        * @param task - Task to add
        * @param subTasks - List of subtasks
        */
        void EnqueueTask(
            const SGProcessing::Task& task,
            const std::list<SGProcessing::SubTask>& subTasks) override;

        /** Get subtasks by task id, returns true if we got subtasks
        * @param taskId - id to look for subtasks of
        * @param subTasks - Reference of subtask list
        */
        bool GetSubTasks(
            const std::string& taskId,
            std::list<SGProcessing::SubTask>& subTasks) override;

        /** Get task by task key, returns true if we got a task
        * @param grabbedTaskKey - id to look for task 
        * @param task - Reference of task
        */
        bool GrabTask(std::string& grabbedTaskKey, SGProcessing::Task& task) override;

        /** Complete task by task key, returns true if successful
        * @param taskKey - id to look for task
        * @param taskResult - Reference of a task result
        */
        bool CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& taskResult) override;

        /** Find if a task is locked
        * @param taskKey - id to look for task
        */
        bool IsTaskLocked(const std::string& taskKey);

        /** Lock a task by key
        * @param taskKey - id to look for task
        */
        bool LockTask(const std::string& taskKey);

        /** Move lock if expired, true if successful 
        * @param taskKey - id to look for task
        * @param task - task reference
        */
        bool MoveExpiredTaskLock(const std::string& taskKey, SGProcessing::Task& task);

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::chrono::system_clock::duration m_processingTimeout;
        sgns::base::Logger m_logger = sgns::base::createLogger("ProcessingTaskQueueImpl");
    };

}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP