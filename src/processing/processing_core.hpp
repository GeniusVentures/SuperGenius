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

    /** Process a single subtask.
    * @param subTask - Subtask that needs to be processed.
    * @param initialHashCode - Initial hash code used to calculate result hash.
    */
    virtual outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) = 0;

    /** Get current processing progress
    * @return Progress percentage (0.0 to 100.0)
    */
    virtual float GetProgress() const { return 0.0f; }
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
