/**
* Header file for subtask queue accessor implementation
* @author creativeid00
*/

#ifndef SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_IMPL_HPP
#define SUPERGENIUS_PROCESSING_SUBTASK_QUEUE_ACCESSOR_IMPL_HPP

#include "networkregistry/NetworkMembershipFilter.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_subtask_result_storage.hpp"
#include "processing/processing_validation_core.hpp"

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <boost/asio.hpp>

// Forward declaration for bitswap
namespace sgns::ipfs_bitswap
{
    class Bitswap;
}

namespace sgns::processing
{
    /**
     * @brief Subtask queue accessor implementation.
     */
    class SubTaskQueueAccessorImpl : public SubTaskQueueAccessor,
                                     public std::enable_shared_from_this<SubTaskQueueAccessorImpl>
    {
    public:
        /** Creates subtask queue accessor implementation object.
        * @param gossipPubSub PubSub host used to subscribe to result channel.
        * @param subTaskQueueManager In-memory queue manager.
        * @param subTaskResultStorage Processing results storage.
        * @param taskResultProcessingSink Callback invoked when task processing completes.
        * @param processingErrorSink Callback invoked on processing errors.
        */
        SubTaskQueueAccessorImpl( std::shared_ptr<ipfs_pubsub::GossipPubSub>              gossipPubSub,
                                  std::shared_ptr<ProcessingSubTaskQueueManager>          subTaskQueueManager,
                                  std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
                                  std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
                                  std::function<void( const std::string & )>              processingErrorSink );
        ~SubTaskQueueAccessorImpl() override;

        /** SubTaskQueueAccessor overrides
    */
        bool ConnectToSubTaskQueue( std::function<void()> onSubTaskQueueConnectedEventSink ) override;
        bool AssignSubTasks( std::list<SGProcessing::SubTask> &subTasks ) override;
        void GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback ) override;
        void CompleteSubTask( const std::string &subTaskId, const SGProcessing::SubTaskResult &subTaskResult ) override;
        bool CreateResultsChannel( const std::string &task_id ) override;

        /// @brief Set callback invoked when a mirrored result arrives. The callback receives the ipfs_results_data_id string.
        void setMirrorResultCallback( std::function<void( const std::string & )> callback );

        /// @brief Set bitswap instance for data availability checks on IPFS results.
        void setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap );

        /// @brief Set membership filter gating the results channel (empty = public
        ///        pass-through; non-member senders are dropped before any result handling).
        void SetMembershipFilter( sgns::networkregistry::MembershipFilter filter );

        /** Returns available results of subtask queue
    * @return a vector of subtask id->results pairs
    */
        std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> GetResults() const;

        enum class FinalizationRetVal
        {
            NOT_FINALIZED           = 0,
            FINALIZED               = 1,
            FINALIZED_BUT_NOT_OWNER = 2,
        };

    private:
        bool               OnResultReceived( SGProcessing::SubTaskResult &&subTaskResult );
        void               OnSubTaskQueueAssigned( const std::vector<std::string> &subTaskIds,
                                                   std::function<void()>           onSubTaskQueueConnectedEventSink );
        void               UpdateResultsFromStorage( const std::set<std::string> &subTaskIds );
        FinalizationRetVal FinalizeQueueProcessing( const SGProcessing::SubTaskCollection &subTasks,
                                                    std::set<std::string>                 &invalidSubTaskIds );

        static void OnResultChannelMessage( std::weak_ptr<SubTaskQueueAccessorImpl>                     weakThis,
                                            boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        void        StartPeriodicStateBroadcast();
        void        ScheduleStateBroadcast();
        void        PublishExistingResults();
        // Helper method to find a subtask by ID
        boost::optional<SGProcessing::SubTask> FindSubTaskById( const std::string &subTaskId ) const;
        /// @brief Validate result output scheme (9a) and optionally check IPFS data availability (9b).
        /// @param requireAvailable If true, also verifies data is locally fetchable via bitswap.
        /// @return true if the result passes all checks.
        bool ValidateResultData( const SGProcessing::SubTaskResult &result, bool requireAvailable ) const;

        std::shared_ptr<ipfs_pubsub::GossipPubSub>              m_gossipPubSub;
        std::shared_ptr<ProcessingSubTaskQueueManager>          m_subTaskQueueManager;
        std::shared_ptr<SubTaskResultStorage>                   m_subTaskResultStorage;
        std::function<void( const SGProcessing::TaskResult & )> m_taskResultProcessingSink;
        std::function<void( const std::string & )>              m_processingErrorSink;
        std::shared_ptr<boost::asio::io_context>                m_localContext;
        using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
        std::optional<WorkGuard>                   m_localWorkGuard;
        std::thread                                m_localThread;
        std::shared_ptr<boost::asio::steady_timer> m_stateTimer;

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_resultChannel;

        std::function<void( const std::string & )> m_mirrorResultCallback; ///< Invoked when a mirrored result arrives.
        mutable std::mutex                         m_mutexMirrorCallback;

        sgns::networkregistry::MembershipFilter m_membershipFilter;    ///< Membership gate for results-channel messages (empty = public).
        mutable std::mutex                      m_mutexMembershipFilter; ///< Guards m_membershipFilter (setters vs pubsub callback threads).

        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> m_bitswap; ///< For data availability checks on IPFS results.

        mutable std::mutex                                 m_mutexResults;
        std::map<std::string, SGProcessing::SubTaskResult> m_results;
        ProcessingValidationCore                           m_validationCore;

        base::Logger m_logger = base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
    };
}

#endif
