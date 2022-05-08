/**
* Header file for processing data storage interface
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_STATE_STORAGE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_STATE_STORAGE_HPP

#include <processing/proto/SGProcessing.pb.h>
#include <optional>

namespace sgns::processing
{
/** Handles subtask states storage
*/
class SubTaskStateStorage
{
public:
    virtual ~SubTaskStateStorage() = default;

    /** Changes current subtask state
    * @param subTaskId - subtask id
    * @param state - new subtask state
    */
    virtual void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) = 0;

    /** Returns subtask state
    * @param subTaskId - subtask id
    * @return subtask state, std::nullopt if no state exists for a passed subtask id
    */
    virtual std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) = 0;
};
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_STORAGE_HPP
