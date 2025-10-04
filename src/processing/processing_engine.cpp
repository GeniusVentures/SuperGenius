#include "processing_engine.hpp"

#include <thread>
#include <memory>
#include <utility>

namespace sgns::processing
{
    ProcessingEngine::ProcessingEngine( std::string                                nodeId,
                                        std::shared_ptr<ProcessingCore>            processingCore,
                                        std::function<void( const std::string & )> processingErrorSink,
                                        std::function<void( void )>                processingDoneSink ) :
        m_nodeId( std::move( nodeId ) ),
        m_processingCore( std::move( processingCore ) ),
        m_processingErrorSink( std::move( processingErrorSink ) ),
        m_processingDoneSink( std::move( processingDoneSink ) )
    {
    }

    ProcessingEngine::~ProcessingEngine()
    {
        m_logger->debug( "[RELEASED] m_nodeId: {},", m_nodeId );
    }

    void ProcessingEngine::StartQueueProcessing( std::shared_ptr<SubTaskQueueAccessor> subTaskQueueAccessor )
    {
        std::lock_guard<std::mutex> queueGuard( m_mutexSubTaskQueue );
        m_logger->debug( "[START QUEUE PROCESSING] m_nodeId: {},", m_nodeId );
        m_subTaskQueueAccessor = std::move( subTaskQueueAccessor );

        m_subTaskQueueAccessor->GrabSubTask(
            [weakThis( weak_from_this() )]( boost::optional<const SGProcessing::SubTask &> subTask )
            {
                auto _this = weakThis.lock();
                if ( !_this )
                {
                    return;
                }
                _this->OnSubTaskGrabbed( subTask );
            } );
    }

    void ProcessingEngine::StopQueueProcessing()
    {
        std::lock_guard<std::mutex> queueGuard( m_mutexSubTaskQueue );
        m_subTaskQueueAccessor.reset();
        m_logger->debug( "[PROCESSING_STOPPED] m_nodeId: {}", m_nodeId );
    }

    bool ProcessingEngine::IsQueueProcessingStarted() const
    {
        std::lock_guard<std::mutex> queueGuard( m_mutexSubTaskQueue );
        return m_subTaskQueueAccessor != nullptr;
    }

    void ProcessingEngine::OnSubTaskGrabbed( boost::optional<const SGProcessing::SubTask &> subTask )
    {
        if ( subTask )
        {
            try {
                std::string subtaskId = subTask->subtaskid(); // Test if subtask is valid
                m_logger->debug( "[GRABBED] m_nodeId ({}), subtask ({}).", m_nodeId, subtaskId );
                ProcessSubTask( *subTask );
            } catch (const std::exception& e) {
                m_logger->error( "[GRABBED ERROR] m_nodeId ({}), error: {}", m_nodeId, e.what() );
            }
        }
        else
        {
            m_logger->debug( "ALL SUBTASKS ARE GRABBED. ({}).", m_nodeId );
            m_processingDoneSink();
        }
        // When results for all subtasks are available, no subtask is received (optnull).
    }

    void ProcessingEngine::ProcessSubTask( SGProcessing::SubTask subTask )
    {
        // Safely validate subTask before processing
        std::string subtaskId;
        try {
            subtaskId = subTask.subtaskid();
            if (subtaskId.empty()) {
                m_logger->error("ProcessSubTask called with empty subtaskid for node: {}", m_nodeId);
                return;
            }
        } catch (const std::exception& e) {
            m_logger->error("ProcessSubTask called with corrupted subTask for node: {} - {}", m_nodeId, e.what());
            return;
        }

        m_logger->debug( "[PROCESSING_STARTED]. m_nodeId ({}), subtask ({}).", m_nodeId, subtaskId );
        std::thread thread(
            [subTask( std::move( subTask ) ), _this( shared_from_this() )]()
            {
                // Double-check we haven't been destroyed
                if (!_this) {
                    return;
                }
                
                // Make a local copy of critical data to avoid corruption
                std::string subtaskId = subTask.subtaskid();
                std::string nodeId = _this->m_nodeId;
                
                if (subtaskId.empty()) {
                    _this->m_logger->error("Subtask ID became empty during processing for node: {}", nodeId);
                    return;
                }

                // @todo set initial hash code that depends on node id
                auto maybe_result = _this->m_processingCore->ProcessSubTask(
                    subTask,
                    std::hash<std::string>{}( nodeId ) );
                if ( maybe_result.has_value() )
                {
                    SGProcessing::SubTaskResult result = maybe_result.value();
                    
                    // Use local copies to avoid corruption
                    try {
                        result.set_subtaskid( subtaskId );
                        result.set_node_address( nodeId );
                        
                        _this->m_logger->debug( "[PROCESSED]. m_nodeId ({}), subtask ({}).",
                                                nodeId,
                                                subtaskId );
                        
                        std::lock_guard<std::mutex> queueGuard( _this->m_mutexSubTaskQueue );
                        if ( _this->m_subTaskQueueAccessor )
                        {
                            _this->m_subTaskQueueAccessor->CompleteSubTask( subtaskId, result );
                            _this->m_subTaskQueueAccessor->GrabSubTask(
                                [weakThis( std::weak_ptr<sgns::processing::ProcessingEngine>( _this ) )](
                                    boost::optional<const SGProcessing::SubTask &> subTask )
                                {
                                    auto _this = weakThis.lock();
                                    if ( !_this )
                                    {
                                        return;
                                }
                                _this->OnSubTaskGrabbed( subTask );
                            } );
                        }
                    } catch (const std::exception& e) {
                        _this->m_logger->error("Error setting protobuf fields for subtask {}: {}", subtaskId, e.what());
                    }
                }
                else
                {
                    _this->m_processingErrorSink( maybe_result.error().message() );
                }
            } );
        thread.detach();
    }

}
