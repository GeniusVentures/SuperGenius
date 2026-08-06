/**
 * @file       Consensus.cpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/Consensus.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <system_error>
#include <boost/format.hpp>

#include <gsl/span>
#include <utility>

#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/ConsensusAuth.hpp"

namespace sgns
{
    namespace
    {
        bool IsPublicChainMintChainId( std::string_view chain_id )
        {
            if ( chain_id.empty() )
            {
                return false;
            }
            if ( chain_id == "public" )
            {
                return true;
            }
            return std::all_of( chain_id.begin(),
                                chain_id.end(),
                                []( unsigned char c ) { return c >= '0' && c <= '9'; } );
        }

        std::optional<base::Hash256> ParseSubjectTypeHash( const ConsensusSubject &subject )
        {
            if ( !subject.has_subject_type_hash() )
            {
                return std::nullopt;
            }
            auto hash = base::Hash256::fromString( subject.subject_type_hash().hash() );
            return hash.has_value() ? std::optional<base::Hash256>{ hash.value() } : std::nullopt;
        }
    }

    base::Logger ConsensusManagerLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "ConsensusManager" );
    }

    std::shared_ptr<ConsensusManager> ConsensusManager::New( std::shared_ptr<ValidatorRegistry>         registry,
                                                             std::shared_ptr<crdt::GlobalDB>            db,
                                                             std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                             Signer                                     signer,
                                                             std::string                                address,
                                                             std::string consensus_topic )
    {
        if ( !registry )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: registry is null", __func__ );
            return nullptr;
        }
        if ( !db )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: db is null", __func__ );
            return nullptr;
        }
        if ( !pubsub )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: pubsub is null", __func__ );
            return nullptr;
        }
        if ( !signer )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: signer is null", __func__ );
            return nullptr;
        }
        if ( address.empty() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: address is empty", __func__ );
            return nullptr;
        }

        auto instance = std::shared_ptr<ConsensusManager>( new ConsensusManager( std::move( registry ),
                                                                                 std::move( db ),
                                                                                 std::move( pubsub ),
                                                                                 std::move( signer ),
                                                                                 std::move( address ),
                                                                                 std::move( consensus_topic ) ) );
        instance->certificate_work_journal_ = instance->db_->GetWorkJournal();

        if ( !instance->certificate_work_journal_ )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: crdt work journal is empty",
                                             __func__ );
            return nullptr;
        }

        instance->consensus_subs_future_ = instance->pubsub_->Subscribe(
            instance->consensus_messages_topic_,
            [weakptr( std::weak_ptr<ConsensusManager>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    ConsensusManagerLogger()->trace( "{}: Received Consensus Message on topic {}",
                                                     __func__,
                                                     self->consensus_messages_topic_ );
                    self->OnConsensusMessage( message );
                }
            } );
        ConsensusManagerLogger()->debug( "{}: Subscribed to Consensus topic {}",
                                         __func__,
                                         instance->consensus_messages_topic_ );
        instance->StartRoundTimer();
        if ( !instance->RegisterCertificateFilter() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to register certificate filter", __func__ );
        }
        instance->RecoverPendingCertificateWork();

        return instance;
    }

    ConsensusManager::ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                        std::shared_ptr<crdt::GlobalDB>            db,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        Signer                                     signer,
                                        std::string                                address,
                                        std::string                                consensus_topic ) :
        registry_( std::move( registry ) ),       //
        db_( std::move( db ) ),                   //
        pubsub_( std::move( pubsub ) ),           //
        signer_( std::move( signer ) ),           //
        account_address_( std::move( address ) ), //
        consensus_messages_topic_( fmt::format( "{}{}{}",
                                                CONSENSUS_CHANNEL_PREFIX,
                                                sgns::version::GetNetAndVersionAppendix(),
                                                consensus_topic ) ),
        consensus_datastore_topic_( consensus_messages_topic_ + "#datastore" )
    {
    }

    ConsensusManager::~ConsensusManager()
    {
        Close();
        ConsensusManagerLogger()->debug( "{}: Finished shutting down ConsensusManager", __func__ );
    }

    void ConsensusManager::Close()
    {
        bool expected = false;
        if ( !close_started_.compare_exchange_strong( expected, true ) )
        {
            return;
        }

        // Account switches reuse GlobalDB. Remove this manager's registrations
        // before a replacement ConsensusManager registers the same pattern.
        // The one-shot guard also prevents a delayed destructor from removing
        // registrations that belong to the replacement manager.
        const std::string pattern = std::string( CERT_KEY_PATTERN );
        if ( db_ )
        {
            if ( certificate_callback_registered_ )
            {
                db_->UnregisterNewElementCallback( pattern );
                certificate_callback_registered_ = false;
            }
            if ( certificate_filter_registered_ )
            {
                db_->UnregisterElementFilter( pattern );
                certificate_filter_registered_ = false;
            }
        }

        stop_timer_.store( true );
        timer_cv_.notify_all();
        if ( round_timer_.joinable() )
        {
            round_timer_.join();
        }
    }

    void ConsensusManager::StartRoundTimer()
    {
        if ( round_timer_.joinable() )
        {
            return;
        }
        if ( stop_timer_.load() )
        {
            return;
        }

        round_timer_ = std::thread(
            [this]()
            {
                constexpr auto MIN_INTERVAL = std::chrono::milliseconds( 500 );
                while ( true )
                {
                    std::unique_lock<std::mutex> lock( timer_mutex_ );
                    auto                         interval = std::max( round_duration_ / 2, MIN_INTERVAL );
                    if ( certificates_pending_.load() )
                    {
                        // Work is pending: run on cadence, only interrupt for shutdown.
                        timer_cv_.wait_for( lock, interval, [this]() { return stop_timer_.load(); } );
                    }
                    else
                    {
                        // No pending work: wait up to interval, but wake immediately when new work appears.
                        timer_cv_.wait_for( lock,
                                            interval,
                                            [this]() { return stop_timer_.load() || certificates_pending_.load(); } );
                    }
                    if ( stop_timer_.load() )
                    {
                        return;
                    }
                    lock.unlock();
                    if ( certificates_pending_.load() )
                    {
                        ProcessCertificates();
                    }
                    ExpirePendingProposals();
                    ProcessDuePendingRetries();
                    // Keep replaying unfinished certificate work while the node is running.
                    RecoverPendingCertificateWork();
                }
            } );
    }

    outcome::result<void> ConsensusManager::Publish( const ConsensusMessage &message )
    {
        std::vector<uint8_t> serialized_proto( message.ByteSizeLong() );
        if ( !message.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: Failed to serialize consensus message", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        ConsensusManagerLogger()->debug( "{}: Sending consensus packet to {}", __func__, consensus_messages_topic_ );
        pubsub_->Publish( consensus_messages_topic_, serialized_proto );
        ConsensusManagerLogger()->debug( "{}: Consensus packet published (bytes={})",
                                         __func__,
                                         serialized_proto.size() );

        return outcome::success();
    }

    bool ConsensusManager::RegisterSubjectHandler( std::string_view subject_type, SubjectHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid handler subject_type={}", __func__, subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering subject handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_[type_hash.value()] = std::move( handler );
        return true;
    }

    void ConsensusManager::UnregisterSubjectHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Subject handler with subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_.erase( type_hash.value() );
    }

    bool ConsensusManager::RegisterCertificateHandler( std::string_view          subject_type,
                                                       CertificateSubjectHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid certificate handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering certificate handler subject_type={}",
                                         __func__,
                                         subject_type );
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_[type_hash.value()] = std::move( handler );
        return true;
    }

    void ConsensusManager::UnregisterCertificateHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Certificate handler with subject_type={}",
                                         __func__,
                                         subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_.erase( type_hash.value() );
    }

    bool ConsensusManager::RegisterProposalCleanupHandler( std::string_view       subject_type,
                                                           ProposalCleanupHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid cleanup handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering cleanup handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( cleanup_handlers_mutex_ );
        proposal_cleanup_handlers_[type_hash.value()].push_back( std::move( handler ) );
        return true;
    }

    void ConsensusManager::UnregisterProposalCleanupHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing cleanup handler with subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( cleanup_handlers_mutex_ );
        proposal_cleanup_handlers_.erase( type_hash.value() );
    }

    void ConsensusManager::RegisterSlotKeyHandler( std::string_view subject_type, SlotKeyHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid slot key handler subject_type={}",
                                             __func__,
                                             subject_type );
            return;
        }
        ConsensusManagerLogger()->debug( "{}: Registering slot key handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( slot_key_handlers_mutex_ );
        slot_key_handlers_[type_hash.value()] = std::move( handler );
    }

    void ConsensusManager::UnregisterSlotKeyHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing slot key handler subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( slot_key_handlers_mutex_ );
        slot_key_handlers_.erase( type_hash.value() );
    }

    void ConsensusManager::FireProposalCleanupCallbacks( const Proposal &proposal )
    {
        auto subject_hash = GetSubjectHash( proposal.subject() );
        if ( subject_hash.has_error() )
        {
            return;
        }
        auto nonce_payload = DecodeNonceSubject( proposal.subject() );
        if ( nonce_payload.has_error() )
        {
            return;
        }
        auto tx_hash = nonce_payload.value().tx_hash();
        if ( tx_hash.empty() )
        {
            return;
        }
        auto type_hash = ParseSubjectTypeHash( proposal.subject() );
        if ( !type_hash )
        {
            return;
        }

        std::vector<ProposalCleanupHandler> handlers_copy;
        {
            std::shared_lock lock( cleanup_handlers_mutex_ );
            auto             it = proposal_cleanup_handlers_.find( type_hash.value() );
            if ( it != proposal_cleanup_handlers_.end() )
            {
                handlers_copy = it->second;
            }
        }
        for ( auto &handler : handlers_copy )
        {
            handler( tx_hash );
        }
    }

    void ConsensusManager::ConfigureCertificateDelay( std::chrono::milliseconds delay )
    {
        if ( delay.count() < 0 )
        {
            ConsensusManagerLogger()->warn( "{}: using zero delay", __func__ );
            certificate_delay_ = std::chrono::milliseconds( 0 );
            return;
        }
        certificate_delay_ = delay;
    }

    bool ConsensusManager::IsTimestampSane( uint64_t timestamp_ms ) const
    {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        if ( timestamp_ms == 0 || now_ms < 0 )
        {
            return false;
        }

        const auto now        = static_cast<uint64_t>( now_ms );
        const auto difference = timestamp_ms > now ? timestamp_ms - now : now - timestamp_ms;
        return difference <= static_cast<uint64_t>( timestamp_window_.count() );
    }

    uint64_t ConsensusManager::GetCurrentRound( uint64_t proposal_ts_ms ) const
    {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        if ( proposal_ts_ms == 0 || now_ms <= 0 )
        {
            return 0;
        }

        const auto now = static_cast<uint64_t>( now_ms );
        if ( proposal_ts_ms >= now )
        {
            return 0;
        }

        const auto elapsed = now - proposal_ts_ms;
        const auto skew    = static_cast<uint64_t>( round_skew_.count() );
        return elapsed > skew ? ( elapsed - skew ) / static_cast<uint64_t>( round_duration_.count() ) : 0;
    }

    ConsensusManager::AggregatorRole ConsensusManager::GetAggregatorRole(
        const Proposal                    &proposal,
        const ValidatorRegistry::Registry &registry ) const
    {
        ConsensusManagerLogger()->trace( "{}: Checking local aggregator role for proposal", __func__ );

        std::vector<std::string> ordered;
        ordered.reserve( registry.validators_size() );
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() == ValidatorRegistry::Status::ACTIVE )
            {
                ordered.push_back( entry.validator_id() );
            }
        }
        std::sort( ordered.begin(), ordered.end() );

        if ( ordered.empty() )
        {
            return AggregatorRole::NotInRegistry;
        }

        if ( std::find( ordered.begin(), ordered.end(), account_address_ ) == ordered.end() )
        {
            return AggregatorRole::NotInRegistry;
        }

        const auto proposal_hash = sgns::crypto::sha2_256( proposal.proposal_id().data(),
                                                           proposal.proposal_id().size() );
        uint64_t   proposal_seed = 0;
        for ( size_t i = 0; i < sizeof( proposal_seed ); ++i )
        {
            proposal_seed = ( proposal_seed << 8 ) | proposal_hash[i];
        }

        const auto validator_count = ordered.size();
        const auto starting_index  = proposal_seed % validator_count;
        const auto round_offset    = GetCurrentRound( proposal.timestamp() ) % validator_count;
        const auto selected_index  = ( starting_index + round_offset ) % validator_count;

        return ordered[selected_index] == account_address_ ? AggregatorRole::CurrentAggregator
                                                           : AggregatorRole::ActiveButNotAggregator;
    }

    outcome::result<std::string> ConsensusManager::GetSubjectHash( const Subject &subject )
    {
        if ( SubjectTypeMatches( subject, NONCE_SUBJECT_TYPE ) )
        {
            auto payload = DecodeNonceSubject( subject );
            if ( payload.has_error() || payload.value().tx_hash().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return payload.value().tx_hash();
        }
        if ( SubjectTypeMatches( subject, TASK_RESULT_SUBJECT_TYPE ) )
        {
            auto payload = DecodeTaskResultSubject( subject );
            if ( payload.has_error() || payload.value().task_result_hash().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return payload.value().task_result_hash();
        }
        if ( SubjectTypeMatches( subject, REGISTRY_BATCH_SUBJECT_TYPE ) )
        {
            auto payload = DecodeRegistryBatchSubject( subject );
            if ( payload.has_error() || payload.value().batch_root().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return std::string( payload.value().batch_root() );
        }
        return ComputeSubjectId( subject );
    }

    void ConsensusManager::ContinueProposalAfterSubject( const Proposal &proposal )
    {
        const auto &proposal_id = proposal.proposal_id();
        const auto  slot_key    = GetSlotKey( proposal );
        bool        should_vote = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto [proposal_it, inserted] = proposals_.try_emplace( proposal_id );
            if ( inserted )
            {
                proposal_it->second.proposal = proposal;
                proposal_it->second.slot_key = slot_key;
            }

            auto      &slot_state       = slot_states_[slot_key];
            auto      &best_proposal_id = slot_state.best_proposal_id;
            const bool is_better        = best_proposal_id.empty() ||
                                          IsBetterProposal( proposal, proposals_.at( best_proposal_id ).proposal );
            if ( is_better )
            {
                best_proposal_id = proposal_id;
            }
            should_vote = best_proposal_id == proposal_id && slot_state.voted_proposal_ids.insert( proposal_id ).second;
        }

        std::vector<Vote> pending_votes;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = pending_votes_.find( proposal_id );
            if ( it != pending_votes_.end() )
            {
                pending_votes = std::move( it->second );
                pending_votes_.erase( it );
            }
        }
        for ( const auto &vote : pending_votes )
        {
            HandleVote( vote );
        }

        if ( should_vote )
        {
            auto vote_result = CreateVote( proposal_id, account_address_, true, signer_ );
            if ( vote_result.has_value() )
            {
                (void) SubmitVote( vote_result.value() );
            }
            else
            {
                ConsensusManagerLogger()->error( "{}: self-vote failed for hash {}, id={} error={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal_id.substr( 0, 8 ),
                                                 vote_result.error().message() );
            }
        }
    }

    bool ConsensusManager::AddPendingProposal( const Proposal                       &proposal,
                                               const std::string                    &subject_hash,
                                               const ValidationResult               &validation_result,
                                               std::size_t                           scheduled_retry_count,
                                               std::chrono::steady_clock::time_point last_retry_at )
    {
        std::lock_guard lock( proposals_mutex_ );
        if ( pending_entries_.find( proposal.proposal_id() ) != pending_entries_.end() )
        {
            RemovePendingProposalLocked( proposal.proposal_id(), "replace" );
        }
        auto [proposal_it, inserted] = proposals_.try_emplace( proposal.proposal_id() );
        if ( inserted )
        {
            proposal_it->second.proposal = proposal;
            proposal_it->second.slot_key = GetSlotKey( proposal );
        }

        const auto  retained_bytes = static_cast<std::size_t>( proposal.ByteSizeLong() );
        const auto &proposer_id    = proposal.proposer_id();
        if ( pending_entries_.size() >= pending_config_.max_pending_proposals )
        {
            ConsensusManagerLogger()->warn( "{}: pending admission refused: global limit reached proposal_id={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }
        auto proposer_it = pending_count_by_proposer_.find( proposer_id );
        if ( proposer_it != pending_count_by_proposer_.end() &&
             proposer_it->second >= pending_config_.max_pending_per_proposer )
        {
            ConsensusManagerLogger()->warn(
                "{}: pending admission refused: proposer limit reached proposer={} proposal_id={}",
                __func__,
                proposer_id.substr( 0, 8 ),
                proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }
        if ( pending_retained_bytes_ + retained_bytes > pending_config_.max_retained_pending_bytes )
        {
            ConsensusManagerLogger()->warn( "{}: pending admission refused: retained byte limit reached proposal_id={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }

        auto dependencies = validation_result.dependencies;
        if ( dependencies.empty() )
        {
            dependencies.emplace_back( PendingDependencyKey::Certificate( subject_hash ) );
        }

        ConsensusManagerLogger()->debug( "{}: Adding pending proposal for {}: proposal with id {}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        const auto now         = std::chrono::steady_clock::now();
        auto       retry_delay = validation_result.retry_after.value_or( std::chrono::seconds( 10 ) );
        if ( !validation_result.retry_after && !pending_config_.scheduled_retry_delays.empty() )
        {
            const auto index = std::min( scheduled_retry_count, pending_config_.scheduled_retry_delays.size() - 1 );
            retry_delay      = pending_config_.scheduled_retry_delays[index];
        }

        PendingProposalEntry entry;
        entry.proposal              = proposal;
        entry.dependencies          = std::move( dependencies );
        entry.admitted_at           = now;
        entry.expires_at            = now + pending_config_.pending_ttl;
        entry.last_retry_at         = last_retry_at;
        entry.retry_after           = validation_result.retry_after;
        entry.retained_bytes        = retained_bytes;
        entry.proposer_id           = proposer_id;
        entry.scheduled_retry_count = scheduled_retry_count;
        entry.next_retry_at         = now + retry_delay;

        pending_retained_bytes_                 += retained_bytes;
        pending_count_by_proposer_[proposer_id] += 1;
        for ( const auto &dependency : entry.dependencies )
        {
            pending_by_dependency_[dependency].insert( proposal.proposal_id() );
        }
        pending_entries_.emplace( proposal.proposal_id(), std::move( entry ) );
        timer_cv_.notify_all();
        return true;
    }

    std::vector<ConsensusManager::Proposal> ConsensusManager::TakePendingProposals( const std::string &subject_hash )
    {
        std::vector<Proposal> result;
        std::lock_guard       lock( proposals_mutex_ );
        const auto            dependency = PendingDependencyKey::Certificate( subject_hash );
        auto                  it         = pending_by_dependency_.find( dependency );
        if ( it == pending_by_dependency_.end() )
        {
            ConsensusManagerLogger()->trace( "{}: No pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
            return result;
        }
        auto proposal_ids = std::move( it->second );
        pending_by_dependency_.erase( it );
        for ( const auto &proposal_id : proposal_ids )
        {
            auto prop_it = pending_entries_.find( proposal_id );
            if ( prop_it != pending_entries_.end() )
            {
                result.push_back( prop_it->second.proposal );
                RemovePendingProposalLocked( proposal_id, "take" );
            }
        }
        ConsensusManagerLogger()->debug( "{}: Taking pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
        return result;
    }

    bool ConsensusManager::RemovePendingProposal( const std::string &proposal_id, std::string_view reason )
    {
        std::lock_guard lock( proposals_mutex_ );
        return RemovePendingProposalLocked( proposal_id, reason );
    }

    bool ConsensusManager::RemovePendingProposalLocked( const std::string &proposal_id, std::string_view reason )
    {
        pending_votes_.erase( proposal_id );

        auto entry_it = pending_entries_.find( proposal_id );
        if ( entry_it == pending_entries_.end() )
        {
            return false;
        }

        const auto &entry        = entry_it->second;
        pending_retained_bytes_ -= std::min( pending_retained_bytes_, entry.retained_bytes );

        auto proposer_it = pending_count_by_proposer_.find( entry.proposer_id );
        if ( proposer_it != pending_count_by_proposer_.end() )
        {
            if ( proposer_it->second > 1 )
            {
                --proposer_it->second;
            }
            else
            {
                pending_count_by_proposer_.erase( proposer_it );
            }
        }

        for ( const auto &dependency : entry.dependencies )
        {
            auto dep_it = pending_by_dependency_.find( dependency );
            if ( dep_it == pending_by_dependency_.end() )
            {
                continue;
            }
            dep_it->second.erase( proposal_id );
            if ( dep_it->second.empty() )
            {
                pending_by_dependency_.erase( dep_it );
            }
        }

        pending_entries_.erase( entry_it );
        ConsensusManagerLogger()->debug( "{}: removed pending proposal_id={} reason={}",
                                         __func__,
                                         proposal_id.substr( 0, 8 ),
                                         reason );
        return true;
    }

    void ConsensusManager::RetryPendingProposal( const Proposal                       &proposal,
                                                 std::string_view                      reason,
                                                 std::size_t                           scheduled_retry_count,
                                                 std::chrono::steady_clock::time_point last_retry_at )
    {
        auto type_hash = ParseSubjectTypeHash( proposal.subject() );
        if ( !type_hash )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid subject type hash reason={}", __func__, reason );
            return;
        }
        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( type_hash.value() );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: subject handler missing type_hash={} reason={}",
                    __func__,
                    base::hex_lower( gsl::span<const uint8_t>(
                        reinterpret_cast<const uint8_t *>( proposal.subject().subject_type_hash().hash().data() ),
                        proposal.subject().subject_type_hash().hash().size() ) ),
                    reason );
                return;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( proposal.subject() );
        if ( subject_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error proposal_id={} reason={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ),
                                             reason );
            return;
        }

        const auto &validation_result = subject_result.value();
        if ( validation_result.check == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed proposal_id={} reason={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ),
                                             reason );
            return;
        }

        if ( validation_result.check == Check::Stalled )
        {
            ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled proposal_id={} reason={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ),
                                            reason );
            return;
        }

        if ( validation_result.check == Check::Pending )
        {
            auto subject_hash_result = GetSubjectHash( proposal.subject() );
            if ( subject_hash_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={} reason={}",
                                                 __func__,
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 reason );
                return;
            }
            AddPendingProposal( proposal,
                                subject_hash_result.value(),
                                validation_result,
                                scheduled_retry_count,
                                last_retry_at );
            return;
        }

        ContinueProposalAfterSubject( proposal );
    }

    outcome::result<void> ConsensusManager::WakePendingDependency( const PendingDependencyKey &dependency )
    {
        struct DependencyRetryCandidate
        {
            Proposal    proposal;
            std::size_t scheduled_retry_count = 0;
        };

        std::vector<DependencyRetryCandidate> retry_now;
        const auto                            now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            dep_it = pending_by_dependency_.find( dependency );
            if ( dep_it == pending_by_dependency_.end() )
            {
                return outcome::success();
            }

            const std::vector<std::string> proposal_ids( dep_it->second.begin(), dep_it->second.end() );
            for ( const auto &proposal_id : proposal_ids )
            {
                auto entry_it = pending_entries_.find( proposal_id );
                if ( entry_it == pending_entries_.end() )
                {
                    continue;
                }
                if ( now >= entry_it->second.expires_at )
                {
                    continue;
                }
                if ( entry_it->second.last_retry_at != std::chrono::steady_clock::time_point{} &&
                     now - entry_it->second.last_retry_at < pending_config_.min_dependency_retry_interval )
                {
                    entry_it->second.next_retry_at = entry_it->second.last_retry_at +
                                                     pending_config_.min_dependency_retry_interval;
                    continue;
                }

                DependencyRetryCandidate candidate;
                candidate.proposal              = entry_it->second.proposal;
                candidate.scheduled_retry_count = entry_it->second.scheduled_retry_count;
                entry_it->second.last_retry_at  = now;
                retry_now.push_back( std::move( candidate ) );
                RemovePendingProposalLocked( proposal_id, "dependency-wake" );
            }
        }

        for ( const auto &candidate : retry_now )
        {
            RetryPendingProposal( candidate.proposal, "dependency-wake", candidate.scheduled_retry_count, now );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessDuePendingRetries()
    {
        struct RetryCandidate
        {
            Proposal    proposal;
            std::size_t scheduled_retry_count = 0;
        };

        std::vector<RetryCandidate> retry_now;
        const auto                  now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto it = pending_entries_.begin(); it != pending_entries_.end(); )
            {
                if ( now < it->second.next_retry_at || now >= it->second.expires_at )
                {
                    ++it;
                    continue;
                }

                const auto     proposal_id = it->first;
                RetryCandidate candidate;
                candidate.proposal              = it->second.proposal;
                candidate.scheduled_retry_count = it->second.scheduled_retry_count + 1;
                it->second.last_retry_at        = now;
                retry_now.push_back( std::move( candidate ) );
                ++it;
                RemovePendingProposalLocked( proposal_id, "scheduled-retry" );
            }
        }

        for ( const auto &candidate : retry_now )
        {
            RetryPendingProposal( candidate.proposal, "scheduled-retry", candidate.scheduled_retry_count, now );
        }
    }

    void ConsensusManager::ExpirePendingProposals()
    {
        std::vector<Proposal> expired;
        const auto            now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto it = pending_entries_.begin(); it != pending_entries_.end(); )
            {
                if ( now < it->second.expires_at )
                {
                    ++it;
                    continue;
                }
                const auto proposal_id = it->first;
                expired.push_back( it->second.proposal );
                ++it;
                RemovePendingProposalLocked( proposal_id, "ttl-expired" );
            }
        }

        for ( const auto &proposal : expired )
        {
            FireProposalCleanupCallbacks( proposal );
            ClearProposalSlot( proposal );
        }
    }

    outcome::result<ConsensusManager::Proposal> ConsensusManager::CreateProposal( const Subject     &subject,
                                                                                  const std::string &proposer_id,
                                                                                  const std::string &registry_cid,
                                                                                  uint64_t           registry_epoch )
    {
        return CreateProposal( subject, proposer_id, registry_cid, registry_epoch, signer_ );
    }

    outcome::result<ConsensusManager::Proposal> ConsensusManager::CreateProposal( const Subject     &subject,
                                                                                  const std::string &proposer_id,
                                                                                  const std::string &registry_cid,
                                                                                  uint64_t           registry_epoch,
                                                                                  Signer             sign )
    {
        ConsensusManagerLogger()->trace( "{}: called by {} with hash {}, registry CID {} and epoch {}",
                                         __func__,
                                         proposer_id.substr( 0, 8 ),
                                         GetPrintableSubjectHash( subject ),
                                         registry_cid,
                                         registry_epoch );

        if ( !ValidateSubject( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: failed for hash {}: subject validation failed",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return outcome::failure( std::errc::invalid_argument );
        }

        Proposal proposal;
        *proposal.mutable_subject() = subject;
        proposal.set_proposer_id( proposer_id );
        proposal.set_registry_cid( registry_cid );
        proposal.set_registry_epoch( registry_epoch );
        proposal.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        proposal.set_proposal_id( CreateProposalId( proposal ) );
        auto signing_bytes = sgns::ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }
        ConsensusManagerLogger()->debug( "{}: Creating proposal ID {} for hash {}",
                                         __func__,
                                         proposal.proposal_id().substr( 0, 8 ),
                                         GetPrintableSubjectHash( subject ) );
        BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        proposal.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: success for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( subject ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        return proposal;
    }

    outcome::result<ConsensusManager::Vote> ConsensusManager::CreateVote( const std::string &proposal_id,
                                                                          const std::string &voter_id,
                                                                          bool               approve,
                                                                          Signer             sign )
    {
        Vote vote;
        vote.set_proposal_id( proposal_id );
        vote.set_voter_id( voter_id );
        vote.set_approve( approve );
        vote.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        // Phase 6 (D-01): populate slot_N_hash fields before signing so the
        // signature commits to them (T-06-01). No-op when no populator is set.
        SlotHashPopulator slot_hash_populator;
        {
            std::lock_guard<std::mutex> lock( slot_hash_populator_mutex_ );
            slot_hash_populator = slot_hash_populator_;
        }
        if ( slot_hash_populator )
        {
            slot_hash_populator( vote );
            ConsensusManagerLogger()->debug( "{}: populated slot hashes for proposal_id={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ) );
        }

        auto signing_bytes = sgns::VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }

        BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        vote.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: {} voted for proposal_id={}",
                                         __func__,
                                         voter_id.substr( 0, 8 ),
                                         proposal_id.substr( 0, 8 ) );
        return vote;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::CreateCertificate( const Proposal &proposal,
                                                                                        const std::vector<Vote> &votes )
    {
        ConsensusManagerLogger()->trace(
            "{}: Creating certificate for hash {}: proposal_id={} number of votes={} registry CID={}, epoch={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            votes.size(),
            proposal.registry_cid(),
            proposal.registry_epoch() );
        auto tally_result = TallyVotes( proposal, votes );
        if ( tally_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: tally error={}", __func__, tally_result.error().message() );
            return outcome::failure( tally_result.error() );
        }

        const auto &tally = tally_result.value();
        Certificate cert;
        cert.set_proposal_id( proposal.proposal_id() );
        cert.set_registry_cid( proposal.registry_cid() );
        cert.set_registry_epoch( proposal.registry_epoch() );
        cert.set_total_weight( tally.total_weight );
        cert.set_approved_weight( tally.approved_weight );
        uint64_t max_vote_ts = 0;
        for ( const auto &vote : votes )
        {
            max_vote_ts = std::max( vote.timestamp(), max_vote_ts );
        }
        if ( max_vote_ts == 0 )
        {
            max_vote_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch() )
                              .count();
        }
        cert.set_timestamp( max_vote_ts );
        for ( const auto &vote : votes )
        {
            *cert.add_votes() = vote;
        }
        *cert.mutable_proposal() = proposal;

        ConsensusManagerLogger()->debug( "{}: Success creating certificate for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        return cert;
    }

    bool ConsensusManager::IsBridgeMintSubject( const Proposal &proposal )
    {
        // Fail-closed (RESEARCH Pattern 2): any decode failure returns false so
        // the single-pool IsQuorum path applies. Only a successfully-decoded
        // NonceSubject carrying a kMintV2 with public-chain metadata is a
        // bridge mint. Test/local chains can still use a registered
        // IInputValidator without being forced through RPC slot quorum.
        const auto nonce_payload = DecodeNonceSubject( proposal.subject() );
        if ( nonce_payload.has_error() )
        {
            return false;
        }
        const auto &transaction = nonce_payload.value().transaction();
        if ( transaction.transaction_case() != EmbeddedTransaction::kMintV2 )
        {
            return false;
        }
        const auto &mint = transaction.mint_v2();
        return IsPublicChainMintChainId( mint.chain_id() );
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::EvaluateQuorum(
        const Proposal                    &proposal,
        const std::vector<Vote>           &votes,
        const ValidatorRegistry::Registry &registry ) const
    {
        QuorumTally tally;

        if ( IsBridgeMintSubject( proposal ) )
        {
            // Bridge-mint subject: cumulative slot tally (D-06).
            const auto slot_result = registry_->EvaluateSlotQuorum( votes, registry );
            tally.total_weight     = ValidatorRegistry::TotalWeight( registry );
            tally.approved_weight  = slot_result.total_voting_reputation;
            tally.has_quorum       = slot_result.has_quorum;
            tally.qualified_sum    = slot_result.qualified_sum;
            tally.slot_threshold   = slot_result.threshold;
            ConsensusManagerLogger()->debug( "{}: bridge-mint slot tally hash {} proposal_id={} qualified_sum={} "
                                             "threshold={} total_voting_rep={} has_quorum={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ),
                                             slot_result.qualified_sum,
                                             slot_result.threshold,
                                             slot_result.total_voting_reputation,
                                             slot_result.has_quorum );
            return tally;
        }

        // Non-bridge subject: unchanged single-pool IsQuorum path.
        const uint64_t                  total_weight    = ValidatorRegistry::TotalWeight( registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;
        for ( const auto &vote : votes )
        {
            if ( vote.proposal_id() != proposal.proposal_id() )
            {
                continue;
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                continue;
            }
            const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                continue;
            }
            if ( vote.approve() )
            {
                approved_weight += validator->weight();
            }
        }
        tally.total_weight    = total_weight;
        tally.approved_weight = approved_weight;
        tally.has_quorum      = registry_->IsQuorum( approved_weight, total_weight );
        // qualified_sum / slot_threshold remain zero for non-bridge (observability).
        return tally;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes(
        const Proposal                    &proposal,
        const std::vector<Vote>           &votes,
        const ValidatorRegistry::Registry &registry,
        const std::string                 &registry_cid ) const
    {
        if ( !proposal.registry_cid().empty() && !registry_cid.empty() && proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error(
                "{}: failed: registry cid mismatch hash {}, proposal CID ={} registry CID={}",
                __func__,
                GetPrintableSubjectHash( proposal.subject() ),
                proposal.registry_cid(),
                registry_cid );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( proposal.registry_epoch() != registry.epoch() )
        {
            ConsensusManagerLogger()->error(
                "{}: failed: registry epoch mismatch hash {}, proposal Epoch={} registry Epoch={}",
                __func__,
                GetPrintableSubjectHash( proposal.subject() ),
                proposal.registry_epoch(),
                registry.epoch() );
            return outcome::failure( std::errc::invalid_argument );
        }

        uint64_t                        total_weight    = ValidatorRegistry::TotalWeight( registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;

        for ( const auto &vote : votes )
        {
            ConsensusManagerLogger()->trace( "{}: processing vote for hash {}: voter_id={} approve={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             vote.voter_id().substr( 0, 8 ),
                                             vote.approve() );
            if ( vote.proposal_id() != proposal.proposal_id() )
            {
                continue;
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                continue;
            }

            const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                ConsensusManagerLogger()->debug( "{}: processing vote for hash {}: voter_id={} approve={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 vote.voter_id().substr( 0, 8 ),
                                                 vote.approve() );
                continue;
            }

            auto signing_bytes = sgns::VoteSigningBytes( vote );
            if ( signing_bytes.has_error() )
            {
                continue;
            }

            if ( !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
            {
                continue;
            }

            ConsensusManagerLogger()->debug( "{}: Valid voter signature for hash {}: voter_id={} approve={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             vote.voter_id().substr( 0, 8 ),
                                             vote.approve() );
            if ( vote.approve() )
            {
                ConsensusManagerLogger()->debug( "{}: Adding weight for hash {}: voter_id={} weight={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 vote.voter_id().substr( 0, 8 ),
                                                 validator->weight() );
                approved_weight += validator->weight();
            }
        }

        QuorumTally tally;
        tally.total_weight    = total_weight;
        tally.approved_weight = approved_weight;
        // Phase 6 (D-06): route the final has_quorum decision through the
        // shared EvaluateQuorum dispatcher so bridge-mint subjects use the
        // cumulative slot model. The non-bridge branch recomputes the same
        // single-pool IsQuorum result; sig verification stays in this loop.
        // Both TallyVotes (certificate creation) and the incremental HandleVote
        // tally agree on bridge-mint quorum via this single helper (Pitfall 1).
        auto quorum_result = EvaluateQuorum( proposal, votes, registry );
        if ( quorum_result.has_error() )
        {
            return quorum_result.error();
        }
        tally.has_quorum     = quorum_result.value().has_quorum;
        tally.qualified_sum  = quorum_result.value().qualified_sum;
        tally.slot_threshold = quorum_result.value().slot_threshold;
        ConsensusManagerLogger()->debug(
            "{}: Votes tallied for hash {} proposal_id={} approved_weight={} total_weight={} quorum={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            approved_weight,
            total_weight,
            tally.has_quorum );
        return tally;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes( const Proposal          &proposal,
                                                                                 const std::vector<Vote> &votes ) const
    {
        ConsensusManagerLogger()->trace(
            "{}: Tallying with current registry for hash {}, proposal_id={} number of votes={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            votes.size() );

        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: failed: proposal registry CID is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry load error={} cid={}",
                                             __func__,
                                             registry_result.error().message(),
                                             proposal.registry_cid() );
            return outcome::failure( registry_result.error() );
        }
        return TallyVotes( proposal, votes, registry_result.value(), proposal.registry_cid() );
    }

    outcome::result<void> ConsensusManager::SubmitProposal( const Proposal &proposal, bool self_vote )
    {
        ConsensusManagerLogger()->trace( "{}: called for hash {} proposal_id={} self_vote={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ),
                                         self_vote );
        const auto slot_key = GetSlotKey( proposal );
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( proposal.proposal_id() );
            if ( it == proposals_.end() )
            {
                ConsensusManagerLogger()->debug( "{}: Creating proposal state for hash {} proposal_id={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                ProposalState state;
                state.proposal = proposal;
                state.slot_key = slot_key;
                proposals_.emplace( proposal.proposal_id(), std::move( state ) );
            }
        }

        ConsensusMessage message;
        *message.mutable_proposal() = proposal;
        auto publish_result         = Publish( message );
        if ( publish_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}",
                                             __func__,
                                             publish_result.error().message() );
            return publish_result;
        }
        ConsensusManagerLogger()->debug( "{}: success for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );

        if ( self_vote )
        {
            HandleProposal( proposal );
        }

        return outcome::success();
    }

    outcome::result<void> ConsensusManager::SubmitVote( const Vote &vote, bool self_handle )
    {
        ConsensusManagerLogger()->trace( "{}: called by {} proposal_id={}",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id().substr( 0, 8 ) );
        ConsensusMessage message;
        *message.mutable_vote() = vote;
        auto result             = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: success voter_id={} proposal_id={} ",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id().substr( 0, 8 ) );
        if ( self_handle )
        {
            HandleVote( vote );
        }
        return result;
    }

    outcome::result<void> ConsensusManager::SubmitCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: called for hash {} and proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( certificate.proposal().subject() ),
                                         certificate.proposal_id().substr( 0, 8 ) );
        ConsensusMessage message;
        *message.mutable_certificate() = certificate;
        auto result                    = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}", __func__, result.error().message() );
            return result;
        }

        auto subject_hash_result = GetSubjectHash( certificate.proposal().subject() );
        if ( subject_hash_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject hash {} error proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( certificate.proposal().subject() ),
                                             certificate.proposal_id().substr( 0, 8 ) );
            return outcome::failure( subject_hash_result.error() );
        }

        std::string serialized;
        if ( !certificate.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate serialize error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto             key = std::string{ CERTIFICATE_BASE_PATH_KEY } + subject_hash_result.value();
        crdt::HierarchicalKey  cert_key( key );
        crdt::GlobalDB::Buffer cert_value;
        cert_value.put( serialized );

        auto cert_put = db_->Put( cert_key, cert_value, { consensus_datastore_topic_ } );
        if ( cert_put.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: cert put for hash {} error={}",
                                             __func__,
                                             GetPrintableSubjectHash( certificate.proposal().subject() ),
                                             cert_put.error().message() );
            return outcome::failure( cert_put.error() );
        }

        ConsensusManagerLogger()->debug( "{}: success submitting certificate for {} and proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( certificate.proposal().subject() ),
                                         certificate.proposal_id().substr( 0, 8 ) );
        return result;
    }

    void ConsensusManager::HandleProposal( const Proposal &proposal )
    {
        if ( !CheckProposal( proposal ) )
        {
            return;
        }

        const auto &subject     = proposal.subject();
        const auto &proposal_id = proposal.proposal_id();
        if ( !IsTimestampSane( proposal.timestamp() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: timestamp out of bounds for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             proposal_id.substr( 0, 8 ) );
            return;
        }

        auto subject_hash_result = GetSubjectHash( subject );
        if ( subject_hash_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ) );
            return;
        }
        const auto &subject_hash = subject_hash_result.value();

        auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
        if ( proposal_registry_result.has_error() )
        {
            ConsensusManagerLogger()->warn(
                "{}: deferred: registry load error={} proposal={} proposal_id={} hash={}. Keeping proposal pending",
                __func__,
                proposal_registry_result.error().message(),
                proposal.registry_cid(),
                proposal_id.substr( 0, 8 ),
                subject_hash.substr( 0, 8 ) );
            AddPendingProposal( proposal, subject_hash );
            return;
        }
        if ( proposal.registry_epoch() != proposal_registry_result.value().epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry epoch mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_epoch(),
                                             proposal_registry_result.value().epoch() );
            return;
        }

        if ( !CheckSubject( subject ) )
        {
            return;
        }

        if ( CheckCertificateForSubject( subject_hash ) )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: subject already certified hash={} proposal_id={}",
                                             __func__,
                                             subject_hash.substr( 0, 8 ),
                                             proposal_id.substr( 0, 8 ) );
            std::lock_guard lock( proposals_mutex_ );
            RemovePendingProposalLocked( proposal_id, "already-certified" );
            return;
        }

        const auto     type_hash = ParseSubjectTypeHash( subject ).value();
        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( type_hash );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: subject handler missing type_hash={}",
                    __func__,
                    base::hex_lower( gsl::span<const uint8_t>(
                        reinterpret_cast<const uint8_t *>( subject.subject_type_hash().hash().data() ),
                        subject.subject_type_hash().hash().size() ) ) );
                return;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( subject );
        if ( subject_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error for hash {} proposal_id={}",
                                             __func__,
                                             subject_hash.substr( 0, 8 ),
                                             proposal_id.substr( 0, 8 ) );
            return;
        }

        const auto &validation_result = subject_result.value();
        switch ( validation_result.check )
        {
            case Check::Reject:
                ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal_id.substr( 0, 8 ) );
                return;
            case Check::Stalled:
                ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled for hash {} proposal_id={}",
                                                __func__,
                                                subject_hash.substr( 0, 8 ),
                                                proposal_id.substr( 0, 8 ) );
                return;
            case Check::Pending:
                AddPendingProposal( proposal, subject_hash, validation_result );
                return;
            case Check::Approve:
                ContinueProposalAfterSubject( proposal );
                return;
        }
    }

    outcome::result<void> ConsensusManager::ResumeProposalHandling( const std::string &subject_hash )
    {
        if ( subject_hash.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        ConsensusManagerLogger()->trace( "{}: Attempting to resume proposals for hash={}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ) );

        for ( const auto &proposal : TakePendingProposals( subject_hash ) )
        {
            RetryPendingProposal( proposal, "subject-resume" );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessCertificates()
    {
        std::vector<ProposalState> to_process;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( const auto &[_, state] : proposals_ )
            {
                if ( state.quorum_reached )
                {
                    to_process.push_back( state );
                }
            }
        }

        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        for ( const auto &state : to_process )
        {
            const auto &proposal     = state.proposal;
            const auto &proposal_id  = proposal.proposal_id();
            auto        subject_hash = GetSubjectHash( proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                ConsensusManagerLogger()->debug( "{}: hash {} already certified, clearing proposal_id={}",
                                                 __func__,
                                                 subject_hash.value().substr( 0, 8 ),
                                                 proposal_id.substr( 0, 8 ) );
                ClearProposalSlot( proposal );
                continue;
            }

            const bool certificate_delay_elapsed = state.quorum_reached_ts_ms == 0 ||
                                                   std::chrono::milliseconds(
                                                       now_ms - static_cast<int64_t>( state.quorum_reached_ts_ms ) ) >=
                                                       certificate_delay_;
            if ( !certificate_delay_elapsed )
            {
                continue;
            }

            const auto round = GetCurrentRound( proposal.timestamp() );
            if ( round == state.last_attempt_round )
            {
                ConsensusManagerLogger()->debug(
                    "{}: proposal already attempted in round for hash {} proposal_id={} round={}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ),
                    round );
                continue;
            }

            auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry load error={} proposal_id={}",
                                                 __func__,
                                                 proposal_registry_result.error().message(),
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry epoch mismatch proposal_id={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }

            const auto aggregator_role = GetAggregatorRole( proposal, proposal_registry );
            if ( aggregator_role == AggregatorRole::NotInRegistry )
            {
                ConsensusManagerLogger()->debug(
                    "{}: local node not in proposal registry; clearing local proposal for hash {} proposal_id={}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ) );
                ClearProposalSlot( proposal );
                continue;
            }
            if ( aggregator_role == AggregatorRole::ActiveButNotAggregator )
            {
                ConsensusManagerLogger()->debug( "{}: not aggregator for proposal for hash {} proposal_id={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }

            {
                std::lock_guard lock( proposals_mutex_ );
                auto            it = proposals_.find( proposal_id );
                if ( it != proposals_.end() )
                {
                    it->second.last_attempt_round = round;
                }
            }
            ConsensusManagerLogger()->debug( "{}: Attempting to create certificate for hash {} proposal_id={} round={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal_id.substr( 0, 8 ),
                                             round );
            auto certificate_result = CreateCertificate( proposal, state.votes );
            if ( certificate_result.has_error() )
            {
                ConsensusManagerLogger()->error(
                    "{}: failed: certificate creation error for hash {} proposal_id {}: {}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ),
                    certificate_result.error().message() );
                continue;
            }

            if ( SubmitCertificate( certificate_result.value() ).has_error() )
            {
                continue;
            }
            ClearProposalSlot( proposal );
            ConsensusManagerLogger()->debug( "{}: certificate submitted for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal_id.substr( 0, 8 ) );
        }
    }

    bool ConsensusManager::RegisterCertificateFilter()
    {
        const std::string pattern = std::string( CERT_KEY_PATTERN );

        auto weak_self                 = weak_from_this();
        certificate_filter_registered_ = db_->RegisterElementFilter(
            pattern,
            [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_self.lock() )
                {
                    return strong->FilterCertificate( element );
                }
                return std::nullopt;
            } );

        certificate_callback_registered_ = db_->RegisterNewElementCallback(
            pattern,
            [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->CertificateReceived( std::move( new_data ), cid );
                }
            } );

        db_->AddListenTopic( consensus_datastore_topic_ );

        return certificate_filter_registered_ && certificate_callback_registered_;
    }

    std::optional<std::vector<crdt::pb::Element>> ConsensusManager::FilterCertificate(
        const crdt::pb::Element &element )
    {
        ConsensusManagerLogger()->trace( "{}: entry key={}", __func__, element.key() );
        Certificate certificate;
        if ( !certificate.ParseFromString( element.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: parse failed, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        if ( certificate.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: missing proposal_id, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        if ( ValidateCertificate( certificate ) == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: validation failed, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        ConsensusManagerLogger()->debug( "{}: certificate accepted key={}", __func__, element.key() );
        return std::nullopt;
    }

    void ConsensusManager::CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string & )
    {
        const auto &[key, value] = new_data;
        Certificate certificate;
        if ( !certificate.ParseFromArray( value.data(), value.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
            return;
        }

        const auto certificate_check = ValidateCertificate( certificate );
        if ( certificate_check == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected invalid certificate for key {}", __func__, key );
            return;
        }

        if ( certificate_check == Check::Stalled )
        {
            ConsensusManagerLogger()->error(
                "{}: Validation of the certificate pending for key {}, certificate handler not called ",
                __func__,
                key );
            certificate_work_journal_->MarkStalled( key );
            return;
        }

        const auto &proposal            = certificate.proposal();
        const auto &proposal_id         = certificate.proposal_id();
        auto        subject_hash_result = GetSubjectHash( proposal.subject() );
        if ( subject_hash_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed getting subject hash proposal_id={} error={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ),
                                             subject_hash_result.error().message() );
            return;
        }
        const auto &subject_hash = subject_hash_result.value();

        auto registry_result = registry_->OnFinalizedCertificate( certificate );
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: registry finalization failed proposal_id={} error={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ),
                                             registry_result.error().message() );
            certificate_work_journal_->MarkStalled( key );
            return;
        }

        const auto                type_hash = ParseSubjectTypeHash( proposal.subject() ).value();
        CertificateSubjectHandler handler;
        {
            std::shared_lock lock( certificate_handlers_mutex_ );
            auto             it = certificate_subject_handlers_.find( type_hash );
            if ( it != certificate_subject_handlers_.end() )
            {
                handler = it->second;
            }
        }

        if ( handler )
        {
            auto result = handler( subject_hash, certificate );
            if ( result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: certificate handler error proposal_id={} error={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ),
                                                 result.error().message() );
                return;
            }

            if ( result.value() == Check::Stalled )
            {
                ConsensusManagerLogger()->warn( "{}: certificate handler stalled proposal_id={}",
                                                __func__,
                                                proposal_id.substr( 0, 8 ) );
                certificate_work_journal_->MarkStalled( key );
                return;
            }
        }
        else
        {
            ConsensusManagerLogger()->warn( "{}: no subject handler for certificate key={}", __func__, key );
        }

        (void) certificate_work_journal_->MarkDone( key );
        (void) WakePendingDependency( PendingDependencyKey::Certificate( subject_hash ) );
    }

    ConsensusManager::Check ConsensusManager::ValidateCertificate( const Certificate &certificate ) const
    {
        if ( certificate.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Certificate proposal ID missing ", __func__ );
            return Check::Reject;
        }
        if ( !certificate.has_proposal() )
        {
            ConsensusManagerLogger()->error( "{}: Certificate missing proposal ", __func__ );
            return Check::Reject;
        }

        const auto &proposal = certificate.proposal();
        if ( proposal.proposal_id() != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: proposal_id mismatch cert={} proposal={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             proposal.proposal_id() );
            return Check::Reject;
        }
        if ( proposal.registry_cid() != certificate.registry_cid() ||
             proposal.registry_epoch() != certificate.registry_epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return Check::Reject;
        }
        auto registry_ret = registry_->LoadRegistryByCid( certificate.registry_cid() );
        if ( registry_ret.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry load error={} for registry cid {} proposal_id={}",
                                             __func__,
                                             registry_ret.error().message(),
                                             certificate.registry_cid(),
                                             certificate.proposal_id() );
            return Check::Stalled;
        }
        auto &registry = registry_ret.value();
        if ( !ValidateSubject( proposal.subject() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid subject proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return Check::Reject;
        }
        if ( !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid proposal proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return Check::Reject;
        }

        const auto computed_id = CreateProposalId( proposal );
        if ( computed_id.empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: computed_id empty", __func__ );
            return Check::Reject;
        }
        if ( computed_id != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: computed_id mismatch cert={} computed={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             computed_id );
            return Check::Reject;
        }

        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            votes.push_back( vote );
        }
        auto tally = TallyVotes( proposal, votes, registry, certificate.registry_cid() );
        if ( tally.has_error() || !tally.value().has_quorum )
        {
            return Check::Reject;
        }

        return Check::Approve;
    }

    void ConsensusManager::HandleVote( const Vote &vote )
    {
        const auto &proposal_id = vote.proposal_id();
        const auto &voter_id    = vote.voter_id();
        if ( !CheckVote( vote ) )
        {
            return;
        }
        if ( !vote.approve() )
        {
            return;
        }

        auto signing_bytes = sgns::VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return;
        }
        if ( !GeniusAccount::VerifySignature( voter_id, vote.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signature verification failed voter_id={}",
                                             __func__,
                                             voter_id.substr( 0, 8 ) );
            return;
        }

        bool reached_quorum = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( proposal_id );
            if ( it == proposals_.end() )
            {
                pending_votes_[proposal_id].push_back( vote );
                return;
            }

            auto       &state        = it->second;
            const auto &proposal     = state.proposal;
            auto        subject_hash = GetSubjectHash( proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                pending_votes_.erase( proposal_id );
                return;
            }

            auto slot_it = slot_states_.find( state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != proposal_id )
            {
                return;
            }
            if ( state.quorum_reached || state.seen_voters.find( voter_id ) != state.seen_voters.end() )
            {
                return;
            }

            auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->warn( "{}: deferred vote: registry load error={} proposal_id={}",
                                                __func__,
                                                proposal_registry_result.error().message(),
                                                proposal_id.substr( 0, 8 ) );
                pending_votes_[proposal_id].push_back( vote );
                return;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ) );
                return;
            }

            const auto *validator = registry_->FindValidator( proposal_registry, voter_id );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                ConsensusManagerLogger()->debug( "{}: ignored vote from inactive validator voter_id={}",
                                                 __func__,
                                                 voter_id.substr( 0, 8 ) );
                return;
            }

            state.votes.push_back( vote );
            state.seen_voters.insert( voter_id );
            // ponytail: recomputing the tally makes admission O(votes^2); restore an incremental tally if validator
            // sets grow enough for this to matter.
            auto tally = EvaluateQuorum( proposal, state.votes, proposal_registry );
            if ( tally.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: quorum evaluation failed proposal_id={} error={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ),
                                                 tally.error().message() );
                return;
            }
            if ( !tally.value().has_quorum )
            {
                return;
            }

            state.quorum_reached       = true;
            state.quorum_reached_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch() )
                                             .count();
            reached_quorum             = true;
        }

        if ( reached_quorum )
        {
            certificates_pending_.store( true );
            timer_cv_.notify_all();
        }
    }

    void ConsensusManager::HandleCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, certificate.proposal_id() );

        if ( ValidateCertificate( certificate ) == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid certificate proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return;
        }

        ProposalState proposal_state;
        auto          fetch_proposal_state_ret = FetchProposalState( certificate );
        if ( fetch_proposal_state_ret.has_value() )
        {
            proposal_state = fetch_proposal_state_ret.value();
            ConsensusManagerLogger()->debug( "{}: fetched proposal state, proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
        }
        else
        {
            ConsensusManagerLogger()->debug( "{}: proposal state not found, creating new one proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            proposal_state = CreateProposalState( certificate );
        }

        if ( !ValidateCertificateBestProposal( proposal_state, certificate ) )
        {
            return;
        }

        ClearProposalSlot( certificate.proposal() );
        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, certificate.proposal_id() );
    }

    outcome::result<ConsensusManager::ProposalState> ConsensusManager::FetchProposalState(
        const Certificate &certificate )
    {
        std::lock_guard lock( proposals_mutex_ );
        auto            it = proposals_.find( certificate.proposal_id() );
        if ( it == proposals_.end() )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        return it->second;
    }

    ConsensusManager::ProposalState ConsensusManager::CreateProposalState( const Certificate &certificate )
    {
        ProposalState new_state;
        new_state.proposal = certificate.proposal();
        new_state.slot_key = GetSlotKey( new_state.proposal );
        proposals_.emplace( new_state.proposal.proposal_id(), new_state );

        auto &slot_state = slot_states_[new_state.slot_key];
        if ( slot_state.best_proposal_id.empty() )
        {
            slot_state.best_proposal_id = new_state.proposal.proposal_id();
        }

        return new_state;
    }

    bool ConsensusManager::ValidateCertificateBestProposal( const ProposalState &state,
                                                            const Certificate   &certificate ) const
    {
        if ( certificate.has_proposal() && certificate.proposal().has_subject() &&
             DecodeRegistryBatchSubject( certificate.proposal().subject() ).has_value() )
        {
            // Registry-batch subjects can have multiple competing proposals for the same deterministic batch root.
            // Once a valid certificate exists, accept it even if local best_proposal_id changed due proposal races.
            return true;
        }
        std::lock_guard lock( proposals_mutex_ );
        auto            slot_it = slot_states_.find( state.slot_key );
        if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: not best proposal proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return false;
        }
        return true;
    }

    void ConsensusManager::ClearProposalSlot( const Proposal &proposal )
    {
        std::lock_guard lock( proposals_mutex_ );

        std::string slot_key;
        auto        it = proposals_.find( proposal.proposal_id() );
        if ( it != proposals_.end() )
        {
            slot_key = it->second.slot_key;
        }
        else
        {
            slot_key = GetSlotKey( proposal );
        }

        std::unordered_set<std::string> ids_to_remove;
        ids_to_remove.insert( proposal.proposal_id() );
        for ( const auto &kv : proposals_ )
        {
            if ( kv.second.slot_key == slot_key )
            {
                ids_to_remove.insert( kv.first );
            }
        }

        for ( const auto &proposal_id : ids_to_remove )
        {
            RemovePendingProposalLocked( proposal_id, "slot-cleanup" );
            proposals_.erase( proposal_id );
        }

        slot_states_.erase( slot_key );

        bool has_pending = false;
        for ( const auto &kv : proposals_ )
        {
            if ( kv.second.quorum_reached )
            {
                has_pending = true;
                break;
            }
        }
        certificates_pending_.store( has_pending );
        if ( !has_pending )
        {
            timer_cv_.notify_all();
        }
    }

    std::string ConsensusManager::GetSlotKey( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );

        auto type_hash = ParseSubjectTypeHash( proposal.subject() );
        if ( !type_hash )
        {
            return proposal.proposal_id();
        }
        const auto &subject = proposal.subject();

        {
            std::shared_lock lock( slot_key_handlers_mutex_ );
            auto             it = slot_key_handlers_.find( type_hash.value() );
            if ( it != slot_key_handlers_.end() )
            {
                return it->second( subject );
            }
        }

        auto subject_id = ComputeSubjectId( subject );
        return subject_id.has_value() ? subject_id.value() : proposal.proposal_id();
    }

    bool ConsensusManager::IsBetterProposal( const Proposal &candidate, const Proposal &current ) const
    {
        auto candidate_nonce = DecodeNonceSubject( candidate.subject() );
        auto current_nonce   = DecodeNonceSubject( current.subject() );
        if ( candidate_nonce.has_value() && current_nonce.has_value() )
        {
            const auto &cand_hash = candidate_nonce.value().tx_hash();
            const auto &curr_hash = current_nonce.value().tx_hash();
            if ( cand_hash == curr_hash )
            {
                return candidate.proposal_id() < current.proposal_id();
            }
            return cand_hash < curr_hash;
        }

        return candidate.proposal_id() < current.proposal_id();
    }

    outcome::result<std::string> ConsensusManager::ComputeSubjectId( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        std::string serialized;
        if ( !subject.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto hash = sgns::crypto::sha2_256( serialized.data(), serialized.size() );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    namespace
    {
        constexpr size_t kSubjectTypeHashSize = base::Hash256::size();

        outcome::result<std::string> ComputePayloadHash( const std::string &payload )
        {
            if ( payload.empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto hash = sgns::crypto::sha2_256( payload.data(), payload.size() );
            return std::string( reinterpret_cast<const char *>( hash.data() ), hash.size() );
        }

        bool SetSubjectPayload( ConsensusSubject                    *subject,
                                const base::Hash256                 &subject_type_hash,
                                const google::protobuf::MessageLite &payload )
        {
            if ( subject == nullptr )
            {
                return false;
            }
            std::string serialized;
            if ( !payload.SerializeToString( &serialized ) )
            {
                return false;
            }
            std::string canonical_payload = subject_type_hash.toString() + serialized;
            auto        payload_hash      = ComputePayloadHash( canonical_payload );
            if ( payload_hash.has_error() )
            {
                return false;
            }
            subject->set_payload( canonical_payload.data(), canonical_payload.size() );
            subject->set_payload_hash( payload_hash.value().data(), payload_hash.value().size() );
            return true;
        }

        outcome::result<std::string> ExtractBuiltinPayload( const ConsensusSubject &subject,
                                                            std::string_view        subject_type )
        {
            BOOST_OUTCOME_TRY( auto expected, ConsensusManager::ComputeSubjectTypeHash( subject_type ) );
            const auto expected_bytes = expected.toString();
            if ( !subject.has_subject_type_hash() || subject.subject_type_hash().hash() != expected_bytes ||
                 subject.payload().size() <= kSubjectTypeHashSize ||
                 subject.payload().compare( 0, kSubjectTypeHashSize, expected_bytes ) != 0 )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return subject.payload().substr( kSubjectTypeHashSize );
        }
    }

    outcome::result<base::Hash256> ConsensusManager::ComputeSubjectTypeHash( std::string_view subject_type )
    {
        if ( subject_type.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return sgns::crypto::sha2_256( subject_type.data(), subject_type.size() );
    }

    outcome::result<NonceSubject> ConsensusManager::DecodeNonceSubject( const Subject &subject )
    {
        BOOST_OUTCOME_TRY( auto raw_payload, ExtractBuiltinPayload( subject, NONCE_SUBJECT_TYPE ) );
        NonceSubject payload;
        if ( !payload.ParseFromString( raw_payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    outcome::result<TaskResultSubject> ConsensusManager::DecodeTaskResultSubject( const Subject &subject )
    {
        BOOST_OUTCOME_TRY( auto raw_payload, ExtractBuiltinPayload( subject, TASK_RESULT_SUBJECT_TYPE ) );
        TaskResultSubject payload;
        if ( !payload.ParseFromString( raw_payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    outcome::result<RegistryBatchSubject> ConsensusManager::DecodeRegistryBatchSubject( const Subject &subject )
    {
        BOOST_OUTCOME_TRY( auto raw_payload, ExtractBuiltinPayload( subject, REGISTRY_BATCH_SUBJECT_TYPE ) );
        RegistryBatchSubject payload;
        if ( !payload.ParseFromString( raw_payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    bool ConsensusManager::SubjectHasValidTypeHash( Subject *subject )
    {
        return subject != nullptr && ParseSubjectTypeHash( *subject ).has_value();
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject(
        const std::string                             &account_id,
        uint64_t                                       nonce,
        const std::string                             &tx_hash,
        const EmbeddedTransaction                     &transaction,
        const std::optional<UTXOTransitionCommitment> &utxo_commitment,
        const std::optional<UTXOWitness>              &utxo_witness )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} nonce={}", __func__, account_id, nonce );
        Subject subject;
        subject.set_account_id( account_id );
        NonceSubject payload;
        payload.set_nonce( nonce );
        payload.set_tx_hash( tx_hash.data(), tx_hash.size() );
        *payload.mutable_transaction() = transaction;
        if ( utxo_commitment.has_value() )
        {
            *payload.mutable_utxo_commitment() = utxo_commitment.value();
        }
        if ( utxo_witness.has_value() )
        {
            *payload.mutable_utxo_witness() = utxo_witness.value();
        }
        auto type_hash = ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE );
        if ( type_hash.has_error() || !SetSubjectPayload( &subject, type_hash.value(), payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        subject.mutable_subject_type_hash()->set_hash( type_hash.value().data(), type_hash.value().size() );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateTaskResultSubject(
        const std::string &account_id,
        const std::string &escrow_path,
        const std::string &task_result_hash,
        uint64_t           result_epoch )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} result_epoch={}",
                                         __func__,
                                         account_id,
                                         result_epoch );
        Subject subject;
        subject.set_account_id( account_id );
        TaskResultSubject payload;
        payload.set_escrow_path( escrow_path );
        payload.set_task_result_hash( task_result_hash.data(), task_result_hash.size() );
        payload.set_result_epoch( result_epoch );
        auto type_hash = ComputeSubjectTypeHash( TASK_RESULT_SUBJECT_TYPE );
        if ( type_hash.has_error() || !SetSubjectPayload( &subject, type_hash.value(), payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        subject.mutable_subject_type_hash()->set_hash( type_hash.value().data(), type_hash.value().size() );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateRegistryBatchSubject(
        const std::string &account_id,
        const std::string &base_registry_cid,
        uint64_t           base_registry_epoch,
        uint64_t           target_registry_epoch,
        uint32_t           certificate_count,
        const std::string &batch_root )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} base_epoch={} target_epoch={} certificates={}",
                                         __func__,
                                         account_id.substr( 0, 8 ),
                                         base_registry_epoch,
                                         target_registry_epoch,
                                         certificate_count );
        Subject subject;
        subject.set_account_id( account_id );
        RegistryBatchSubject payload;
        payload.set_base_registry_cid( base_registry_cid );
        payload.set_base_registry_epoch( base_registry_epoch );
        payload.set_target_registry_epoch( target_registry_epoch );
        payload.set_certificate_count( certificate_count );
        payload.set_batch_root( batch_root.data(), batch_root.size() );
        auto type_hash = ComputeSubjectTypeHash( REGISTRY_BATCH_SUBJECT_TYPE );
        if ( type_hash.has_error() || !SetSubjectPayload( &subject, type_hash.value(), payload ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        subject.mutable_subject_type_hash()->set_hash( type_hash.value().data(), type_hash.value().size() );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateGenericSubject(
        const std::string          &account_id,
        std::string_view            subject_type,
        const std::vector<uint8_t> &payload )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} subject_type={}",
                                         __func__,
                                         account_id.substr( 0, 8 ),
                                         subject_type );
        if ( account_id.empty() || subject_type.empty() || payload.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        Subject subject;
        subject.set_account_id( account_id );
        subject.set_payload( payload.data(), payload.size() );
        auto payload_hash = ComputePayloadHash( subject.payload() );
        auto type_hash    = ComputeSubjectTypeHash( subject_type );
        if ( payload_hash.has_error() || type_hash.has_error() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        subject.set_payload_hash( payload_hash.value().data(), payload_hash.value().size() );
        subject.mutable_subject_type_hash()->set_hash( type_hash.value().data(), type_hash.value().size() );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    std::string ConsensusManager::CreateProposalId( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: Creating proposal ID", __func__ );
        // Proposal ID must be derived from the proposal contents excluding the proposal_id itself.
        Proposal copy = proposal;
        copy.clear_proposal_id();
        auto signing_bytes = sgns::ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed, no proposal ID created: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return {};
        }

        auto hash        = sgns::crypto::sha2_256( signing_bytes.value().data(), signing_bytes.value().size() );
        auto proposal_id = base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
        ConsensusManagerLogger()->debug( "{}: Proposal ID {} created", __func__, proposal_id.substr( 0, 8 ) );
        return proposal_id;
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        if ( subject.account_id().empty() )
        {
            return false;
        }
        if ( !ParseSubjectTypeHash( subject ) )
        {
            return false;
        }
        if ( subject.payload().empty() || subject.payload_hash().empty() )
        {
            return false;
        }
        auto payload_hash = ComputePayloadHash( subject.payload() );
        if ( payload_hash.has_error() || payload_hash.value() != subject.payload_hash() )
        {
            return false;
        }

        if ( SubjectTypeMatches( subject, NONCE_SUBJECT_TYPE ) )
        {
            auto payload = DecodeNonceSubject( subject );
            if ( payload.has_error() || payload.value().tx_hash().empty() )
            {
                return false;
            }
            if ( payload.value().has_utxo_witness() && !payload.value().has_utxo_commitment() )
            {
                return false;
            }
            return true;
        }
        if ( SubjectTypeMatches( subject, TASK_RESULT_SUBJECT_TYPE ) )
        {
            auto payload = DecodeTaskResultSubject( subject );
            return payload.has_value() && !payload.value().task_result_hash().empty();
        }
        if ( SubjectTypeMatches( subject, REGISTRY_BATCH_SUBJECT_TYPE ) )
        {
            auto payload = DecodeRegistryBatchSubject( subject );
            if ( payload.has_error() )
            {
                return false;
            }
            return !payload.value().base_registry_cid().empty() &&
                   payload.value().target_registry_epoch() == payload.value().base_registry_epoch() + 1 &&
                   payload.value().certificate_count() > 0 && !payload.value().batch_root().empty();
        }
        return true;
    }

    void ConsensusManager::OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        if ( !message )
        {
            ConsensusManagerLogger()->error( "{}: ignored: message is empty", __func__ );
            return;
        }

        ConsensusMessage decoded;
        if ( !decoded.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
        {
            ConsensusManagerLogger()->error( "{}: Failed to decode consensus message", __func__ );
            return;
        }

        if ( decoded.has_proposal() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded proposal", __func__ );
            HandleProposal( decoded.proposal() );
            return;
        }
        if ( decoded.has_vote() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded vote", __func__ );
            HandleVote( decoded.vote() );
            return;
        }
        if ( decoded.has_certificate() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded certificate", __func__ );
            HandleCertificate( decoded.certificate() );
        }
    }

    bool ConsensusManager::CheckSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );

        if ( subject.account_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject account_id is empty", __func__ );
            return false;
        }

        if ( !ParseSubjectTypeHash( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: subject subject_type_hash is invalid", __func__ );
            return false;
        }
        if ( subject.payload().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload is empty", __func__ );
            return false;
        }
        if ( subject.payload_hash().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload_hash is empty", __func__ );
            return false;
        }
        auto payload_hash = ComputePayloadHash( subject.payload() );
        if ( payload_hash.has_error() || payload_hash.value() != subject.payload_hash() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload_hash mismatch", __func__ );
            return false;
        }

        if ( SubjectTypeMatches( subject, NONCE_SUBJECT_TYPE ) )
        {
            auto payload = DecodeNonceSubject( subject );
            if ( payload.has_error() || payload.value().tx_hash().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject nonce tx_hash is empty", __func__ );
                return false;
            }
            return true;
        }

        if ( SubjectTypeMatches( subject, TASK_RESULT_SUBJECT_TYPE ) )
        {
            auto payload = DecodeTaskResultSubject( subject );
            if ( payload.has_error() || payload.value().escrow_path().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject task_result escrow_path is empty", __func__ );
                return false;
            }
            if ( payload.value().task_result_hash().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject task_result task_result_hash is empty", __func__ );
                return false;
            }
            return true;
        }

        if ( SubjectTypeMatches( subject, REGISTRY_BATCH_SUBJECT_TYPE ) )
        {
            auto payload = DecodeRegistryBatchSubject( subject );
            if ( payload.has_error() )
            {
                return false;
            }
            if ( payload.value().base_registry_cid().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch base_registry_cid is empty", __func__ );
                return false;
            }
            if ( payload.value().target_registry_epoch() != payload.value().base_registry_epoch() + 1 )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch target epoch mismatch", __func__ );
                return false;
            }
            if ( payload.value().certificate_count() == 0 )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch certificate_count is zero", __func__ );
                return false;
            }
            if ( payload.value().batch_root().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch batch_root is empty", __func__ );
                return false;
            }
        }

        return true;
    }

    bool ConsensusManager::CheckProposal( const Proposal &proposal )
    {
        if ( proposal.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Proposal ID missing ", __func__ );
            return false;
        }
        if ( proposal.proposer_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Proposer ID missing ", __func__ );
            return false;
        }
        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Registry CID missing ", __func__ );
            return false;
        }
        if ( !proposal.has_subject() )
        {
            ConsensusManagerLogger()->error( "{}: Proposal without subject ", __func__ );
            return false;
        }
        auto signing_bytes = sgns::ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return false;
        }
        if ( !GeniusAccount::VerifySignature( proposal.proposer_id(), proposal.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signature verification failed proposer_id={}",
                                             __func__,
                                             proposal.proposer_id() );
            return false;
        }
        return true;
    }

    bool ConsensusManager::CheckVote( const Vote &vote )
    {
        if ( vote.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Vote proposal ID missing ", __func__ );
            return false;
        }
        if ( vote.voter_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Vote voter ID missing ", __func__ );
            return false;
        }
        return true;
    }

    void ConsensusManager::RecoverPendingCertificateWork()
    {
        auto recovered = certificate_work_journal_->RecoverStaleProcessing( CERT_KEY_PATTERN,
                                                                            std::chrono::seconds( 15 ) );
        if ( recovered > 0 )
        {
            ConsensusManagerLogger()->info( "{}: recovered {} stale certificate work items", __func__, recovered );
        }

        auto       unfinished = certificate_work_journal_->ListUnfinished( CERT_KEY_PATTERN );
        const auto now_ms     = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        for ( const auto &entry : unfinished )
        {
            if ( entry.key.empty() )
            {
                continue;
            }
            if ( entry.state != crdt::CRDTWorkJournal::State::Stalled )
            {
                continue;
            }
            if ( entry.lease_until_ms != 0 && entry.lease_until_ms > now_ms )
            {
                continue;
            }
            auto value = db_->Get( { entry.key } );
            if ( value.has_error() )
            {
                continue;
            }
            CertificateReceived( { entry.key, value.value() }, std::string{} );
        }
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::GetCertificateBySubjectHash(
        const std::string &subject_hash ) const
    {
        const auto key = std::string{ CERTIFICATE_BASE_PATH_KEY } + subject_hash;

        BOOST_OUTCOME_TRY( auto certificate_data, db_->Get( { key } ) );

        Certificate certificate;
        if ( !certificate.ParseFromArray( certificate_data.data(), certificate_data.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
            return outcome::failure( std::errc::invalid_argument );
        }

        BOOST_OUTCOME_TRY( auto current_hash, GetSubjectHash( certificate.proposal().subject() ) );
        if ( current_hash != subject_hash )
        {
            ConsensusManagerLogger()->error( "{}: certificate subject hash mismatch expected={} actual={}",
                                             __func__,
                                             subject_hash,
                                             current_hash );
            return outcome::failure( std::errc::invalid_argument );
        }
        return certificate;
    }

    bool ConsensusManager::CheckCertificateForSubject( const std::string &subject_hash ) const
    {
        auto certificate_result = GetCertificateBySubjectHash( subject_hash );
        if ( certificate_result.has_error() )
        {
            return false;
        }
        auto certificate_check = ValidateCertificate( certificate_result.value() );
        return certificate_check == Check::Approve;
    }

    bool ConsensusManager::CheckCertificateForSubject( const ConsensusManager::Subject &subject ) const
    {
        auto current_hash = GetSubjectHash( subject );
        if ( current_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to get the hash for the subject, error: {}",
                                             __func__,
                                             current_hash.error().message() );
            return false;
        }
        auto certificate_result = GetCertificateBySubjectHash( current_hash.value() );
        if ( certificate_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to get the certificate for the hash {}, error: {}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             certificate_result.error().message() );
            return false;
        }
        auto &certificate                   = certificate_result.value();
        auto  certificate_subject_id_result = ComputeSubjectId( certificate.proposal().subject() );
        if ( certificate_subject_id_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed for hash {}: certificate subject id computation error={}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             certificate_subject_id_result.error().message() );
            return false;
        }
        auto &certificate_subject_id = certificate_subject_id_result.value();
        auto  subject_id_result      = ComputeSubjectId( subject );
        if ( subject_id_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed for hash {}: subject id computation error={}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             subject_id_result.error().message() );
            return false;
        }
        auto proposed_subject_id = subject_id_result.value();
        bool equal               = proposed_subject_id == certificate_subject_id;
        if ( !equal )
        {
            ConsensusManagerLogger()->debug( "{}: Match for subject and certificate (hash {}): MISMATCH",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return false;
        }
        auto certificate_check = ValidateCertificate( certificate );
        if ( certificate_check != Check::Approve )
        {
            ConsensusManagerLogger()->error( "{}: certificate failed validation for hash {}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Match for subject and certificate (hash {}): {}",
                                         __func__,
                                         GetPrintableSubjectHash( subject ),
                                         equal ? "Match" : "MISMATCH" );
        return true;
    }

    std::string ConsensusManager::GetPrintableSubjectHash( const Subject &subject )
    {
        auto              subject_hash = GetSubjectHash( subject );
        const std::string short_hash   = subject_hash.has_value() ? subject_hash.value().substr( 0, 8 ) : "Invalid";
        return short_hash;
    }

}
