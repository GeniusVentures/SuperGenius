#include "processing_subtask_queue_accessor_impl.hpp"
#include <fmt/std.h>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include "base/sgns_version.hpp"
#include <libp2p/multi/content_identifier_codec.hpp>
#include <bitswap.hpp>

namespace sgns::processing
{
    SubTaskQueueAccessorImpl::SubTaskQueueAccessorImpl(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
        std::shared_ptr<ProcessingSubTaskQueueManager>          subTaskQueueManager,
        std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
        std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
        std::function<void( const std::string & )>              processingErrorSink ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_subTaskQueueManager( std::move( subTaskQueueManager ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_taskResultProcessingSink( std::move( taskResultProcessingSink ) ),
        m_processingErrorSink( std::move( processingErrorSink ) )
    {
        m_localContext = std::make_shared<boost::asio::io_context>();
        m_localWorkGuard.emplace( m_localContext->get_executor() );
        m_localThread = std::thread( [ctx = m_localContext]() { ctx->run(); } );
        // @todo replace hardcoded channel identified with an input value
        m_logger->debug( "[CREATED] this: {}, thread_id {}",
                         reinterpret_cast<size_t>( this ),
                         std::this_thread::get_id() );
    }

    SubTaskQueueAccessorImpl::~SubTaskQueueAccessorImpl()
    {
        if ( m_stateTimer )
        {
            boost::system::error_code ec;
            m_stateTimer->cancel( ec );
            m_stateTimer.reset();
        }
        if ( m_localContext )
        {
            m_localContext->stop();
        }
        if ( m_localWorkGuard )
        {
            m_localWorkGuard->reset();
        }
        if ( m_localThread.joinable() )
        {
            // Avoid joining from the same thread (would throw/terminate)
            if ( std::this_thread::get_id() == m_localThread.get_id() )
            {
                m_localThread.detach();
            }
            else
            {
                m_localThread.join();
            }
        }
        m_logger->debug( "[RELEASED] this: {}, thread_id {}",
                         reinterpret_cast<size_t>( this ),
                         std::this_thread::get_id() );
    }

    void SubTaskQueueAccessorImpl::setMirrorResultCallback( std::function<void( const std::string & )> callback )
    {
        std::lock_guard<std::mutex> guard( m_mutexMirrorCallback );
        m_mirrorResultCallback = std::move( callback );
    }

    void SubTaskQueueAccessorImpl::setBitswap( std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap )
    {
        m_bitswap = std::move( bitswap );
    }

    void SubTaskQueueAccessorImpl::SetMembershipFilter( sgns::networkregistry::MembershipFilter filter )
    {
        std::lock_guard<std::mutex> guard( m_mutexMembershipFilter );
        m_membershipFilter = std::move( filter );
    }

    bool SubTaskQueueAccessorImpl::CreateResultsChannel( const std::string &task_id )
    {
        bool ret           = false;
        auto results_topic = "RESULT_CHANNEL_ID_" + task_id + sgns::version::GetNetAndVersionAppendix();
        if ( !m_resultChannel )
        {
            m_resultChannel = std::make_shared<ipfs_pubsub::GossipPubSubTopic>( m_gossipPubSub, results_topic );
            m_logger->debug( "Results channel created with {}", results_topic );
            ret = true;
        }
        else
        {
            m_logger->error( "Tried creating channel with {} but channel already created", results_topic );
        }
        StartPeriodicStateBroadcast();
        return ret;
    }

    bool SubTaskQueueAccessorImpl::ConnectToSubTaskQueue( std::function<void()> onSubTaskQueueConnectedEventSink )
    {
        bool ret = false;
        m_subTaskQueueManager->SetSubTaskQueueAssignmentEventSink(
            [weakptr = weak_from_this(), onSubTaskQueueConnectedEventSink]( const std::vector<std::string> &subTaskIds )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->OnSubTaskQueueAssigned( subTaskIds, onSubTaskQueueConnectedEventSink );
                }
            } );

        // It cannot be called in class constructor because shared_from_this doesn't work for the case
        // The weak_from_this() is required to prevent a case when the message processing callback
        // is called using an invalid 'this' pointer to destroyed object

        if ( m_resultChannel )
        {
            m_resultChannel->Subscribe( std::bind( &SubTaskQueueAccessorImpl::OnResultChannelMessage,
                                                   weak_from_this(),
                                                   std::placeholders::_1 ) );
            m_logger->debug( "Subscribed OnResultChannelMessage callback to Results Channel" );
            ret = true;
        }
        else
        {
            m_logger->error( "Attempting to subscribe OnResultChannelMessage to missing Results Channel " );
        }

        return ret;
    }

    bool SubTaskQueueAccessorImpl::AssignSubTasks( std::list<SGProcessing::SubTask> &subTasks )
    {
        return m_subTaskQueueManager->CreateQueue( subTasks );
    }

    void SubTaskQueueAccessorImpl::UpdateResultsFromStorage( const std::set<std::string> &subTaskIds )
    {
        auto results = m_subTaskResultStorage->GetSubTaskResults( subTaskIds );

        m_logger->debug( "[RESULTS_LOADED] {} results loaded from results storage", results.size() );

        if ( !results.empty() )
        {
            for ( auto &result : results )
            {
                const auto &subTaskId = result.subtaskid();
                if ( subTaskIds.find( subTaskId ) != subTaskIds.end() )
                {
                    m_results.emplace( subTaskId, std::move( result ) );
                }
                else
                {
                    m_logger->error( "INVALID_RESULT_FOUND subtaskid: '{}'", subTaskId );
                    m_processingErrorSink( "INVALID_RESULT_FOUND for subtasks" );
                }
            }
        }
    }

    void SubTaskQueueAccessorImpl::OnSubTaskQueueAssigned( const std::vector<std::string> &subTaskIds,
                                                           std::function<void()> onSubTaskQueueConnectedEventSink )
    {
        // @todo Consider possibility to use the received subTaskIds instead of m_subTaskQueueManager->GetQueueSnapshot() call
        // Call it asynchronously to prevent multiple mutex locks
        m_localContext->post( [onSubTaskQueueConnectedEventSink]() { onSubTaskQueueConnectedEventSink(); } );
    }

    void SubTaskQueueAccessorImpl::GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback )
    {
        std::lock_guard<std::mutex> guard( m_mutexResults );
        auto                        queue = m_subTaskQueueManager->GetQueueSnapshot();
        auto                        finalization_ret = FinalizationRetVal::NOT_FINALIZED;

        std::set<std::string> subTaskIds;
        for ( size_t itemIdx = 0; itemIdx < static_cast<size_t>( queue->subtasks().items_size() ); ++itemIdx )
        {
            subTaskIds.insert( queue->subtasks().items( itemIdx ).subtaskid() );
        }

        UpdateResultsFromStorage( subTaskIds );

        std::set<std::string> processedSubTaskIds;
        for ( const auto &[subTaskId, result] : m_results )
        {
            processedSubTaskIds.insert( subTaskId );
        }

        m_subTaskQueueManager->ChangeSubTaskProcessingStates( processedSubTaskIds, true );
        auto isFullyProcessed = m_subTaskQueueManager->IsProcessed();
        if ( isFullyProcessed )
        {
            std::set<std::string> invalidSubTaskIds;
            finalization_ret = FinalizeQueueProcessing( queue->subtasks(), invalidSubTaskIds );
            if ( finalization_ret == FinalizationRetVal::NOT_FINALIZED )
            {
                m_subTaskQueueManager->ChangeSubTaskProcessingStates( processedSubTaskIds, false );
                isFullyProcessed = false;
            }
        }

        // no need to try to keep grabbing
        if ( !isFullyProcessed )
        {
            m_subTaskQueueManager->GrabSubTask( onSubTaskGrabbedCallback );
            return;
        }

        if ( finalization_ret == FinalizationRetVal::FINALIZED_BUT_NOT_OWNER )
        {
            // The owner finalized using the received results; signal completion so this worker can shut down cleanly.
            onSubTaskGrabbedCallback( boost::none );
        }
    }

