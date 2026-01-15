/**
 * @file       Consensus.cpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/Consensus.hpp"

#include <chrono>
#include <set>
#include <system_error>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "crypto/hasher/hasher_impl.hpp"

namespace sgns::blockchain
{
    ConsensusManager::ConsensusManager( std::shared_ptr<ValidatorRegistry> registry ) :
        registry_( std::move( registry ) )
    {
    }

    void ConsensusManager::SetRegistry( std::shared_ptr<ValidatorRegistry> registry )
    {
        registry_ = std::move( registry );
    }

    outcome::result<ConsensusManager::Proposal> ConsensusManager::CreateProposal(
        const Subject     &subject,
        const std::string &proposer_id,
        const std::string &registry_cid,
        uint64_t           registry_epoch,
        Signer             sign )
    {
        if ( !sign )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( !ValidateSubject( subject ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        Proposal proposal;
        *proposal.mutable_subject() = subject;
        proposal.set_proposer_id( proposer_id );
        proposal.set_registry_cid( registry_cid );
        proposal.set_registry_epoch( registry_epoch );
        proposal.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch() )
                                    .count() );

        if ( proposal.subject().subject_id().empty() )
        {
            auto subject_id_result = ComputeSubjectId( proposal.subject() );
            if ( subject_id_result.has_error() )
            {
                return outcome::failure( subject_id_result.error() );
            }
            proposal.mutable_subject()->set_subject_id( subject_id_result.value() );
        }

        proposal.set_proposal_id( CreateProposalId( proposal ) );
        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        auto signature = sign( signing_bytes.value() );
        proposal.set_signature( signature.data(), signature.size() );

        return proposal;
    }

    outcome::result<ConsensusManager::Vote> ConsensusManager::CreateVote(
        const std::string &proposal_id,
        const std::string &voter_id,
        bool               approve,
        Signer             sign )
    {
        if ( !sign )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        Vote vote;
        vote.set_proposal_id( proposal_id );
        vote.set_voter_id( voter_id );
        vote.set_approve( approve );
        vote.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count() );

        auto signing_bytes = VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        auto signature = sign( signing_bytes.value() );
        vote.set_signature( signature.data(), signature.size() );

        return vote;
    }

    outcome::result<ConsensusManager::VoteBundle> ConsensusManager::CreateVoteBundle(
        const std::string       &proposal_id,
        const std::string       &aggregator_id,
        const std::vector<Vote> &votes,
        Signer                   sign )
    {
        if ( !sign )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        VoteBundle bundle;
        bundle.set_proposal_id( proposal_id );
        bundle.set_aggregator_id( aggregator_id );
        bundle.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch() )
                                  .count() );
        for ( const auto &vote : votes )
        {
            *bundle.add_votes() = vote;
        }

        auto signing_bytes = VoteBundleSigningBytes( bundle );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        auto signature = sign( signing_bytes.value() );
        bundle.set_signature( signature.data(), signature.size() );

        return bundle;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::CreateCertificate(
        const Proposal         &proposal,
        const std::vector<Vote> &votes,
        Verifier                verify )
    {
        auto tally_result = TallyVotes( proposal, votes, std::move( verify ) );
        if ( tally_result.has_error() )
        {
            return outcome::failure( tally_result.error() );
        }

        const auto &tally = tally_result.value();
        Certificate cert;
        cert.set_proposal_id( proposal.proposal_id() );
        cert.set_registry_cid( proposal.registry_cid() );
        cert.set_registry_epoch( proposal.registry_epoch() );
        cert.set_total_weight( tally.total_weight );
        cert.set_approved_weight( tally.approved_weight );
        cert.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count() );
        for ( const auto &vote : votes )
        {
            *cert.add_votes() = vote;
        }

        return cert;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes(
        const Proposal         &proposal,
        const std::vector<Vote> &votes,
        Verifier                verify )
    {
        if ( !registry_ )
        {
            return outcome::failure( std::errc::not_supported );
        }

        auto registry_result = registry_->LoadRegistry();
        if ( registry_result.has_error() )
        {
            return outcome::failure( registry_result.error() );
        }

        const auto &registry = registry_result.value();
        if ( proposal.registry_epoch() != registry.epoch() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        uint64_t total_weight = registry_->TotalWeight( registry );
        uint64_t approved_weight = 0;
        std::set<std::string> seen;

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

            const auto *validator = FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                continue;
            }

            if ( verify )
            {
                auto signing_bytes = VoteSigningBytes( vote );
                if ( signing_bytes.has_error() )
                {
                    continue;
                }
                if ( !verify( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
                {
                    continue;
                }
            }

            if ( vote.approve() )
            {
                approved_weight += validator->weight();
            }
        }

        QuorumTally tally;
        tally.total_weight = total_weight;
        tally.approved_weight = approved_weight;
        tally.has_quorum = registry_->IsQuorum( approved_weight, total_weight );
        return tally;
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::ProposalSigningBytes( const Proposal &proposal )
    {
        Proposal copy = proposal;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteSigningBytes( const Vote &vote )
    {
        Vote copy = vote;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<std::vector<uint8_t>> ConsensusManager::VoteBundleSigningBytes( const VoteBundle &bundle )
    {
        VoteBundle copy = bundle;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<std::string> ConsensusManager::ComputeSubjectId( const Subject &subject )
    {
        Subject copy = subject;
        copy.clear_subject_id();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        sgns::crypto::HasherImpl hasher;
        auto hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( serialized.data() ),
                                      serialized.size() ) );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject(
        const std::string        &account_id,
        uint64_t                  nonce,
        const std::vector<uint8_t> &tx_hash )
    {
        Subject subject;
        subject.set_type( SubjectType::SUBJECT_NONCE );
        subject.set_account_id( account_id );
        auto *payload = subject.mutable_nonce();
        payload->set_nonce( nonce );
        payload->set_tx_hash( tx_hash.data(), tx_hash.size() );

        auto subject_id = ComputeSubjectId( subject );
        if ( subject_id.has_error() )
        {
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateTaskResultSubject(
        const std::string        &account_id,
        const std::string        &escrow_path,
        const std::vector<uint8_t> &task_result_hash,
        uint64_t                  result_epoch )
    {
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
            return outcome::failure( subject_id.error() );
        }
        subject.set_subject_id( subject_id.value() );
        return subject;
    }

    std::string ConsensusManager::CreateProposalId( const Proposal &proposal ) const
    {
        auto signing_bytes = ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            return {};
        }

        sgns::crypto::HasherImpl hasher;
        auto hash = hasher.sha2_256(
            gsl::span<const uint8_t>( signing_bytes.value().data(), signing_bytes.value().size() ) );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject ) const
    {
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
        for ( const auto &validator : registry.validators() )
        {
            if ( validator.validator_id() == validator_id )
            {
                return &validator;
            }
        }
        return nullptr;
    }
}
