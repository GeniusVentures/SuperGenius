/**
 * @file       TaskQueueImpl.hpp
 * @brief      
 * @date       2026-05-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "processing/processing_task_queue.hpp"
#include "processing/impl/TaskKeys.hpp"

namespace sgns::processing
{
    class TaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        explicit TaskQueueImpl( std::shared_ptr<sgns::crdt::GlobalDB> db, std::string processing_topic ) :
            db_( std::move( db ) ), processing_topic_( std::move( processing_topic ) )
        {
        }

        ~TaskQueueImpl() override = default;

        outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                           const std::list<SGProcessing::SubTask> &subTasks ) override;

        bool GetSubTasks( const std::string &taskId, std::list<SGProcessing::SubTask> &subTasks ) override;

        outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override;

        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CompleteTask(
            const std::string              &taskKey,
            const SGProcessing::TaskResult &taskResult ) override;

        bool IsTaskCompleted( const std::string &taskId ) override;
        void MarkTaskBad( const std::string &taskKey ) override;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> db_;
        std::string                           processing_topic_;
    };
}
