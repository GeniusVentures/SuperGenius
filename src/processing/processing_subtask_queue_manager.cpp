#include "processing_subtask_queue_manager.hpp"

#include <utility>

namespace sgns::processing
{
    ////////////////////////////////////////////////////////////////////////////////
    ProcessingSubTaskQueueManager::ProcessingSubTaskQueueManager(
        std::shared_ptr<ProcessingSubTaskQueueChannel> queueChannel,
        std::shared_ptr<boost::asio::io_context>       context,
        const std::string                             &localNodeId,
        std::function<void( const std::string & )>     processingErrorSink ) :
        m_queueChannel( std::move( queueChannel ) ),
        m_context( std::move( context ) ),
        m_localNodeId( localNodeId ),
        m_dltQueueResponseTimeout( *m_context ),
        m_queueResponseTimeout( boost::posix_time::seconds( 5 ) ),
        m_dltGrabSubTaskTimeout( *m_context ),
        m_processingQueue( localNodeId ),
        m_processingTimeout( std::chrono::seconds( 15 ) ),
        m_processingErrorSink( std::move( processingErrorSink ) ),
        m_queue_timestamp_( 0 ),
        m_ownership_acquired_at_( 0 )
    {
    }

    void ProcessingSubTaskQueueManager::SetProcessingTimeout(
        const std::chrono::system_clock::duration &processingTimeout )
    {
        m_processingTimeout = processingTimeout;
    }

    ProcessingSubTaskQueueManager::~ProcessingSubTaskQueueManager()
    {
        m_logger->debug( "[RELEASED] this: {}", reinterpret_cast<size_t>( this ) );
    }

    bool ProcessingSubTaskQueueManager::CreateQueue( std::list<SGProcessing::SubTask> &subTasks )
    {
        auto timestamp = std::chrono::system_clock::now();

        bool hasChunksDuplicates = false;

        auto queue           = std::make_shared<SGProcessing::SubTaskQueue>();
        auto queueSubTasks   = queue->mutable_subtasks();
        auto processingQueue = queue->mutable_processing_queue();
        for ( auto itSubTask : subTasks )
        {
            // Move subtask to heap
            auto subTask = std::make_unique<SGProcessing::SubTask>( std::move( itSubTask ) );
            queueSubTasks->mutable_items()->AddAllocated( subTask.release() );
            processingQueue->add_items();
        }

        std::unique_lock<std::mutex> guard( m_queueMutex );
        m_queue = std::move( queue );

        m_processedSubTaskIds = {};
        // Map subtask IDs to subtask indices
        std::vector<int> unprocessedSubTaskIndices;
        for ( int subTaskIdx = 0; subTaskIdx < m_queue->subtasks().items_size(); ++subTaskIdx )
        {
            const auto &subTaskId = m_queue->subtasks().items( subTaskIdx ).subtaskid();
            if ( m_processedSubTaskIds.find( subTaskId ) == m_processedSubTaskIds.end() )
            {
                unprocessedSubTaskIndices.push_back( subTaskIdx );
            }
        }

        m_processingQueue.CreateQueue( processingQueue, unprocessedSubTaskIndices );

        // Record ownership acquisition time when creating a queue
        m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch() )
                                       .count();

        m_logger->debug( "Subtask Queue created in timestamp {}", m_ownership_acquired_at_ );
        LogQueue();

        PublishSubTaskQueue();

        if ( m_subTaskQueueAssignmentEventSink )
        {
            std::vector<std::string> subTaskIds;
            subTaskIds.reserve( m_queue->subtasks().items_size() );
            for ( int subTaskIdx = 0; subTaskIdx < m_queue->subtasks().items_size(); ++subTaskIdx )
            {
                subTaskIds.push_back( m_queue->subtasks().items( subTaskIdx ).subtaskid() );
            }

            guard.unlock();
            m_subTaskQueueAssignmentEventSink( subTaskIds );
        }

