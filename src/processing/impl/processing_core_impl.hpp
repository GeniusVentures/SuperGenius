/**
 * @file       processing_core_impl.hpp
 * @brief      Header file of the Processing Core implementation that uses the ProcessingManager to execute subtasks.
 * @date       2024-03-28
 * @author     Justin Church (jchurch@gnus.ai)
 *             Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef PROCESSING_CORE_IMPL_HPP
#define PROCESSING_CORE_IMPL_HPP

#include <cmath>
#include <memory>
#include <iostream>
#include <utility>
#include <cstdint>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/injector/kademlia_injector.hpp>

#include "processing/processing_core.hpp"
#include "processing/processing_task_queue.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "account/TokenID.hpp"

// Forward declaration
namespace sgns::sgprocessing
{
    class ProcessingManager;
}

namespace sgns::processing
{
    /**
     * @brief Default implementation of ProcessingCore backed by GlobalDB.
     */
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        /**
         * @brief      Error codes for ProcessingCoreImpl operations.
         */
        enum class Error
        {
            MAX_NUMBER_SUBTASKS = 1,    ///< Number of subtasks exceeds the configured maximum
            GLOBALDB_READ_ERROR,        ///< Database read error
            NO_BUFFER_FROM_JOB_DATA,    ///< No buffer available from job data
            TASK_DESERIALIZATION_ERROR, ///< Error occurred while deserializing the task
            JOB_INCOMPATIBILITY_ERROR,  ///< Job is incompatible with the processing core
            INVALID_MODEL_ERROR         ///< The model is invalid
        };

        /**
         * @brief       Factory method to create a new instance of ProcessingCoreImpl.
         * @param[in]   task_queue A shared pointer to the task queue used for retrieving tasks
         * @param[in]   maximalProcessingSubTaskCount The maximum number of subtasks that can be processed concurrently
         * @param[in]   tokenId The Token ID used on the results
         * @return      A shared pointer to the created ProcessingCoreImpl instance or nullptr if creation failed
         */
        static std::shared_ptr<ProcessingCoreImpl> New( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                                        uint32_t maximalProcessingSubTaskCount,
                                                        TokenID  tokenId );

        ~ProcessingCoreImpl() = default;

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override;

        float GetProgress() const override;

        std::shared_ptr<ProcessingTaskQueue> GetTaskQueue() const override;

    private:
        /**
         * @brief       Private constructor for ProcessingCoreImpl. Use the static New method to create instances.
         * @param[in]   task_queue A shared pointer to the task queue used for retrieving tasks
         * @param[in]   maximalProcessingSubTaskCount The maximum number of subtasks that can be processed concurrently
         * @param[in]   tokenId The Token ID used on the results
         */
        explicit ProcessingCoreImpl( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                     uint32_t                             maximalProcessingSubTaskCount,
                                     TokenID                              tokenId );

        /**
         * @brief       Increments the count of currently processing subtasks. Returns failure if the count exceeds the maximum allowed.
         * @return      Success if the count was incremented successfully, failure if the maximum number of processing subtasks has been exceeded
         */
        outcome::result<void> IncProcessingSubTaskCount();

        /**
         * @brief      Decrements the count of currently processing subtasks. Called whenever a processing finished.
         */
        void DecProcessingSubTaskCount();

        /// Shared pointer to the task queue for retrieving tasks and subtasks
        std::shared_ptr<ProcessingTaskQueue> task_queue_;
        /// The Token ID to be used on the results of the processed subtasks
        TokenID token_ID_;
        /// The maximum number of subtasks that can be processed concurrently.
        uint32_t max_processing_subtask_count_;

        /// Mutex to protect access to the processing subtask count
        std::mutex subtask_count_mutex_;
        /// The current count of subtasks being processed
        uint32_t processing_subtask_count_{ 0 };
        /// The last processing manager used for processing a subtask, kept here to allow progress retrieval during processing
        mutable std::shared_ptr<sgprocessing::ProcessingManager> processing_manager_;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingCoreImpl::Error );

#endif // PROCESSING_CORE_IMPL_HPP
