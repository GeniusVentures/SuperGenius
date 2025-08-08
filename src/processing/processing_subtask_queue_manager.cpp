#include "processing_subtask_queue_manager.hpp"

#include <utility>
#include <thread>

namespace sgns::processing
{
    ////////////////////////////////////////////////////////////////////////////////
    ProcessingSubTaskQueueManager::ProcessingSubTaskQueueManager(
        std::shared_ptr<ProcessingSubTaskQueueChannel> queueChannel,
        std::shared_ptr<boost::asio::io_context>       context,
        const std::string                             &localNodeId,
        std::function<void( const std::string & )>     processingErrorSink,
        uint64_t delayBetweenProcessingMs
    ) :
        m_queueChannel( std::move( queueChannel ) ),
        m_context( std::move( context ) ),
        m_localNodeId( localNodeId ),
        m_dltQueueResponseTimeout( *m_context ),
        m_queueResponseTimeout( boost::posix_time::seconds( 5 ) ),
        m_dltGrabSubTaskTimeout( *m_context ),
        m_processingQueue(localNodeId, [this]() { return this->GetCurrentQueueTimestamp(); }),
        m_processingTimeout( std::chrono::seconds( 15 ) ),
        m_processingErrorSink( std::move( processingErrorSink ) ),
        m_delayBetweenProcessingMs( std::move( delayBetweenProcessingMs ) )
    {
        m_maxSubtasksPerOwnership = m_defaultMaxSubtasksPerOwnership;
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
        // Check if all subtasks have at least one chunk to process
        bool hasValidChunks = true;
        for (const auto& subtask : subTasks) {
            if (subtask.chunkstoprocess_size() == 0) {
                hasValidChunks = false;
                break;
            }
        }

        if (!hasValidChunks) {
            m_logger->error("Failed to create queue: subtasks must have at least one chunk to process");
            return false;
        }

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

        // Record ownership acquisition time when creating a queue
        m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch() )
                                       .count();
        m_queue_timestamp_ = m_ownership_acquired_at_;
        m_ownership_last_delta_time_ = m_ownership_acquired_at_;

        processingQueue->set_processing_timeout_length( m_processingTimeout.count() );
        processingQueue->set_last_update_timestamp( m_queue_timestamp_ );
        std::unique_lock guard( m_queueMutex );
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

    bool ProcessingSubTaskQueueManager::UpdateQueue(SGProcessing::SubTaskQueue *pQueue)
    {
        auto queueChanged = false;
        if (pQueue != nullptr)
        {
            std::shared_ptr<SGProcessing::SubTaskQueue> queue(pQueue);

            // First, decide whether to update our local set or the queue's set
            if (!HasOwnership()) {
                m_queue_timestamp_ = queue->processing_queue().last_update_timestamp();
                UpdateQueueTimestamp();
                // merged processed subtasks just in case we caught some messages the owner didn't
                // when we get ownership we will update the processed_subtask_ids
                for (int i = 0; i < queue->processing_queue().processed_subtask_ids_size(); ++i) {
                    m_processedSubTaskIds.insert(queue->processing_queue().processed_subtask_ids(i));
                }
            } else {

                // Merge incoming processed IDs into local set before updating queue
                for (int i = 0; i < queue->processing_queue().processed_subtask_ids_size(); ++i) {
                    auto [_, success] = m_processedSubTaskIds.insert(queue->processing_queue().processed_subtask_ids(i));
                    if (success)
                    {
                        queueChanged = true;
                    }
                }
                // If we're the owner, update the processed IDs in the queue from our local set
                queue->mutable_processing_queue()->clear_processed_subtask_ids();
                for (const auto& processedId : m_processedSubTaskIds) {
                    queue->mutable_processing_queue()->add_processed_subtask_ids(processedId);
                }

                if (queueChanged)
                {
                    m_queue_timestamp_ = queue->processing_queue().last_update_timestamp();
                    UpdateQueueTimestamp();
                    m_logger->debug("QUEUE_PUBLISH_SUBTASK_PROCESSED_UPDATE: on {} at {}ms", m_localNodeId, m_queue_timestamp_);
                    LogQueue();
                    PublishSubTaskQueue();
                }
            }

            // Now map subtask IDs to subtask indices based on the updated m_processedSubTaskIds
            std::vector<int> unprocessedSubTaskIndices;
            UpdateUnprocessedSubTaskIndices(queue.get(), unprocessedSubTaskIndices);

            if (m_processingQueue.UpdateQueue(queue->mutable_processing_queue(), unprocessedSubTaskIndices))
            {
                m_logger->debug("QUEUE_LOCAL_UPDATE: on {} at {}ms", m_localNodeId, m_queue_timestamp_);
                m_queue.swap(queue);
                LogQueue();
                queueChanged = true;
            }
        }
        return queueChanged;
    }