        return true;
    }

    bool ProcessingSubTaskQueueManager::UpdateQueue( SGProcessing::SubTaskQueue *pQueue )
    {
        if ( pQueue != nullptr )
        {
            std::shared_ptr<SGProcessing::SubTaskQueue> queue( pQueue );

            // Map subtask IDs to subtask indices
            std::vector<int> unprocessedSubTaskIndices;
            for ( int subTaskIdx = 0; subTaskIdx < queue->subtasks().items_size(); ++subTaskIdx )
            {
                const auto &subTaskId = queue->subtasks().items( subTaskIdx ).subtaskid();
                if ( m_processedSubTaskIds.find( subTaskId ) == m_processedSubTaskIds.end() )
                {
                    unprocessedSubTaskIndices.push_back( subTaskIdx );
                }
            }

            if ( m_processingQueue.UpdateQueue( queue->mutable_processing_queue(), unprocessedSubTaskIndices ) )
            {
                m_queue.swap( queue );
                LogQueue();
                return true;
            }
        }
        return false;
    }

    void ProcessingSubTaskQueueManager::ProcessPendingSubTaskGrabbing()
    {
        // The method has to be called in scoped lock of queue mutex
        m_dltGrabSubTaskTimeout.expires_at( boost::posix_time::pos_infin );

        // Update queue timestamp based on current ownership duration
        UpdateQueueTimestamp();

        while ( !m_onSubTaskGrabbedCallbacks.empty() )
        {
            size_t itemIdx = 0;
            if ( m_processingQueue.GrabItem( itemIdx ) )
            {
                // Track that we're using queue timestamp for this item
                // Actual timestamp is managed by ProcessingQueue via lock_timestamp
                // This will be checked when calculating task expiration

                LogQueue();
                PublishSubTaskQueue();

                m_onSubTaskGrabbedCallbacks.front()( { m_queue->subtasks().items( itemIdx ) } );
                m_onSubTaskGrabbedCallbacks.pop_front();
            }
            else
            {
                // No available subtasks found
                auto unlocked = m_processingQueue.UnlockExpiredItems( m_processingTimeout );
                if ( !unlocked )
                {
                    break;
                }
            }
        }

        if ( !m_onSubTaskGrabbedCallbacks.empty() )
        {
            if ( m_processedSubTaskIds.size() < static_cast<size_t>( m_queue->processing_queue().items_size() ) )
            {
                // Wait for subtasks are processed
                auto timestamp          = std::chrono::system_clock::now();
                auto lastLockTimestamp  = m_processingQueue.GetLastLockTimestamp();
                auto lastExpirationTime = lastLockTimestamp + m_processingTimeout;

                std::chrono::milliseconds grabSubTaskTimeout =
                    ( lastExpirationTime > timestamp )
                        ? std::chrono::duration_cast<std::chrono::milliseconds>( lastExpirationTime - timestamp )
                        : std::chrono::milliseconds( 1 );

                m_logger->debug( "GRAB_TIMEOUT {}ms", grabSubTaskTimeout.count() );

                m_dltGrabSubTaskTimeout.expires_from_now(
                    boost::posix_time::milliseconds( grabSubTaskTimeout.count() ) );

                m_dltGrabSubTaskTimeout.async_wait(
                    [instance = shared_from_this()]( const boost::system::error_code &ec )
                    { instance->HandleGrabSubTaskTimeout( ec ); } );
            }
            else
            {
                bool no_more_subtasks = false;
                while ( !m_onSubTaskGrabbedCallbacks.empty() )
                {
                    no_more_subtasks = true;
                    // Let the requester know that there are no available subtasks
                    m_onSubTaskGrabbedCallbacks.front()( {} );
                    // Reset the callback
                    m_onSubTaskGrabbedCallbacks.pop_front();
                }
                if ( no_more_subtasks )
                {
                    m_processingErrorSink( "deleting process node, no more subtasks" );
                }
            }
        }
    }

    void ProcessingSubTaskQueueManager::HandleGrabSubTaskTimeout( const boost::system::error_code &ec )
    {
        if ( ec != boost::asio::error::operation_aborted )
        {
            std::lock_guard<std::mutex> guard( m_queueMutex );
            m_dltGrabSubTaskTimeout.expires_at( boost::posix_time::pos_infin );
            m_logger->debug( "HANDLE_GRAB_TIMEOUT" );
            if ( !m_onSubTaskGrabbedCallbacks.empty() &&
                 ( m_processedSubTaskIds.size() < static_cast<size_t>( m_queue->processing_queue().items_size() ) ) )
            {
                GrabSubTasks();
            }
        }
    }

    void ProcessingSubTaskQueueManager::GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback )
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );
        m_onSubTaskGrabbedCallbacks.push_back( std::move( onSubTaskGrabbedCallback ) );
        GrabSubTasks();
    }

    void ProcessingSubTaskQueueManager::GrabSubTasks()
    {
        if ( m_processingQueue.HasOwnership() )
        {
            ProcessPendingSubTaskGrabbing();
        }
        else
        {
            // Send a request to grab a subtask queue
            m_queueChannel->RequestQueueOwnership( m_localNodeId );
        }
    }

    bool ProcessingSubTaskQueueManager::MoveOwnershipTo( const std::string &nodeId )
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );

        // Calculate how long we've owned the queue
        auto current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch() )
                                   .count();
        auto ownership_duration_ms = current_time_ms - m_ownership_acquired_at_;

        // Update the queue's total time counter
        m_queue_timestamp_ += ownership_duration_ms;

        if ( m_processingQueue.MoveOwnershipTo( nodeId ) )
        {
            LogQueue();
            PublishSubTaskQueue();
            return true;
        }
        return false;
    }

    bool ProcessingSubTaskQueueManager::HasOwnership() const
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );
        return m_processingQueue.HasOwnership();
    }

    void ProcessingSubTaskQueueManager::PublishSubTaskQueue() const
    {
        m_queueChannel->PublishQueue( m_queue );
        m_logger->debug( "QUEUE_PUBLISHED" );
    }

    bool ProcessingSubTaskQueueManager::ProcessSubTaskQueueMessage( SGProcessing::SubTaskQueue *queue )
    {
        bool hadOwnership = HasOwnership();

        std::unique_lock<std::mutex> guard( m_queueMutex );
        m_dltQueueResponseTimeout.expires_at( boost::posix_time::pos_infin );

        bool queueInitilalized = ( m_queue != nullptr );
        bool queueChanged      = UpdateQueue( queue );

        if ( queueChanged )
        {
            bool hasOwnershipNow = m_processingQueue.HasOwnership();
            if ( !hadOwnership && hasOwnershipNow )
            {
                // We just acquired ownership - record the time
                m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch() )
                                               .count();
            }

            if ( hasOwnershipNow )
            {
                ProcessPendingSubTaskGrabbing();
            }
        }

        if ( m_subTaskQueueAssignmentEventSink )
        {
            if ( !queueInitilalized && queueChanged )
            {
                std::vector<std::string> subTaskIds;
                subTaskIds.reserve( queue->subtasks().items_size() );
                for ( int subTaskIdx = 0; subTaskIdx < queue->subtasks().items_size(); ++subTaskIdx )
                {
                    subTaskIds.push_back( queue->subtasks().items( subTaskIdx ).subtaskid() );
                }
                guard.unlock();
                m_subTaskQueueAssignmentEventSink( subTaskIds );
            }
        }

        return queueChanged;
    }

    bool ProcessingSubTaskQueueManager::ProcessSubTaskQueueRequestMessage(
        const SGProcessing::SubTaskQueueRequest &request )
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );
        if ( m_processingQueue.MoveOwnershipTo( request.node_id() ) )
        {
            // Calculate how long we've owned the queue before transferring ownership
            auto current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch() )
                                       .count();
            auto ownership_duration_ms = current_time_ms - m_ownership_acquired_at_;

            // Update the queue's total time counter
            m_queue_timestamp_ += ownership_duration_ms;

            LogQueue();
            PublishSubTaskQueue();
            return true;
        }

        m_logger->debug( "QUEUE_REQUEST_RECEIVED" );
        m_dltQueueResponseTimeout.expires_from_now( m_queueResponseTimeout );
        m_dltQueueResponseTimeout.async_wait( [instance = shared_from_this()]( const boost::system::error_code &ec )
                                              { instance->HandleQueueRequestTimeout( ec ); } );

        return false;
    }

    void ProcessingSubTaskQueueManager::HandleQueueRequestTimeout( const boost::system::error_code &ec )
    {
        if ( ec != boost::asio::error::operation_aborted )
        {
            std::lock_guard<std::mutex> guard( m_queueMutex );
            m_logger->debug( "QUEUE_REQUEST_TIMEOUT" );
            m_dltQueueResponseTimeout.expires_at( boost::posix_time::pos_infin );
            if ( m_processingQueue.RollbackOwnership() )
            {
                // Record ownership acquisition time when rolling back ownership
                m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch() )
                                               .count();

                LogQueue();
                PublishSubTaskQueue();

                if ( m_processingQueue.HasOwnership() )
                {
                    ProcessPendingSubTaskGrabbing();
                }
            }
        }
    }

    std::unique_ptr<SGProcessing::SubTaskQueue> ProcessingSubTaskQueueManager::GetQueueSnapshot() const
    {
        auto queue = std::make_unique<SGProcessing::SubTaskQueue>();

        std::lock_guard<std::mutex> guard( m_queueMutex );
        if ( m_queue )
        {
            queue->CopyFrom( *m_queue.get() );
        }
        return queue;
    }

    void ProcessingSubTaskQueueManager::ChangeSubTaskProcessingStates( const std::set<std::string> &subTaskIds,
                                                                       bool                         isProcessed )
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );
        for ( const auto &subTaskId : subTaskIds )
        {
            if ( isProcessed )
            {
                m_processedSubTaskIds.insert( subTaskId );
                m_logger->debug( "Subtask flagged as processed {}", subTaskId );
            }
            else
            {
                m_processedSubTaskIds.erase( subTaskId );
                m_logger->debug( "Subtask flagged as UNPROCESSED {}", subTaskId );
            }
        }

        // Map subtask IDs to subtask indices
        std::vector<int> unprocessedSubTaskIndices;
        for ( int subTaskIdx = 0; subTaskIdx < m_queue->subtasks().items_size(); ++subTaskIdx )
        {
            const auto &subTaskId = m_queue->subtasks().items( subTaskIdx ).subtaskid();
            if ( m_processedSubTaskIds.find( subTaskId ) == m_processedSubTaskIds.end() )
            {
                unprocessedSubTaskIndices.push_back( subTaskIdx );
            }
        }
        m_processingQueue.UpdateQueue( m_queue->mutable_processing_queue(), unprocessedSubTaskIndices );
    }

    bool ProcessingSubTaskQueueManager::IsProcessed() const
    {
        std::lock_guard<std::mutex> guard( m_queueMutex );
        // The queue can contain only valid results
        m_logger->debug( "IS_PROCESSED: {} {} {}",
                         m_processedSubTaskIds.size(),
                         m_queue->subtasks().items_size(),
                         (size_t)this );
        return m_processedSubTaskIds.size() >= (size_t)m_queue->subtasks().items_size();
    }

    void ProcessingSubTaskQueueManager::SetSubTaskQueueAssignmentEventSink(
        std::function<void( const std::vector<std::string> & )> subTaskQueueAssignmentEventSink )
    {
        m_subTaskQueueAssignmentEventSink = std::move( subTaskQueueAssignmentEventSink );
        if ( m_subTaskQueueAssignmentEventSink )
        {
            std::unique_lock<std::mutex> guard( m_queueMutex );
            bool                         isQueueInitialized = ( m_queue != nullptr );

            if ( isQueueInitialized )
            {
                std::vector<std::string> subTaskIds;
                subTaskIds.reserve( m_queue->subtasks().items_size() );
                for ( int subTaskIdx = 0; subTaskIdx < m_queue->subtasks().items_size(); ++subTaskIdx )
                {
                    subTaskIds.push_back( m_queue->subtasks().items( subTaskIdx ).subtaskid() );
                }
                guard.unlock();
                m_subTaskQueueAssignmentEventSink( subTaskIds );
            }
        }
    }

    void ProcessingSubTaskQueueManager::LogQueue() const
    {
        if ( m_logger->level() <= spdlog::level::trace )
        {
            std::stringstream ss;
            ss << "{";
            ss << "\"this\":\"" << reinterpret_cast<size_t>( this ) << "\"";
            ss << "\"owner_node_id\":\"" << m_queue->processing_queue().owner_node_id() << "\"";
            ss << "," << "\"last_update_timestamp\":" << m_queue->processing_queue().last_update_timestamp();
            ss << "," << "\"items\":[";
            for ( int itemIdx = 0; itemIdx < m_queue->processing_queue().items_size(); ++itemIdx )
            {
                auto item = m_queue->processing_queue().items( itemIdx );
                ss << "{\"lock_node_id\":\"" << item.lock_node_id() << "\"";
                ss << ",\"lock_timestamp\":" << item.lock_timestamp() << "},";
            }
            ss << "]}";

            m_logger->trace( ss.str() );
        }
    }

    void ProcessingSubTaskQueueManager::UpdateQueueTimestamp()
    {
        if (m_processingQueue.HasOwnership())
        {
            auto current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto ownership_duration_delta_ms = current_time_ms - m_ownership_last_delta_time_;

            // Update the queue's total time counter
            m_queue_timestamp_ += ownership_duration_delta_ms;

            // Reset ownership acquisition time to now
            m_ownership_last_delta_time_ = current_time_ms;
        }
        else
        {

        }
    }
}
