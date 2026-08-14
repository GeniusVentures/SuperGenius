/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/
#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>

namespace sgns::processing
{
// Forward declaration -- only a shared_ptr to ProcessingTaskQueue is returned below,
// so a full include of processing_task_queue.hpp is not needed here.
class ProcessingTaskQueue;

/**
* @brief Processing core interface.
*
* Implementations encapsulate specific processing algorithms for subtasks.
*/
class ProcessingCore
{
public:
    virtual ~ProcessingCore() = default;

    /**
     * @brief       Processes a subtask and returns the result.
     * @param[in]   subTask The subtask to process
     * @param[in]   initialHashCode An initial hash code that can be used for processing
     * @return      The result of processing the subtask, or failure if processing failed
     */
    virtual outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) = 0;


    /**
     * @brief       Returns the progress of the processing core as a float between 0.0 and 100.0
     * @return      The percentage of the processing of the subtask
     */
    virtual float GetProgress() const { return 0.0f; }

    /**
     * @brief       Returns the ProcessingTaskQueue backing this ProcessingCore, if any.
     *
     * Lets a validation-side caller (Plan 15-03) recover the originating Task's full
     * schema (for tolerance-threshold derivation) without every ProcessingCore
     * implementation needing to support it. The safe nullptr default means
     * implementations that don't override it (e.g. any existing or future test mock)
     * simply signal "no task-queue lookup available", which callers must treat as a
     * normal, non-fatal case.
     * @return      The backing ProcessingTaskQueue, or nullptr if unavailable.
     */
    virtual std::shared_ptr<ProcessingTaskQueue> GetTaskQueue() const { return nullptr; }
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