    void ProcessingSubTaskQueueManager::ProcessPendingSubTaskGrabbing()
    {
        m_dltGrabSubTaskTimeout.expires_at( boost::posix_time::pos_infin );

        m_logger->trace("QUEUE_PROCESS_PENDING: for node {} at {}ms.", m_localNodeId, m_queue_timestamp_);

        // Update queue timestamp based on current ownership duration
        UpdateQueueTimestamp();

        CheckActiveCount();

        bool losingOwnership = false;
        bool lockReleased = false;

        while ( !losingOwnership &&
                !m_onSubTaskGrabbedCallbacks.empty() &&
                (m_processedSubtasksInCurrentOwnership < m_maxSubtasksPerOwnership) )
        {
            // If the lock was released in the previous iteration, reacquire it
            std::unique_lock guard(m_queueMutex, std::defer_lock);
            if (lockReleased || !guard.owns_lock())
            {
                guard.lock();
                lockReleased = false;
            }

            size_t itemIdx = 0;
            if ( m_processingQueue.GrabItem( itemIdx, m_queue_timestamp_ ) )
            {
                // Track that we're using queue timestamp for this item
                // Actual timestamp is managed by ProcessingQueue via lock_timestamp
                // This will be checked when calculating task expiration
                LogQueue();
                PublishSubTaskQueue();

                m_processedSubtasksInCurrentOwnership++;

                // Make a copy of the subtask and callback
                auto subtaskCopy = m_queue->subtasks().items(itemIdx);
                auto callback = m_onSubTaskGrabbedCallbacks.front();
                m_onSubTaskGrabbedCallbacks.pop_front();

                m_logger->debug("GRAB_SUBTASK_TO_PROCESS: Subtask {} grabbed for node {} at {}ms.", subtaskCopy.subtaskid(), m_localNodeId, m_queue_timestamp_);

                // Check for pending ownership requests AFTER processing a subtask
                if ( m_queue->processing_queue().ownership_requests_size() > 0 )
                {
                    m_logger->debug("QUEUE_PROCESSING_PAUSED: Pending ownership requests detected. Stopping further subtask processing for node {} at {}ms.",
                        m_localNodeId, m_queue_timestamp_);
                    losingOwnership = true;
                }
                // Release the lock before calling the callback
                guard.unlock();
                lockReleased = true;

                // Call the callback without holding the lock
                callback({subtaskCopy});
                if (losingOwnership)
                {
                    break;
                }

            }
            else
            {
                // No available subtasks found
                auto unlocked = m_processingQueue.UnlockExpiredItems( m_queue_timestamp_ );
                if ( !unlocked )
                {
                    break;
                }
            }
        }

        // After the while loop, ensure we have the lock
        // After the while loop, ensure we have the lock for the remaining operations
        std::unique_lock finalGuard(m_queueMutex, std::defer_lock);
        if (lockReleased)
        {
            finalGuard.lock();
            lockReleased = false;
        }

        if ( losingOwnership && !HasOwnership() && !IsProcessed() )
        {
            UpdateQueueTimestamp();
            // Add current node's ownership request before publishing
            m_processingQueue.AddOwnershipRequest( m_localNodeId,
                m_queue_timestamp_ );

            m_logger->debug( "READD_QUEUE_OWNDERSHIP_REQUEST: node {} at {}ms", m_localNodeId, m_queue_timestamp_ );
            PublishSubTaskQueue();
        }

        if ( !m_onSubTaskGrabbedCallbacks.empty() )
        {
            if ( m_processedSubTaskIds.size() < static_cast<size_t>( m_queue->processing_queue().items_size() ) )
            {
                // Calculate time until expiration in queue time
                uint64_t grabSubTaskTimeoutMs = CalculateGrabSubTaskTimeout();

                m_logger->trace("GRAB_TIMEOUT set to {}ms for node {}", grabSubTaskTimeoutMs, m_localNodeId);

                m_dltGrabSubTaskTimeout.expires_from_now(
                    boost::posix_time::milliseconds( grabSubTaskTimeoutMs ) );

                m_dltGrabSubTaskTimeout.async_wait(
                [instance = shared_from_this()](const boost::system::error_code &ec)
                { instance->HandleGrabSubTaskTimeout(ec); });

            }
            else
            {

                while ( !m_onSubTaskGrabbedCallbacks.empty() )
                {
                    // Let the requester know that there are no available subtasks
                    m_onSubTaskGrabbedCallbacks.front()( {} );
                    // Reset the callback
                    m_onSubTaskGrabbedCallbacks.pop_front();
                }

            }
        }
        // if we are here, we have processed all the subtasks we can or are waiting for ownership to grab more.
        // Small delay to allow pubsub and other threads to process
        std::this_thread::sleep_for(std::chrono::milliseconds(m_delayBetweenProcessingMs));
    }