    void SubTaskQueueAccessorImpl::CompleteSubTask( const std::string                 &subTaskId,
                                                    const SGProcessing::SubTaskResult &subTaskResult )
    {
        m_logger->info( "CompleteSubTask called with subtask {}", subTaskResult.subtaskid() );
        // Find the corresponding subtask
        auto maybeSubTask = FindSubTaskById( subTaskId );
        if ( !maybeSubTask )
        {
            m_logger->error( "Cannot find subtask {} for validation", subTaskId );
            m_processingErrorSink( "Cannot find subtask for validation: " + subTaskId );
            return;
        }

        // Validate before storing
        if ( auto validation_res = m_validationCore.ValidateIndividualResult( maybeSubTask.value(), subTaskResult );
             validation_res.has_error() )
        {
            m_logger->error( "Invalid result for subtask {}: {}, not storing",
                             subTaskId,
                             validation_res.error().message() );
            m_processingErrorSink( "Invalid result for subtask: " + subTaskId );
            return;
        }

        // 9a: Validate output scheme matches (ipfs:// results must have ipfs:// CIDs)
        if ( !ValidateResultData( subTaskResult, false ) )
        {
            m_logger->error( "Result for subtask {} failed output scheme validation", subTaskId );
            m_processingErrorSink( "Output scheme mismatch for subtask: " + subTaskId );
            return;
        }

        m_subTaskResultStorage->AddSubTaskResult( subTaskResult );
        // tell local queue manager we completed this task as well.
        m_subTaskQueueManager->ChangeSubTaskProcessingStates( { subTaskId }, true );

        if ( m_resultChannel )
        {
            m_resultChannel->Publish( subTaskResult.SerializeAsString() );

            m_logger->debug( "Published SubTask results to Results Channel" );
        }
        else
        {
            m_logger->error( "Attempting to publish results to missing Results Channel " );
        }
    }

    bool SubTaskQueueAccessorImpl::OnResultReceived( SGProcessing::SubTaskResult &&subTaskResult )
    {
        m_logger->info( "OnResultReceived called with subtask {} {}",
                        reinterpret_cast<size_t>( this ),
                        subTaskResult.subtaskid() );
        bool should_have_finalized = false;
        if ( !m_subTaskQueueManager->IsQueueInit() )
        {
            return should_have_finalized;
        }
        std::string subTaskId = subTaskResult.subtaskid();

        auto maybeSubTask = FindSubTaskById( subTaskResult.subtaskid() );
        if ( !maybeSubTask )
        {
            m_logger->error( "Cannot find subtask {} for validation", subTaskResult.subtaskid() );
            m_processingErrorSink( "Cannot find subtask for validation: " + subTaskResult.subtaskid() );
            return false;
        }

        if ( auto validation_res = m_validationCore.ValidateIndividualResult( maybeSubTask.value(), subTaskResult );
             validation_res.has_error() )
        {
            m_logger->error( "Rejecting invalid external result for subtask {}: {}",
                             subTaskResult.subtaskid(),
                             validation_res.error().message() );
            m_processingErrorSink( "Invalid external result for subtask: " + subTaskResult.subtaskid() );
            return false;
        }

        // 9a+9b: Validate output scheme and data availability for external results
        if ( !ValidateResultData( subTaskResult, true ) )
        {
            m_logger->warn( "Rejecting external result for subtask {} — data not available or scheme mismatch",
                            subTaskResult.subtaskid() );
            return false;
        }

        // Results accumulation
        std::lock_guard<std::mutex> guard( m_mutexResults );
        m_results.emplace( subTaskId, std::move( subTaskResult ) );

        m_subTaskQueueManager->ChangeSubTaskProcessingStates( { subTaskId }, true );

        // Task processing finished
        if ( m_subTaskQueueManager->IsProcessed() )
        {
            std::set<std::string> invalidSubTaskIds;
            auto                  queue = m_subTaskQueueManager->GetQueueSnapshot();

            auto finalized_ret = FinalizeQueueProcessing( queue->subtasks(), invalidSubTaskIds );
            if ( finalized_ret == FinalizationRetVal::NOT_FINALIZED )
            {
                m_subTaskQueueManager->ChangeSubTaskProcessingStates( invalidSubTaskIds, false );
            }
            else if ( finalized_ret == FinalizationRetVal::FINALIZED_BUT_NOT_OWNER )
            {
                should_have_finalized = true;
            }
        }
        return should_have_finalized;
    }

    SubTaskQueueAccessorImpl::FinalizationRetVal SubTaskQueueAccessorImpl::FinalizeQueueProcessing(
        const SGProcessing::SubTaskCollection &subTasks,
        std::set<std::string>                 &invalidSubTaskIds )
    {
        auto validate_res = m_validationCore.ValidateResults( subTasks, m_results, invalidSubTaskIds );
        bool valid        = !validate_res.has_error();

        FinalizationRetVal finalization_ret = FinalizationRetVal::NOT_FINALIZED;
        m_logger->debug( "RESULTS_VALIDATED: {}", valid ? "VALID" : "INVALID" );
        if ( valid )
        {
            // @todo Add a test where the owner disconnected, but the last valid result is received by slave nodes
            // @todo Request the ownership instead of just checking
            if ( m_subTaskQueueManager->HasOwnership() )
            {
                SGProcessing::TaskResult taskResult;
                auto                     results = taskResult.mutable_subtask_results();
                for ( const auto &r : m_results )
                {
                    auto result = results->Add();
                    result->CopyFrom( r.second );
                }
                m_taskResultProcessingSink( taskResult );
                finalization_ret = FinalizationRetVal::FINALIZED;
            }
            else
            {
                m_logger->debug( "NOT_THE_OWNER: Can't finalize if not the owner" );
                finalization_ret = FinalizationRetVal::FINALIZED_BUT_NOT_OWNER;

                // @todo Process task finalization expiration
            }
        }
        else
        {
            for ( const auto &subTaskId : invalidSubTaskIds )
            {
                m_results.erase( subTaskId );
            }
            m_processingErrorSink( "Invalid results for the entire task" );
        }
        return finalization_ret;
    }

    std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> SubTaskQueueAccessorImpl::GetResults() const
    {
        std::lock_guard<std::mutex>                                       guard( m_mutexResults );
        std::vector<std::tuple<std::string, SGProcessing::SubTaskResult>> results;
        results.reserve( m_results.size() );
        for ( auto &item : m_results )
        {
            results.emplace_back( item.first, item.second );
        }
        std::sort( results.begin(),
                   results.end(),
                   []( const std::tuple<std::string, SGProcessing::SubTaskResult> &v1,
                       const std::tuple<std::string, SGProcessing::SubTaskResult> &v2 )
                   { return std::get<0>( v1 ) < std::get<0>( v2 ); } );

        return results;
    }

    void SubTaskQueueAccessorImpl::OnResultChannelMessage(
        std::weak_ptr<SubTaskQueueAccessorImpl>                           weakThis,
        boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message )
    {
        auto _this = weakThis.lock();
        if ( !_this )
        {
            return;
        }

        // Membership gate (15-13): drop messages whose transport sender is not an
        // authorized member BEFORE any result/mirror handling. Empty filter = public
        // pass-through; empty/malformed `from` is denied under a set filter (fail-closed).
        if ( message )
        {
            sgns::networkregistry::MembershipFilter membershipFilter;
            {
                std::lock_guard<std::mutex> guard( _this->m_mutexMembershipFilter );
                membershipFilter = _this->m_membershipFilter;
            }
            if ( !sgns::networkregistry::AuthorizeGossipSender( membershipFilter, message->from ) )
            {
                _this->m_logger->debug( "Results channel message from unauthorized sender ignored" );
                return;
            }
        }

        bool rebroadcast_results = false;

        if ( message )
        {
            SGProcessing::SubTaskResult result;
            if ( result.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                _this->m_logger->debug( "[RESULT_RECEIVED]. ({}).", result.subtaskid() );

                // If this node mirrors results and the result has IPFS data, trigger mirror fetch.
                std::function<void( const std::string & )> mirrorResultCallback;
                {
                    std::lock_guard<std::mutex> guard( _this->m_mutexMirrorCallback );
                    mirrorResultCallback = _this->m_mirrorResultCallback;
                }

                if ( mirrorResultCallback && !result.ipfs_results_data_id().empty() )
                {
                    mirrorResultCallback( result.ipfs_results_data_id() );
                }

                rebroadcast_results = _this->OnResultReceived( std::move( result ) );
            }
        }
    }

    boost::optional<SGProcessing::SubTask> SubTaskQueueAccessorImpl::FindSubTaskById(
        const std::string &subTaskId ) const
    {
        auto queue = m_subTaskQueueManager->GetQueueSnapshot();
        if ( !queue )
        {
            return boost::none;
        }

        const auto &subTasks = queue->subtasks();
        for ( int i = 0; i < subTasks.items_size(); ++i )
        {
            const auto &subTask = subTasks.items( i );
            if ( subTask.subtaskid() == subTaskId )
            {
                return subTask;
            }
        }

        return boost::none;
    }

