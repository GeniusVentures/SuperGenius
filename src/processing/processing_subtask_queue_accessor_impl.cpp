#include <fmt/std.h>
#include "processing_subtask_queue_accessor_impl.hpp"
#include <thread>
#include <utility>

namespace sgns::processing
{
    SubTaskQueueAccessorImpl::SubTaskQueueAccessorImpl(
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        gossipPubSub,
        std::shared_ptr<ProcessingSubTaskQueueManager>          subTaskQueueManager,
        std::shared_ptr<SubTaskStateStorage>                    subTaskStateStorage,
        std::shared_ptr<SubTaskResultStorage>                   subTaskResultStorage,
        std::function<void( const SGProcessing::TaskResult & )> taskResultProcessingSink,
        std::function<void( const std::string & )>              processingErrorSink ) :
        m_gossipPubSub( std::move( gossipPubSub ) ),
        m_subTaskQueueManager( std::move( subTaskQueueManager ) ),
        m_subTaskStateStorage( std::move( subTaskStateStorage ) ),
        m_subTaskResultStorage( std::move( subTaskResultStorage ) ),
        m_taskResultProcessingSink( std::move( taskResultProcessingSink ) ),
        m_processingErrorSink( std::move( processingErrorSink ) )
    {
        // @todo replace hardcoded channel identified with an input value
        m_logger->debug( "[CREATED] this: {}, thread_id {}",
                         reinterpret_cast<size_t>( this ),
                         std::this_thread::get_id() );
    }

    SubTaskQueueAccessorImpl::~SubTaskQueueAccessorImpl()
    {
        m_logger->debug( "[RELEASED] this: {}, thread_id {}",
                         reinterpret_cast<size_t>( this ),
                         std::this_thread::get_id() );
    }

    bool SubTaskQueueAccessorImpl::CreateResultsChannel( const std::string &task_id )
    {
        bool ret = false;
        if ( !m_resultChannel )
        {
            m_resultChannel = std::make_shared<ipfs_pubsub::GossipPubSubTopic>( m_gossipPubSub,
                                                                                "RESULT_CHANNEL_ID_" + task_id );
            m_logger->debug( "Results channel created with \"RESULT_CHANNEL_ID_{}\"", task_id );
            ret = true;
        }
        else
        {
            m_logger->error( "Tried creating channel with \"RESULT_CHANNEL_ID_{}\" but channel already created",
                             task_id );
        }
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
        for ( const auto &subTask : subTasks )
        {
            m_subTaskStateStorage->ChangeSubTaskState( subTask.subtaskid(), SGProcessing::SubTaskState::ENQUEUED );
        }
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
        m_gossipPubSub->GetAsioContext()->post( [onSubTaskQueueConnectedEventSink]()
                                                { onSubTaskQueueConnectedEventSink(); } );
    }

    void SubTaskQueueAccessorImpl::GrabSubTask( SubTaskGrabbedCallback onSubTaskGrabbedCallback )
    {
        std::lock_guard<std::mutex> guard( m_mutexResults );
        auto                        queue = m_subTaskQueueManager->GetQueueSnapshot();

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
            auto                  finalized_ret = FinalizeQueueProcessing( queue->subtasks(), invalidSubTaskIds );
            if ( finalized_ret == FinalizationRetVal::NOT_FINALIZED )
            {
                m_subTaskQueueManager->ChangeSubTaskProcessingStates( processedSubTaskIds, false );
                isFullyProcessed = false;
            }
        }

        // no need to try to keep grabbing
        if ( !isFullyProcessed )
        {
            m_subTaskQueueManager->GrabSubTask( onSubTaskGrabbedCallback );
        }
    }

    void SubTaskQueueAccessorImpl::CompleteSubTask( const std::string                 &subTaskId,
                                                    const SGProcessing::SubTaskResult &subTaskResult )
    {
        m_subTaskResultStorage->AddSubTaskResult( subTaskResult );
        m_subTaskStateStorage->ChangeSubTaskState( subTaskId, SGProcessing::SubTaskState::PROCESSED );
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
        bool should_have_finalized = false;
        if ( !m_subTaskQueueManager->IsQueueInit() )
        {
            return should_have_finalized;
        }
        std::string subTaskId = subTaskResult.subtaskid();

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
        bool valid = m_validationCore.ValidateResults( subTasks, m_results, invalidSubTaskIds );

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

        bool rebroadcast_results = false;

        if ( message )
        {
            SGProcessing::SubTaskResult result;
            if ( result.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                _this->m_logger->debug( "[RESULT_RECEIVED]. ({}).", result.subtaskid() );

                rebroadcast_results = _this->OnResultReceived( std::move( result ) );

                if ( rebroadcast_results )
                {
                    _this->m_resultChannel->Publish( message->data);
                }
            }
        }
    }
}
