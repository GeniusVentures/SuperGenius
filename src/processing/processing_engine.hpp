/**
* Header file for the distrubuted processing Room
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_ENGINE_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_ENGINE_HPP

#include "processing/processing_core.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"
#include "base/logger.hpp"

namespace sgns::processing
{
/** Handles subtask processing and processing results accumulation
*/
class ProcessingEngine : public std::enable_shared_from_this<ProcessingEngine>
{
public:
    /** Create a processing engine object
    * @param nodeId - current processing node ID
    * @param processingCore specific processing core that process a subtask using specific algorithm
    */
    ProcessingEngine(
        std::string nodeId,
        std::shared_ptr<ProcessingCore> processingCore);
    ~ProcessingEngine();

    // @todo rename to StartProcessing
    void StartQueueProcessing(std::shared_ptr<SubTaskQueueAccessor> subTaskQueueAccessor);

    void StopQueueProcessing();
    bool IsQueueProcessingStarted() const;

    void SetProcessingErrorSink(std::function<void(const std::string&)> processingErrorSink);

private:
    void OnSubTaskGrabbed(boost::optional<const SGProcessing::SubTask&> subTask);

    /** Processes a subtask and send the processing result to corresponding result channel
    * @param subTask - subtask that should be processed
    */
    void ProcessSubTask(SGProcessing::SubTask subTask);

    std::string m_nodeId;
    std::shared_ptr<ProcessingCore> m_processingCore;
    std::function<void(const std::string&)> m_processingErrorSink;

    std::shared_ptr<SubTaskQueueAccessor> m_subTaskQueueAccessor;

    mutable std::mutex m_mutexSubTaskQueue;
    std::chrono::steady_clock::time_point m_lastProcessedTime;
    
    base::Logger m_logger = base::createLogger("ProcessingEngine");
};
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_ENGINE_HPP
