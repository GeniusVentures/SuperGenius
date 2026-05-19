/**
 * @file       TaskKeys.hpp
 * @brief      
 * @date       2026-05-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once
#include <string>
#include <string_view>

#include "base/sgns_version.hpp"

namespace sgns::processing
{
class TaskKeys
{
public:
    static std::string ProcessingPrefix()
    {
        return std::string( PROCESSING_PREFIX_BASE ) + std::to_string( sgns::version::ProcessingVersion() );
    }

    static std::string TaskListKey()
    {
        return ProcessingPrefix() + std::string( TASK_LIST_SUFFIX );
    }

    static std::string SubTaskListKey()
    {
        return ProcessingPrefix() + std::string( SUBTASK_LIST_SUFFIX );
    }

    static std::string TaskKey( std::string_view taskId )
    {
        return TaskListKey() + std::string( TASK_KEY_PREFIX ) + std::string( taskId );
    }

    static std::string SubTaskKey( std::string_view taskId, std::string_view subTaskId )
    {
        return SubTaskListKey() + std::string( TASK_KEY_PREFIX ) + std::string( taskId ) + "/" + std::string( subTaskId );
    }

    static std::string ResultTaskKey( std::string_view taskId )
    {
        return ProcessingPrefix() + std::string( RESULTS_SUFFIX ) + std::string( TASK_LIST_SUFFIX ) +
               std::string( TASK_KEY_PREFIX ) + std::string( taskId );
    }

    static std::string LockKey( std::string_view taskKey )
    {
        return std::string( LOCK_KEY_PREFIX ) + std::string( taskKey );
    }

private:
    static constexpr std::string_view PROCESSING_PREFIX_BASE = "/processing_";
    static constexpr std::string_view TASK_LIST_SUFFIX       = "/tasks";
    static constexpr std::string_view SUBTASK_LIST_SUFFIX    = "/subtasks";
    static constexpr std::string_view RESULTS_SUFFIX         = "/task_results";
    static constexpr std::string_view TASK_KEY_PREFIX        = "/TASK_";
    static constexpr std::string_view LOCK_KEY_PREFIX        = "/lock_";
};
}
