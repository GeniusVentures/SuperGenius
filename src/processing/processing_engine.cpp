#include "processing_engine.hpp"

#include <thread>
#include <memory>
#include <utility>

namespace sgns::processing
{
    ProcessingEngine::ProcessingEngine( std::string                     nodeId,
                                        std::string                     escrow_path,
                                        std::shared_ptr<ProcessingCore> processingCore ) :
        m_nodeId( std::move( nodeId ) ),
        m_escrowPath( std::move( escrow_path ) ),
        m_processingCore( std::move( processingCore ) ),
        m_lastProcessedTime( std::chrono::steady_clock::now() - std::chrono::minutes( 2 ) )
    {
    }

    ProcessingEngine::~ProcessingEngine()
    {
        m_logger->debug( "[RELEASED] m_nodeId: {},", m_nodeId );
    }

    void ProcessingEngine::StartQueueProcessing( std::shared_ptr<SubTaskQueueAccessor> subTaskQueueAccessor )
    {
        std::lock_guard<std::mutex> queueGuard( m_mutexSubTaskQueue );
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
            m_logger->debug( "[GRABBED] m_nodeId ({}), subtask ({}).", m_nodeId, subTask->subtaskid() );
            ProcessSubTask( *subTask );
        }
        else
        {
            m_logger->debug( "ALL SUBTASKS ARE GRABBED. ({}).", m_nodeId );
        }
        // When results for all subtasks are available, no subtask is received (optnull).
    }

    void ProcessingEngine::SetProcessingErrorSink( std::function<void( const std::string & )> processingErrorSink )
    {
        m_processingErrorSink = std::move( processingErrorSink );
    }

    void ProcessingEngine::ProcessSubTask( SGProcessing::SubTask subTask )
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
        m_logger->debug( "[PROCESSING_STARTED]. m_nodeId ({}), subtask ({}).", m_nodeId, subTask.subtaskid() );
        std::thread thread(
            [subTask( std::move( subTask ) ), _this( shared_from_this() )]()
            {
                // @todo set initial hash code that depends on node id
                auto maybe_result = _this->m_processingCore->ProcessSubTask(
                    subTask,
                    std::hash<std::string>{}( _this->m_nodeId ) );
                if ( maybe_result.has_value() )
                {
                    SGProcessing::SubTaskResult result = maybe_result.value();
                    // @todo replace results_channel with subtaskid
                    result.set_subtaskid( subTask.subtaskid() );
                    result.set_node_address( _this->m_nodeId );
                    result.set_escrow_path( _this->m_escrowPath );
                    _this->m_logger->debug( "[PROCESSED]. m_nodeId ({}), subtask ({}).",
                                            _this->m_nodeId,
                                            subTask.subtaskid() );
                    std::lock_guard<std::mutex> queueGuard( _this->m_mutexSubTaskQueue );
                    if ( _this->m_subTaskQueueAccessor )
                    {
                        _this->m_subTaskQueueAccessor->CompleteSubTask( subTask.subtaskid(), result );
                        // @todo Should a new subtask be grabbed once the perivious one is processed?
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
                }
                else
                {
                    if ( _this->m_processingErrorSink )
                    {
                        _this->m_processingErrorSink( maybe_result.error().message() );
                    }
                }
            } );
        thread.detach();
    }

}
