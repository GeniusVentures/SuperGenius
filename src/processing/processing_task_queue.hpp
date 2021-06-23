/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP

#include <processing/proto/SGProcessing.pb.h>

class ProcessingTaksQueue
{
public:
    virtual ~ProcessingTaksQueue() = default;

    virtual bool PopTask(SGProcessing::Task& task) = 0;
};

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_TASK_QUEUE_HPP
