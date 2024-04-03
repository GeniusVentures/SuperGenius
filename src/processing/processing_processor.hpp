/**
* Header file for base class for processors. Derived classes will handle processing various
* types of AI/ML processing as needed. Give this to a ProcessingCoreImpl.
* @author Justin Church
*/
#ifndef PROCESSING_PROCESSOR_HPP
#define PROCESSING_PROCESSOR_HPP
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <processing/proto/SGProcessing.pb.h>

namespace sgns::processing
{
    class ProcessingProcessor
    {
    public:
        virtual ~ProcessingProcessor()
        {
        }

        /** Process data
        */
        virtual std::vector<uint8_t> StartProcessing(SGProcessing::SubTaskResult& result, const SGProcessing::Task& task, const SGProcessing::SubTask& subTask) = 0;


        virtual void SetData(std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers) = 0;
    };
}

#endif 