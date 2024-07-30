/**
* Header file for base class for processors. Derived classes will handle processing various
* types of AI/ML processing as needed. Give this to a ProcessingCoreImpl.
* @author Justin Church
*/
#ifndef PROCESSING_PROCESSOR_HPP
#define PROCESSING_PROCESSOR_HPP

#include <cmath>
#include <memory>
#include <vector>

#include "processing/proto/SGProcessing.pb.h"

namespace sgns::processing
{
    class ProcessingProcessor
    {
    public:
        virtual ~ProcessingProcessor()
        {
        }

        /** Start processing data
        * @param result - Reference to result item to set hashes to
        * @param task - Reference to task to get image split data
        * @param subTask - Reference to subtask to get chunk data from
        */
        virtual std::vector<uint8_t> StartProcessing(SGProcessing::SubTaskResult& result, const SGProcessing::Task& task, const SGProcessing::SubTask& subTask) = 0;

        /** Set data for processor
        * @param buffers - Data containing file name and data pair lists.
        */
        virtual void SetData(std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers) = 0;
    };
}

#endif 