/**
 * @file       processing_core_impl.hpp
 * @brief      Header file of the Processing Core implementation that uses the ProcessingManager to execute subtasks.
 * @date       2024-03-28
 * @author     Justin Church (jchurch@gnus.ai)
 *             Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

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
        enum class Error
        {
            MAX_NUMBER_SUBTASKS = 1,
            GLOBALDB_READ_ERROR,
            NO_BUFFER_FROM_JOB_DATA,
            TASK_DESERIALIZATION_ERROR,
            JOB_INCOMPATIBILITY_ERROR,
            INVALID_MODEL_ERROR
        };

        static std::shared_ptr<ProcessingCoreImpl> New( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                                        uint32_t maximalProcessingSubTaskCount,
                                                        TokenID  tokenId );

        ~ProcessingCoreImpl() = default;

        /** Process a single subtask.
        * @param subTask - Subtask that needs to be processed.
        * @param initialHashCode - Initial hash code used to calculate result hash.
        */
        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override;

        /** Get current processing progress.
        * @return Progress percentage (0.0 to 100.0).
        */
        float GetProgress() const override;

    private:
        explicit ProcessingCoreImpl( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                     uint32_t                             maximalProcessingSubTaskCount,
                                     TokenID                              tokenId );

        outcome::result<void> IncProcessingSubTaskCount();
        outcome::result<void> DecProcessingSubTaskCount();

        std::shared_ptr<ProcessingTaskQueue> task_queue_;
        TokenID                              token_ID_;
        uint32_t                             max_processing_subtask_count_;

        std::mutex subtask_count_mutex_;
        uint32_t   processing_subtask_count_{ 0 };

        mutable std::shared_ptr<sgprocessing::ProcessingManager> processing_manager_;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingCoreImpl::Error );
