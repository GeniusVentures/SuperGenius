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

        /**
         * @brief       Stores a task with its subtasks
         * @param[in]   task The task to store
         * @param[in]   subTasks The subtasks to store
         * @return      Success if the task and subtasks were stored successfully, failure otherwise
         */
        outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                           const std::list<SGProcessing::SubTask> &subTasks )
        {
            return EnqueueTask( task, subTasks, nullptr );
        }

        /**
         * @brief       Stores a task with its subtasks within an atomic transaction
         * @param[in]   task The task to store
         * @param[in]   subTasks The subtasks to store
         * @param[in]   crdt_transaction The atomic transaction to use
         * @return      Success if the task and subtasks were stored successfully, failure otherwise
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

        /**
         * @brief       Retrieves the subtasks for a given task ID.
         * @param[in]   taskId The ID of the task for which to retrieve subtasks.
         * @param[in]   subTasks A reference to a list where the retrieved subtasks will be stored.
         * @return      true if the subtasks were retrieved successfully, false otherwise.
         */
        virtual bool GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks ) = 0;

        /**
         * @brief       Grabs task from the storage, returning its ID and data.
         * @return      A pair of task ID and task data if a task is found, failure otherwise.
         */
        virtual outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() = 0;
        
        /**
         * @brief       Completes a task with its result returning an atomic transaction to commit the completion.
         * @param[in]   taskId The ID of the task to complete
         * @param[in]   result The result of the completed task
         * @return      A CRDT atomic transaction if the task completion was successful, failure otherwise
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

        /**
         * @brief       Lists all known task IDs in the queue.
         * @return      Vector of task IDs, or empty vector if none found.
         */
        virtual std::vector<std::string> ListTaskKeys() = 0;

        /**
         * @brief       Retrieves the completed task result, if available.
         * @param[in]   taskId The ID of the task to retrieve the result for.
         * @return      The TaskResult if the task is completed, or failure otherwise.
         */
        virtual outcome::result<SGProcessing::TaskResult> GetTaskResult( const std::string &taskId ) = 0;
    };
}


#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
