/**
* Header file for processing data storage interface
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SUTASK_RESULT_STORAGE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SUTASK_RESULT_STORAGE_HPP

#include <processing/proto/SGProcessing.pb.h>
#include <chrono>

namespace sgns::processing
{
/** Handles subtask results storage
*/
class SubTaskResultStorage
{
public:

    virtual ~SubTaskResultStorage() = default;

    /** Adds a result to the storage
    * @param subTaskResult - processing result
    */
    virtual void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) = 0;

    /** Removes result from the storage
    * @param subTaskId subtask id that the result was generated for
    */
    virtual void RemoveSubTaskResult(const std::string& subTaskId) = 0;

    /** Returns results for specified subtask ids
    * @param subTaskIds - list of subtask ids
    * @return results
    */
    virtual void GetSubTaskResults(
        const std::set<std::string>& subTaskIds, 
        std::vector<SGProcessing::SubTaskResult>& results) = 0;

};
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SUTASK_RESULT_STORAGE_HPP
