#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "blockchain/ConsensusAuth.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"

namespace
{
    // Tests for Phase 6 (D-01) slot-hash fields on ConsensusVote.
    // These exercise ONLY the proto surface and the signing-surface helper;
    // they do not instantiate ConsensusManager (which requires full wiring).

    TEST( ConsensusVoteSlotHashTest, DefaultSlotHashesAreEmptyAbstainSentinel )
    {
        sgns::ConsensusVote vote;
        // proto3 bytes default = empty string; empty == abstain (D-05).
        EXPECT_TRUE( vote.slot_0_hash().empty() );
        EXPECT_TRUE( vote.slot_1_hash().empty() );
        EXPECT_TRUE( vote.slot_2_hash().empty() );
    }

    TEST( ConsensusVoteSlotHashTest, SlotHashAccessorsRoundTrip )
    {
        sgns::ConsensusVote vote;
        const std::string   slot0( 32, '\xAB' );
        const std::string   slot1( 32, '\xCD' );
        const std::string   slot2( 32, '\xEF' );

        vote.set_slot_0_hash( slot0 );
        vote.set_slot_1_hash( slot1 );
        vote.set_slot_2_hash( slot2 );

        EXPECT_EQ( vote.slot_0_hash(), slot0 );
        EXPECT_EQ( vote.slot_1_hash(), slot1 );
        EXPECT_EQ( vote.slot_2_hash(), slot2 );
        EXPECT_EQ( vote.slot_0_hash().size(), 32u );
    }

    TEST( ConsensusVoteSlotHashTest, SlotHashesArePartOfSigningSurface )
    {
        // VoteSigningBytes clears ONLY the signature field, so the slot hashes
        // must be included in the serialized signing payload (T-06-01 mitigation).
        sgns::ConsensusVote baseline;
        baseline.set_proposal_id( "proposal-1" );
        baseline.set_voter_id( "voter-1" );
        baseline.set_approve( true );
        baseline.set_timestamp( 12345 );
        baseline.set_signature( "sig" );

        sgns::ConsensusVote with_slots = baseline;
        with_slots.set_slot_0_hash( std::string( 32, '\x01' ) );

        auto baseline_bytes = sgns::VoteSigningBytes( baseline );
        auto with_slots_bytes = sgns::VoteSigningBytes( with_slots );

        ASSERT_FALSE( baseline_bytes.has_error() );
        ASSERT_FALSE( with_slots_bytes.has_error() );

        // Different slot content => different signing payload.
        EXPECT_NE( baseline_bytes.value(), with_slots_bytes.value() );
    }
} // namespace
