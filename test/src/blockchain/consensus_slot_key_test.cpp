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
#include "account/TransactionManager.hpp"

#include <optional>
#include <string>

using namespace sgns;

/// @brief 64-character hex strings for Hash256 burn tx hashes.
static const std::string kBurnHash1( 64, 'a' );
static const std::string kBurnHash2( 64, 'b' );

namespace sgns
{
    /// @brief Friend accessor for now-static ConsensusManager::GetSlotKey.
    class ConsensusSlotKeyTestAccess
    {
    public:
        static std::string GetSlotKey( const ConsensusProposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
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
        const std::string &burn_tx_hash,
        uint64_t           nonce = 1 )
    {
        NonceSubject subject;
        subject.set_nonce( nonce );
        subject.set_tx_hash( "test-tx-hash" );

        auto *embedded = subject.mutable_transaction();
        auto *mint     = embedded->mutable_mint_v2();
        mint->set_chain_id( chain_id );
        mint->set_token_id( token_id );
        mint->set_amount( amount );
        auto *output = mint->mutable_utxo_params()->mutable_outputs()->Add();
        output->set_dest_addr( dest_addr );
        output->set_encrypted_amount( amount );
        output->set_token_id( token_id );

        if ( !burn_tx_hash.empty() )
        {
            auto *commitment = subject.mutable_utxo_commitment();
            auto *outpoint   = commitment->mutable_consumed_outpoints()->Add();
            outpoint->set_tx_id_hash( burn_tx_hash );
            outpoint->set_output_index( 0 );

            auto *input = mint->mutable_utxo_params()->mutable_inputs()->Add();
            input->set_tx_id_hash( burn_tx_hash );
            input->set_output_index( 0 );
        }

        return subject;
    }

    /// @brief Build a ConsensusProposal by going through CreateNonceSubject.
    ConsensusProposal MakeProposal( const NonceSubject &nonce_subject,
                                    const std::string  &proposal_id = "test-proposal",
                                    const std::string  &account_id  = "test-account" )
    {
        // Build UTXO commitment optional
        std::optional<UTXOTransitionCommitment> utxo_commitment;
        if ( nonce_subject.has_utxo_commitment() )
        {
            utxo_commitment = nonce_subject.utxo_commitment();
        }

        auto subject_result = ConsensusManager::CreateNonceSubject(
            account_id,
            nonce_subject.nonce(),
            nonce_subject.tx_hash(),
            nonce_subject.transaction(),
            utxo_commitment,
            std::nullopt );
        assert( subject_result.has_value() );

        ConsensusProposal proposal;
        proposal.set_proposal_id( proposal_id );
        proposal.set_proposer_id( account_id );
        *proposal.mutable_subject() = subject_result.value();
        return proposal;
    }

    /// @brief Build a Non-MintV2 proposal (standard account:nonce key).
    ConsensusProposal MakeNonMintProposal( const std::string &account_id = "test-account", uint64_t nonce = 1 )
    {
        EmbeddedTransaction empty_transaction;
        auto subject_result = ConsensusManager::CreateNonceSubject(
            account_id,
            nonce,
            "test-tx-hash",
            empty_transaction,
            std::nullopt,
            std::nullopt );
        assert( subject_result.has_value() );

        ConsensusProposal proposal;
        proposal.set_proposal_id( "test-proposal" );
        proposal.set_proposer_id( account_id );
        *proposal.mutable_subject() = subject_result.value();
        return proposal;
    }
} // namespace

// ─── Test Fixture ──────────────────────────────────────────────────────────

class ConsensusSlotKeyTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        ConsensusManager::RegisterSlotKeyHandler(
            NONCE_SUBJECT_TYPE,
            []( const ConsensusManager::Subject &subject ) -> std::string
            {
                auto nonce = ConsensusManager::DecodeNonceSubject( subject );
                if ( nonce.has_value() &&
                     nonce.value().transaction().transaction_case() !=
                         EmbeddedTransaction::TRANSACTION_NOT_SET )
                {
                    auto tx = TransactionManager::DeSerializeEmbeddedTransaction(
                        nonce.value().transaction() );
                    if ( tx.has_value() )
                    {
                        return tx.value()->GetSlotID();
                    }
                }
                return subject.account_id() + ":" +
                       std::to_string( nonce.has_value() ? nonce.value().nonce() : 0ULL );
            } );
    }
};

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST_F( ConsensusSlotKeyTest, DifferentBurnHashesProduceDifferentKeys )
{
    // Two proposals with identical chain/token/amount/dest but different
    // burn tx hashes must produce different slot keys.

    auto subject_a = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", kBurnHash1 );
    auto subject_b = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", kBurnHash2 );

    auto proposal_a = MakeProposal( subject_a, "proposal-a" );
    auto proposal_b = MakeProposal( subject_b, "proposal-b" );

    auto key_a = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_a );
    auto key_b = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_b );

    EXPECT_NE( key_a, key_b );
    EXPECT_TRUE( key_a.find( "mint-v2:" ) == 0 );
    EXPECT_TRUE( key_b.find( "mint-v2:" ) == 0 );
}

