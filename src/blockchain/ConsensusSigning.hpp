/**
 * @file       ConsensusSigning.hpp
 * @brief      Header-only helpers for consensus signing bytes.
 * @date       2026-02-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <system_error>
#include <vector>

#include "blockchain/impl/proto/Consensus.pb.h"
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
}
