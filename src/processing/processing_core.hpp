/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/
#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include <boost/asio/io_context.hpp>

namespace sgns::processing
{
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
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
