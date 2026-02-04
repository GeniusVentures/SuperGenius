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

#include <gsl/span>

#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "account/GeniusAccount.hpp"

namespace sgns::blockchain
{

    base::Logger ConsensusManagerLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "ConsensusManager" );
    }

    std::shared_ptr<ConsensusManager> ConsensusManager::New( std::shared_ptr<ValidatorRegistry>         registry,
                                                             std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                             Signer                                     signer,
                                                             std::string consensus_topic )
    {
        if ( !pubsub )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: pubsub is null", __func__ );
            return nullptr;
        }

        auto instance = std::shared_ptr<ConsensusManager>( new ConsensusManager( std::move( registry ),
                                                                                 std::move( pubsub ),
                                                                                 std::move( signer ),
                                                                                 std::move( consensus_topic ) ) );

        instance->consensus_subs_future_ = std::move( instance->pubsub_->Subscribe(
            instance->consensus_topic_,
            [weakptr( std::weak_ptr<ConsensusManager>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    ConsensusManagerLogger()->trace( "{}: Received Consensus Message on topic {}", __func__, self->consensus_topic_ );
                    self->OnConsensusMessage( message );
                }
            } ) );
        ConsensusManagerLogger()->debug( "{}: Subscribed to Consensus topic {}", __func__, instance->consensus_topic_ );

        return instance;
    }

    ConsensusManager::ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        Signer                                     signer,
                                        std::string                                consensus_topic ) :
        registry_( std::move( registry ) ), //
        pubsub_( std::move( pubsub ) ),     //
        signer_( std::move( signer ) ),     //
        consensus_topic_( std::string( CONSENSUS_CHANNEL_PREFIX ) + sgns::version::GetNetAndVersionAppendix() +
                          consensus_topic )
    {
    }

    void ConsensusManager::SetProposalValidator( ProposalValidator validator )
    {
        proposal_validator_ = std::move( validator );
    }

    void ConsensusManager::SetCertificateCallback( CertificateCallback callback )
    {
        certificate_callback_ = std::move( callback );
    }

    outcome::result<void> ConsensusManager::Publish( const ConsensusMessage &message )
    {
        std::vector<uint8_t> serialized_proto( message.ByteSizeLong() );
        if ( !message.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: Failed to serialize consensus message", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        ConsensusManagerLogger()->debug( "{}: Sending consensus packet to {}", __func__, consensus_topic_ );
        pubsub_->Publish( consensus_topic_, serialized_proto );
        ConsensusManagerLogger()->debug( "{}: Consensus packet published (bytes={})", __func__, serialized_proto.size() );

        return outcome::success();
    }

    void ConsensusManager::SetProposalHandler( ProposalHandler handler )
    {
        proposal_handler_ = std::move( handler );
    }

    void ConsensusManager::SetVoteHandler( VoteHandler handler )
    {
        vote_handler_ = std::move( handler );
    }

    void ConsensusManager::SetVoteBundleHandler( VoteBundleHandler handler )
    {
        vote_bundle_handler_ = std::move( handler );
    }

    void ConsensusManager::SetCertificateHandler( CertificateHandler handler )
    {
        certificate_handler_ = std::move( handler );
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
        ConsensusManagerLogger()->trace( "{}: CreateProposal called for proposer_id={}", __func__, proposer_id );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: CreateProposal failed: signer is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( !ValidateSubject( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: CreateProposal failed: subject validation failed", __func__ );
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
                ConsensusManagerLogger()->error( "{}: CreateProposal failed: subject id computation error={}", __func__,
                                                 subject_id_result.error().message() );
                return outcome::failure( subject_id_result.error() );
            }
            proposal.mutable_subject()->set_subject_id( subject_id_result.value() );
        }

        proposal.set_proposal_id( CreateProposalId( proposal ) );
        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: CreateProposal failed: signing bytes error={}", __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }
        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        proposal.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: CreateProposal success proposal_id={}", __func__, proposal.proposal_id() );
        return proposal;
    }

    outcome::result<ConsensusManager::Vote> ConsensusManager::CreateVote( const std::string &proposal_id,
                                                                          const std::string &voter_id,
                                                                          bool               approve,
                                                                          Signer             sign )
    {
        ConsensusManagerLogger()->trace( "{}: CreateVote called proposal_id={} voter_id={} approve={}", __func__,
                                         proposal_id,
                                         voter_id,
                                         approve );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: CreateVote failed: signer is empty", __func__ );
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
            ConsensusManagerLogger()->error( "{}: CreateVote failed: signing bytes error={}", __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }

        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        vote.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: CreateVote success proposal_id={} voter_id={}", __func__, proposal_id, voter_id );
        return vote;
    }

    outcome::result<ConsensusManager::VoteBundle> ConsensusManager::CreateVoteBundle( const std::string &proposal_id,
                                                                                      const std::string &aggregator_id,
                                                                                      const std::vector<Vote> &votes,
                                                                                      Signer                   sign )
    {
        ConsensusManagerLogger()->trace( "{}: CreateVoteBundle called proposal_id={} aggregator_id={} votes={}", __func__,
                                         proposal_id,
                                         aggregator_id,
                                         votes.size() );
        if ( !sign )
        {
            ConsensusManagerLogger()->error( "{}: CreateVoteBundle failed: signer is empty", __func__ );
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
            ConsensusManagerLogger()->error( "{}: CreateVoteBundle failed: signing bytes error={}", __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }

        OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        bundle.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: CreateVoteBundle success proposal_id={} votes={}", __func__,
                                         proposal_id,
                                         votes.size() );
        return bundle;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::CreateCertificate( const Proposal &proposal,
                                                                                        const std::vector<Vote> &votes )
    {
        ConsensusManagerLogger()->trace( "{}: CreateCertificate called proposal_id={} votes={}", __func__,
                                         proposal.proposal_id(),
                                         votes.size() );
        auto tally_result = TallyVotes( proposal, votes );
        if ( tally_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: CreateCertificate failed: tally error={}", __func__,
                                             tally_result.error().message() );
            return outcome::failure( tally_result.error() );
        }

        const auto &tally = tally_result.value();
        Certificate cert;
        cert.set_proposal_id( proposal.proposal_id() );
        cert.set_registry_cid( proposal.registry_cid() );
        cert.set_registry_epoch( proposal.registry_epoch() );
        cert.set_total_weight( tally.total_weight );
        cert.set_approved_weight( tally.approved_weight );
        cert.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        for ( const auto &vote : votes )
        {
            *cert.add_votes() = vote;
        }
        *cert.mutable_proposal() = proposal;

        ConsensusManagerLogger()->debug( "{}: CreateCertificate success proposal_id={}", __func__, proposal.proposal_id() );
        return cert;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes( const Proposal          &proposal,
                                                                                 const std::vector<Vote> &votes )
    {
        ConsensusManagerLogger()->trace( "{}: TallyVotes called proposal_id={} votes={}", __func__,
                                         proposal.proposal_id(),
                                         votes.size() );
        if ( !registry_ )
        {
            ConsensusManagerLogger()->error( "{}: TallyVotes failed: registry is null", __func__ );
            return outcome::failure( std::errc::not_supported );
        }

        auto registry_result = registry_->LoadRegistry();
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: TallyVotes failed: registry load error={}", __func__,
                                             registry_result.error().message() );
            return outcome::failure( registry_result.error() );
        }

        const auto &registry     = registry_result.value();
        const auto  registry_cid = registry_->GetRegistryCid();
        if ( !proposal.registry_cid().empty() && !registry_cid.empty() && proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error( "{}: TallyVotes failed: registry cid mismatch proposal={} registry={}", __func__,
                                             proposal.registry_cid(),
                                             registry_cid );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( proposal.registry_epoch() != registry.epoch() )
        {
            ConsensusManagerLogger()->error( "{}: TallyVotes failed: registry epoch mismatch proposal={} registry={}", __func__,
                                             proposal.registry_epoch(),
                                             registry.epoch() );
            return outcome::failure( std::errc::invalid_argument );
        }

        uint64_t              total_weight    = registry_->TotalWeight( registry );
        uint64_t              approved_weight = 0;
        std::set<std::string> seen;

        for ( const auto &vote : votes )
        {
            ConsensusManagerLogger()->trace( "{}: TallyVotes processing vote voter_id={} approve={}", __func__,
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

            const auto *validator = FindValidator( registry, vote.voter_id() );
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
        ConsensusManagerLogger()->debug( "{}: TallyVotes success proposal_id={} approved_weight={} total_weight={} quorum={}", __func__,
                                         proposal.proposal_id(),
                                         approved_weight,
                                         total_weight,
                                         tally.has_quorum );
        return tally;
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::ProposalSigningBytes( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: ProposalSigningBytes called proposal_id={}", __func__, proposal.proposal_id() );
        Proposal copy = proposal;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: ProposalSigningBytes failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteSigningBytes( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: VoteSigningBytes called voter_id={} proposal_id={}", __func__,
                                         vote.voter_id(),
                                         vote.proposal_id() );
        Vote copy = vote;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: VoteSigningBytes failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteBundleSigningBytes( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: VoteBundleSigningBytes called proposal_id={} votes={}", __func__,
                                         bundle.proposal_id(),
                                         bundle.votes_size() );
        VoteBundle copy = bundle;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: VoteBundleSigningBytes failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<void> ConsensusManager::SubmitProposal( const Proposal &proposal, bool self_vote )
    {
        ConsensusManagerLogger()->trace( "{}: SubmitProposal called proposal_id={} self_vote={}", __func__,
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
            ConsensusManagerLogger()->error( "{}: SubmitProposal failed: publish error={}", __func__,
                                             publish_result.error().message() );
            return publish_result;
        }
        ConsensusManagerLogger()->debug( "{}: SubmitProposal success proposal_id={}", __func__, proposal.proposal_id() );

        if ( self_vote )
        {
            HandleProposal( proposal );
        }

        return outcome::success();
    }

    outcome::result<void> ConsensusManager::SubmitVote( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: SubmitVote called proposal_id={} voter_id={}", __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
        ConsensusMessage message;
        *message.mutable_vote() = vote;
        auto result = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: SubmitVote failed: publish error={}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: SubmitVote success proposal_id={} voter_id={}", __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
        return result;
    }

    outcome::result<void> ConsensusManager::SubmitCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: SubmitCertificate called proposal_id={}", __func__, certificate.proposal_id() );
        ConsensusMessage message;
        *message.mutable_certificate() = certificate;
        auto result = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: SubmitCertificate failed: publish error={}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: SubmitCertificate success proposal_id={}", __func__, certificate.proposal_id() );
        return result;
    }

    void ConsensusManager::HandleProposal( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: HandleProposal called proposal_id={}", __func__, proposal.proposal_id() );
        if ( proposal_handler_ )
        {
            proposal_handler_( proposal );
        }

        if ( !registry_ )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal aborted: registry is null", __func__ );
            return;
        }

        const auto registry_cid = registry_->GetRegistryCid();
        if ( !proposal.registry_cid().empty() && !registry_cid.empty() && proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal rejected: registry cid mismatch proposal={} registry={}", __func__,
                                             proposal.registry_cid(),
                                             registry_cid );
            return;
        }
        if ( proposal.registry_epoch() != registry_->GetRegistryEpoch() )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal rejected: registry epoch mismatch proposal={} registry={}", __func__,
                                             proposal.registry_epoch(),
                                             registry_->GetRegistryEpoch() );
            return;
        }

        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal rejected: signing bytes error={}", __func__,
                                             signing_bytes.error().message() );
            return;
        }
        if ( !GeniusAccount::VerifySignature( proposal.proposer_id(), proposal.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal rejected: signature verification failed proposer_id={}", __func__,
                                             proposal.proposer_id() );
            return;
        }

        if ( proposal_validator_ && !proposal_validator_( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: HandleProposal rejected: proposal validator failed proposal_id={}", __func__,
                                             proposal.proposal_id() );
            return;
        }

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

        if ( should_vote && signer_ && !account_address_.empty() )
        {
            auto vote_result = CreateVote( proposal.proposal_id(), account_address_, true, signer_ );
            if ( vote_result.has_value() )
            {
                (void)SubmitVote( vote_result.value() );
                ConsensusManagerLogger()->debug( "{}: HandleProposal self-vote submitted proposal_id={}", __func__,
                                                 proposal.proposal_id() );
            }
            else
            {
                ConsensusManagerLogger()->error( "{}: HandleProposal self-vote failed proposal_id={} error={}", __func__,
                                                 proposal.proposal_id(),
                                                 vote_result.error().message() );
            }
        }
    }

    void ConsensusManager::HandleVote( const Vote &vote )
    {
        ConsensusManagerLogger()->trace( "{}: HandleVote called proposal_id={} voter_id={}", __func__,
                                         vote.proposal_id(),
                                         vote.voter_id() );
        if ( vote_handler_ )
        {
            vote_handler_( vote );
        }

        if ( !registry_ )
        {
            ConsensusManagerLogger()->error( "{}: HandleVote aborted: registry is null", __func__ );
            return;
        }

        ProposalState state;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( vote.proposal_id() );
            if ( it == proposals_.end() )
            {
                ConsensusManagerLogger()->error( "{}: HandleVote ignored: proposal not found proposal_id={}", __func__,
                                                 vote.proposal_id() );
                return;
            }
            it->second.votes.push_back( vote );
            state = it->second;

            auto slot_it = slot_states_.find( state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != vote.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: HandleVote ignored: not best proposal proposal_id={}", __func__,
                                                 vote.proposal_id() );
                return;
            }
        }

        auto tally_result = TallyVotes( state.proposal, state.votes );
        if ( tally_result.has_error() || !tally_result.value().has_quorum )
        {
            if ( tally_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: HandleVote aborted: tally error={}", __func__,
                                                 tally_result.error().message() );
            }
            return;
        }

        if ( state.certificate.has_value() )
        {
            ConsensusManagerLogger()->debug( "{}: HandleVote skipped: certificate already present proposal_id={}", __func__,
                                             vote.proposal_id() );
            return;
        }

        auto certificate_result = CreateCertificate( state.proposal, state.votes );
        if ( certificate_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: HandleVote failed: certificate creation error={}", __func__,
                                             certificate_result.error().message() );
            return;
        }

        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( vote.proposal_id() );
            if ( it != proposals_.end() )
            {
                it->second.certificate = certificate_result.value();
            }
        }

        (void)SubmitCertificate( certificate_result.value() );
        NotifyCertificate( state.proposal, certificate_result.value() );
        ConsensusManagerLogger()->debug( "{}: HandleVote certificate submitted proposal_id={}", __func__, vote.proposal_id() );
    }

    void ConsensusManager::HandleVoteBundle( const VoteBundle &bundle )
    {
        ConsensusManagerLogger()->trace( "{}: HandleVoteBundle called proposal_id={} votes={}", __func__,
                                         bundle.proposal_id(),
                                         bundle.votes_size() );
        if ( vote_bundle_handler_ )
        {
            vote_bundle_handler_( bundle );
        }
        for ( const auto &vote : bundle.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: HandleVoteBundle processing voter_id={}", __func__, vote.voter_id() );
            HandleVote( vote );
        }
    }

    void ConsensusManager::HandleCertificate( const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: HandleCertificate called proposal_id={}", __func__, certificate.proposal_id() );
        if ( certificate_handler_ )
        {
            certificate_handler_( certificate );
        }

        if ( !registry_ )
        {
            ConsensusManagerLogger()->error( "{}: HandleCertificate aborted: registry is null", __func__ );
            return;
        }

        ProposalState state;
        Proposal      proposal;
        bool          have_proposal = false;
        if ( certificate.has_proposal() )
        {
            proposal = certificate.proposal();

            if ( proposal.proposal_id() != certificate.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: proposal_id mismatch cert={} proposal={}", __func__,
                                                 certificate.proposal_id(),
                                                 proposal.proposal_id() );
                return;
            }

            if ( proposal.registry_cid() != certificate.registry_cid() ||
                 proposal.registry_epoch() != certificate.registry_epoch() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: registry mismatch proposal_id={}", __func__,
                                                 certificate.proposal_id() );
                return;
            }

            if ( !ValidateSubject( proposal.subject() ) )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: invalid subject proposal_id={}", __func__,
                                                 proposal.proposal_id() );
                return;
            }

            auto signing_bytes = ProposalSigningBytes( proposal );
            if ( signing_bytes.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: signing bytes error={}", __func__,
                                                 signing_bytes.error().message() );
                return;
            }
            if ( !GeniusAccount::VerifySignature( proposal.proposer_id(),
                                                  proposal.signature(),
                                                  signing_bytes.value() ) )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: signature verification failed proposer_id={}", __func__,
                                                 proposal.proposer_id() );
                return;
            }

            const auto computed_id = CreateProposalId( proposal );
            if ( computed_id.empty() || computed_id != certificate.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: computed_id mismatch cert={} computed={}", __func__,
                                                 certificate.proposal_id(),
                                                 computed_id );
                return;
            }

            have_proposal = true;
        }
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( certificate.proposal_id() );
            if ( it != proposals_.end() )
            {
                if ( it->second.certificate.has_value() )
                {
                    ConsensusManagerLogger()->debug( "{}: HandleCertificate skipped: already have certificate proposal_id={}", __func__,
                                                     certificate.proposal_id() );
                    return;
                }
                state         = it->second;
                proposal      = state.proposal;
                have_proposal = true;
            }
            else if ( have_proposal )
            {
                ProposalState new_state;
                new_state.proposal = proposal;
                new_state.slot_key = GetSlotKey( proposal );
                proposals_.emplace( proposal.proposal_id(), new_state );
                state = std::move( new_state );

                auto &slot_state = slot_states_[state.slot_key];
                if ( slot_state.best_proposal_id.empty() )
                {
                    slot_state.best_proposal_id = proposal.proposal_id();
                    if ( proposal.subject().has_nonce() )
                    {
                        slot_state.best_tx_hash = proposal.subject().nonce().tx_hash();
                    }
                }
            }
            else
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate aborted: missing proposal proposal_id={}", __func__,
                                                 certificate.proposal_id() );
                return;
            }

            auto slot_it = slot_states_.find( state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != certificate.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate rejected: not best proposal proposal_id={}", __func__,
                                                 certificate.proposal_id() );
                return;
            }
        }

        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: HandleCertificate processing vote voter_id={}", __func__, vote.voter_id() );
            votes.push_back( vote );
        }

        auto tally_result = TallyVotes( proposal, votes );
        if ( tally_result.has_error() || !tally_result.value().has_quorum )
        {
            if ( tally_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: HandleCertificate aborted: tally error={}", __func__,
                                                 tally_result.error().message() );
            }
            return;
        }

        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( certificate.proposal_id() );
            if ( it != proposals_.end() )
            {
                it->second.certificate = certificate;
            }
        }

        NotifyCertificate( proposal, certificate );
        ConsensusManagerLogger()->debug( "{}: HandleCertificate success proposal_id={}", __func__, certificate.proposal_id() );
    }

    void ConsensusManager::NotifyCertificate( const Proposal &proposal, const Certificate &certificate )
    {
        ConsensusManagerLogger()->trace( "{}: NotifyCertificate called proposal_id={}", __func__, proposal.proposal_id() );
        if ( certificate_callback_ )
        {
            certificate_callback_( proposal, certificate );
        }
    }

    std::string ConsensusManager::GetSlotKey( const Proposal &proposal ) const
    {
        ConsensusManagerLogger()->trace( "{}: GetSlotKey called proposal_id={}", __func__, proposal.proposal_id() );
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
        ConsensusManagerLogger()->trace( "{}: IsBetterProposal called candidate={} current={}", __func__,
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
        ConsensusManagerLogger()->trace( "{}: ComputeSubjectId called subject_type={}", __func__, static_cast<int>( subject.type() ) );
        Subject copy = subject;
        copy.clear_subject_id();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: ComputeSubjectId failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( serialized.data() ), serialized.size() ) );
        ConsensusManagerLogger()->debug( "{}: ComputeSubjectId success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject( const std::string &account_id,
                                                                                     uint64_t           nonce,
                                                                                     const std::string &tx_hash )
    {
        ConsensusManagerLogger()->trace( "{}: CreateNonceSubject called account_id={} nonce={}", __func__, account_id, nonce );
        Subject subject;
        subject.set_type( SubjectType::SUBJECT_NONCE );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_nonce();
        payload->set_nonce( nonce );
        payload->set_tx_hash( tx_hash.data(), tx_hash.size() );

        auto subject_id = ComputeSubjectId( subject );
        if ( subject_id.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: CreateNonceSubject failed: subject id error={}", __func__,
                                             subject_id.error().message() );
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        ConsensusManagerLogger()->debug( "{}: CreateNonceSubject success subject_id={}", __func__, subject.subject_id() );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateTaskResultSubject(
        const std::string &account_id,
        const std::string &escrow_path,
        const std::string &task_result_hash,
        uint64_t           result_epoch )
    {
        ConsensusManagerLogger()->trace( "{}: CreateTaskResultSubject called account_id={} result_epoch={}", __func__,
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
            ConsensusManagerLogger()->error( "{}: CreateTaskResultSubject failed: subject id error={}", __func__,
                                             subject_id.error().message() );
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        ConsensusManagerLogger()->debug( "{}: CreateTaskResultSubject success subject_id={}", __func__, subject.subject_id() );
        return subject;
    }

    std::string ConsensusManager::CreateProposalId( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: CreateProposalId called proposal_id={}", __func__, proposal.proposal_id() );
        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: CreateProposalId failed: signing bytes error={}", __func__,
                                             signing_bytes.error().message() );
            return {};
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( signing_bytes.value().data(), signing_bytes.value().size() ) );
        ConsensusManagerLogger()->debug( "{}: CreateProposalId success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: ValidateSubject called subject_type={}", __func__, static_cast<int>( subject.type() ) );
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

    const ValidatorRegistry::ValidatorEntry *ConsensusManager::FindValidator(
        const ValidatorRegistry::Registry &registry,
        const std::string                 &validator_id ) const
    {
        ConsensusManagerLogger()->trace( "{}: FindValidator called validator_id={}", __func__, validator_id );
        for ( const auto &validator : registry.validators() )
        {
            if ( validator.validator_id() == validator_id )
            {
                return &validator;
            }
        }
        return nullptr;
    }

    void ConsensusManager::OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        ConsensusManagerLogger()->trace( "{}: OnConsensusMessage called", __func__ );
        if ( !message )
        {
            ConsensusManagerLogger()->error( "{}: OnConsensusMessage ignored: message is empty", __func__ );
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
            ConsensusManagerLogger()->debug( "{}: OnConsensusMessage decoded proposal", __func__ );
            HandleProposal( decoded.proposal() );
            return;
        }
        if ( decoded.has_vote() )
        {
            ConsensusManagerLogger()->debug( "{}: OnConsensusMessage decoded vote", __func__ );
            HandleVote( decoded.vote() );
            return;
        }
        if ( decoded.has_vote_bundle() )
        {
            ConsensusManagerLogger()->debug( "{}: OnConsensusMessage decoded vote bundle", __func__ );
            HandleVoteBundle( decoded.vote_bundle() );
            return;
        }
        if ( decoded.has_certificate() )
        {
            ConsensusManagerLogger()->debug( "{}: OnConsensusMessage decoded certificate", __func__ );
            HandleCertificate( decoded.certificate() );
        }
    }
}
