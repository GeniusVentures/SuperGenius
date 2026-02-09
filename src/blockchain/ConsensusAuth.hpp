/**
 * @file       ConsensusAuth.hpp
 * @brief      Header-only helpers for consensus signing and validation.
 * @date       2026-02-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <system_error>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher/hasher_impl.hpp"
#include <gsl/span>
#include "outcome/outcome.hpp"

namespace sgns
{
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

    inline outcome::result<std::string> ComputeProposalId( const ConsensusProposal &proposal )
    {
        ConsensusProposal copy = proposal;
        copy.clear_proposal_id();
        auto signing_bytes = ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( signing_bytes.value().data(), signing_bytes.value().size() ) );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

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

        if ( !GeniusAccount::VerifySignature( proposal.proposer_id(),
                                              proposal.signature(),
                                              signing_bytes.value() ) )
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