TEST_F( ConsensusSlotKeyTest, SameBurnHashProducesSameKey )
{
    // Two proposals with identical everything (including burn hash)
    // must produce the same slot key.

    auto subject_a = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", kBurnHash1 );
    auto subject_b = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", kBurnHash1 );

    auto proposal_a = MakeProposal( subject_a, "proposal-a" );
    auto proposal_b = MakeProposal( subject_b, "proposal-b" );

    auto key_a = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_a );
    auto key_b = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_b );

    EXPECT_EQ( key_a, key_b );
}

TEST_F( ConsensusSlotKeyTest, MintSlotIgnoresProposalEnvelope )
{
    const auto baseline = MakeMintV2NonceSubject(
        "1", "token-42", 1000000, "0xdest", kBurnHash1, 1 );
    const auto competing = MakeMintV2NonceSubject(
        "1", "token-42", 1000000, "0xdest", kBurnHash1, 99 );

    const auto baseline_proposal = MakeProposal(
        baseline, "proposal-baseline", "proposer-baseline" );
    const auto competing_proposal = MakeProposal(
        competing, "proposal-competing", "proposer-competing" );

    const auto baseline_slot = ConsensusSlotKeyTestAccess::GetSlotKey( baseline_proposal );
    const auto competing_slot = ConsensusSlotKeyTestAccess::GetSlotKey( competing_proposal );

    EXPECT_EQ( baseline_slot, competing_slot );
    EXPECT_EQ( 0, baseline_slot.find( "mint-v2:" ) );
}

TEST_F( ConsensusSlotKeyTest, MintSlotRetainsEveryVerifiedBurnFact )
{
    const auto baseline = MakeMintV2NonceSubject(
        "1", "token-42", 1000000, "0xdest", kBurnHash1 );
    const auto baseline_slot = ConsensusSlotKeyTestAccess::GetSlotKey(
        MakeProposal( baseline, "proposal-baseline", "proposer-baseline" ) );

    const auto changed_chain = MakeMintV2NonceSubject(
        "2", "token-42", 1000000, "0xdest", kBurnHash1 );
    const auto changed_token = MakeMintV2NonceSubject(
        "1", "token-43", 1000000, "0xdest", kBurnHash1 );
    const auto changed_source = MakeMintV2NonceSubject(
        "1", "token-42", 1000000, "0xdest", kBurnHash2 );
    const auto changed_amount = MakeMintV2NonceSubject(
        "1", "token-42", 1000001, "0xdest", kBurnHash1 );
    const auto changed_destination = MakeMintV2NonceSubject(
        "1", "token-42", 1000000, "0xother-dest", kBurnHash1 );

    EXPECT_NE( baseline_slot, ConsensusSlotKeyTestAccess::GetSlotKey( MakeProposal( changed_chain ) ) );
    EXPECT_NE( baseline_slot, ConsensusSlotKeyTestAccess::GetSlotKey( MakeProposal( changed_token ) ) );
    EXPECT_NE( baseline_slot, ConsensusSlotKeyTestAccess::GetSlotKey( MakeProposal( changed_source ) ) );
    EXPECT_NE( baseline_slot, ConsensusSlotKeyTestAccess::GetSlotKey( MakeProposal( changed_amount ) ) );
    EXPECT_NE( baseline_slot,
               ConsensusSlotKeyTestAccess::GetSlotKey( MakeProposal( changed_destination ) ) );
}

TEST_F( ConsensusSlotKeyTest, NoBurnHashFallsBackToCurrentKey )
{
    // A proposal with no consumed_outpoints should produce a key
    // without the burn hash suffix.

    auto subject_with    = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", kBurnHash1 );
    auto subject_without = MakeMintV2NonceSubject( "1", "token-42", 1000000, "0xdest", "" );

    auto proposal_with    = MakeProposal( subject_with, "proposal-with" );
    auto proposal_without = MakeProposal( subject_without, "proposal-without" );

    auto key_with    = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_with );
    auto key_without = ConsensusSlotKeyTestAccess::GetSlotKey( proposal_without );

    EXPECT_NE( key_with, key_without );
    EXPECT_LT( key_without.size(), key_with.size() );
}

TEST_F( ConsensusSlotKeyTest, NonMintProposalUsesSubjectId )
{
    // Non-EmbeddedTransaction proposals fall through to ComputeSubjectId.

    auto proposal = MakeNonMintProposal( "test-account", 42 );
    auto key      = ConsensusSlotKeyTestAccess::GetSlotKey( proposal );

    EXPECT_FALSE( key.empty() );
    // Deterministic: same account/nonce produces same key
    auto proposal2 = MakeNonMintProposal( "test-account", 42 );
    EXPECT_EQ( key, ConsensusSlotKeyTestAccess::GetSlotKey( proposal2 ) );
}
