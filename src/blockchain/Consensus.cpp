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
#include "crypto/hasher/hasher_impl.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/ConsensusAuth.hpp"

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
        pubsub_->Publish( consensus_messages_topic_, serialized_proto );
        ConsensusManagerLogger()->debug( "{}: Consensus packet published (bytes={})",
                                         __func__,
                                         serialized_proto.size() );

        return outcome::success();
    }

    bool ConsensusManager::RegisterSubjectHandler( SubjectType type, SubjectHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty handler type={}", __func__, static_cast<int>( type ) );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering subject handler type={}",
                                         __func__,
                                         static_cast<int>( type ) );
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_[static_cast<int>( type )] = std::move( handler );
        return true;
    }

    void ConsensusManager::UnregisterSubjectHandler( SubjectType type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Subject handler with type={}",
                                         __func__,
                                         static_cast<int>( type ) );
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_.erase( static_cast<int>( type ) );
    }

    bool ConsensusManager::RegisterCertificateHandler( SubjectType type, CertificateSubjectHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty certificate handler type={}",
                                             __func__,
                                             static_cast<int>( type ) );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering certificate handler type={}",
                                         __func__,
                                         static_cast<int>( type ) );
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_[static_cast<int>( type )] = std::move( handler );
        return true;
    }

    void ConsensusManager::UnregisterCertificateHandler( SubjectType type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Certificate handler with type={}",
                                         __func__,
                                         static_cast<int>( type ) );
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_.erase( static_cast<int>( type ) );
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
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        const auto window_ms = timestamp_window_.count();
        const auto ts_ms     = static_cast<std::int64_t>( timestamp_ms );
        return ( ts_ms >= now_ms - window_ms ) && ( ts_ms <= now_ms + window_ms );
    }

    uint64_t ConsensusManager::GetCurrentRound( uint64_t proposal_ts_ms ) const
    {
        if ( proposal_ts_ms == 0 || round_duration_.count() <= 0 )
        {
            return 0;
        }
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
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

    bool ConsensusManager::IsCurrentAggregator( const Proposal                    &proposal,
                                                const ValidatorRegistry::Registry &registry ) const
    {
        ConsensusManagerLogger()->trace( "{}: Checking if is current aggregator for proposal", __func__ );
        auto ordered = GetOrderedActiveValidators( registry );
        if ( ordered.empty() )
        {
            return false;
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( proposal.proposal_id().data() ),
                                      proposal.proposal_id().size() ) );
        uint64_t base_index = 0;
        for ( size_t i = 0; i < sizeof( uint64_t ) && i < hash.size(); ++i )
        {
            base_index = ( base_index << 8 ) | hash[i];
        }
        base_index = base_index % ordered.size();

        const auto round = GetCurrentRound( proposal.timestamp() );
        const auto index = ( base_index + round ) % ordered.size();

        return ordered[index] == account_address_;
    }

    outcome::result<std::string> ConsensusManager::GetSubjectHash( const Subject &subject )
    {
        if ( subject.type() == SubjectType::SUBJECT_NONCE )
        {
            if ( !subject.has_nonce() || subject.nonce().tx_hash().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return subject.nonce().tx_hash();
        }
        if ( subject.type() == SubjectType::SUBJECT_TASK_RESULT )
        {
            if ( !subject.has_task_result() || subject.task_result().task_result_hash().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return subject.task_result().task_result_hash();
        }
        if ( subject.type() == SubjectType::SUBJECT_REGISTRY_BATCH )
        {
            if ( !subject.has_registry_batch() || subject.registry_batch().batch_root().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return std::string( subject.registry_batch().batch_root() );
        }
        return outcome::failure( std::errc::invalid_argument );
    }

    void ConsensusManager::ContinueProposalAfterSubject( const Proposal &proposal )
    {
        ConsensusManagerLogger()->debug( "{}: Continuing proposal: hash {}, id {}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        const auto slot_key    = GetSlotKey( proposal );
        bool       should_vote = false;

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
            if ( slot_state.best_proposal_id.empty() )
            {
                ConsensusManagerLogger()->debug( "{}: Configuring best proposal for hash {}, id={}, slot key {}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 slot_key );
                slot_state.best_proposal_id = proposal.proposal_id();
                if ( proposal.subject().has_nonce() )
                {
                    slot_state.best_tx_hash = proposal.subject().nonce().tx_hash();
                }
            }
            else
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
                    if ( proposal.subject().has_nonce() )
                    {
                        slot_state.best_tx_hash = proposal.subject().nonce().tx_hash();
                    }
                }
            }

            if ( slot_state.best_proposal_id == proposal.proposal_id() && !slot_state.voted )
            {
                ConsensusManagerLogger()->debug(
                    "{}: My proposal for hash {}, id={}, slot key {} is better so let's vote on it. ",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal.proposal_id().substr( 0, 8 ),
                    slot_key );
                slot_state.voted = true;
                should_vote      = true;
            }
        }

        auto pending_votes = TakePendingVotes( proposal.proposal_id() );
        for ( const auto &vote : pending_votes )
        {
            HandleVote( vote );
        }

        if ( should_vote )
        {
            auto vote_result = CreateVote( proposal.proposal_id(), account_address_, true, signer_ );
            if ( vote_result.has_value() )
            {
                (void)SubmitVote( vote_result.value() );
                ConsensusManagerLogger()->debug( "{}: self-vote submitted for hash {}, id={}, slot key {}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 slot_key );
            }
            else
            {
                ConsensusManagerLogger()->error( "{}: self-vote failed for hash {}, id={}, slot key {} error={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 slot_key,
                                                 vote_result.error().message() );
            }
        }
    }

    void ConsensusManager::AddPendingProposal( const Proposal &proposal, const std::string &subject_hash )
    {
        std::lock_guard lock( proposals_mutex_ );
        if ( pending_proposals_.find( proposal.proposal_id() ) != pending_proposals_.end() )
        {
            ConsensusManagerLogger()->error(
                "{}: Failed adding pending proposal for {}: already have a proposal with id {}",
                __func__,
                subject_hash.substr( 0, 8 ),
                proposal.proposal_id().substr( 0, 8 ) );
            return;
        }
        ConsensusManagerLogger()->debug( "{}: Adding pending proposal for {}: proposal with id {}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        pending_proposals_.emplace( proposal.proposal_id(), proposal );
        pending_by_subject_hash_[subject_hash].push_back( proposal.proposal_id() );
    }

    std::vector<ConsensusManager::Proposal> ConsensusManager::TakePendingProposals( const std::string &subject_hash )
    {
        std::vector<Proposal> result;
        std::lock_guard       lock( proposals_mutex_ );
        auto                  it = pending_by_subject_hash_.find( subject_hash );
        if ( it == pending_by_subject_hash_.end() )
        {
            ConsensusManagerLogger()->trace( "{}: No pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
            return result;
        }
        for ( const auto &proposal_id : it->second )
        {
            auto prop_it = pending_proposals_.find( proposal_id );
            if ( prop_it != pending_proposals_.end() )
            {
                result.push_back( prop_it->second );
                pending_proposals_.erase( prop_it );
            }
        }
        ConsensusManagerLogger()->debug( "{}: Taking pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
        pending_by_subject_hash_.erase( it );
        return result;
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

        if ( proposal.subject().subject_id().empty() )
        {
            auto subject_id_result = ComputeSubjectId( proposal.subject() );
            if ( subject_id_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: failed for hash {}: subject id computation error={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( subject ),
                                                 subject_id_result.error().message() );
                return outcome::failure( subject_id_result.error() );
            }
            proposal.mutable_subject()->set_subject_id( subject_id_result.value() );
        }

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

        uint64_t              total_weight    = ValidatorRegistry::TotalWeight( registry );
        uint64_t              approved_weight = 0;
        std::set<std::string> seen;

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
                ConsensusManagerLogger()->error( "{}: processing vote for hash {}: voter_id={} approve={}",
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

        auto registry_result = registry_->LoadRegistry( proposal.registry_cid() );
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

        auto proposal_registry_result = registry_->LoadRegistry( proposal.registry_cid() );
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

        if ( CheckCertificateForSubject( subject_hash.value() ) )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: subject already certified hash={} proposal_id={}",
                                             __func__,
                                             subject_hash.value().substr( 0, 8 ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            std::lock_guard lock( proposals_mutex_ );
            pending_votes_.erase( proposal.proposal_id() );
            pending_proposals_.erase( proposal.proposal_id() );
            return;
        }

        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( static_cast<int>( proposal.subject().type() ) );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject handler missing type={}",
                                                 __func__,
                                                 static_cast<int>( proposal.subject().type() ) );
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

        if ( subject_result.value() == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        if ( subject_result.value() == Check::Pending )
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
            AddPendingProposal( proposal, subject_hash.value() );
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
                auto             handler_it = subject_handlers_.find( static_cast<int>( proposal.subject().type() ) );
                if ( handler_it == subject_handlers_.end() )
                {
                    ConsensusManagerLogger()->error( "{}: rejected: subject handler missing type={}",
                                                     __func__,
                                                     static_cast<int>( proposal.subject().type() ) );
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

            if ( subject_result.value() == Check::Reject )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                continue;
            }

            if ( subject_result.value() == Check::Pending )
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
                AddPendingProposal( proposal, subject_hash_result.value() );
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
            auto subject_hash = GetSubjectHash( state.proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                ConsensusManagerLogger()->debug( "{}: hash {} already certified, clearing proposal_id={}",
                                                 __func__,
                                                 subject_hash.value().substr( 0, 8 ),
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
            auto proposal_registry_result = registry_->LoadRegistry( state.proposal.registry_cid() );
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

            if ( !IsCurrentAggregator( state.proposal, proposal_registry ) )
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

            (void)SubmitCertificate( certificate_result.value() );
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
        (void)cid;
        Certificate certificate;
        if ( !certificate.ParseFromArray( value.data(), value.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
            return;
        }

        auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
        if ( subject_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed getting subject hash proposal_id={} error={}",
                                             __func__,
                                             certificate.proposal_id().substr( 0, 8 ),
                                             subject_hash.error().message() );
            return;
        }

        auto certificate_check = ValidateCertificate( certificate );

        if ( certificate_check == Check::Stalled )
        {
            ConsensusManagerLogger()->error(
                "{}: Validation of the certificate pending for key {}, certificate handler not called ",
                __func__,
                key );
            certificate_work_journal_->MarkStalled( key );
            return;
        }

        registry_->OnFinalizedCertificate( certificate );

        CertificateSubjectHandler handler;
        {
            std::shared_lock lock( certificate_handlers_mutex_ );
            auto it = certificate_subject_handlers_.find( static_cast<int>( certificate.proposal().subject().type() ) );
            if ( it == certificate_subject_handlers_.end() )
            {
                (void)certificate_work_journal_->MarkDone( key );
                return;
            }
            handler = it->second;
        }

        auto certificate_handler_result = handler( subject_hash.value(), certificate );

        if ( certificate_handler_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: certificate handler error proposal_id={} error={}",
                                             __func__,
                                             certificate.proposal_id().substr( 0, 8 ),
                                             certificate_handler_result.error().message() );
            return;
        }
        auto certificate_result = certificate_handler_result.value();

        if ( certificate_result == Check::Stalled )
        {
            ConsensusManagerLogger()->error( "{}: certificate rejected by handler proposal_id={}",
                                             __func__,
                                             certificate.proposal_id().substr( 0, 8 ) );
            ConsensusManagerLogger()->debug( "{}: Key {} is not Done yet", __func__, key );
            certificate_work_journal_->MarkStalled( key );
            return;
        }
        (void)certificate_work_journal_->MarkDone( key );
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
        auto registry_ret = registry_->LoadRegistry( certificate.registry_cid() );
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
            auto  subject_hash   = GetSubjectHash( proposal_state.proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                ConsensusManagerLogger()->debug( "{}: ignored: vote for already certified hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.value().substr( 0, 8 ),
                                                 vote.proposal_id().substr( 0, 8 ) );
                pending_votes_.erase( vote.proposal_id() );
                return;
            }
            auto slot_it = slot_states_.find( proposal_state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != vote.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: ignored: not best proposal proposal_id={}",
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

            auto proposal_registry_result = registry_->LoadRegistry( proposal_state.proposal.registry_cid() );
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
            if ( new_state.proposal.subject().has_nonce() )
            {
                slot_state.best_tx_hash = new_state.proposal.subject().nonce().tx_hash();
            }
        }

        return new_state;
    }

    bool ConsensusManager::ValidateCertificateBestProposal( const ProposalState &state,
                                                            const Certificate   &certificate ) const
    {
        if ( certificate.has_proposal() && certificate.proposal().has_subject() &&
             certificate.proposal().subject().type() == SubjectType::SUBJECT_REGISTRY_BATCH )
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
            proposals_.erase( proposal_id );
            pending_proposals_.erase( proposal_id );
            pending_votes_.erase( proposal_id );
        }

        for ( auto it_hash = pending_by_subject_hash_.begin(); it_hash != pending_by_subject_hash_.end(); )
        {
            auto &vec = it_hash->second;
            vec.erase( std::remove_if( vec.begin(),
                                       vec.end(),
                                       [&]( const std::string &proposal_id )
                                       { return ids_to_remove.find( proposal_id ) != ids_to_remove.end(); } ),
                       vec.end() );
            if ( vec.empty() )
            {
                it_hash = pending_by_subject_hash_.erase( it_hash );
            }
            else
            {
                ++it_hash;
            }
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

    std::string ConsensusManager::GetSlotKey( const Proposal &proposal ) const
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );
        if ( proposal.subject().type() == SubjectType::SUBJECT_NONCE && proposal.subject().has_nonce() )
        {
            return proposal.subject().account_id() + ":" + std::to_string( proposal.subject().nonce().nonce() );
        }
        if ( !proposal.subject().subject_id().empty() )
        {
            return proposal.subject().subject_id();
        }
        return proposal.proposal_id();
    }

    bool ConsensusManager::IsBetterProposal( const Proposal &candidate, const Proposal &current ) const
    {
        ConsensusManagerLogger()->trace( "{}: called candidate={} current={}",
                                         __func__,
                                         candidate.proposal_id(),
                                         current.proposal_id() );
        const bool candidate_nonce = candidate.subject().type() == SubjectType::SUBJECT_NONCE &&
                                     candidate.subject().has_nonce();
        const bool current_nonce = current.subject().type() == SubjectType::SUBJECT_NONCE &&
                                   current.subject().has_nonce();
        if ( candidate_nonce && current_nonce )
        {
            const auto &cand_hash = candidate.subject().nonce().tx_hash();
            const auto &curr_hash = current.subject().nonce().tx_hash();
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
        ConsensusManagerLogger()->trace( "{}: called subject_type={}", __func__, static_cast<int>( subject.type() ) );
        Subject copy = subject;
        copy.clear_subject_id();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( serialized.data() ), serialized.size() ) );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject(
        const std::string                             &account_id,
        uint64_t                                       nonce,
        const std::string                             &tx_hash,
        const std::optional<UTXOTransitionCommitment> &utxo_commitment,
        const std::optional<UTXOWitness>              &utxo_witness )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} nonce={}", __func__, account_id, nonce );
        Subject subject;
        subject.set_type( SubjectType::SUBJECT_NONCE );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_nonce();
        payload->set_nonce( nonce );
        payload->set_tx_hash( tx_hash.data(), tx_hash.size() );
        if ( utxo_commitment.has_value() )
        {
            *payload->mutable_utxo_commitment() = utxo_commitment.value();
        }
        if ( utxo_witness.has_value() )
        {
            *payload->mutable_utxo_witness() = utxo_witness.value();
        }

        auto subject_id = ComputeSubjectId( subject );
        if ( subject_id.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject id error={}",
                                             __func__,
                                             subject_id.error().message() );
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        ConsensusManagerLogger()->debug( "{}: success subject_id={}", __func__, subject.subject_id() );
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
        subject.set_type( SubjectType::SUBJECT_TASK_RESULT );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_task_result();
        payload->set_escrow_path( escrow_path );
        payload->set_task_result_hash( task_result_hash.data(), task_result_hash.size() );
        payload->set_result_epoch( result_epoch );

        auto subject_id = ComputeSubjectId( subject );
        if ( subject_id.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject id error={}",
                                             __func__,
                                             subject_id.error().message() );
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        ConsensusManagerLogger()->debug( "{}: success subject_id={}", __func__, subject.subject_id() );
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
        subject.set_type( SubjectType::SUBJECT_REGISTRY_BATCH );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_registry_batch();
        payload->set_base_registry_cid( base_registry_cid );
        payload->set_base_registry_epoch( base_registry_epoch );
        payload->set_target_registry_epoch( target_registry_epoch );
        payload->set_certificate_count( certificate_count );
        payload->set_batch_root( batch_root.data(), batch_root.size() );

        auto subject_id = ComputeSubjectId( subject );
        if ( subject_id.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject id error={}",
                                             __func__,
                                             subject_id.error().message() );
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        ConsensusManagerLogger()->debug( "{}: success subject_id={}", __func__, subject.subject_id() );
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

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( signing_bytes.value().data(), signing_bytes.value().size() ) );
        auto proposal_id = base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
        ConsensusManagerLogger()->debug( "{}: Proposal ID {} created", __func__, proposal_id.substr( 0, 8 ) );
        return proposal_id;
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called subject_type={}", __func__, static_cast<int>( subject.type() ) );
        if ( subject.account_id().empty() )
        {
            return false;
        }
        if ( subject.subject_id().empty() )
        {
            return false;
        }

        const auto expected_subject_id = ComputeSubjectId( subject );
        if ( expected_subject_id.has_error() || expected_subject_id.value() != subject.subject_id() )
        {
            return false;
        }

        switch ( subject.type() )
        {
            case SubjectType::SUBJECT_NONCE:
                if ( !subject.has_nonce() || subject.nonce().tx_hash().empty() )
                {
                    return false;
                }
                // Allow commitment-only subjects (public-chain flow). Witness remains optional.
                // But a witness without a commitment is always invalid.
                if ( subject.nonce().has_utxo_witness() && !subject.nonce().has_utxo_commitment() )
                {
                    return false;
                }
                return true;
            case SubjectType::SUBJECT_TASK_RESULT:
                return subject.has_task_result() && !subject.task_result().task_result_hash().empty();
            case SubjectType::SUBJECT_REGISTRY_BATCH:
                return subject.has_registry_batch() && !subject.registry_batch().base_registry_cid().empty() &&
                       subject.registry_batch().target_registry_epoch() ==
                           subject.registry_batch().base_registry_epoch() + 1 &&
                       subject.registry_batch().certificate_count() > 0 &&
                       !subject.registry_batch().batch_root().empty();
            case SubjectType::SUBJECT_UNSPECIFIED:
            default:
                return false;
        }
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
        ConsensusManagerLogger()->trace( "{}: subject_type={}", __func__, static_cast<int>( subject.type() ) );

        if ( subject.account_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject account_id is empty", __func__ );
            return false;
        }

        if ( subject.subject_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject subject_id is empty", __func__ );
            return false;
        }
        auto expected_subject_id = ComputeSubjectId( subject );
        if ( expected_subject_id.has_error() || expected_subject_id.value() != subject.subject_id() )
        {
            ConsensusManagerLogger()->error( "{}: subject subject_id mismatch", __func__ );
            return false;
        }

        if ( subject.type() != SubjectType::SUBJECT_NONCE && subject.type() != SubjectType::SUBJECT_TASK_RESULT &&
             subject.type() != SubjectType::SUBJECT_REGISTRY_BATCH )
        {
            ConsensusManagerLogger()->error( "{}: Invalid Subject type {}",
                                             __func__,
                                             static_cast<int>( subject.type() ) );
            return false;
        }
        if ( subject.type() == SubjectType::SUBJECT_NONCE )
        {
            if ( !subject.has_nonce() )
            {
                ConsensusManagerLogger()->error( "{}: subject missing nonce payload", __func__ );
                return false;
            }
            if ( subject.nonce().tx_hash().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject nonce tx_hash is empty", __func__ );
                return false;
            }
        }

        if ( subject.type() == SubjectType::SUBJECT_TASK_RESULT )
        {
            if ( !subject.has_task_result() )
            {
                ConsensusManagerLogger()->error( "{}: subject missing task_result payload", __func__ );
                return false;
            }
            if ( subject.task_result().escrow_path().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject task_result escrow_path is empty", __func__ );
                return false;
            }
            if ( subject.task_result().task_result_hash().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject task_result task_result_hash is empty", __func__ );
                return false;
            }
        }

        if ( subject.type() == SubjectType::SUBJECT_REGISTRY_BATCH )
        {
            if ( !subject.has_registry_batch() )
            {
                ConsensusManagerLogger()->error( "{}: subject missing registry_batch payload", __func__ );
                return false;
            }
            if ( subject.registry_batch().base_registry_cid().empty() )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch base_registry_cid is empty", __func__ );
                return false;
            }
            if ( subject.registry_batch().target_registry_epoch() !=
                 subject.registry_batch().base_registry_epoch() + 1 )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch target epoch mismatch", __func__ );
                return false;
            }
            if ( subject.registry_batch().certificate_count() == 0 )
            {
                ConsensusManagerLogger()->error( "{}: subject registry_batch certificate_count is zero", __func__ );
                return false;
            }
            if ( subject.registry_batch().batch_root().empty() )
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
        auto recovered = certificate_work_journal_->RecoverStaleProcessing( CERT_KEY_PATTERN, std::chrono::seconds( 15 ) );
        if ( recovered > 0 )
        {
            ConsensusManagerLogger()->info( "{}: recovered {} stale certificate work items", __func__, recovered );
        }

        auto unfinished = certificate_work_journal_->ListUnfinished( CERT_KEY_PATTERN );
        const auto now_ms =
            static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch() )
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

        auto current_hash = GetSubjectHash( certificate.proposal().subject() );
        if ( current_hash.has_error() )
        {
            return outcome::failure( current_hash.error() );
        }
        if ( current_hash.value() != subject_hash )
        {
            ConsensusManagerLogger()->error( "{}: certificate subject hash mismatch expected={} actual={}",
                                             __func__,
                                             subject_hash,
                                             current_hash.value() );
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
            ConsensusManagerLogger()->error( "{}: Failed to get the hash for the subject with ID {}, error: {}",
                                             __func__,
                                             subject.subject_id().substr( 0, 8 ),
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
