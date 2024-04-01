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
        virtual std::vector<uint8_t> StartProcessing(size_t numchunks, 
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen) = 0;


        virtual void SetData(std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers) = 0;
    };
}

#endif 