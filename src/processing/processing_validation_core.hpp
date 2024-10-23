/**
* Header file for the task results validator implementation
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP
#define SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP

#include "processing/processing_subtask_queue.hpp"
#include "processing/processing_subtask_queue_channel.hpp"

#include "processing/proto/SGProcessing.pb.h"

#include <boost/asio.hpp>
#include <boost/optional.hpp>

namespace sgns::processing
{
/** Task results validator implementation
*/
class ProcessingValidationCore
{
public:
    ProcessingValidationCore();

    /** Checks if check result hashes are valid.
    * If invalid chunk hashes found corresponding subtasks are invalidated and returned to processing queue
    * @return true if all chunk results are valid
    */
    bool ValidateResults(
        const SGProcessing::SubTaskCollection& subTasks,
        const std::map<std::string, SGProcessing::SubTaskResult>& results,
        std::set<std::string>& invalidSubTaskIds);

private:
    bool CheckSubTaskResultHashes(
        const SGProcessing::SubTask& subTask,
        const std::map<std::string, std::vector<uint8_t>>& chunks) const;

    base::Logger m_logger = base::createLogger("ProcessingValidationCore");
};
}

#endif // SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP
