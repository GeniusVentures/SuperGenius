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

        std::weak_ptr<ConsensusManager> weak_self = shared_from_this();
        round_timer_                              = std::thread(
            [weak_self]()
            {
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
                    if ( self->timer_cv_.wait_for( lock, interval, [self]() { return self->stop_timer_.load(); } ) )
                    {
                        return;
                    }
                    lock.unlock();
                    self->ProcessCertificates();
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

    void ConsensusManager::SetVoteBundleHandler( VoteBundleHandler handler )
    {
        vote_bundle_handler_ = std::move( handler );
    }

    bool ConsensusManager::RegisterSubjectHandler( SubjectType type, SubjectHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->warn( "{}: ignored empty handler type={}", __func__, static_cast<int>( type ) );
            return false;
        }
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
            ConsensusManagerLogger()->warn( "{}: ignored empty certificate handler type={}",
                                            __func__,
                                            static_cast<int>( type ) );
            return false;
        }
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
        return static_cast<uint64_t>( ( elapsed - skew_ms ) / round_ms );
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
        return validators;
    }

    bool ConsensusManager::IsCurrentAggregator( const Proposal                    &proposal,
                                                const ValidatorRegistry::Registry &registry ) const
    {
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

    outcome::result<std::string> ConsensusManager::GetSubjectHash( const Subject &subject ) const
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
        return outcome::failure( std::errc::invalid_argument );
    }

    void ConsensusManager::ContinueProposalAfterSubject( const Proposal &proposal )
    {
        const auto slot_key    = GetSlotKey( proposal );
        bool       should_vote = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( proposals_.find( proposal.proposal_id() ) == proposals_.end() )
            {
                ProposalState state;
                state.proposal = proposal;
                state.slot_key = slot_key;
                proposals_.emplace( proposal.proposal_id(), std::move( state ) );
            }

            auto &slot_state = slot_states_[slot_key];
            if ( slot_state.best_proposal_id.empty() )
            {
                slot_state.best_proposal_id = proposal.proposal_id();
                if ( proposal.subject().has_nonce() )
                {
                    slot_state.best_tx_hash = proposal.subject().nonce().tx_hash();
                }
            }
            else
            {
                const auto &current = proposals_.at( slot_state.best_proposal_id ).proposal;
                if ( IsBetterProposal( proposal, current ) )
                {
                    slot_state.best_proposal_id = proposal.proposal_id();
                    if ( proposal.subject().has_nonce() )
                    {
                        slot_state.best_tx_hash = proposal.subject().nonce().tx_hash();
                    }
                }
            }

            if ( slot_state.best_proposal_id == proposal.proposal_id() && !slot_state.voted )
            {
                slot_state.voted = true;
                should_vote      = true;
            }
        }

        if ( should_vote )
        {
            auto vote_result = CreateVote( proposal.proposal_id(), account_address_, true, signer_ );
            if ( vote_result.has_value() )
            {
                (void)SubmitVote( vote_result.value() );
                ConsensusManagerLogger()->debug( "{}: self-vote submitted proposal_id={}",
                                                 __func__,
                                                 proposal.proposal_id() );
            }
            else
            {
                ConsensusManagerLogger()->error( "{}: self-vote failed proposal_id={} error={}",
                                                 __func__,
                                                 proposal.proposal_id(),
                                                 vote_result.error().message() );
            }
        }
    }

    void ConsensusManager::AddPendingProposal( const Proposal &proposal, const std::string &subject_hash )
    {
        std::lock_guard lock( proposals_mutex_ );
        if ( pending_proposals_.find( proposal.proposal_id() ) != pending_proposals_.end() )
        {
            return;
        }
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
        pending_by_subject_hash_.erase( it );
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
        ConsensusManagerLogger()->trace( "{}: called for proposer_id={}", __func__, proposer_id );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: failed: signer is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( !ValidateSubject( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject validation failed", __func__ );
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
                ConsensusManagerLogger()->error( "{}: failed: subject id computation error={}",
                                                 __func__,
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
        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        proposal.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, proposal.proposal_id() );
        return proposal;
    }

    outcome::result<ConsensusManager::Vote> ConsensusManager::CreateVote( const std::string &proposal_id,
                                                                          const std::string &voter_id,
                                                                          bool               approve,
                                                                          Signer             sign )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} voter_id={} approve={}",
                                         __func__,
                                         proposal_id,
                                         voter_id,
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

        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        vote.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: success proposal_id={} voter_id={}", __func__, proposal_id, voter_id );
        return vote;
    }

    outcome::result<ConsensusManager::VoteBundle> ConsensusManager::CreateVoteBundle( const std::string &proposal_id,
                                                                                      const std::string &aggregator_id,
                                                                                      const std::vector<Vote> &votes,
                                                                                      Signer                   sign )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} aggregator_id={} votes={}",
                                         __func__,
                                         proposal_id,
                                         aggregator_id,
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

        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        bundle.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: success proposal_id={} votes={}", __func__, proposal_id, votes.size() );
        return bundle;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::CreateCertificate( const Proposal &proposal,
                                                                                        const std::vector<Vote> &votes )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         proposal.proposal_id(),
                                         votes.size() );
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

        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, proposal.proposal_id() );
        return cert;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes( const Proposal          &proposal,
                                                                                 const std::vector<Vote> &votes ) const
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         proposal.proposal_id(),
                                         votes.size() );
        if ( !registry_ )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry is null", __func__ );
            return outcome::failure( std::errc::not_supported );
        }

        auto registry_result = registry_->LoadRegistry();
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry load error={}",
                                             __func__,
                                             registry_result.error().message() );
            return outcome::failure( registry_result.error() );
        }

        const auto &registry     = registry_result.value();
        const auto  registry_cid = registry_->GetRegistryCid();
        if ( !proposal.registry_cid().empty() && !registry_cid.empty() && proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry cid mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_cid(),
                                             registry_cid );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( proposal.registry_epoch() != registry.epoch() )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry epoch mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_epoch(),
                                             registry.epoch() );
            return outcome::failure( std::errc::invalid_argument );
        }

        uint64_t              total_weight    = registry_->TotalWeight( registry );
        uint64_t              approved_weight = 0;
        std::set<std::string> seen;

        for ( const auto &vote : votes )
        {
            ConsensusManagerLogger()->trace( "{}: processing vote voter_id={} approve={}",
                                             __func__,
                                             vote.voter_id(),
                                             vote.approve() );
            if ( vote.proposal_id() != proposal.proposal_id() )
            {
                continue;
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                continue;
            }

            const auto *validator = registry_->FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
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

            if ( vote.approve() )
            {
                approved_weight += validator->weight();
            }
        }

        QuorumTally tally;
        tally.total_weight    = total_weight;
        tally.approved_weight = approved_weight;
        tally.has_quorum      = registry_->IsQuorum( approved_weight, total_weight );
        ConsensusManagerLogger()->debug( "{}: success proposal_id={} approved_weight={} total_weight={} quorum={}",
                                         __func__,
                                         proposal.proposal_id(),
                                         approved_weight,
                                         total_weight,
                                         tally.has_quorum );
        return tally;
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::ProposalSigningBytes( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );
        return sgns::ProposalSigningBytes( proposal );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteSigningBytes( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: called voter_id={} proposal_id={}",
                                         __func__,
                                         vote.voter_id(),
                                         vote.proposal_id() );
        return sgns::VoteSigningBytes( vote );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteBundleSigningBytes( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         bundle.proposal_id(),
                                         bundle.votes_size() );
        return sgns::VoteBundleSigningBytes( bundle );
    }

    outcome::result<void> ConsensusManager::SubmitProposal( const Proposal &proposal, bool self_vote )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} self_vote={}",
                                         __func__,
                                         proposal.proposal_id(),
                                         self_vote );
        const auto slot_key = GetSlotKey( proposal );
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( proposal.proposal_id() );
            if ( it == proposals_.end() )
            {
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
        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, proposal.proposal_id() );

        if ( self_vote )
        {
            HandleProposal( proposal );
        }

        return outcome::success();
    }

    outcome::result<void> ConsensusManager::SubmitVote( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} voter_id={}",
                                         __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
        ConsensusMessage message;
        *message.mutable_vote() = vote;
        auto result             = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: success proposal_id={} voter_id={}",
                                         __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
        return result;
    }

    outcome::result<void> ConsensusManager::SubmitCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, certificate.proposal_id() );
        ConsensusMessage message;
        *message.mutable_certificate() = certificate;
        auto result                    = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}", __func__, result.error().message() );
            return result;
        }

        if ( !db_ )
        {
            ConsensusManagerLogger()->error( "{}: failed: db is null", __func__ );
            return outcome::failure( std::errc::not_supported );
        }
        if ( !certificate.has_proposal() )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate missing proposal proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto subject_hash_result = GetSubjectHash( certificate.proposal().subject() );
        if ( subject_hash_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: subject hash error proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return outcome::failure( subject_hash_result.error() );
        }

        std::string serialized;
        if ( !certificate.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate serialize error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto             key = "/cert/" + subject_hash_result.value();
        crdt::HierarchicalKey  cert_key( key );
        crdt::GlobalDB::Buffer cert_value;
        cert_value.put( serialized );

        auto put_result = db_->Put( cert_key, cert_value, { consensus_datastore_topic_ } );
        if ( put_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: crdt put error={}", __func__, put_result.error().message() );
            return outcome::failure( put_result.error() );
        }

        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, certificate.proposal_id() );
        return result;
    }

    void ConsensusManager::HandleProposal( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );

        if ( !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Invalid proposal proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return;
        }

        if ( !IsTimestampSane( proposal.timestamp() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: timestamp out of bounds proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return;
        }

        const auto registry_cid = registry_->GetRegistryCid();
        if ( registry_cid.empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Local registry doesn't have a CID. proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return;
        }
        if ( proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry cid mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_cid(),
                                             registry_cid );
            return;
        }
        if ( proposal.registry_epoch() != registry_->GetRegistryEpoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry epoch mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_epoch(),
                                             registry_->GetRegistryEpoch() );
            return;
        }

        if ( !CheckSubject( proposal.subject() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
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
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return;
        }

        if ( subject_result.value() == SubjectCheck::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return;
        }

        if ( subject_result.value() == SubjectCheck::Pending )
        {
            auto subject_hash = GetSubjectHash( proposal.subject() );
            if ( subject_hash.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                                 __func__,
                                                 proposal.proposal_id() );
                return;
            }
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
                ConsensusManagerLogger()->error( "{}: rejected: subject handler error proposal_id={}",
                                                 __func__,
                                                 proposal.proposal_id() );
                continue;
            }

            if ( subject_result.value() == SubjectCheck::Reject )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject check failed proposal_id={}",
                                                 __func__,
                                                 proposal.proposal_id() );
                continue;
            }

            if ( subject_result.value() == SubjectCheck::Pending )
            {
                auto subject_hash_result = GetSubjectHash( proposal.subject() );
                if ( subject_hash_result.has_error() )
                {
                    ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                                     __func__,
                                                     proposal.proposal_id() );
                    continue;
                }
                AddPendingProposal( proposal, subject_hash_result.value() );
                continue;
            }

            ContinueProposalAfterSubject( proposal );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessCertificates()
    {
        auto registry_result = registry_->LoadRegistry();
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: aborted: registry load error={}",
                                             __func__,
                                             registry_result.error().message() );
            return;
        }
        const auto &registry = registry_result.value();

        std::vector<ProposalState> to_process;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto &kv : proposals_ )
            {
                auto &state = kv.second;
                if ( !state.quorum_reached )
                {
                    continue;
                }
                to_process.push_back( state );
            }
        }

        for ( auto &state : to_process )
        {
            const auto round = GetCurrentRound( state.proposal.timestamp() );
            if ( round == state.last_attempt_round )
            {
                continue;
            }
            if ( !IsCurrentAggregator( state.proposal, registry ) )
            {
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

            auto certificate_result = CreateCertificate( state.proposal, state.votes );
            if ( certificate_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: failed: certificate creation error={}",
                                                 __func__,
                                                 certificate_result.error().message() );
                continue;
            }

            (void)SubmitCertificate( certificate_result.value() );
            ClearProposalState( state.proposal );
            ConsensusManagerLogger()->debug( "{}: certificate submitted proposal_id={}",
                                             __func__,
                                             state.proposal.proposal_id() );
        }
    }

    bool ConsensusManager::RegisterCertificateFilter()
    {
        const std::string pattern = "^/?cert/[^/]+";

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

        if ( !ValidateCertificateForCrdt( certificate ) )
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

        HandleCertificate( certificate );

        if ( !certificate.has_proposal() )
        {
            return;
        }
        auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
        if ( subject_hash.has_error() )
        {
            return;
        }

        CertificateSubjectHandler handler;
        {
            std::shared_lock lock( certificate_handlers_mutex_ );
            auto it = certificate_subject_handlers_.find( static_cast<int>( certificate.proposal().subject().type() ) );
            if ( it == certificate_subject_handlers_.end() )
            {
                return;
            }
            handler = it->second;
        }

        handler( subject_hash.value(), certificate );
    }

    bool ConsensusManager::ValidateCertificateForCrdt( const Certificate &certificate ) const
    {
        if ( !registry_ )
        {
            return false;
        }
        if ( !certificate.has_proposal() )
        {
            return false;
        }

        const auto &proposal = certificate.proposal();
        if ( proposal.proposal_id() != certificate.proposal_id() )
        {
            return false;
        }
        if ( proposal.registry_cid() != certificate.registry_cid() ||
             proposal.registry_epoch() != certificate.registry_epoch() )
        {
            return false;
        }
        if ( !ValidateSubject( proposal.subject() ) )
        {
            return false;
        }

        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            return false;
        }
        if ( !GeniusAccount::VerifySignature( proposal.proposer_id(), proposal.signature(), signing_bytes.value() ) )
        {
            return false;
        }

        auto computed_id = CreateProposalId( proposal );
        if ( computed_id.empty() || computed_id != certificate.proposal_id() )
        {
            return false;
        }

        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            votes.push_back( vote );
        }
        auto tally = TallyVotes( proposal, votes );
        if ( tally.has_error() || !tally.value().has_quorum )
        {
            return false;
        }

        return true;
    }

    void ConsensusManager::HandleVote( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} voter_id={}",
                                         __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
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
            ConsensusManagerLogger()->debug( "{}: ignored: vote not approved voter_id={}", __func__, vote.voter_id() );
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
                                             vote.voter_id() );
            return;
        }

        auto registry_result = registry_->LoadRegistry();
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry load error={}",
                                             __func__,
                                             registry_result.error().message() );
            return;
        }
        const auto &registry = registry_result.value();

        bool          has_quorum = false;
        ProposalState state;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( vote.proposal_id() );
            if ( it == proposals_.end() )
            {
                ConsensusManagerLogger()->error( "{}: ignored: proposal not found proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id() );
                return;
            }
            auto &proposal_state = it->second;
            auto  slot_it        = slot_states_.find( proposal_state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != vote.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: ignored: not best proposal proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id() );
                return;
            }

            if ( proposal_state.seen_voters.find( vote.voter_id() ) != proposal_state.seen_voters.end() )
            {
                ConsensusManagerLogger()->trace( "{}: ignored: duplicate vote voter_id={}", __func__, vote.voter_id() );
                return;
            }

            if ( proposal_state.proposal.registry_cid() != registry_->GetRegistryCid() ||
                 proposal_state.proposal.registry_epoch() != registry.epoch() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                                 __func__,
                                                 vote.proposal_id() );
                return;
            }

            const auto *validator = registry_->FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                ConsensusManagerLogger()->error( "{}: rejected: validator not active voter_id={}",
                                                 __func__,
                                                 vote.voter_id() );
                return;
            }

            if ( it->second.total_weight == 0 )
            {
                it->second.total_weight = registry_->TotalWeight( registry );
            }

            it->second.votes.push_back( vote );
            it->second.seen_voters.insert( vote.voter_id() );
            it->second.approved_weight += validator->weight();
            has_quorum                  = registry_->IsQuorum( it->second.approved_weight, it->second.total_weight );
            if ( has_quorum )
            {
                it->second.quorum_reached = true;
                ConsensusManagerLogger()->debug(
                    "{}: quorum reached; certificate will be created by timer proposal_id={}",
                    __func__,
                    vote.proposal_id() );
            }
            state = it->second;
        }
    }

    void ConsensusManager::HandleVoteBundle( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={} votes={}",
                                         __func__,
                                         bundle.proposal_id(),
                                         bundle.votes_size() );
        if ( vote_bundle_handler_ )
        {
            vote_bundle_handler_( bundle );
        }
        for ( const auto &vote : bundle.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: processing voter_id={}", __func__, vote.voter_id() );
            HandleVote( vote );
        }
    }

    void ConsensusManager::HandleCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, certificate.proposal_id() );

        if ( !CheckCertificate( certificate ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid certificate proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return;
        }

        auto fetch_proposal_state_ret = FetchProposalState( certificate );
        if ( fetch_proposal_state_ret.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Proposal state already has a certificate, proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return;
        }
        auto &proposal_state = fetch_proposal_state_ret.value();
        auto &proposal       = certificate.proposal();

        if ( !ValidateCertificateBestProposal( proposal_state, certificate ) )
        {
            return;
        }

        auto votes = CollectCertificateVotes( certificate );
        if ( !HasQuorumForCertificate( proposal, votes ) )
        {
            return;
        }

        ClearProposalState( proposal );
        ConsensusManagerLogger()->debug( "{}: success proposal_id={}", __func__, certificate.proposal_id() );
    }

    outcome::result<ConsensusManager::ProposalState> ConsensusManager::FetchProposalState(
        const Certificate &certificate )
    {
        std::lock_guard lock( proposals_mutex_ );
        auto            it = proposals_.find( certificate.proposal_id() );
        if ( it != proposals_.end() )
        {
            return it->second;
        }

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

    bool ConsensusManager::HasQuorumForCertificate( const Proposal &proposal, const std::vector<Vote> &votes ) const
    {
        auto tally_result = TallyVotes( proposal, votes );
        if ( tally_result.has_error() || !tally_result.value().has_quorum )
        {
            if ( tally_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: aborted: tally error={}",
                                                 __func__,
                                                 tally_result.error().message() );
            }
            return false;
        }
        return true;
    }

    void ConsensusManager::ClearProposalState( const Proposal &proposal )
    {
        std::lock_guard lock( proposals_mutex_ );
        auto            it = proposals_.find( proposal.proposal_id() );
        if ( it != proposals_.end() )
        {
            const auto slot_key = it->second.slot_key;
            proposals_.erase( it );
            auto slot_it = slot_states_.find( slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id == proposal.proposal_id() )
            {
                slot_states_.erase( slot_it );
            }
        }

        auto pending_it = pending_proposals_.find( proposal.proposal_id() );
        if ( pending_it != pending_proposals_.end() )
        {
            pending_proposals_.erase( pending_it );
        }

        for ( auto &kv : pending_by_subject_hash_ )
        {
            auto &vec = kv.second;
            vec.erase( std::remove( vec.begin(), vec.end(), proposal.proposal_id() ), vec.end() );
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
            return std::lexicographical_compare( cand_hash.begin(),
                                                 cand_hash.end(),
                                                 curr_hash.begin(),
                                                 curr_hash.end() );
        }

        return candidate.proposal_id() < current.proposal_id();
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

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject( const std::string &account_id,
                                                                                     uint64_t           nonce,
                                                                                     const std::string &tx_hash )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} nonce={}", __func__, account_id, nonce );
        Subject subject;
        subject.set_type( SubjectType::SUBJECT_NONCE );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_nonce();
        payload->set_nonce( nonce );
        payload->set_tx_hash( tx_hash.data(), tx_hash.size() );

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

    std::string ConsensusManager::CreateProposalId( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );
        // Proposal ID must be derived from the proposal contents excluding the proposal_id itself.
        Proposal copy = proposal;
        copy.clear_proposal_id();
        auto signing_bytes = ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return {};
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( signing_bytes.value().data(), signing_bytes.value().size() ) );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called subject_type={}", __func__, static_cast<int>( subject.type() ) );
        if ( subject.account_id().empty() )
        {
            return false;
        }

        switch ( subject.type() )
        {
            case SubjectType::SUBJECT_NONCE:
                return subject.has_nonce() && !subject.nonce().tx_hash().empty();
            case SubjectType::SUBJECT_TASK_RESULT:
                return subject.has_task_result() && !subject.task_result().task_result_hash().empty();
            case SubjectType::SUBJECT_UNSPECIFIED:
            default:
                break;
        }

        if ( subject.has_nonce() )
        {
            return !subject.nonce().tx_hash().empty();
        }
        if ( subject.has_task_result() )
        {
            return !subject.task_result().task_result_hash().empty();
        }

        return false;
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

        if ( subject.type() != SubjectType::SUBJECT_NONCE && subject.type() != SubjectType::SUBJECT_TASK_RESULT )
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
        if ( proposal.registry_epoch() == 0 )
        {
            ConsensusManagerLogger()->error( "{}: Registry EPOCH is zero ", __func__ );
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

    bool ConsensusManager::CheckCertificate( const Certificate &certificate )
    {
        if ( certificate.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Certificate proposal ID missing ", __func__ );
            return false;
        }
        if ( !certificate.has_proposal() )
        {
            ConsensusManagerLogger()->error( "{}: Certificate missing proposal ", __func__ );
            return false;
        }

        auto &proposal = certificate.proposal();

        if ( proposal.proposal_id() != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: proposal_id mismatch cert={} proposal={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             proposal.proposal_id() );
            return false;
        }

        if ( proposal.registry_cid() != certificate.registry_cid() ||
             proposal.registry_epoch() != certificate.registry_epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return false;
        }

        if ( !ValidateSubject( proposal.subject() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid subject proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return false;
        }
        if ( !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid proposal proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return false;
        }

        const auto computed_id = CreateProposalId( proposal );
        if ( computed_id.empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: computed_id empty", __func__ );
            return false;
        }
        if ( computed_id != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: computed_id mismatch cert={} computed={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             computed_id );
            return false;
        }
        return true;
    }
}
