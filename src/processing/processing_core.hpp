/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/
#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP

#include <processing/proto/SGProcessing.pb.h>
#include <list>

namespace sgns::processing
{
/**
* Processing core interface.
* An implementatin of the interface depends on specific processing algorithm
*/
class ProcessingCore
{
public:
    typedef std::list<std::unique_ptr<SGProcessing::SubTask>> SubTaskList;
    virtual ~ProcessingCore() = default;

    /** Splits a task to subtasks to allow distribute a processing over computation nodes
    * @param task - task that needs to be split
    * @param subTasks - a reference of list of subtasks that the original task was split to.
    */
    virtual void SplitTask(const SGProcessing::Task& task, SubTaskList& subTasks) = 0;

    /** Process a single subtask
    * @param subTask - subtask that needs to be processed
    * @param result - subtask result
    * @param initialHashCode - an initial hash code which is used to calculate result hash
    */
    virtual void ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode) = 0;
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