    void ProcessingSubTaskQueueManager::HandleGrabSubTaskTimeout( const boost::system::error_code &ec )
    {
        if ( ec != boost::asio::error::operation_aborted )
        {
            std::lock_guard guard( m_queueMutex );
            m_dltGrabSubTaskTimeout.expires_at( boost::posix_time::pos_infin );
            m_logger->trace( "HANDLE_GRAB_TIMEOUT at {}ms from node {}", m_queue_timestamp_, m_localNodeId );
            if ( !m_onSubTaskGrabbedCallbacks.empty() &&
                 ( m_processedSubTaskIds.size() < static_cast<size_t>( m_queue->processing_queue().items_size() ) ) )
            {
                GrabSubTasks();
            }
        }
    }

    void ProcessingSubTaskQueueManager::GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback )
    {
        {
            std::lock_guard guard( m_queueMutex );
            m_onSubTaskGrabbedCallbacks.push_back( std::move( onSubTaskGrabbedCallback ) );
        }
        GrabSubTasks();
    }

    void ProcessingSubTaskQueueManager::GrabSubTasks()
    {
        if ( HasOwnership() )
        {
            ProcessPendingSubTaskGrabbing();
        }
        else
        {
            // Since we're not the owner, we must use the pubsub channel
            // to request ownership from the current owner
            m_queueChannel->RequestQueueOwnership( m_localNodeId );
        }
    }

    bool ProcessingSubTaskQueueManager::MoveOwnershipTo( const std::string &nodeId )
    {
        std::lock_guard guard( m_queueMutex );

        if ( m_processingQueue.MoveOwnershipTo( nodeId ) )
        {
            PublishSubTaskQueue();
            return true;
        }
        return false;
    }

    bool ProcessingSubTaskQueueManager::HasOwnership() const
    {
        // Always locks for this thread
        std::lock_guard tempLock(m_queueMutex);
        return m_processingQueue.HasOwnership();
    }

    void ProcessingSubTaskQueueManager::PublishSubTaskQueue()
    {
        UpdateQueueTimestamp();
        m_queueChannel->PublishQueue( m_queue );
        m_logger->debug( "QUEUE_PUBLISHED: by {} at {}ms", m_localNodeId, m_queue_timestamp_ );
    }

    bool ProcessingSubTaskQueueManager::ProcessSubTaskQueueMessage( SGProcessing::SubTaskQueue *queue )
    {
        bool hadOwnership = HasOwnership();

        std::unique_lock guard( m_queueMutex );

        // If we own the queue and this message is for a queue we own, discard it
        // as it's likely our own published message coming back
        if (m_queue != nullptr &&
            m_queue->processing_queue().owner_node_id() == m_localNodeId &&
            queue->processing_queue().owner_node_id() == m_localNodeId)
        {
            m_logger->debug("Discarding queue message as we already own this queue");
            return false;
        }

        m_dltQueueResponseTimeout.expires_at( boost::posix_time::pos_infin );

        bool queueInitilalized = ( m_queue != nullptr );
        bool queueChanged      = UpdateQueue( queue );

        if ( queueChanged )
        {
            bool hasOwnershipNow = HasOwnership();
            if ( !hadOwnership && hasOwnershipNow )
            {
                // We just acquired ownership - record the time
                m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch() )
                                               .count();
                m_queue_timestamp_ = queue->processing_queue().last_update_timestamp();
                m_ownership_last_delta_time_ = m_ownership_acquired_at_;
                m_processedSubtasksInCurrentOwnership = 0;  // Reset processed subtasks on new ownership

                m_logger->debug( "QUEUE_OWNERSHIP_ACQUIRED: by {} at {}ms", m_localNodeId, m_queue_timestamp_ );

                // If we have both available work and callbacks waiting to process it,
                // cancel both timers to enable immediate processing
                if (HasAvailableWork() && !m_onSubTaskGrabbedCallbacks.empty()) {
                    m_dltGrabSubTaskTimeout.cancel();
                    m_dltQueueResponseTimeout.cancel();
                    m_logger->debug("CANCEL_ASYNC_TIMERS: {} is Canceling timers due to ownership acquisition and available work at {}ms", m_localNodeId,m_queue_timestamp_);
                    ProcessPendingSubTaskGrabbing(); // Start processing immediately
                }

                PublishSubTaskQueue();
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
                m_subTaskQueueAssignmentEventSink( subTaskIds );
            }
        }

        return queueChanged;
    }

    bool ProcessingSubTaskQueueManager::ProcessSubTaskQueueRequestMessage(
        const SGProcessing::SubTaskQueueRequest &request )
    {
        std::lock_guard guard( m_queueMutex );

        auto requstingNodeId = request.node_id();
        m_logger->debug( "QUEUE_OWNERSHIP_REQUEST: node {} from node {} at {}ms", m_localNodeId, requstingNodeId, m_queue_timestamp_ );

        // If we are the owner and there are still subtasks to be processed, we
        // can immediately transfer ownership, do so
        if (HasOwnership()  && !HasAvailableWork() )
        {
            if ( m_processingQueue.MoveOwnershipTo( requstingNodeId ) )
            {
                PublishSubTaskQueue();
                m_logger->debug( "QUEUE_OWNERSHIP_TRANSFERRED: from {} to {} at queue timestamp of {}ms",
                    m_localNodeId, requstingNodeId, m_queue_timestamp_ );
                return true;
            }
        }

        // Otherwise, add to the shared ownership request queue
        if (HasAvailableWork(false))
        {
            bool added = m_processingQueue.AddOwnershipRequest(requstingNodeId, request.request_timestamp());
            if (added) {
                m_logger->debug("Added ownership request from node {} to queue", requstingNodeId);
                PublishSubTaskQueue();  // Publish updated queue with new request

                // Only start the timer if this is the first request in the queue
                int requestCount = m_queue->processing_queue().ownership_requests_size();
                if (requestCount == 1) {
                    m_dltQueueResponseTimeout.expires_from_now( m_queueResponseTimeout );
                    HandleQueueRequestTimeout( boost::system::error_code{} );;
                }
            }
        } else
        {
            m_logger->debug("No available work to process, not adding ownership request from node {}", requstingNodeId);
        }

        return true;
    }

    void ProcessingSubTaskQueueManager::HandleQueueRequestTimeout( const boost::system::error_code &ec )
    {
        if ( ec != boost::asio::error::operation_aborted )
        {
            std::unique_lock guard( m_queueMutex );
            m_logger->debug( "QUEUE_REQUEST_TIMEOUT" );
            m_dltQueueResponseTimeout.expires_at( boost::posix_time::pos_infin );

            // If there's no available work, clear the ownership requests and exit
            if ( !HasAvailableWork(false) ) {
                // Clear ownership requests but remain the owner
                m_queue->mutable_processing_queue()->mutable_ownership_requests()->Clear();
                LogQueue();
                PublishSubTaskQueue();
                return;
            }

            // If we're the owner and there are pending requests, process immediately
            if (HasOwnership() && m_queue->processing_queue().ownership_requests_size() > 0) {
                if (m_processingQueue.ProcessNextOwnershipRequest()) {
                    LogQueue();
                    PublishSubTaskQueue();
                    return;  // Successfully processed a request, exit
                }
            }

            // Before attempting rollback, check if the current owner is responsive
            auto current_time = std::chrono::steady_clock::now();
            auto time_since_last_update = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - m_lastQueueUpdateTime ).count();

            // Only attempt rollback if we haven't received updates for a while
            if ( time_since_last_update > m_queueResponseTimeout.total_milliseconds() &&
                m_processingQueue.RollbackOwnership() )
            {
                // Record ownership acquisition time when rolling back ownership
                m_ownership_acquired_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch() )
                                               .count();
                m_queue_timestamp_ = m_queue->processing_queue().last_update_timestamp();
                m_ownership_last_delta_time_ = m_ownership_acquired_at_;
                m_processedSubtasksInCurrentOwnership = 0;  // Reset processed subtasks on new ownership
                LogQueue();
                PublishSubTaskQueue();

                if (HasOwnership() )
                {
                    // Check if there are any pending ownership requests
                    if ( m_queue->processing_queue().ownership_requests_size() > 0 )
                    {
                        // If there are requests, try to process the next request instead of grabbing subtasks
                        if ( m_processingQueue.ProcessNextOwnershipRequest() )
                        {
                            LogQueue();
                            PublishSubTaskQueue();
                            return;  // Exit without processing subtasks
                        }
                    }
                    guard.unlock();
                    ProcessPendingSubTaskGrabbing();
                    return;
                }
            }

            m_dltQueueResponseTimeout.async_wait( [instance = shared_from_this()]( const boost::system::error_code &ec )
                                                 { instance->HandleQueueRequestTimeout( ec ); } );
            // If it hasn't been long enough, schedule another timeout check, with shorter subsequent checks.
            m_dltQueueResponseTimeout.expires_from_now( boost::posix_time::milliseconds(100) );

        }
    }

    std::unique_ptr<SGProcessing::SubTaskQueue> ProcessingSubTaskQueueManager::GetQueueSnapshot() const
    {
        auto queue = std::make_unique<SGProcessing::SubTaskQueue>();

        std::lock_guard guard( m_queueMutex );
        if ( m_queue )
        {
            queue->CopyFrom( *m_queue.get() );
        }
        return queue;
    }

    void ProcessingSubTaskQueueManager::ChangeSubTaskProcessingStates( const std::set<std::string> &subTaskIds,
                                                                       bool                         isProcessed )
    {
        if (!m_queue)
        {
            m_logger->error( "No queue for task");
            return;
        }

        std::lock_guard guard( m_queueMutex );
        for ( const auto &subTaskId : subTaskIds )
        {
            if ( isProcessed )
            {
                m_processedSubTaskIds.insert( subTaskId );
                // Add to queue's processed subtask IDs
                m_queue->mutable_processing_queue()->add_processed_subtask_ids(subTaskId);
                m_logger->trace( "Subtask flagged as processed {}", subTaskId );
            }
            else
            {
                m_processedSubTaskIds.erase( subTaskId );

                // For removal, clear and rebuild from our updated set
                auto* processed_ids = m_queue->mutable_processing_queue();
                processed_ids->clear_processed_subtask_ids();
                for (const auto& id : m_processedSubTaskIds) {
                    processed_ids->add_processed_subtask_ids(id);
                }
                m_logger->trace( "Subtask flagged as UNPROCESSED {}", subTaskId );
            }
        }

        // Map subtask IDs to subtask indices
        std::vector<int> unprocessedSubTaskIndices;
        UpdateUnprocessedSubTaskIndices(m_queue.get(), unprocessedSubTaskIndices);

        m_processingQueue.UpdateQueue( m_queue->mutable_processing_queue(), unprocessedSubTaskIndices );

        // Add this at the end of the method
        if (HasOwnership()) {
            PublishSubTaskQueue();
        }
    }

    bool ProcessingSubTaskQueueManager::IsProcessed() const
    {
        std::lock_guard guard( m_queueMutex );
        if (!m_queue)
        {
            m_logger->error( "CHECK_IS_PROCESSED Queue is null: {} for node {}",
                             m_processedSubTaskIds.size(),
                             m_processedSubTaskIds.size(),
                             m_localNodeId );
            return false;
        }
        // The queue can contain only valid results
        m_logger->debug( "CHECK_IS_PROCESSED: {} of {} for node {}",
                         m_processedSubTaskIds.size(),
                         m_queue->subtasks().items_size(),
                         m_localNodeId );
        return m_processedSubTaskIds.size() >= (size_t)m_queue->subtasks().items_size();
    }

    void ProcessingSubTaskQueueManager::SetSubTaskQueueAssignmentEventSink(
        std::function<void( const std::vector<std::string> & )> subTaskQueueAssignmentEventSink )
    {
        m_subTaskQueueAssignmentEventSink = std::move( subTaskQueueAssignmentEventSink );
        if ( m_subTaskQueueAssignmentEventSink )
        {
            std::unique_lock guard( m_queueMutex );
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

    bool ProcessingSubTaskQueueManager::HasAvailableWork(bool checkOwnershipQuota) const
    {
        // Check if we've already processed the maximum allowed subtasks per ownership
        if ( checkOwnershipQuota && (m_processedSubtasksInCurrentOwnership >= m_maxSubtasksPerOwnership) )
        {
            return false;
        }

        if (!m_queue)
        {
            m_logger->error( "No queue for check of available work" );
            return false;
        }

        // Use the helper method to check if any subtasks are available
        std::vector<int> unprocessedSubTaskIndices;
        return UpdateUnprocessedSubTaskIndices(m_queue.get(), unprocessedSubTaskIndices);

    }

    void ProcessingSubTaskQueueManager::UpdateQueueTimestamp()
    {
        if (HasOwnership() )
        {
            auto current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch() )
                                       .count();
            auto ownership_duration_delta_ms = current_time_ms - m_ownership_last_delta_time_;

            // Update the queue's total time counter
            m_queue_timestamp_ += ownership_duration_delta_ms;

            // Reset ownership acquisition time to now
            m_ownership_last_delta_time_ = current_time_ms;

            // Update the queue's timestamp if it exists
            if ( m_queue )
            {
                m_queue->mutable_processing_queue()->set_last_update_timestamp( m_queue_timestamp_ );
            }
        }
    }

    void ProcessingSubTaskQueueManager::CheckActiveCount()
    {
        auto now = std::chrono::steady_clock::now();
        auto duration = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastActiveCountCheck
        ).count());
        auto activeNodeCount = m_queueChannel->GetActiveNodesCount();

        if (activeNodeCount > 1 ||
            (m_queue->processing_queue().ownership_requests_size() > 0))
        {
            // Limit processing to just one subtask
            m_maxSubtasksPerOwnership = m_defaultMaxSubtasksPerOwnership;
            m_waitTimeBeforeReset = 100; // Short wait if other nodes are active
            m_initialDelayPassed = true; // Consider initial delay passed when other nodes appear
        }
        else
        {
            // Check if enough time has passed to reset
            if (duration > m_waitTimeBeforeReset)
            {
                // Reset processed subtasks and prepare for more processing
                m_processedSubtasksInCurrentOwnership = 0;
                m_maxSubtasksPerOwnership = m_defaultMaxSubtasksPerOwnership;

                // After initial delay, use shorter wait time
                if (!m_initialDelayPassed) {
                    m_waitTimeBeforeReset = 100; // Switch to shorter delay after initial wait
                    m_initialDelayPassed = true;
                }
            }
        }
        // Update last check time
        m_lastActiveCountCheck = now;
    }

    uint64_t ProcessingSubTaskQueueManager::GetCurrentQueueTimestamp()
    {
        // Update and return the current queue timestamp
        UpdateQueueTimestamp();
        return m_queue_timestamp_;
    }

    uint64_t ProcessingSubTaskQueueManager::CalculateGrabSubTaskTimeout() const
    {
        auto lastLockTimestamp = m_processingQueue.GetLastLockTimestamp();  // Queue time base
        auto currentQueueTime = m_queue_timestamp_;  // Queue time base
        auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(m_processingTimeout).count();

        // Calculate when the lock expires in queue time
        uint64_t lockExpirationTime = lastLockTimestamp + timeoutMs;

        // Calculate time until expiration in queue time
        uint64_t grabSubTaskTimeoutMs = 1;
        if (lockExpirationTime > currentQueueTime) {
            grabSubTaskTimeoutMs = lockExpirationTime - currentQueueTime;
            // Cap at a reasonable maximum (e.g., 15 seconds)
            grabSubTaskTimeoutMs = std::min(grabSubTaskTimeoutMs, static_cast<uint64_t>(m_waitTimeBeforeReset));
        }

        m_logger->trace("calculated GRAB_TIMEOUT {}ms", grabSubTaskTimeoutMs);
        return grabSubTaskTimeoutMs;
    }

    bool ProcessingSubTaskQueueManager::UpdateUnprocessedSubTaskIndices(
        const SGProcessing::SubTaskQueue* queue,
        std::vector<int>& unprocessedSubTaskIndices) const
    {
        unprocessedSubTaskIndices.clear();

        for (int subTaskIdx = 0; subTaskIdx < queue->subtasks().items_size(); ++subTaskIdx)
        {
            const auto &subTaskId = queue->subtasks().items(subTaskIdx).subtaskid();
            const auto &processingItem = queue->processing_queue().items(subTaskIdx);

            // Check if subtask is not processed
            bool isUnprocessed = m_processedSubTaskIds.find(subTaskId) == m_processedSubTaskIds.end();

            // Check if subtask is not locked or its lock has expired
            bool isUnlocked = processingItem.lock_node_id().empty();
            bool isLockExpired = false;

            if (!isUnlocked) {
                // Lock has expired if current queue timestamp is greater than or equal to lock timestamp
                isLockExpired = m_queue_timestamp_ >= processingItem.lock_timestamp();
            }

            // Subtask is available if it's not processed AND (not locked OR lock has expired)
            if (isUnprocessed && (isUnlocked || isLockExpired))
            {
                unprocessedSubTaskIndices.push_back(subTaskIdx);
            }
        }

        return !unprocessedSubTaskIndices.empty();
    }

}