    void SubTaskQueueAccessorImpl::StartPeriodicStateBroadcast()
    {
        // Every few seconds, if we have ownership and results, broadcast them
        m_stateTimer = std::make_shared<boost::asio::steady_timer>( *m_localContext );
        ScheduleStateBroadcast();
    }

    void SubTaskQueueAccessorImpl::ScheduleStateBroadcast()
    {
        if ( !m_stateTimer )
        {
            return;
        }

        m_stateTimer->expires_from_now( std::chrono::seconds( 2 ) );

        // Capture weak_ptr to prevent use-after-destruction
        std::weak_ptr<SubTaskQueueAccessorImpl> weakSelf = shared_from_this();

        m_stateTimer->async_wait(
            [weakSelf]( const boost::system::error_code &ec )
            {
                if ( ec )
                {
                    return; // Timer was cancelled
                }

                auto self = weakSelf.lock();
                if ( !self )
                {
                    // Object was destroyed, don't execute callback
                    return;
                }

                self->PublishExistingResults();
                self->StartPeriodicStateBroadcast(); // Schedule next
            } );
    }

    void SubTaskQueueAccessorImpl::PublishExistingResults()
    {
        std::lock_guard<std::mutex> guard( m_mutexResults );
        for ( const auto &[subTaskId, result] : m_results )
        {
            if ( m_resultChannel )
            {
                m_resultChannel->Publish( result.SerializeAsString() );
                m_logger->debug( "Published existing result for {}", subTaskId );
            }
        }
        // If I'm the owner and have results, try to finalize
        if ( m_subTaskQueueManager->HasOwnership() && !m_results.empty() )
        {
            if ( m_subTaskQueueManager->IsProcessed() )
            {
                std::set<std::string> invalidSubTaskIds;
                auto                  queue = m_subTaskQueueManager->GetQueueSnapshot();

                auto finalized_ret = FinalizeQueueProcessing( queue->subtasks(), invalidSubTaskIds );
                if ( finalized_ret == FinalizationRetVal::FINALIZED )
                {
                    m_logger->debug( "Successfully finalized during periodic broadcast" );
                    // Stop periodic broadcasting since we're done
                    if ( m_stateTimer )
                    {
                        m_stateTimer->cancel();
                    }
                }
            }
        }
    }

    bool SubTaskQueueAccessorImpl::ValidateResultData( const SGProcessing::SubTaskResult &result,
                                                       bool                               requireAvailable ) const
    {
        static constexpr std::string_view kIpfsUriScheme = "ipfs://";

        const auto &dataId = result.ipfs_results_data_id();
        if ( dataId.empty() )
        {
            return true; // No IPFS data to validate
        }

        // 9a: Scheme validation — every non-empty line must be an ipfs:// URL
        std::istringstream stream( dataId );
        std::string        line;
        while ( std::getline( stream, line ) )
        {
            if ( line.empty() )
            {
                continue;
            }
            if ( line.compare( 0, kIpfsUriScheme.size(), kIpfsUriScheme.data(), kIpfsUriScheme.size() ) != 0 )
            {
                m_logger->error( "Result for subtask {} has non-IPFS output: {}",
                                 result.subtaskid(),
                                 line );
                return false;
            }
        }

        // 9b: Data availability — check that all CIDs are locally fetchable
        if ( requireAvailable && m_bitswap )
        {
            stream.clear();
            stream.seekg( 0 );
            while ( std::getline( stream, line ) )
            {
                if ( line.empty() )
                {
                    continue;
                }
                std::string cidStr = line.substr( kIpfsUriScheme.size() );
                auto        cid    = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
                if ( !cid )
                {
                    m_logger->warn( "Result for subtask {} has unparseable CID: {}",
                                    result.subtaskid(),
                                    cidStr );
                    return false;
                }
                if ( !m_bitswap->HasBlock( cid.value() ) )
                {
                    m_logger->warn( "Result for subtask {} has unavailable data (CID not in store): {}",
                                    result.subtaskid(),
                                    cidStr );
                    return false;
                }
            }
        }

        return true;
    }
}
