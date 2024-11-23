#include "processing_engine.hpp"

#include <thread>
#include <memory>
#include <utility>

namespace sgns::processing
{
    ProcessingEngine::ProcessingEngine( std::string nodeId, std::shared_ptr<ProcessingCore> processingCore ) :
        m_nodeId( std::move( nodeId ) ), m_processingCore( std::move( processingCore ) ), m_lastProcessedTime(std::chrono::steady_clock::now() - std::chrono::minutes(2))
    {
    }

ProcessingEngine::~ProcessingEngine()
{
    m_logger->debug("[RELEASED] this: {}, thread_id {}", reinterpret_cast<size_t>(this), std::this_thread::get_id());
}

void ProcessingEngine::StartQueueProcessing(
    std::shared_ptr<SubTaskQueueAccessor> subTaskQueueAccessor)
{
    std::lock_guard<std::mutex> queueGuard( m_mutexSubTaskQueue );
    m_subTaskQueueAccessor = std::move( subTaskQueueAccessor );

    m_subTaskQueueAccessor->GrabSubTask(
        [weakThis(weak_from_this())](boost::optional<const SGProcessing::SubTask&> subTask) {
            auto _this = weakThis.lock();
            if (!_this)
            {
                return;
            }
            _this->OnSubTaskGrabbed(subTask);
        });
}

void ProcessingEngine::StopQueueProcessing()
{
    std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
    m_subTaskQueueAccessor.reset();
    m_logger->debug("[PROCESSING_STOPPED] this: {}", reinterpret_cast<size_t>(this));
}

bool ProcessingEngine::IsQueueProcessingStarted() const
{
    std::lock_guard<std::mutex> queueGuard(m_mutexSubTaskQueue);
    return m_subTaskQueueAccessor != nullptr;
}

void ProcessingEngine::OnSubTaskGrabbed(boost::optional<const SGProcessing::SubTask&> subTask)
{
    if (subTask)
    {
        m_logger->debug("[GRABBED]. ({}).", subTask->subtaskid());
        ProcessSubTask(*subTask);
    }

    // When results for all subtasks are available, no subtask is received (optnull).
}

void ProcessingEngine::SetProcessingErrorSink(std::function<void(const std::string&)> processingErrorSink)
{
    m_processingErrorSink = std::move( processingErrorSink );
}

void ProcessingEngine::ProcessSubTask(SGProcessing::SubTask subTask)
{

    //         // Cooldown check
    // auto now = std::chrono::steady_clock::now();
    // auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastProcessedTime);
    // if (elapsed < std::chrono::minutes(2))
    // {
    //     std::cout << "Cooldown active. Skipping processing of subtask." << std::endl;
    //     return;
    // }

    // m_lastProcessedTime = now;
    m_logger->debug("[PROCESSING_STARTED]. ({}).", subTask.subtaskid());
    std::thread thread([subTask(std::move(subTask)), _this(shared_from_this())]()
    {
        try
        {
            SGProcessing::SubTaskResult result;
            // @todo set initial hash code that depends on node id
            _this->m_processingCore->ProcessSubTask(
                subTask, result, std::hash<std::string>{}(_this->m_nodeId));
            // @todo replace results_channel with subtaskid
            result.set_subtaskid(subTask.subtaskid());
            result.set_node_address(_this->m_nodeId);
            _this->m_logger->debug("[PROCESSED]. ({}).", subTask.subtaskid());
            std::lock_guard<std::mutex> queueGuard(_this->m_mutexSubTaskQueue);
            if (_this->m_subTaskQueueAccessor)
            {
                _this->m_subTaskQueueAccessor->CompleteSubTask(subTask.subtaskid(), result);
                // @todo Should a new subtask be grabbed once the perivious one is processed?
                _this->m_subTaskQueueAccessor->GrabSubTask(
                    [weakThis(std::weak_ptr<sgns::processing::ProcessingEngine>(_this))](boost::optional<const SGProcessing::SubTask&> subTask) {
                    auto _this = weakThis.lock();
                    if (!_this)
                    {
                        return;
                    }
                    _this->OnSubTaskGrabbed(subTask);
                });
            }
        }
        catch (std::exception& ex)
        {
            if (_this->m_processingErrorSink)
            {
                _this->m_processingErrorSink(ex.what());
            }
        }
    });
    thread.detach();
}

}
