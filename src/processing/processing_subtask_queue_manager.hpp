/**
* Header file for the distributed subtasks queue
* @author SuperGenius/Creative00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_MANAGER_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_MANAGER_HPP

#include "processing/processing_subtask_queue.hpp"
#include "processing/processing_subtask_queue_channel.hpp"

#include "processing/proto/SGProcessing.pb.h"

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <list>

namespace sgns::processing
{
    /** Distributed subtask queue implementation
    */
    class ProcessingSubTaskQueueManager : public std::enable_shared_from_this<ProcessingSubTaskQueueManager>
    {
    public:
        using SubTaskGrabbedCallback = std::function<void( boost::optional<const SGProcessing::SubTask &> )>;

        /** Construct an empty queue
        * @param queueChannel - task processing channel
        * @param context - io context to handle timers
        * @param localNodeId local processing node ID
        */
        ProcessingSubTaskQueueManager( std::shared_ptr<ProcessingSubTaskQueueChannel> queueChannel,
                                       std::shared_ptr<boost::asio::io_context>       context,
                                       const std::string                             &localNodeId,
                                       std::function<void( const std::string & )>     processingErrorSink );
        ~ProcessingSubTaskQueueManager();

        /** Set a timeout for subtask processing
        * @param processingTimeout - subtask processing timeout
        * Once the timeout is exceeded the subtask is marked as expired.
        */
        void SetProcessingTimeout( const std::chrono::system_clock::duration &processingTimeout );

        /** Create a subtask queue by splitting the task to subtasks using the processing code
        * @param subTasks - a list of subtasks that should be added to the queue
        * in subtasks to allow a validation
        * @return false if not queue was created due to errors
        */
        bool CreateQueue( std::list<SGProcessing::SubTask> &subTasks );

        /** Asynchronous getting of a subtask from the queue
        * @param onSubTaskGrabbedCallback a callback that is called when a grapped iosubtask is locked by the local node
        */
        void GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback );

        /** Transfer the queue ownership to another processing node
        * @param nodeId - processing node ID that the ownership should be transferred
        */
        bool MoveOwnershipTo( const std::string &nodeId );

        /** Checks id the local processing node owns the queue
        * @return true is the lolca node owns the queue
        */
        bool HasOwnership() const;

        /** Changes the local queue state with respect to passed queue snapshot
        * The method should be called from a processing channel message handler
        * @param queue received queue snapshot
        */
        bool ProcessSubTaskQueueMessage( SGProcessing::SubTaskQueue *queue );

        /** Changes the local queue state with respect to passed queue request
        * The method should be called from a processing channel message handler
        * @param request is a request for the queue ownership transferring
        */
        bool ProcessSubTaskQueueRequestMessage( const SGProcessing::SubTaskQueueRequest &request );

        /** Returns the current local queue snapshot
        * @return the queue snapshot
        */
        std::unique_ptr<SGProcessing::SubTaskQueue> GetQueueSnapshot() const;

        /** Mark a subtask as processed/unprocessed
        * @param subTaskIds - a list of subtask which state should be changed
        * @param isProcessed - new state
        */
        void ChangeSubTaskProcessingStates( const std::set<std::string> &subTaskIds, bool isProcessed );

        /** Check whether queue has been initialized to prevent nullptr access to the queue.
        */
        bool IsQueueInit()
        {
            return m_queue != nullptr;
        }

        /** Checks if all subtask in the queue are processed
        * @return true if the queue is processed
        */
        bool IsProcessed() const;

        /**
         * Sink that gets subtask assignment events
         * @param subTaskQueueAssignmentEventSink * lambda or function handling the event
         */
        void SetSubTaskQueueAssignmentEventSink(
            std::function<void( const std::vector<std::string> & )> subTaskQueueAssignmentEventSink );
        /**
         * Retrieves the current timestamp associated with the queue.
         * @return The current queue timestamp as a 64-bit unsigned integer.
         */
        uint64_t GetCurrentQueueTimestamp();

    private:
        /** Updates the local queue with a snapshot that have the most recent timestamp
        * @param queue - the queue snapshot
        */
        bool UpdateQueue( SGProcessing::SubTaskQueue *queue );

        void HandleQueueRequestTimeout( const boost::system::error_code &ec );
        void PublishSubTaskQueue();
        void ProcessPendingSubTaskGrabbing();
        void GrabSubTasks();
        void HandleGrabSubTaskTimeout( const boost::system::error_code &ec );
        void LogQueue() const;

        /**
         * Check if the queue has any current work to do
         * @return bool if there is work to be done
         */
        bool HasAvailableWork(bool checkOwnershipQuota = true) const;
        /**
         * Updates m_queue_timestamp_ based on current ownership duration
         */
        void UpdateQueueTimestamp();

        void CheckActiveCount();

        /**
         * Calculates the timeout for grabbing a subtask in milliseconds.
         *
         * This method determines the remaining time until the current lock on the subtask
         * expires in the queue's time base. If the lock is already expired, a minimal timeout
         * value is returned. The calculated timeout is capped at a maximum threshold to
         * ensure a reasonable upper limit.
         *
         * @return The timeout duration in milliseconds for grabbing a subtask.
         */
        uint64_t CalculateGrabSubTaskTimeout() const;

        std::shared_ptr<ProcessingSubTaskQueueChannel> m_queueChannel;
        std::shared_ptr<boost::asio::io_context>       m_context;
        std::string                                    m_localNodeId;

        std::shared_ptr<SGProcessing::SubTaskQueue> m_queue;
        mutable std::recursive_mutex                m_queueMutex;
        std::list<SubTaskGrabbedCallback>           m_onSubTaskGrabbedCallbacks;

        std::function<void( const std::vector<std::string> & )> m_subTaskQueueAssignmentEventSink;
        std::set<std::string>                                   m_processedSubTaskIds;

        boost::asio::deadline_timer      m_dltQueueResponseTimeout;
        boost::posix_time::time_duration m_queueResponseTimeout;

        boost::asio::deadline_timer m_dltGrabSubTaskTimeout;

        ProcessingSubTaskQueue                     m_processingQueue;
        std::chrono::system_clock::duration        m_processingTimeout;
        std::function<void( const std::string & )> m_processingErrorSink;

        uint64_t m_queue_timestamp_ =  0;            // Aggregate time counter for the queue
        uint64_t m_ownership_acquired_at_ = 0;      // When this node acquired ownership (in ms)
        uint64_t m_ownership_last_delta_time_ = 0;   // When this node last updated the queue timestamp

        // Add to private section of ProcessingSubTaskQueueManager
        struct OwnershipRequest {
            std::string node_id;
            uint64_t timestamp;  // Timestamp when request was received
        };
        std::queue<OwnershipRequest> m_ownershipRequestQueue;

        base::Logger m_logger = base::createLogger( "ProcessingSubTaskQueueManager" );
        std::chrono::steady_clock::time_point m_lastQueueUpdateTime =  std::chrono::steady_clock::now();

        size_t m_processedSubtasksInCurrentOwnership = 0;
        size_t m_defaultMaxSubtasksPerOwnership = 1;
        size_t m_maxSubtasksPerOwnership;

        std::chrono::steady_clock::time_point m_lastActiveCountCheck = std::chrono::steady_clock::now();
        uint64_t m_waitTimeBeforeReset = 3000; // Initial wait time of 3000ms
        bool m_initialDelayPassed = false;    // Track if initial delay has passed
    };
}

#endif // SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_MANAGER_HPP
