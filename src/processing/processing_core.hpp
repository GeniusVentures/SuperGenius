/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/
#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP

#include <processing/proto/SGProcessing.pb.h>
#include <list>
#include <boost/asio/io_context.hpp>

namespace sgns::processing
{
/**
* Processing core interface.
* An implementatin of the interface depends on specific processing algorithm
*/
class ProcessingCore
{
public:
    virtual ~ProcessingCore() = default;

    /** Process a single subtask
    * @param subTask - subtask that needs to be processed
    * @param result - subtask result
    * @param initialHashCode - an initial hash code which is used to calculate result hash
    */
    virtual void ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode) = 0;

    /** Get processing type from json data to set processor
    * @param jsondata - jsondata that needs to be parsed
    */
    virtual bool SetProcessingTypeFromJson(std::string jsondata) = 0;

    virtual std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> GetCidForProc(const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result, std::string cid) = 0;
    virtual std::pair<std::vector<std::string>, std::vector<std::vector<char>>> GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc, std::string url) = 0;
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
