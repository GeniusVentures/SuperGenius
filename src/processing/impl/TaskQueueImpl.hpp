/**
 * @file       TaskQueueImpl.hpp
 * @brief      Header file for the implementation of the task queue using CRDT
 * @date       2026-05-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef TASK_QUEUE_IMPL_HPP
#define TASK_QUEUE_IMPL_HPP

#include <unordered_set>
#include <string>
#include <utility>
#include <memory>

#include "processing/processing_task_queue.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/atomic_transaction.hpp"
#include "processing/impl/TaskKeys.hpp"
#include "outcome/outcome.hpp"

namespace sgns::processing
{
    /**
     * @brief      Implements the task storage on CRDT
     */
    class TaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        /**
         * @brief       Factory method to create a TaskQueueImpl instance
         * @param[in]   db The database instance to use for storing tasks and subtasks
         * @param[in]   processing_topic The topic for processing tasks
         * @return      Instance of the TaskQueueImpl initialized or nullptr if error occurs
         */
        static std::shared_ptr<TaskQueueImpl> New( std::shared_ptr<sgns::crdt::GlobalDB> db,
                                                   std::string                           processing_topic );

        ~TaskQueueImpl() override = default;

        outcome::result<void> EnqueueTask(
            const SGProcessing::Task                &task,
            const std::list<SGProcessing::SubTask>  &subTasks,
            std::shared_ptr<crdt::AtomicTransaction> crdt_transaction = nullptr ) override;

        outcome::result<SGProcessing::Task> GetTask( const std::string &taskId ) override;
        bool GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks ) override;

        outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override;

        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CompleteTask(
            const std::string              &taskKey,
            const SGProcessing::TaskResult &taskResult ) override;

        bool IsTaskCompleted( const std::string &taskId ) override;
        void MarkTaskBad( const std::string &taskKey ) override;

        std::vector<std::string> ListTaskKeys() override;
        outcome::result<SGProcessing::TaskResult> GetTaskResult( const std::string &taskId ) override;

    private:
        static constexpr auto LOCK_TIMEOUT = std::chrono::seconds( 10 );
        /**
         * @brief       Constructs a task queue implementation with the given database and processing topic.
         * @param[in]   db The database instance to use for storing tasks and subtasks
         * @param[in]   processing_topic The topic for processing tasks
         */
        explicit TaskQueueImpl( std::shared_ptr<sgns::crdt::GlobalDB> db, std::string processing_topic );

        /**
         * @brief       Checks if a task is currently locked
         * @param[in]   taskKey The key of the task to check
         * @return      true if the task is locked, false otherwise
         */
        bool IsTaskLocked( const std::string &taskKey );

        /**
         * @brief       Locks a task for processing by creating a lock entry in the database.
         * @param[in]   taskKey The key of the task to lock
         * @return      true if the task was successfully locked, false otherwise
         */
        bool LockTask( const std::string &taskKey );

        /**
         * @brief       Try to aquire lock for an expired task, if successful, loads the task data and returns true, otherwise returns false.
         * @param[in]   taskKey The key of the task for which to acquire lock
         * @param[in]   task The task object to load with data if lock is acquired
         * @return      true if lock is acquired, false otherwise
         */

        bool MoveExpiredTaskLock( const std::string &taskKey, SGProcessing::Task &task );
        /// The CRDT database instance used for storing tasks and subtasks
        std::shared_ptr<sgns::crdt::GlobalDB> db_;
        /// The topic used by CRDT to share tasks and subtasks across the network
        std::string processing_topic_;
        /// Jobs (tasks) that are incompatible with ProcessingManager
        std::unordered_set<std::string> incompatible_jobs_;
    };
}
#endif // TASK_QUEUE_IMPL_HPP
