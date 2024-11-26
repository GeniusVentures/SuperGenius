/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/
#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
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
    virtual outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) = 0;

    /** Get processing type from json data to set processor
    * @param jsondata - jsondata that needs to be parsed
    */
    virtual bool SetProcessingTypeFromJson(std::string jsondata) = 0;

    /** Get settings.json and then get data we need for processing based on parsing
    * @param CID - CID of directory to get settings.json from
    */
    virtual std::shared_ptr<std::pair<std::vector<char>, std::vector<char>>> GetCidForProc(std::string json_data, std::string base_json) = 0;
    /** Get files from a set URL and insert them into pair reference
    * @param ioc - IO context to run on
    * @param url - ipfs gateway url to get from
    * @param results - reference to data pair to insert into.
    */
    virtual void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc, std::string url, std::vector<char>& results) = 0;
};

} // namespace sgns::processing

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_HPP
