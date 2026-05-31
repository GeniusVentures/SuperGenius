/**
 * @file       consensus_slot_key_test.cpp
 * @brief      Tests for MintV2 slot key collision resistance.
 * @details    Verifies that GetSlotKey produces different keys for distinct
 *             burns with identical chain/token/amount/dest but different
 *             burn tx hashes (addresses Codex P1 #3 from PR #298).
 * @date       2026-05-31
 */
#include <gtest/gtest.h>

#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/proto/SGTransaction.pb.h"

using namespace sgns;

namespace sgns
{
    /// @brief Friend accessor for private ConsensusManager::GetSlotKey.
    class ConsensusSlotKeyTestAccess
    {
    public:
        static std::string GetSlotKey( const ConsensusManager &manager, const ConsensusProposal &proposal )
        {
            return manager.GetSlotKey( proposal );
        }
    };
} // namespace sgns

namespace
{
    /// @brief Build a NonceSubject proto with a MintV2 embedded transaction.
    NonceSubject MakeMintV2NonceSubject(
        const std::string &chain_id,
        const std::string &token_id,
        uint64_t           amount,
        const std::string &dest_addr,
        const std::string &burn_tx_hash )
    {
        NonceSubject subject;
        subject.set_nonce( 1 );
        subject.set_tx_hash( "test-tx-hash" );

        // Embedded MintV2
        auto *embedded = subject.mutable_transaction();
        auto *mint     = embedded->mutable_mint_v2();
        mint->set_chain_id( chain_id );
        mint->set_token_id( token_id );
        mint->set_amount( amount );
        mint->mutable_utxo_params()->mutable_outputs()->Add()->set_dest_addr( dest_addr );

        // UTXO commitment with consumed outpoint (burn tx hash)
        if ( !burn_tx_hash.empty() )
        {
            auto *commitment = subject.mutable_utxo_commitment();
            auto *outpoint   = commitment->mutable_consumed_outpoints()->Add();
            outpoint->set_tx_id_hash( burn_tx_hash );
            outpoint->set_output_index( 0 );
        }

        return subject;
    }

    /// @brief Build a ConsensusProposal wrapping a NonceSubject.
    ConsensusProposal MakeProposal( const NonceSubject &subject, const std::string &proposal_id = "test-proposal" )
    {
        ConsensusProposal proposal;
        proposal.set_proposal_id( proposal_id );
        proposal.set_proposer_id( "test-account" );

        // Serialize NonceSubject into ConsensusSubject payload
        ConsensusSubject cs;
        cs.set_account_id( "test-account" );
        cs.set_payload( subject.SerializeAsString() );
        *proposal.mutable_subject() = cs;

        return proposal;
    }

    /// @brief Build a Non-MintV2 proposal (standard account:nonce key).
    ConsensusProposal MakeNonMintProposal( const std::string &account_id = "test-account", uint64_t nonce = 1 )
    {
        ConsensusProposal proposal;
        proposal.set_proposal_id( "test-proposal" );
        proposal.set_proposer_id( account_id );

        ConsensusSubject cs;
        cs.set_account_id( account_id );

        NonceSubject subject;
        subject.set_nonce( nonce );
        subject.set_tx_hash( "test-tx-hash" );
        // No EmbeddedTransaction — falls through to account_id:nonce
        cs.set_payload( subject.SerializeAsString() );
        *proposal.mutable_subject() = cs;

        return proposal;
    }
} // namespace

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST( ConsensusSlotKeyTest, DifferentBurnHashesProduceDifferentKeys )
{
    // Two proposals with identical chain/token/amount/dest but different
    // burn tx hashes must produce different slot keys.

    auto subject_a = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "aabbccdd" );
    auto subject_b = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "11223344" );

    auto proposal_a = MakeProposal( subject_a, "proposal-a" );
    auto proposal_b = MakeProposal( subject_b, "proposal-b" );

    ConsensusManager manager;
    auto key_a = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_a );
    auto key_b = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_b );

    EXPECT_NE( key_a, key_b );
    EXPECT_TRUE( key_a.find( "mint-v2:" ) == 0 );
    EXPECT_TRUE( key_b.find( "mint-v2:" ) == 0 );
}

TEST( ConsensusSlotKeyTest, SameBurnHashProducesSameKey )
{
    // Two proposals with identical everything (including burn hash)
    // must produce the same slot key.

    auto subject_a = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "aabbccdd" );
    auto subject_b = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "aabbccdd" );

    auto proposal_a = MakeProposal( subject_a, "proposal-a" );
    auto proposal_b = MakeProposal( subject_b, "proposal-b" );

    ConsensusManager manager;
    auto key_a = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_a );
    auto key_b = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_b );

    EXPECT_EQ( key_a, key_b );
}

TEST( ConsensusSlotKeyTest, NoBurnHashFallsBackToCurrentKey )
{
    // A proposal with no consumed_outpoints should produce a key
    // without the burn hash suffix.

    auto subject_with    = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "aabbccdd" );
    auto subject_without = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "" );

    auto proposal_with    = MakeProposal( subject_with, "proposal-with" );
    auto proposal_without = MakeProposal( subject_without, "proposal-without" );

    ConsensusManager manager;
    auto key_with    = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_with );
    auto key_without = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal_without );

    // Keys must differ (with has burn hash, without doesn't)
    EXPECT_NE( key_with, key_without );

    // Without should be shorter (no hex hash suffix)
    EXPECT_LT( key_without.size(), key_with.size() );
}

TEST( ConsensusSlotKeyTest, NonMintProposalUsesAccountNonce )
{
    // Non-EmbeddedTransaction proposals must use account_id:nonce format.

    auto proposal = MakeNonMintProposal( "test-account", 42 );

    ConsensusManager manager;
    auto key = ConsensusSlotKeyTestAccess::GetSlotKey( manager, proposal );

    EXPECT_EQ( key, "test-account:42" );
}
