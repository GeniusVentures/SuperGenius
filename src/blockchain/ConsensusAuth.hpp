/**
 * @file       ConsensusAuth.hpp
 * @brief      Header-only helpers for consensus signing and validation.
 * @date       2026-02-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_CONSENSUS_AUTH_HPP
#define SGNS_CONSENSUS_AUTH_HPP

#include <system_error>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher.hpp"
#include <gsl/span>
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @brief Builds canonical bytes used to sign a consensus proposal.
     *
     * The signature field is cleared before serialization so signatures are
     * never part of their own signing payload.
     *
     * @param[in] proposal Proposal object to serialize for signing.
     * @return Serialized signing bytes on success, or `std::errc::invalid_argument`
     * when protobuf serialization fails.
     */
    inline outcome::result<std::vector<uint8_t>> ProposalSigningBytes( const ConsensusProposal &proposal )
    {
        ConsensusProposal copy = proposal;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    /**
     * @brief Builds canonical bytes used to sign a consensus vote.
     *
     * The signature field is cleared before serialization so signatures are
     * never part of their own signing payload.
     *
     * @param[in] vote Vote object to serialize for signing.
     * @return Serialized signing bytes on success, or `std::errc::invalid_argument`
     * when protobuf serialization fails.
     */
    inline outcome::result<std::vector<uint8_t>> VoteSigningBytes( const ConsensusVote &vote )
    {
        ConsensusVote copy = vote;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    /**
     * @brief Builds canonical bytes used to sign a consensus vote bundle.
     *
     * The signature field is cleared before serialization so signatures are
     * never part of their own signing payload.
     *
     * @param[in] bundle Vote bundle to serialize for signing.
     * @return Serialized signing bytes on success, or `std::errc::invalid_argument`
     * when protobuf serialization fails.
     */
    inline outcome::result<std::vector<uint8_t>> VoteBundleSigningBytes( const ConsensusVoteBundle &bundle )
    {
        ConsensusVoteBundle copy = bundle;
        copy.clear_signature();
        std::string serialized;
        if ( !copy.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    /**
     * @brief Computes deterministic proposal id from proposal content.
     *
     * The proposal id field is cleared before hashing to guarantee stable id
     * derivation from the signed payload content only.
     *
     * @param[in] proposal Proposal used to derive the identifier.
     * @return Lowercase hex SHA-256 proposal id on success, or propagated error
     * when signing bytes cannot be produced.
     */
    inline outcome::result<std::string> ComputeProposalId( const ConsensusProposal &proposal )
    {
        ConsensusProposal copy = proposal;
        copy.clear_proposal_id();
        auto signing_bytes = ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        auto hash = sgns::crypto::sha2_256( signing_bytes.value().data(), signing_bytes.value().size() );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    /**
     * @brief Validates proposal basic shape, signature, and computed id.
     * @param[in] proposal Proposal to validate.
     * @return `true` when proposer id/signature/id are present, signature is
     * valid, and computed proposal id matches; otherwise `false`.
     */
    inline bool ValidateProposal( const ConsensusProposal &proposal )
    {
        if ( proposal.proposer_id().empty() || proposal.signature().empty() || proposal.proposal_id().empty() )
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

        auto computed_id = ComputeProposalId( proposal );
        if ( computed_id.has_error() )
        {
            return false;
        }

        return computed_id.value() == proposal.proposal_id();
    }
}

#endif // SGNS_CONSENSUS_AUTH_HPP
