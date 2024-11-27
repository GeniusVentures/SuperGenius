#ifndef SUPERGENIUS_PROCESSING_SUBTASK_ENQUEUER_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_ENQUEUER_HPP

#include "processing/proto/SGProcessing.pb.h"
#include "outcome/outcome.hpp"
#include <list>
#include <string>

namespace sgns::processing
{
// Encapsulates subtask queue construction algorithm
class SubTaskEnqueuer
{
public:
    virtual ~SubTaskEnqueuer() = default;

    virtual outcome::result<SGProcessing::Task> EnqueueSubTasks(
        std::string& subTaskQueueId, 
        std::list<SGProcessing::SubTask>& subTasks) = 0;
};    
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_SELECTOR_HPP
