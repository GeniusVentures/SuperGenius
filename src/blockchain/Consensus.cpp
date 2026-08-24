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

#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "storage/database_error.hpp"

namespace sgns
{

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
                                                                                 address,
                                                                                 consensus_topic ) );
        instance->certificate_work_journal_ = instance->db_->GetWorkJournal();

        if ( !instance->certificate_work_journal_ )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: crdt work journal is empty",
                                             __func__ );
            return nullptr;
        }

        instance->consensus_subs_future_ = std::move( instance->pubsub_->Subscribe(
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
            } ) );
        ConsensusManagerLogger()->debug( "{}: Subscribed to Consensus topic {}",
                                         __func__,
                                         instance->consensus_messages_topic_ );
        instance->RecoverActiveVotes();
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
        registry_( std::move( registry ) ), //
        db_( std::move( db ) ),             //
        pubsub_( std::move( pubsub ) ),     //
        signer_( std::move( signer ) ),     //
        account_address_( address ),        //
        consensus_messages_topic_( std::string( CONSENSUS_CHANNEL_PREFIX ) + sgns::version::GetNetAndVersionAppendix() +
                                   consensus_topic ),
        consensus_datastore_topic_( consensus_messages_topic_ + "#datastore" )
    {
    }

    ConsensusManager::~ConsensusManager()
    {
        stop_timer_.store( true );
        timer_cv_.notify_all();
        ConsensusManagerLogger()->debug( "{}: Finished shutting down ConsensusManager", __func__ );
    }

    void ConsensusManager::Close()
    {
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

        std::weak_ptr<ConsensusManager> weak_self = shared_from_this();
        round_timer_                              = std::thread(
            [weak_self]()
            {
                constexpr auto min_interval = std::chrono::milliseconds( 500 );
                while ( true )
                {
                    auto self = weak_self.lock();
                    if ( !self )
                    {
                        return;
                    }

                    std::unique_lock<std::mutex> lock( self->timer_mutex_ );
                    auto                         interval = self->round_duration_ / 2;
                    if ( interval.count() <= 0 )
                    {
                        interval = DEFAULT_ROUND_DURATION / 2;
                    }
                    if ( interval < min_interval )
                    {
                        interval = min_interval;
                    }
                    if ( self->certificates_pending_.load() )
                    {
                        // Work is pending: run on cadence, only interrupt for shutdown.
                        self->timer_cv_.wait_for( lock, interval, [self]() { return self->stop_timer_.load(); } );
                    }
                    else
                    {
                        // No pending work: wait up to interval, but wake immediately when new work appears.
                        self->timer_cv_.wait_for(
                            lock,
                            interval,
                            [self]() { return self->stop_timer_.load() || self->certificates_pending_.load(); } );
                    }
                    if ( self->stop_timer_.load() )
                    {
                        return;
                    }
                    lock.unlock();
                    if ( self->certificates_pending_.load() )
                    {
                        self->ProcessCertificates();
                        self->UpdateCertificatesPending();
                    }
                    self->ExpirePendingProposals();
                    self->ProcessDuePendingRetries();
                    self->ProcessDueVoteWork();
                    // Keep replaying unfinished certificate work while the node is running.
                    self->RecoverPendingCertificateWork();
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
        auto publish_result = pubsub_->Publish( consensus_messages_topic_, serialized_proto );
        if ( publish_result.has_error() )
        {
            return outcome::failure( publish_result.error() );
        }
        ConsensusManagerLogger()->debug( "{}: Consensus packet published (bytes={})",
                                         __func__,
                                         serialized_proto.size() );

        return outcome::success();
    }

    bool ConsensusManager::RegisterSubjectHandler( std::string_view subject_type, SubjectHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty handler subject_type={}", __func__, subject_type );
            return false;
        }
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
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty certificate handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
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
        {
            std::unique_lock lock( certificate_handlers_mutex_ );
            certificate_subject_handlers_[type_hash.value()] = std::move( handler );
        }
        // A durable certificate may have arrived before its consumer was constructed.
        // Recover only after releasing the handler map lock because dispatch reads it.
        RecoverPendingCertificateWork();
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
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty cleanup handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
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

    void ConsensusManager::SetPendingLifecycleConfig( PendingLifecycleConfig config )
    {
        std::lock_guard lock( proposals_mutex_ );
        pending_config_ = config;
    }

    void ConsensusManager::RegisterSlotKeyHandler( std::string_view subject_type, SlotKeyHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty slot key handler subject_type={}",
                                             __func__,
                                             subject_type );
            return;
        }
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

        std::vector<ProposalCleanupHandler> handlers_copy;
        {
            std::shared_lock lock( cleanup_handlers_mutex_ );
            auto             it = proposal_cleanup_handlers_.find( proposal.subject().subject_type_hash().hash() );
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

    void ConsensusManager::ConfigureTimestampWindow( std::chrono::milliseconds window )
    {
        if ( window.count() <= 0 )
        {
            ConsensusManagerLogger()->warn( "{}: using default window", __func__ );
            timestamp_window_ = DEFAULT_TIMESTAMP_WINDOW;
            return;
        }
        timestamp_window_ = window;
    }

    void ConsensusManager::ConfigureRoundDuration( std::chrono::milliseconds duration )
    {
        if ( duration.count() <= 0 )
        {
            ConsensusManagerLogger()->warn( "{}: using default round duration", __func__ );
            round_duration_ = DEFAULT_ROUND_DURATION;
            return;
        }
        round_duration_ = duration;
    }

    void ConsensusManager::ConfigureRoundSkew( std::chrono::milliseconds skew )
    {
        if ( skew.count() < 0 )
        {
            ConsensusManagerLogger()->warn( "{}: using default round skew", __func__ );
            round_skew_ = DEFAULT_ROUND_SKEW;
            return;
        }
        round_skew_ = skew;
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
        if ( timestamp_ms == 0 )
        {
            return false;
        }
        const auto now_ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count();
        const auto window_ms = timestamp_window_.count();
        if ( now_ms < 0 || window_ms < 0 )
        {
            return false;
        }

        const auto now_u64    = static_cast<uint64_t>( now_ms );
        const auto window_u64 = static_cast<uint64_t>( window_ms );
        const auto min_ts     = ( now_u64 > window_u64 ) ? ( now_u64 - window_u64 ) : 0ULL;
        const auto max_ts     = ( std::numeric_limits<uint64_t>::max() - now_u64 < window_u64 )
                                    ? std::numeric_limits<uint64_t>::max()
                                    : now_u64 + window_u64;
        return ( timestamp_ms >= min_ts ) && ( timestamp_ms <= max_ts );
    }

    uint64_t ConsensusManager::GetCurrentRound( uint64_t proposal_ts_ms ) const
    {
        if ( proposal_ts_ms == 0 || round_duration_.count() <= 0 )
        {
            return 0;
        }
        const auto now_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch() )
                                 .count();
        const auto elapsed = static_cast<int64_t>( now_ms ) - static_cast<int64_t>( proposal_ts_ms );
        if ( elapsed <= 0 )
        {
            return 0;
        }
        const auto skew_ms = static_cast<int64_t>( round_skew_.count() );
        if ( elapsed <= skew_ms )
        {
            return 0;
        }
        const auto round_ms = static_cast<int64_t>( round_duration_.count() );
        auto       round    = static_cast<uint64_t>( ( elapsed - skew_ms ) / round_ms );
        ConsensusManagerLogger()->debug( "{}: Returning round={}", __func__, round );
        return round;
    }

    std::vector<std::string> ConsensusManager::GetOrderedActiveValidators(
        const ValidatorRegistry::Registry &registry ) const
    {
        std::vector<std::string> validators;
        validators.reserve( registry.validators_size() );
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() == ValidatorRegistry::Status::ACTIVE )
            {
                validators.push_back( entry.validator_id() );
            }
        }
        std::sort( validators.begin(), validators.end() );
        ConsensusManagerLogger()->trace( "{}: Returning validators with size ={}", __func__, validators.size() );
        return validators;
    }

    ConsensusManager::AggregatorRole ConsensusManager::GetAggregatorRole(
        const Proposal                    &proposal,
        const ValidatorRegistry::Registry &registry ) const
    {
        ConsensusManagerLogger()->trace( "{}: Checking local aggregator role for proposal", __func__ );
        auto ordered = GetOrderedActiveValidators( registry );
        if ( ordered.empty() )
        {
            return AggregatorRole::NotInRegistry;
        }

        if ( std::find( ordered.begin(), ordered.end(), account_address_ ) == ordered.end() )
        {
            return AggregatorRole::NotInRegistry;
        }

        auto hash = sgns::crypto::sha2_256( proposal.proposal_id().data(), proposal.proposal_id().size() );
        uint64_t                 base_index = 0;
        for ( size_t i = 0; i < sizeof( uint64_t ) && i < hash.size(); ++i )
        {
            base_index = ( base_index << 8 ) | hash[i];
        }
        base_index = base_index % ordered.size();

        const auto round = GetCurrentRound( proposal.timestamp() );
        const auto index = ( base_index + round ) % ordered.size();

        return ordered[index] == account_address_ ? AggregatorRole::CurrentAggregator
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
        ConsensusManagerLogger()->debug( "{}: Continuing proposal: hash {}, id {}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        const auto slot_key    = GetSlotKey( proposal );
        bool       process_due_work = false;
        bool       candidate_admitted = false;

        ConsensusManagerLogger()->debug( "{}: Slot key acquired: hash {}, id {}, slot key {}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ),
                                         slot_key );
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( proposals_.find( proposal.proposal_id() ) == proposals_.end() )
            {
                ConsensusManagerLogger()->debug(
                    "{}: No proposal state found. Creating... : hash {}, id {}, slot key {}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal.proposal_id().substr( 0, 8 ),
                    slot_key );
                ProposalState state;
                state.proposal = proposal;
                state.slot_key = slot_key;
                proposals_.emplace( proposal.proposal_id(), std::move( state ) );
            }

            auto &slot_state = slot_states_[slot_key];
            if ( slot_state.active_vote_locked )
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            auto accepted_certificate = HasAcceptedCertificateForSlot( slot_key );
            if ( accepted_certificate.has_error() )
            {
                // An incomplete legacy scan is never evidence that a slot is unfinalized.
                slot_state.certificate_scan_pending = true;
                if ( ( slot_state.candidate_deadline == std::chrono::steady_clock::time_point{} ||
                       now < slot_state.candidate_deadline ) &&
                     std::none_of( slot_state.scan_pending_candidates.begin(),
                                   slot_state.scan_pending_candidates.end(),
                                   [&proposal]( const ScanPendingCandidate &candidate )
                                   { return candidate.proposal.proposal_id() == proposal.proposal_id(); } ) )
                {
                    slot_state.scan_pending_candidates.push_back( { proposal, now } );
                }
                return;
            }
            slot_state.certificate_scan_pending = false;
            if ( accepted_certificate.value() )
            {
                // Existing accepted legacy data is only a local no-revote fence in Phase 9.
                slot_state.active_vote_locked = true;
                slot_state.candidates_frozen  = true;
                return;
            }
            if ( slot_state.candidate_deadline == std::chrono::steady_clock::time_point{} )
            {
                slot_state.candidate_deadline = now + candidate_window_;
            }
            if ( now < slot_state.candidate_deadline )
            {
                for ( const auto &pending_candidate : slot_state.scan_pending_candidates )
                {
                    if ( pending_candidate.admitted_at < slot_state.candidate_deadline &&
                         std::none_of( slot_state.eligible_candidates.begin(),
                                       slot_state.eligible_candidates.end(),
                                       [&pending_candidate]( const Proposal &candidate )
                                       { return candidate.proposal_id() == pending_candidate.proposal.proposal_id(); } ) )
                    {
                        slot_state.eligible_candidates.push_back( pending_candidate.proposal );
                    }
                }
            }
            slot_state.scan_pending_candidates.clear();
            if ( now >= slot_state.candidate_deadline )
            {
                process_due_work = true;
            }
            else if ( !slot_state.candidates_frozen &&
                      std::none_of( slot_state.eligible_candidates.begin(),
                                    slot_state.eligible_candidates.end(),
                                    [&proposal]( const Proposal &candidate )
                                    { return candidate.proposal_id() == proposal.proposal_id(); } ) )
            {
                slot_state.eligible_candidates.push_back( proposal );
                candidate_admitted = true;
            }
            if ( candidate_admitted && slot_state.best_proposal_id.empty() )
            {
                ConsensusManagerLogger()->debug( "{}: Configuring best proposal for hash {}, id={}, slot key {}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 slot_key );
                slot_state.best_proposal_id = proposal.proposal_id();
                auto nonce_payload          = DecodeNonceSubject( proposal.subject() );
                if ( nonce_payload.has_value() )
                {
                    slot_state.best_tx_hash = nonce_payload.value().tx_hash();
                }
            }
            else if ( candidate_admitted )
            {
                const auto &current = proposals_.at( slot_state.best_proposal_id ).proposal;
                ConsensusManagerLogger()->debug(
                    "{}: Already have a best proposal for hash {}, id={}, slot key {}. Seeing if {} is better ",
                    __func__,
                    GetPrintableSubjectHash( current.subject() ),
                    current.proposal_id().substr( 0, 8 ),
                    slot_key,
                    proposal.proposal_id().substr( 0, 8 ) );
                if ( IsBetterProposal( proposal, current ) )
                {
                    ConsensusManagerLogger()->debug( "{}: Better proposal for hash {}, id={}, slot key {}. ",
                                                     __func__,
                                                     GetPrintableSubjectHash( proposal.subject() ),
                                                     proposal.proposal_id().substr( 0, 8 ),
                                                     slot_key );
                    slot_state.best_proposal_id = proposal.proposal_id();
                    auto nonce_payload          = DecodeNonceSubject( proposal.subject() );
                    if ( nonce_payload.has_value() )
                    {
                        slot_state.best_tx_hash = nonce_payload.value().tx_hash();
                    }
                }
            }

        }

        auto pending_votes = TakePendingVotes( proposal.proposal_id() );
        for ( const auto &vote : pending_votes )
        {
            HandleVote( vote );
        }

        if ( process_due_work )
        {
            ProcessDueVoteWork();
        }
    }

    bool ConsensusManager::CanAdmitPendingProposalLocked( const Proposal    &proposal,
                                                          std::size_t        retained_bytes,
                                                          const std::string &proposer_id ) const
    {
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
        return true;
    }

    std::vector<ConsensusManager::PendingDependencyKey> ConsensusManager::NormalizePendingDependencies(
        const std::string      &subject_hash,
        const ValidationResult &validation_result ) const
    {
        if ( !validation_result.dependencies.empty() )
        {
            return validation_result.dependencies;
        }
        return { PendingDependencyKey::Certificate( subject_hash ) };
    }

    std::chrono::milliseconds ConsensusManager::NextPendingRetryDelayLocked( const PendingProposalEntry &entry ) const
    {
        if ( entry.retry_after.has_value() )
        {
            return entry.retry_after.value();
        }
        if ( pending_config_.scheduled_retry_delays.empty() )
        {
            return std::chrono::seconds( 10 );
        }
        const auto index = std::min( entry.scheduled_retry_count, pending_config_.scheduled_retry_delays.size() - 1 );
        return pending_config_.scheduled_retry_delays[index];
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

        const auto  dependencies   = NormalizePendingDependencies( subject_hash, validation_result );
        const auto  retained_bytes = static_cast<std::size_t>( proposal.ByteSizeLong() );
        const auto &proposer_id    = proposal.proposer_id();
        if ( !CanAdmitPendingProposalLocked( proposal, retained_bytes, proposer_id ) )
        {
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Adding pending proposal for {}: proposal with id {}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        const auto now = std::chrono::steady_clock::now();

        PendingProposalEntry entry;
        entry.proposal              = proposal;
        entry.dependencies          = dependencies;
        entry.admitted_at           = now;
        entry.expires_at            = now + pending_config_.pending_ttl;
        entry.last_retry_at         = last_retry_at;
        entry.retry_after           = validation_result.retry_after;
        entry.retained_bytes        = retained_bytes;
        entry.proposer_id           = proposer_id;
        entry.scheduled_retry_count = scheduled_retry_count;
        entry.next_retry_at         = now + NextPendingRetryDelayLocked( entry );

        pending_retained_bytes_                 += retained_bytes;
        pending_count_by_proposer_[proposer_id] += 1;
        for ( const auto &dependency : dependencies )
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
        const std::vector<std::string> proposal_ids( it->second.begin(), it->second.end() );
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
        auto entry_it = pending_entries_.find( proposal_id );
        if ( entry_it == pending_entries_.end() )
        {
            pending_votes_.erase( proposal_id );
            return false;
        }

        const auto retained_bytes = entry_it->second.retained_bytes;
        if ( pending_retained_bytes_ >= retained_bytes )
        {
            pending_retained_bytes_ -= retained_bytes;
        }
        else
        {
            pending_retained_bytes_ = 0;
        }

        auto proposer_it = pending_count_by_proposer_.find( entry_it->second.proposer_id );
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

        for ( const auto &dependency : entry_it->second.dependencies )
        {
            auto dep_it = pending_by_dependency_.find( dependency );
            if ( dep_it != pending_by_dependency_.end() )
            {
                dep_it->second.erase( proposal_id );
                if ( dep_it->second.empty() )
                {
                    pending_by_dependency_.erase( dep_it );
                }
            }
        }

        pending_entries_.erase( entry_it );
        pending_votes_.erase( proposal_id );
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
        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( proposal.subject().subject_type_hash().hash() );
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

    std::string ConsensusManager::ActiveVoteStorageKey( std::string_view slot_key ) const
    {
        return std::string( ACTIVE_VOTE_BASE_PATH_KEY ) + std::string( slot_key );
    }

    outcome::result<bool> ConsensusManager::HasAcceptedCertificateForSlot( const std::string &slot_key ) const
    {
        if ( slot_key.empty() || !db_ )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( fail_accepted_certificate_scan_for_test_ )
        {
            return outcome::failure( std::errc::io_error );
        }
        const auto key = std::string( CERTIFICATE_BASE_PATH_KEY ) + slot_key;
        auto value = db_->Get( { key } );
        if ( value.has_error() )
        {
            if ( value.error() == storage::DatabaseError::NOT_FOUND )
            {
                return false;
            }
            return outcome::failure( value.error() );
        }
        Certificate certificate;
        if ( !certificate.ParseFromArray( value.value().data(), value.value().size() ) ||
             !ValidateCertificateKey( certificate, key ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        const auto validation = ValidateCertificate( certificate );
        if ( validation == Check::Pending || validation == Check::Stalled )
        {
            return outcome::failure( std::errc::resource_unavailable_try_again );
        }
        return validation == Check::Approve;
    }

    outcome::result<bool> ConsensusManager::ReleaseActiveVoteForAcceptedSlot( const std::string &slot_key )
    {
        if ( slot_key.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        auto datastore = db_ ? db_->GetDataStore() : nullptr;
        if ( !datastore )
        {
            return outcome::failure( std::errc::bad_file_descriptor );
        }
        crdt::GlobalDB::Buffer key;
        key.put( ActiveVoteStorageKey( slot_key ) );
        auto existing = datastore->get( key );
        if ( existing.has_error() )
        {
            if ( existing.error() == storage::DatabaseError::NOT_FOUND )
            {
                return false;
            }
            return outcome::failure( existing.error() );
        }
        auto decoded = DecodeActiveVoteRecord( slot_key, existing.value().toString() );
        if ( decoded.has_error() )
        {
            return outcome::failure( decoded.error() );
        }
        if ( fail_active_vote_removal_for_test_ )
        {
            return outcome::failure( std::errc::io_error );
        }
        auto removed = datastore->remove( key );
        if ( removed.has_error() )
        {
            return outcome::failure( removed.error() );
        }
        std::lock_guard lock( proposals_mutex_ );
        active_votes_.erase( slot_key );
        return true;
    }

    outcome::result<ActiveVoteRecord> ConsensusManager::BuildActiveVoteRecord( const std::string &slot_key,
                                                                                 const Proposal &proposal,
                                                                                 const Vote &vote,
                                                                                 uint64_t acceptance_deadline_ms ) const
    {
        std::string proposal_bytes;
        std::string vote_bytes;
        if ( slot_key.empty() || acceptance_deadline_ms == 0 || !proposal.SerializeToString( &proposal_bytes ) ||
             !vote.SerializeToString( &vote_bytes ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        ActiveVoteRecord record;
        record.set_canonical_slot( slot_key );
        record.set_proposal_bytes( proposal_bytes );
        record.set_vote_bytes( vote_bytes );
        record.set_acceptance_deadline_ms( acceptance_deadline_ms );
        return record;
    }

    outcome::result<ConsensusManager::ActiveVoteState> ConsensusManager::DecodeActiveVoteRecord(
        const std::string &slot_key,
        std::string_view serialized ) const
    {
        ActiveVoteRecord record;
        if ( slot_key.empty() || serialized.empty() || !record.ParseFromArray( serialized.data(), serialized.size() ) ||
             record.canonical_slot() != slot_key || record.proposal_bytes().empty() || record.vote_bytes().empty() ||
             record.acceptance_deadline_ms() == 0 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        ActiveVoteState state;
        if ( !state.proposal.ParseFromString( record.proposal_bytes() ) || !state.vote.ParseFromString( record.vote_bytes() ) ||
             GetSlotKey( state.proposal ) != slot_key || state.vote.proposal_id() != state.proposal.proposal_id() ||
             state.vote.voter_id() != account_address_ || !state.vote.approve() || !CheckVote( state.vote ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        auto signing_bytes = VoteSigningBytes( state.vote );
        if ( signing_bytes.has_error() ||
             !GeniusAccount::VerifySignature( state.vote.voter_id(), state.vote.signature(), signing_bytes.value() ) )
        {
            return outcome::failure( std::errc::permission_denied );
        }
        state.acceptance_deadline_ms = record.acceptance_deadline_ms();
        state.next_retry_at          = std::chrono::steady_clock::now();
        return state;
    }

    outcome::result<ConsensusManager::ActiveVoteState> ConsensusManager::PersistOrLoadExactActiveVote(
        const std::string &slot_key,
        const Proposal &proposal,
        const Vote &vote,
        uint64_t acceptance_deadline_ms )
    {
        auto record = BuildActiveVoteRecord( slot_key, proposal, vote, acceptance_deadline_ms );
        if ( record.has_error() || fail_active_vote_persistence_for_test_ )
        {
            return outcome::failure( record.has_error() ? record.error() : std::make_error_code( std::errc::io_error ) );
        }
        std::string encoded;
        if ( !record.value().SerializeToString( &encoded ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        auto datastore = db_ ? db_->GetDataStore() : nullptr;
        if ( !datastore )
        {
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        crdt::GlobalDB::Buffer key;
        key.put( ActiveVoteStorageKey( slot_key ) );
        auto existing = datastore->get( key );
        if ( existing.has_value() )
        {
            const std::string existing_bytes( existing.value().toString() );
            if ( existing_bytes != encoded )
            {
                return outcome::failure( std::errc::operation_not_permitted );
            }
            return DecodeActiveVoteRecord( slot_key, existing_bytes );
        }
        if ( existing.error() != storage::DatabaseError::NOT_FOUND )
        {
            return outcome::failure( existing.error() );
        }

        crdt::GlobalDB::Buffer value;
        value.put( encoded );
        auto put = datastore->put( key, value );
        if ( put.has_error() )
        {
            return outcome::failure( put.error() );
        }
        return DecodeActiveVoteRecord( slot_key, encoded );
    }

    void ConsensusManager::RecoverActiveVotes()
    {
        auto datastore = db_ ? db_->GetDataStore() : nullptr;
        if ( !datastore )
        {
            return;
        }
        crdt::GlobalDB::Buffer prefix;
        prefix.put( ACTIVE_VOTE_BASE_PATH_KEY );
        auto records = datastore->query( prefix );
        if ( records.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed to enumerate durable active votes: {}", __func__, records.error().message() );
            return;
        }
        const auto now_ms = static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                                             std::chrono::system_clock::now().time_since_epoch() )
                                                             .count() );
        std::lock_guard lock( proposals_mutex_ );
        for ( const auto &[key, value] : records.value() )
        {
            const std::string key_string( key.toString() );
            if ( key_string.rfind( std::string( ACTIVE_VOTE_BASE_PATH_KEY ), 0 ) != 0 )
            {
                continue;
            }
            const auto slot_key = key_string.substr( ACTIVE_VOTE_BASE_PATH_KEY.size() );
            auto decoded = DecodeActiveVoteRecord( slot_key, value.toString() );
            if ( decoded.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: ignored invalid durable active vote slot={}", __func__, slot_key );
                continue;
            }
            auto &slot_state = slot_states_[slot_key];
            slot_state.active_vote_locked = true;
            slot_state.candidates_frozen  = true;
            slot_state.best_proposal_id   = decoded.value().proposal.proposal_id();
            slot_state.voted_proposal_ids.insert( decoded.value().proposal.proposal_id() );
            auto accepted_certificate = HasAcceptedCertificateForSlot( slot_key );
            if ( accepted_certificate.has_error() )
            {
                slot_state.certificate_scan_pending = true;
                if ( now_ms < decoded.value().acceptance_deadline_ms )
                {
                    active_votes_[slot_key] = std::move( decoded.value() );
                }
                continue;
            }
            slot_state.certificate_scan_pending = false;
            if ( accepted_certificate.value() )
            {
                continue;
            }
            if ( now_ms < decoded.value().acceptance_deadline_ms )
            {
                active_votes_[slot_key] = std::move( decoded.value() );
            }
        }
    }

    void ConsensusManager::ProcessDueVoteWork()
    {
        std::vector<ActiveVoteState> publish;
        const auto now_steady = std::chrono::steady_clock::now();
        const auto now_ms = static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::system_clock::now().time_since_epoch() )
                                                         .count() );
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto &[slot_key, slot_state] : slot_states_ )
            {
                if ( slot_state.active_vote_locked || slot_state.candidates_frozen )
                {
                    continue;
                }
                if ( slot_state.certificate_scan_pending )
                {
                    auto accepted_certificate = HasAcceptedCertificateForSlot( slot_key );
                    if ( accepted_certificate.has_error() )
                    {
                        continue;
                    }
                    slot_state.certificate_scan_pending = false;
                    if ( accepted_certificate.value() )
                    {
                        slot_state.active_vote_locked = true;
                        slot_state.candidates_frozen  = true;
                        continue;
                    }
                    if ( slot_state.candidate_deadline == std::chrono::steady_clock::time_point{} )
                    {
                        // No window existed while the scan was unavailable. Start the
                        // fixed window at recovery, retaining every validated contender.
                        slot_state.candidate_deadline = now_steady + candidate_window_;
                    }
                    if ( now_steady < slot_state.candidate_deadline )
                    {
                        for ( const auto &pending_candidate : slot_state.scan_pending_candidates )
                        {
                            if ( pending_candidate.admitted_at < slot_state.candidate_deadline &&
                                 std::none_of( slot_state.eligible_candidates.begin(),
                                               slot_state.eligible_candidates.end(),
                                               [&pending_candidate]( const Proposal &candidate )
                                               { return candidate.proposal_id() == pending_candidate.proposal.proposal_id(); } ) )
                            {
                                slot_state.eligible_candidates.push_back( pending_candidate.proposal );
                            }
                        }
                    }
                    // A successful scan after the original deadline deliberately drops
                    // retained contenders rather than retroactively extending the window.
                    slot_state.scan_pending_candidates.clear();
                }
                if ( slot_state.candidate_deadline == std::chrono::steady_clock::time_point{} ||
                    now_steady < slot_state.candidate_deadline )
                {
                    continue;
                }
                auto accepted_certificate = HasAcceptedCertificateForSlot( slot_key );
                if ( accepted_certificate.has_error() )
                {
                    slot_state.certificate_scan_pending = true;
                    continue;
                }
                slot_state.certificate_scan_pending = false;
                if ( accepted_certificate.value() )
                {
                    slot_state.active_vote_locked = true;
                    slot_state.candidates_frozen  = true;
                    continue;
                }
                slot_state.candidates_frozen = true;
                if ( slot_state.eligible_candidates.empty() )
                {
                    continue;
                }
                const Proposal *winner = &slot_state.eligible_candidates.front();
                for ( const auto &candidate : slot_state.eligible_candidates )
                {
                    if ( IsBetterProposal( candidate, *winner ) )
                    {
                        winner = &candidate;
                    }
                }
                slot_state.best_proposal_id = winner->proposal_id();
                auto vote = CreateVote( winner->proposal_id(), account_address_, true, signer_ );
                if ( vote.has_error() )
                {
                    continue;
                }
                const auto deadline_ms = now_ms + static_cast<uint64_t>( candidate_window_.count() );
                auto active_vote = PersistOrLoadExactActiveVote( slot_key, *winner, vote.value(), deadline_ms );
                if ( active_vote.has_error() )
                {
                    ConsensusManagerLogger()->error( "{}: failed to persist active vote slot={}: {}",
                                                     __func__, slot_key, active_vote.error().message() );
                    continue;
                }
                slot_state.active_vote_locked = true;
                slot_state.voted_proposal_ids.insert( active_vote.value().proposal.proposal_id() );
                active_vote.value().next_retry_at = now_steady + active_vote_retry_interval_;
                publish.push_back( active_vote.value() );
                active_votes_[slot_key] = std::move( active_vote.value() );
            }

            for ( auto &[slot_key, active_vote] : active_votes_ )
            {
                auto accepted_certificate = HasAcceptedCertificateForSlot( slot_key );
                auto slot_state_it = slot_states_.find( slot_key );
                if ( accepted_certificate.has_error() )
                {
                    if ( slot_state_it != slot_states_.end() )
                    {
                        slot_state_it->second.certificate_scan_pending = true;
                    }
                    continue;
                }
                if ( slot_state_it != slot_states_.end() )
                {
                    slot_state_it->second.certificate_scan_pending = false;
                }
                if ( accepted_certificate.value() )
                {
                    continue;
                }
                if ( now_ms >= active_vote.acceptance_deadline_ms || now_steady < active_vote.next_retry_at )
                {
                    continue;
                }
                active_vote.next_retry_at = now_steady + active_vote_retry_interval_;
                publish.push_back( active_vote );
            }
        }
        for ( const auto &active_vote : publish )
        {
            std::string bytes;
            if ( !active_vote.vote.SerializeToString( &bytes ) )
            {
                continue;
            }
            active_vote_announcements_for_test_.push_back( bytes );
            (void) SubmitVote( active_vote.vote );
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

    void ConsensusManager::AddPendingVote( const Vote &vote )
    {
        std::lock_guard lock( proposals_mutex_ );
        pending_votes_[vote.proposal_id()].push_back( vote );
    }

    std::vector<ConsensusManager::Vote> ConsensusManager::TakePendingVotes( const std::string &proposal_id )
    {
        std::vector<Vote> result;
        std::lock_guard   lock( proposals_mutex_ );
        auto              it = pending_votes_.find( proposal_id );
        if ( it == pending_votes_.end() )
        {
            return result;
        }
        result = std::move( it->second );
        pending_votes_.erase( it );
        return result;
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
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: failed for hash {}: signer is empty",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return outcome::failure( std::errc::invalid_argument );
        }

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
        auto signing_bytes = ProposalSigningBytes( proposal );
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
        ConsensusManagerLogger()->trace( "{}: called by {}: proposal_id={} approve={}",
                                         __func__,
                                         voter_id.substr( 0, 8 ),
                                         proposal_id.substr( 0, 8 ),
                                         approve );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: failed: signer is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        Vote vote;
        vote.set_proposal_id( proposal_id );
        vote.set_voter_id( voter_id );
        vote.set_approve( approve );
        vote.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        auto signing_bytes = VoteSigningBytes( vote );
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

    outcome::result<ConsensusManager::VoteBundle> ConsensusManager::CreateVoteBundle( const std::string &proposal_id,
                                                                                      const std::string &aggregator_id,
                                                                                      const std::vector<Vote> &votes,
                                                                                      Signer                   sign )
    {
        ConsensusManagerLogger()->trace( "{}: called by {}: proposal_id={} votes={}",
                                         __func__,
                                         aggregator_id.substr( 0, 8 ),
                                         proposal_id.substr( 0, 8 ),
                                         votes.size() );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: failed: signer is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        VoteBundle bundle;
        bundle.set_proposal_id( proposal_id );
        bundle.set_aggregator_id( aggregator_id );
        bundle.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        for ( const auto &vote : votes )
        {
            *bundle.add_votes() = vote;
        }

        auto signing_bytes = VoteBundleSigningBytes( bundle );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }

        BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        bundle.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug(
            "{}: Vote bundle created successfully by {}: proposal_id={} number of votes={}",
            __func__,
            aggregator_id.substr( 0, 8 ),
            proposal_id.substr( 0, 8 ),
            votes.size() );
        return bundle;
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
            if ( vote.timestamp() > max_vote_ts )
            {
                max_vote_ts = vote.timestamp();
            }
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

            auto signing_bytes = VoteSigningBytes( vote );
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
        tally.has_quorum      = registry_->IsQuorum( approved_weight, total_weight );
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

    outcome::result<std::vector<uint8_t>> ConsensusManager::ProposalSigningBytes( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        return sgns::ProposalSigningBytes( proposal );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteSigningBytes( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: called with voter address {} proposal_id={}",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id() );
        return sgns::VoteSigningBytes( vote );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteBundleSigningBytes( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         bundle.proposal_id().substr( 0, 8 ),
                                         bundle.votes_size() );
        return sgns::VoteBundleSigningBytes( bundle );
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
        if ( ValidateCertificate( certificate ) != Check::Approve )
        {
            ConsensusManagerLogger()->error( "{}: rejected invalid certificate proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return outcome::failure( std::errc::invalid_argument );
        }
        std::string serialized;
        if ( !certificate.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate serialize error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto key = GetExpectedCertificateSlotKey( certificate );
        if ( key.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        crdt::HierarchicalKey  cert_key( key );
        crdt::GlobalDB::Buffer cert_value;
        cert_value.put( serialized );

        auto existing = db_->Get( { key } );
        if ( existing.has_value() )
        {
            Certificate existing_certificate;
            if ( !existing_certificate.ParseFromArray( existing.value().data(), existing.value().size() ) ||
                 !ValidateCertificateKey( existing_certificate, key ) ||
                 ValidateCertificate( existing_certificate ) != Check::Approve )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto existing_hash = crypto::sha2_256( existing.value().data(), existing.value().size() );
            const auto candidate_hash = crypto::sha2_256( serialized.data(), serialized.size() );
            if ( base::hex_lower( gsl::span<const uint8_t>( existing_hash.data(), existing_hash.size() ) ) <
                 base::hex_lower( gsl::span<const uint8_t>( candidate_hash.data(), candidate_hash.size() ) ) )
            {
                return outcome::success();
            }
        }
        else if ( existing.error() != storage::DatabaseError::NOT_FOUND )
        {
            return outcome::failure( existing.error() );
        }

        auto cert_put = db_->PutConvergentImmutable( cert_key, cert_value, { consensus_datastore_topic_ } );
        if ( cert_put.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: cert put for hash {} error={}",
                                             __func__,
                                             GetPrintableSubjectHash( certificate.proposal().subject() ),
                                             cert_put.error().message() );
            return outcome::failure( cert_put.error() );
        }

        ConsensusMessage message;
        *message.mutable_certificate() = certificate;
        auto result                    = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: certificate persisted but notification failed: {}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: success submitting certificate for {} and proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( certificate.proposal().subject() ),
                                         certificate.proposal_id().substr( 0, 8 ) );
        return outcome::success();
    }

    void ConsensusManager::HandleProposal( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );

        if ( !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Invalid proposal for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        if ( !IsTimestampSane( proposal.timestamp() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: timestamp out of bounds for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: proposal registry CID missing for hash {}. proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        auto subject_hash = GetSubjectHash( proposal.subject() );
        if ( subject_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
        if ( proposal_registry_result.has_error() )
        {
            ConsensusManagerLogger()->warn(
                "{}: deferred: registry load error={} proposal={} proposal_id={} hash={}. Keeping proposal pending",
                __func__,
                proposal_registry_result.error().message(),
                proposal.registry_cid(),
                proposal.proposal_id().substr( 0, 8 ),
                subject_hash.value().substr( 0, 8 ) );

            {
                std::lock_guard lock( proposals_mutex_ );
                if ( proposals_.find( proposal.proposal_id() ) == proposals_.end() )
                {
                    ProposalState state;
                    state.proposal = proposal;
                    state.slot_key = GetSlotKey( proposal );
                    proposals_.emplace( proposal.proposal_id(), std::move( state ) );
                }
            }

            AddPendingProposal( proposal, subject_hash.value() );
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

        if ( !CheckSubject( proposal.subject() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        auto accepted_certificate = HasAcceptedCertificateForSlot( GetSlotKey( proposal ) );
        if ( accepted_certificate.has_error() )
        {
            return;
        }
        if ( accepted_certificate.value() )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: subject already certified hash={} proposal_id={}",
                                             __func__,
                                             subject_hash.value().substr( 0, 8 ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            std::lock_guard lock( proposals_mutex_ );
            RemovePendingProposalLocked( proposal.proposal_id(), "already-certified" );
            return;
        }

        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( proposal.subject().subject_type_hash().hash() );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: subject handler missing type_hash={}",
                    __func__,
                    base::hex_lower( gsl::span<const uint8_t>(
                        reinterpret_cast<const uint8_t *>( proposal.subject().subject_type_hash().hash().data() ),
                        proposal.subject().subject_type_hash().hash().size() ) ) );
                return;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( proposal.subject() );
        if ( subject_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        const auto &validation_result = subject_result.value();
        if ( validation_result.check == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        if ( validation_result.check == Check::Stalled )
        {
            ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled for hash {} proposal_id={}",
                                            __func__,
                                            GetPrintableSubjectHash( proposal.subject() ),
                                            proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        if ( validation_result.check == Check::Pending )
        {
            {
                std::lock_guard lock( proposals_mutex_ );
                if ( proposals_.find( proposal.proposal_id() ) == proposals_.end() )
                {
                    ProposalState state;
                    state.proposal = proposal;
                    state.slot_key = GetSlotKey( proposal );
                    proposals_.emplace( proposal.proposal_id(), std::move( state ) );
                }
            }
            ConsensusManagerLogger()->debug( "{}: Adding pending proposal for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            AddPendingProposal( proposal, subject_hash.value(), validation_result );
            return;
        }

        ContinueProposalAfterSubject( proposal );
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

        auto to_process = TakePendingProposals( subject_hash );

        for ( const auto &proposal : to_process )
        {
            SubjectHandler subject_handler;
            {
                std::shared_lock lock( subject_handlers_mutex_ );
                auto             handler_it = subject_handlers_.find( proposal.subject().subject_type_hash().hash() );
                if ( handler_it == subject_handlers_.end() )
                {
                    ConsensusManagerLogger()->error(
                        "{}: rejected: subject handler missing type_hash={}",
                        __func__,
                        base::hex_lower( gsl::span<const uint8_t>(
                            reinterpret_cast<const uint8_t *>( proposal.subject().subject_type_hash().hash().data() ),
                            proposal.subject().subject_type_hash().hash().size() ) ) );
                    continue;
                }
                subject_handler = handler_it->second;
            }

            auto subject_result = subject_handler( proposal.subject() );
            if ( subject_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject handler error for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            const auto &validation_result = subject_result.value();
            if ( validation_result.check == Check::Reject )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            if ( validation_result.check == Check::Stalled )
            {
                ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled for hash {} proposal_id={}",
                                                __func__,
                                                subject_hash.substr( 0, 8 ),
                                                proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            if ( validation_result.check == Check::Pending )
            {
                auto subject_hash_result = GetSubjectHash( proposal.subject() );
                if ( subject_hash_result.has_error() )
                {
                    ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                                     __func__,
                                                     proposal.proposal_id() );
                    continue;
                }
                ConsensusManagerLogger()->debug( "{}: Adding pending proposal for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                AddPendingProposal( proposal, subject_hash_result.value(), validation_result );
                continue;
            }

            ContinueProposalAfterSubject( proposal );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessCertificates()
    {
        std::vector<ProposalState> to_process;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto &kv : proposals_ )
            {
                auto &state = kv.second;
                if ( !state.quorum_reached )
                {
                    ConsensusManagerLogger()->debug(
                        "{}: Found proposal without quorum reached for hash {} proposal_id={}",
                        __func__,
                        GetPrintableSubjectHash( state.proposal.subject() ),
                        state.proposal.proposal_id().substr( 0, 8 ) );

                    continue;
                }
                to_process.push_back( state );
            }
        }

        for ( auto &state : to_process )
        {
            auto accepted_certificate = HasAcceptedCertificateForSlot( state.slot_key );
            if ( accepted_certificate.has_error() )
            {
                continue;
            }
            if ( accepted_certificate.value() )
            {
                ConsensusManagerLogger()->debug( "{}: slot {} already certified, clearing proposal_id={}",
                                                 __func__,
                                                 state.slot_key.substr( 0, 8 ),
                                                 state.proposal.proposal_id().substr( 0, 8 ) );
                ClearProposalSlot( state.proposal );
                continue;
            }
            ConsensusManagerLogger()->debug( "{}: Processing proposal with quorum reached for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( state.proposal.subject() ),
                                             state.proposal.proposal_id().substr( 0, 8 ) );
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch() )
                                    .count();
            if ( state.quorum_reached_ts_ms != 0 && certificate_delay_.count() > 0 )
            {
                const auto elapsed_ms = static_cast<int64_t>( now_ms ) -
                                        static_cast<int64_t>( state.quorum_reached_ts_ms );
                if ( elapsed_ms < static_cast<int64_t>( certificate_delay_.count() ) )
                {
                    continue;
                }
            }

            const auto round = GetCurrentRound( state.proposal.timestamp() );
            if ( state.last_attempt_round != NO_ROUND && round == state.last_attempt_round )
            {
                ConsensusManagerLogger()->debug(
                    "{}: proposal already attempted in round for hash {} proposal_id={} round={}",
                    __func__,
                    GetPrintableSubjectHash( state.proposal.subject() ),
                    state.proposal.proposal_id().substr( 0, 8 ),
                    round );
                continue;
            }
            auto proposal_registry_result = registry_->LoadRegistryByCid( state.proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry load error={} proposal_id={}",
                                                 __func__,
                                                 proposal_registry_result.error().message(),
                                                 state.proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( state.proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry epoch mismatch proposal_id={}",
                                                 __func__,
                                                 state.proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            const auto aggregator_role = GetAggregatorRole( state.proposal, proposal_registry );
            if ( aggregator_role == AggregatorRole::NotInRegistry )
            {
                ConsensusManagerLogger()->debug(
                    "{}: local node not in proposal registry; clearing local proposal for hash {} proposal_id={}",
                    __func__,
                    GetPrintableSubjectHash( state.proposal.subject() ),
                    state.proposal.proposal_id().substr( 0, 8 ) );
                ClearProposalSlot( state.proposal );
                continue;
            }

            if ( aggregator_role == AggregatorRole::ActiveButNotAggregator )
            {
                ConsensusManagerLogger()->debug( "{}: not aggregator for proposal for hash {} proposal_id={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( state.proposal.subject() ),
                                                 state.proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            {
                std::lock_guard lock( proposals_mutex_ );
                auto            it = proposals_.find( state.proposal.proposal_id() );
                if ( it != proposals_.end() )
                {
                    it->second.last_attempt_round = round;
                }
            }
            ConsensusManagerLogger()->debug( "{}: Attempting to create certificate for hash {} proposal_id={} round={}",
                                             __func__,
                                             GetPrintableSubjectHash( state.proposal.subject() ),
                                             state.proposal.proposal_id().substr( 0, 8 ),
                                             round );
            auto certificate_result = CreateCertificate( state.proposal, state.votes );
            if ( certificate_result.has_error() )
            {
                ConsensusManagerLogger()->error(
                    "{}: failed: certificate creation error for hash {} proposal_id {}: {}",
                    __func__,
                    GetPrintableSubjectHash( state.proposal.subject() ),
                    state.proposal.proposal_id().substr( 0, 8 ),
                    certificate_result.error().message() );
                continue;
            }

            if ( ValidateCertificate( certificate_result.value() ) != Check::Approve )
            {
                ConsensusManagerLogger()->error( "{}: rejected invalid generated certificate proposal_id={}",
                                                 __func__,
                                                 state.proposal.proposal_id() );
                continue;
            }
            if ( SubmitCertificate( certificate_result.value() ).has_error() )
            {
                continue;
            }
            ClearProposalSlot( state.proposal );
            ConsensusManagerLogger()->debug( "{}: certificate submitted for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( state.proposal.subject() ),
                                             state.proposal.proposal_id().substr( 0, 8 ) );
        }
    }

    void ConsensusManager::UpdateCertificatesPending()
    {
        bool has_pending = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( const auto &kv : proposals_ )
            {
                if ( kv.second.quorum_reached )
                {
                    has_pending = true;
                    break;
                }
            }
        }
        certificates_pending_.store( has_pending );
        if ( !has_pending )
        {
            timer_cv_.notify_all();
        }
    }

    bool ConsensusManager::RegisterCertificateFilter()
    {
        const std::string pattern = std::string( CERT_KEY_PATTERN );

        auto       weak_self         = weak_from_this();
        const bool filter_registered = db_->RegisterElementFilter(
            pattern,
            [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_self.lock() )
                {
                    return strong->FilterCertificate( element );
                }
                return std::nullopt;
            } );

        const bool callback_registered = db_->RegisterNewElementCallback(
            pattern,
            [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->CertificateReceived( std::move( new_data ), cid );
                }
            } );

        db_->AddListenTopic( consensus_datastore_topic_ );

        return filter_registered && callback_registered;
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

        if ( !ValidateCertificateKey( certificate, element.key() ) )
        {
            ConsensusManagerLogger()->error( "{}: slot key binding failed, rejecting: {}", __func__, element.key() );
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

    void ConsensusManager::CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                const std::string                     &cid )
    {
        auto [key, value] = new_data;
        (void) cid;
        (void) value;
        // CrdtSet invokes this callback before committing its batch. Receipt therefore
        // cannot authorize certificate handling or removal of the local vote lock.
        certificate_work_journal_->MarkSeen( key );
        certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
        timer_cv_.notify_all();
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

        if ( !ValidateCertificateBinding( certificate ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: canonical slot binding failed proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return Check::Reject;
        }

        auto registry_ret = registry_->LoadRegistryByCid( certificate.registry_cid() );
        if ( registry_ret.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: registry load pending error={} for registry cid {} proposal_id={}",
                                             __func__,
                                             registry_ret.error().message(),
                                             certificate.registry_cid(),
                                             certificate.proposal_id() );
            return Check::Stalled;
        }
        auto &registry = registry_ret.value();

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

    bool ConsensusManager::ValidateCertificateBinding( const Certificate &certificate )
    {
        return certificate.has_proposal() && !GetSlotKey( certificate.proposal() ).empty();
    }

    bool ConsensusManager::ValidateCertificateKey( const Certificate &certificate, std::string_view key )
    {
        if ( !ValidateCertificateBinding( certificate ) )
        {
            return false;
        }
        return key == GetExpectedCertificateSlotKey( certificate );
    }

    std::string ConsensusManager::GetExpectedCertificateSlotKey( const Certificate &certificate )
    {
        if ( !ValidateCertificateBinding( certificate ) )
        {
            return {};
        }
        return std::string{ CERTIFICATE_BASE_PATH_KEY } + GetSlotKey( certificate.proposal() );
    }

    void ConsensusManager::HandleVote( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: called. Vote by {} on proposal_id={} ",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id().substr( 0, 8 ) );
        if ( !CheckVote( vote ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Invalid vote proposal_id={} voter_id={}",
                                             __func__,
                                             vote.proposal_id(),
                                             vote.voter_id() );
            return;
        }
        if ( !vote.approve() )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: vote not approved voter_id={}",
                                             __func__,
                                             vote.voter_id().substr( 0, 8 ) );
            //TODO - maybe see reputation?
            return;
        }

        auto signing_bytes = VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return;
        }
        if ( !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signature verification failed voter_id={}",
                                             __func__,
                                             vote.voter_id().substr( 0, 8 ) );
            return;
        }

        bool has_quorum = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( vote.proposal_id() );
            if ( it == proposals_.end() )
            {
                pending_votes_[vote.proposal_id()].push_back( vote );
                ConsensusManagerLogger()->debug( "{}: queued pending vote proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id().substr( 0, 8 ) );
                return;
            }
            auto &proposal_state = it->second;
            auto accepted_certificate = HasAcceptedCertificateForSlot( proposal_state.slot_key );
            if ( accepted_certificate.has_value() && accepted_certificate.value() )
            {
                ConsensusManagerLogger()->debug( "{}: ignored: vote for already certified slot {} proposal_id={}",
                                                 __func__,
                                                 proposal_state.slot_key.substr( 0, 8 ),
                                                 vote.proposal_id().substr( 0, 8 ) );
                pending_votes_.erase( vote.proposal_id() );
                return;
            }
            auto slot_it = slot_states_.find( proposal_state.slot_key );
            if ( slot_it != slot_states_.end() &&
                 ( !slot_it->second.candidates_frozen || slot_it->second.best_proposal_id != vote.proposal_id() ) )
            {
                ConsensusManagerLogger()->error( "{}: ignored: proposal has not won frozen slot arbitration proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id().substr( 0, 8 ) );
                return;
            }

            if ( proposal_state.seen_voters.find( vote.voter_id() ) != proposal_state.seen_voters.end() )
            {
                ConsensusManagerLogger()->trace( "{}: ignored: duplicate vote voter_id={}",
                                                 __func__,
                                                 vote.voter_id().substr( 0, 8 ) );
                return;
            }

            auto proposal_registry_result = registry_->LoadRegistryByCid( proposal_state.proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->warn( "{}: deferred vote: registry load error={} proposal_id={}",
                                                __func__,
                                                proposal_registry_result.error().message(),
                                                vote.proposal_id().substr( 0, 8 ) );
                pending_votes_[vote.proposal_id()].push_back( vote );
                return;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( proposal_state.proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id().substr( 0, 8 ) );
                return;
            }

            const auto *validator           = registry_->FindValidator( proposal_registry, vote.voter_id() );
            const bool  is_active_validator = validator && validator->status() == ValidatorRegistry::Status::ACTIVE;

            if ( it->second.total_weight == 0 )
            {
                it->second.total_weight = registry_->TotalWeight( proposal_registry );
            }

            it->second.votes.push_back( vote );
            it->second.seen_voters.insert( vote.voter_id() );
            if ( is_active_validator )
            {
                it->second.approved_weight += validator->weight();
                has_quorum = registry_->IsQuorum( it->second.approved_weight, it->second.total_weight );
                if ( has_quorum )
                {
                    if ( !it->second.quorum_reached )
                    {
                        it->second.quorum_reached       = true;
                        it->second.quorum_reached_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                              std::chrono::system_clock::now().time_since_epoch() )
                                                              .count();
                    }
                    ConsensusManagerLogger()->debug(
                        "{}: quorum reached; certificate will be created by timer proposal_id={}",
                        __func__,
                        vote.proposal_id() );
                }
            }
            else
            {
                ConsensusManagerLogger()->debug( "{}: accepted vote from non-validator voter_id={}",
                                                 __func__,
                                                 vote.voter_id().substr( 0, 8 ) );
            }
        }
        if ( has_quorum )
        {
            certificates_pending_.store( true );
            timer_cv_.notify_all();
        }
    }

    void ConsensusManager::HandleVoteBundle( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         bundle.proposal_id().substr( 0, 8 ),
                                         bundle.votes_size() );

        for ( const auto &vote : bundle.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: processing voter_id={}", __func__, vote.voter_id().substr( 0, 8 ) );
            HandleVote( vote );
        }
    }

    void ConsensusManager::HandleCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, certificate.proposal_id() );

        if ( ValidateCertificate( certificate ) != Check::Approve )
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
            auto nonce_payload          = DecodeNonceSubject( new_state.proposal.subject() );
            if ( nonce_payload.has_value() )
            {
                slot_state.best_tx_hash = nonce_payload.value().tx_hash();
            }
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

    std::vector<ConsensusManager::Vote> ConsensusManager::CollectCertificateVotes(
        const Certificate &certificate ) const
    {
        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: processing vote voter_id={}", __func__, vote.voter_id() );
            votes.push_back( vote );
        }
        return votes;
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

        if ( !proposal.subject().has_subject_type_hash() )
        {
            return proposal.proposal_id();
        }
        const auto &subject = proposal.subject();
        const auto &hash    = subject.subject_type_hash().hash();

        {
            std::shared_lock lock( slot_key_handlers_mutex_ );
            auto             it = slot_key_handlers_.find( hash );
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
        ConsensusManagerLogger()->trace( "{}: called candidate={} current={}",
                                         __func__,
                                         candidate.proposal_id(),
                                         current.proposal_id() );
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
            return BestHash( curr_hash, cand_hash ) == cand_hash;
        }

        return candidate.proposal_id() < current.proposal_id();
    }

    const std::string &ConsensusManager::BestHash( const std::string &a, const std::string &b )
    {
        return ( a <= b ) ? a : b;
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
                                const std::string                   &subject_type_hash,
                                const google::protobuf::MessageLite &payload )
        {
            if ( subject == nullptr || subject_type_hash.size() != kSubjectTypeHashSize )
            {
                return false;
            }
            std::string serialized;
            if ( !payload.SerializeToString( &serialized ) )
            {
                return false;
            }
            std::string canonical_payload = subject_type_hash + serialized;
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
            auto expected = ConsensusManager::ComputeSubjectTypeHash( subject_type );
            if ( expected.has_error() || !subject.has_subject_type_hash() ||
                 subject.subject_type_hash().hash() != expected.value() ||
                 subject.payload().size() <= kSubjectTypeHashSize || expected.value().size() != kSubjectTypeHashSize ||
                 subject.payload().compare( 0, kSubjectTypeHashSize, expected.value() ) != 0 )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return subject.payload().substr( kSubjectTypeHashSize );
        }
    }

    outcome::result<std::string> ConsensusManager::ComputeSubjectTypeHash( std::string_view subject_type )
    {
        if ( subject_type.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        auto hash = sgns::crypto::sha2_256( subject_type.data(), subject_type.size() );
        return std::string( reinterpret_cast<const char *>( hash.data() ), hash.size() );
    }

    bool ConsensusManager::SubjectTypeMatches( const Subject &subject, std::string_view subject_type )
    {
        auto expected = ComputeSubjectTypeHash( subject_type );
        return expected.has_value() && subject.has_subject_type_hash() &&
               subject.subject_type_hash().hash() == expected.value();
    }

    outcome::result<NonceSubject> ConsensusManager::DecodeNonceSubject( const Subject &subject )
    {
        auto raw_payload = ExtractBuiltinPayload( subject, NONCE_SUBJECT_TYPE );
        if ( raw_payload.has_error() )
        {
            return outcome::failure( raw_payload.error() );
        }
        NonceSubject payload;
        if ( !payload.ParseFromString( raw_payload.value() ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    outcome::result<TaskResultSubject> ConsensusManager::DecodeTaskResultSubject( const Subject &subject )
    {
        auto raw_payload = ExtractBuiltinPayload( subject, TASK_RESULT_SUBJECT_TYPE );
        if ( raw_payload.has_error() )
        {
            return outcome::failure( raw_payload.error() );
        }
        TaskResultSubject payload;
        if ( !payload.ParseFromString( raw_payload.value() ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    outcome::result<RegistryBatchSubject> ConsensusManager::DecodeRegistryBatchSubject( const Subject &subject )
    {
        auto raw_payload = ExtractBuiltinPayload( subject, REGISTRY_BATCH_SUBJECT_TYPE );
        if ( raw_payload.has_error() )
        {
            return outcome::failure( raw_payload.error() );
        }
        RegistryBatchSubject payload;
        if ( !payload.ParseFromString( raw_payload.value() ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return payload;
    }

    bool ConsensusManager::SubjectHasValidTypeHash( Subject *subject )
    {
        return subject != nullptr && subject->has_subject_type_hash() && !subject->subject_type_hash().hash().empty();
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
        auto signing_bytes = ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed, no proposal ID created: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return {};
        }

        auto hash = sgns::crypto::sha2_256( signing_bytes.value().data(), signing_bytes.value().size() );
        auto                     proposal_id = base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
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
        if ( !subject.has_subject_type_hash() || subject.subject_type_hash().hash().empty() )
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
        if ( decoded.has_vote_bundle() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded vote bundle", __func__ );
            HandleVoteBundle( decoded.vote_bundle() );
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

        if ( !subject.has_subject_type_hash() || subject.subject_type_hash().hash().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject subject_type_hash is empty", __func__ );
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
        auto signing_bytes = ProposalSigningBytes( proposal );
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
                certificate_work_journal_->MarkStalled( entry.key, std::chrono::milliseconds( 0 ) );
                continue;
            }
            Certificate certificate;
            if ( !certificate.ParseFromArray( value.value().data(), value.value().size() ) ||
                 !ValidateCertificateKey( certificate, entry.key ) ||
                 ValidateCertificate( certificate ) != Check::Approve )
            {
                certificate_work_journal_->MarkStalled( entry.key, std::chrono::milliseconds( 0 ) );
                continue;
            }

            ProcessCommittedCertificate( entry.key, certificate );
        }
    }

    void ConsensusManager::ProcessCommittedCertificate( const std::string &key, const Certificate &certificate )
    {
        auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
        if ( subject_hash.has_error() )
        {
            certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
            return;
        }

        registry_->OnFinalizedCertificate( certificate );

        CertificateSubjectHandler handler;
        {
            std::shared_lock lock( certificate_handlers_mutex_ );
            auto it = certificate_subject_handlers_.find( certificate.proposal().subject().subject_type_hash().hash() );
            if ( it == certificate_subject_handlers_.end() )
            {
                certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
                ConsensusManagerLogger()->warn( "{}: No subject handler for certificate with key {} ", __func__, key );
                return;
            }
            handler = it->second;
        }

        const auto slot_key = GetSlotKey( certificate.proposal() );
        auto release         = ReleaseActiveVoteForAcceptedSlot( slot_key );
        if ( release.has_error() )
        {
            // A read/decode/remove failure leaves the durable certificate work retryable.
            certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
            return;
        }

        // A successful durable readback proves finality even when this node never
        // held a local active-vote record (or already removed it on an earlier replay).
        ClearProposalSlot( certificate.proposal() );

        auto certificate_handler_result = handler( subject_hash.value(), certificate );
        if ( certificate_handler_result.has_error() || certificate_handler_result.value() == Check::Stalled )
        {
            certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
            return;
        }
        (void) certificate_work_journal_->MarkDone( key );
        (void) WakePendingDependency( PendingDependencyKey::Certificate( subject_hash.value() ) );
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::GetCertificateBySlot( const std::string &slot_key ) const
    {
        if ( slot_key.empty() || !db_ )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        const auto key = std::string{ CERTIFICATE_BASE_PATH_KEY } + slot_key;

        BOOST_OUTCOME_TRY( auto certificate_data, db_->Get( { key } ) );

        Certificate certificate;
        if ( !certificate.ParseFromArray( certificate_data.data(), certificate_data.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( GetExpectedCertificateSlotKey( certificate ) != key )
        {
            ConsensusManagerLogger()->error( "{}: certificate slot key mismatch expected={}",
                                             __func__,
                                             key );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( ValidateCertificate( certificate ) != Check::Approve )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return certificate;
    }

    bool ConsensusManager::CheckCertificateForSlot( const std::string &slot_key ) const
    {
        return GetCertificateBySlot( slot_key ).has_value();
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::GetCertificateBySubjectHash(
        const std::string &slot_key ) const
    {
        return GetCertificateBySlot( slot_key );
    }

    bool ConsensusManager::CheckCertificateForSubject( const std::string &slot_key ) const
    {
        return CheckCertificateForSlot( slot_key );
    }

    bool ConsensusManager::CheckCertificateForSubject( const ConsensusManager::Subject &subject ) const
    {
        Proposal slot_proposal;
        *slot_proposal.mutable_subject() = subject;
        const auto slot_key = GetSlotKey( slot_proposal );
        if ( slot_key.empty() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to derive a slot for subject {}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return false;
        }
        auto certificate_result = GetCertificateBySlot( slot_key );
        if ( certificate_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to get the certificate for slot {}, error: {}",
                                             __func__,
                                             slot_key,
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
