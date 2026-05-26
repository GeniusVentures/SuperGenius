/**
* Header file for the distrubuted task queue
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include "crdt/atomic_transaction.hpp"
#include <list>

namespace sgns::processing
{
    class ProcessingTaskQueue
    {
        /**
         * @brief Distributed task queue interface.
         *
         * Provides enqueue, retrieval, and completion tracking for tasks and their
         * subtasks.
         */
    public:
        virtual ~ProcessingTaskQueue() = default;

        outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                           const std::list<SGProcessing::SubTask> &subTasks )
        {
            return EnqueueTask( task, subTasks, nullptr );
        }

        /** Enqueues a task with subtasks that the task has been split to
        * @param task - task to enqueue
        * @param subTasks - list of subtasks that the task has been split to
        */
        virtual outcome::result<void> EnqueueTask( const SGProcessing::Task                &task,
                                                   const std::list<SGProcessing::SubTask>  &subTasks,
                                                   std::shared_ptr<crdt::AtomicTransaction> crdt_transaction ) = 0;

        /**
        * @brief       Returns a task by task id, returns failure if task not found or invalid
        * @param[in]   taskId the ID of the task
        * @return      The task if found, failure otherwise
        */
        virtual outcome::result<SGProcessing::Task> GetTask( const std::string &taskId ) = 0;

        /** Returns a list of subtasks linked to taskId
        * @param taskId - task id
        * @param subTasks - list of found subtasks
        * @return false if task not found
        */
        virtual bool GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks ) = 0;

        /** Grabs a task from task queue
        * @return taskId - task id
        * @return task
        */
        virtual outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() = 0;

        /** Handles task completion
        * @param taskId - task id
        * @param result - task result
        */
        virtual outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CompleteTask(
            const std::string              &taskId,
            const SGProcessing::TaskResult &result ) = 0;

        /**
         * @brief Checks if the task is completed.
         * @param[in] taskId Task id.
         * @return true if the task is completed, false otherwise.
         */
        virtual bool IsTaskCompleted( const std::string &taskId ) = 0;

        /**
         * @brief Mark a task key as bad to be skipped.
         * @param[in] taskKey Task key to mark as bad.
         */
        virtual void MarkTaskBad( const std::string &taskKey ) = 0;
    };
}


#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
