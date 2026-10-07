/**
 * @file       TaskKeys.hpp
 * @brief      
 * @date       2026-05-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef TASK_KEYS_HPP
#define TASK_KEYS_HPP

#include <string>
#include <string_view>

#include "base/sgns_version.hpp"
#include "crdt/hierarchical_key.hpp"

namespace sgns::processing
{
    class TaskKeys
    {
    public:
        /**
     * @brief       Returns the processing base prefix for the task queue
     * @return      "/<prefix_base
     */
        static std::string ProcessingPrefix()
        {
            return std::string( PROCESSING_PREFIX_BASE ) + std::to_string( sgns::version::ProcessingVersion() );
        }

        /**
     * @brief       Returns the CRDT branch prefix for a private-network job scope.
     * @param[in]   private_network_id Private-network identity (0x-hex-32B); empty = public scope.
     * @return      Empty string for the public scope, "/chain/<private_network_id>" when scoped.
     */
        static std::string ScopePrefix( const std::string &private_network_id )
        {
            if ( private_network_id.empty() )
            {
                return std::string();
            }
            return crdt::HierarchicalKey( "chain" ).ChildString( private_network_id ).GetKey();
        }

        /**
     * @brief       Derives a pubsub topic for a private-network job scope.
     * @param[in]   public_topic Public-network topic string.
     * @param[in]   private_network_id Private-network identity; empty = public scope.
     * @return      @p public_topic unchanged when public, "<public_topic>/<private_network_id>" when scoped.
     */
        static std::string ScopedTopic( std::string_view public_topic, const std::string &private_network_id )
        {
            if ( private_network_id.empty() )
            {
                return std::string( public_topic );
            }
            return std::string( public_topic ) + "/" + private_network_id;
        }

        /**
     * @brief       Prefixes a raw CRDT path with the private-network branch.
     * @param[in]   private_network_id Private-network identity; empty = public scope.
     * @param[in]   path Raw key path (may or may not start with '/').
     * @return      @p path unchanged when public; "/chain/<private_network_id>/<path>" with exactly
     *              one slash between segments when scoped.
     */
        static std::string ScopedKeyPath( const std::string &private_network_id, std::string_view path )
        {
            if ( private_network_id.empty() )
            {
                return std::string( path );
            }
            return crdt::HierarchicalKey( ScopePrefix( private_network_id ) ).ChildString( path ).GetKey();
        }

        /**
     * @brief       Builds the subtask-result key for a private-network job scope.
     * @param[in]   private_network_id Private-network identity; empty = public scope.
     * @param[in]   subTaskId Subtask identifier.
     * @return      "results/<subTaskId>" when public (byte-identical to the legacy
     *              boost::format output), "/chain/<private_network_id>/results/<subTaskId>" when scoped.
     */
        static std::string SubTaskResultKey( const std::string &private_network_id, std::string_view subTaskId )
        {
            return ScopedKeyPath( private_network_id, std::string( "results/" ) + std::string( subTaskId ) );
        }

        static std::string TaskListKey()
        {
            return ProcessingPrefix() + std::string( TASK_LIST_SUFFIX );
        }

        static std::string TaskListKey( const std::string &private_network_id )
        {
            return ScopePrefix( private_network_id ) + TaskListKey();
        }

        static std::string SubTaskListKey()
        {
            return ProcessingPrefix() + std::string( SUBTASK_LIST_SUFFIX );
        }

        // NOTE: no single-argument scoped overload of the bare subtask list exists on purpose —
        // it would collide with SubTaskListKey(std::string_view taskId): a std::string argument
        // would bind exactly to const std::string& and silently reinterpret a task id as a scope.
        // Compose the bare scoped list as ScopePrefix(scope) + SubTaskListKey() if ever needed.

        static std::string SubTaskListKey( std::string_view taskId )
        {
            return SubTaskListKey() + "/" + std::string( taskId );
        }

        static std::string SubTaskListKey( const std::string &private_network_id, std::string_view taskId )
        {
            return ScopePrefix( private_network_id ) + SubTaskListKey( taskId );
        }

        static std::string TaskKey( std::string_view taskId )
        {
            return TaskListKey() + "/" + std::string( taskId );
        }

        static std::string TaskKey( const std::string &private_network_id, std::string_view taskId )
        {
            return ScopePrefix( private_network_id ) + TaskKey( taskId );
        }

        static std::string SubTaskKey( std::string_view taskId, std::string_view subTaskId )
        {
            return SubTaskListKey( taskId ) + "/" + std::string( subTaskId );
        }

        static std::string SubTaskKey( const std::string &private_network_id,
                                       std::string_view          taskId,
                                       std::string_view          subTaskId )
        {
            return ScopePrefix( private_network_id ) + SubTaskKey( taskId, subTaskId );
        }

        static std::string ClaimableListKey()
        {
            return ProcessingPrefix() + std::string( CLAIMABLE_LIST_SUFFIX );
        }

        static std::string ClaimableListKey( const std::string &private_network_id )
        {
            return ScopePrefix( private_network_id ) + ClaimableListKey();
        }

        static std::string ClaimableTaskKey( std::string_view taskId )
        {
            return ClaimableListKey() + "/" + std::string( taskId );
        }

        static std::string ClaimableTaskKey( const std::string &private_network_id, std::string_view taskId )
        {
            return ScopePrefix( private_network_id ) + ClaimableTaskKey( taskId );
        }

        static std::string ResultTaskKey( std::string_view taskId )
        {
            return ProcessingPrefix() + std::string( RESULTS_SUFFIX ) + std::string( TASK_LIST_SUFFIX ) +
                   std::string( taskId );
        }

        static std::string ResultTaskKey( const std::string &private_network_id, std::string_view taskId )
        {
            return ScopePrefix( private_network_id ) + ResultTaskKey( taskId );
        }

        /**
     * @brief       Builds a task-lock key. Scope-agnostic by design: @p taskKey already carries
     *              the network scope when produced by the scoped TaskKey overload.
     */
        static std::string LockKey( std::string_view taskKey )
        {
            return std::string( LOCK_KEY_PREFIX ) + std::string( taskKey );
        }

    private:
        static constexpr std::string_view PROCESSING_PREFIX_BASE = "/processing_";
        static constexpr std::string_view TASK_LIST_SUFFIX       = "/tasks";
        static constexpr std::string_view SUBTASK_LIST_SUFFIX    = "/subtasks";
        static constexpr std::string_view CLAIMABLE_LIST_SUFFIX  = "/claimable";
        static constexpr std::string_view RESULTS_SUFFIX         = "/task_results";
        static constexpr std::string_view LOCK_KEY_PREFIX        = "/lock_";
    };
}

#endif // TASK_KEYS_HPP
