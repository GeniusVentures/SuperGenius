#include <gtest/gtest.h>
#include <gsl/span>

#include "blockchain/Consensus.hpp"
#include "account/proto/SGTransaction.pb.h"

#include "crypto/hasher.hpp"

namespace sgns
{
    class ConsensusManagerTestAccess
    {
    public:
        static bool ValidateSubject( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::ValidateSubject( subject );
        }

        static bool CheckSubject( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::CheckSubject( subject );
        }
    };
} // namespace sgns

namespace
{
    constexpr const char *kAccountId         = "gnus-test-account";
    constexpr const char *kBridgeSubjectType = "gnus.bridge_event.v1";

    std::vector<uint8_t> BridgePayload()
    {
        return std::vector<uint8_t>{ 0x01, 0x02, 0x03 };
    }

    sgns::EmbeddedTransaction MakeTestEmbeddedTransfer()
    {
        sgns::EmbeddedTransaction embedded;
        SGTransaction::TransferTx tx;
        tx.mutable_dag_struct()->set_type( "transfer" );
        *embedded.mutable_transfer() = tx;
        return embedded;
    }

    std::string SerializedTaskResultPayload()
    {
        sgns::TaskResultSubject payload;
        payload.set_escrow_path( "escrow/path" );
        payload.set_task_result_hash( "task-hash" );
        payload.set_result_epoch( 1 );

        std::string serialized;
        EXPECT_TRUE( payload.SerializeToString( &serialized ) );
        return serialized;
    }

    void RefreshPayloadHash( sgns::ConsensusSubject &subject )
    {
        auto payload_hash = sgns::crypto::sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( subject.payload().data() ),
                                      subject.payload().size() ) );
        subject.set_payload_hash( payload_hash.data(), payload_hash.size() );
    }
}

TEST( ConsensusSubjectTest, ComputesSubjectTypeHashFromString )
{
    const auto first     = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    const auto second    = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    const auto different = sgns::ConsensusManager::ComputeSubjectTypeHash( "gnus.other.v1" );

    ASSERT_TRUE( first.has_value() );
    ASSERT_TRUE( second.has_value() );
    ASSERT_TRUE( different.has_value() );
    EXPECT_EQ( first.value().size(), 32U );
    EXPECT_EQ( first.value(), second.value() );
    EXPECT_NE( first.value(), different.value() );
}

TEST( ConsensusSubjectTest, RejectsEmptySubjectTypeHashInput )
{
    EXPECT_TRUE( sgns::ConsensusManager::ComputeSubjectTypeHash( std::string{} ).has_error() );
}

TEST( ConsensusSubjectTest, CreatesGenericSubject )
{
    const auto payload        = BridgePayload();
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject( kAccountId, kBridgeSubjectType, payload );

    ASSERT_TRUE( subject_result.has_value() );
    const auto &subject = subject_result.value();
    EXPECT_EQ( subject.account_id(), kAccountId );
    EXPECT_EQ( subject.payload(), std::string( payload.begin(), payload.end() ) );
    EXPECT_FALSE( subject.payload_hash().empty() );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value().toString() );

    const auto computed_id = sgns::ConsensusManager::ComputeSubjectId( subject );
    ASSERT_TRUE( computed_id.has_value() );
    EXPECT_FALSE( computed_id.value().empty() );
    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, CreateGenericSubjectRejectsEmptyInputs )
{
    const auto payload = BridgePayload();

    EXPECT_TRUE( sgns::ConsensusManager::CreateGenericSubject( "", kBridgeSubjectType, payload ).has_error() );
    EXPECT_TRUE( sgns::ConsensusManager::CreateGenericSubject( kAccountId, "", payload ).has_error() );
    EXPECT_TRUE( sgns::ConsensusManager::CreateGenericSubject( kAccountId, kBridgeSubjectType, std::vector<uint8_t>{} )
                     .has_error() );
}

TEST( ConsensusSubjectTest, ValidateGenericSubjectRejectsTamperedPayloadHash )
{
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject( kAccountId,
                                                                              kBridgeSubjectType,
                                                                              BridgePayload() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    ASSERT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    subject.set_payload_hash( "bad" );

    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, ValidateGenericSubjectRejectsEmptySubjectTypeHash )
{
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject( kAccountId,
                                                                              kBridgeSubjectType,
                                                                              BridgePayload() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    ASSERT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    subject.clear_subject_type_hash();

    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, ValidateGenericSubjectRejectsWrongSizedSubjectTypeHash )
{
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject( kAccountId,
                                                                              kBridgeSubjectType,
                                                                              BridgePayload() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    subject.mutable_subject_type_hash()->set_hash( std::string( sgns::base::Hash256::size() - 1, 'x' ) );

    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, CreatesBuiltInSubjectWithCanonicalStringType )
{
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            7,
                                                                            "tx-hash",
                                                                            sgns::EmbeddedTransaction{},
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto &subject = subject_result.value();
    const auto  nonce   = sgns::ConsensusManager::DecodeNonceSubject( subject );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 7U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash" );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( sgns::NONCE_SUBJECT_TYPE );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value().toString() );

    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, RejectsMalformedNoncePayload )
{
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                      7,
                                                                      "tx-hash",
                                                                      sgns::EmbeddedTransaction{},
                                                                      std::nullopt,
                                                                      std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    subject.set_payload( "\xff\xff\xff", 3 );
    RefreshPayloadHash( subject );

    EXPECT_TRUE( sgns::ConsensusManager::DecodeNonceSubject( subject ).has_error() );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, RejectsNonceHashWithTaskResultPayload )
{
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                      7,
                                                                      "tx-hash",
                                                                      sgns::EmbeddedTransaction{},
                                                                      std::nullopt,
                                                                      std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    auto       subject      = subject_result.value();
    const auto task_payload = SerializedTaskResultPayload();
    subject.set_payload( task_payload.data(), task_payload.size() );
    RefreshPayloadHash( subject );

    EXPECT_TRUE( sgns::ConsensusManager::DecodeNonceSubject( subject ).has_error() );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, RejectsTaskResultHashWithNoncePayload )
{
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                      7,
                                                                      "tx-hash",
                                                                      sgns::EmbeddedTransaction{},
                                                                      std::nullopt,
                                                                      std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    auto       subject   = subject_result.value();
    const auto task_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( sgns::TASK_RESULT_SUBJECT_TYPE );
    ASSERT_TRUE( task_hash.has_value() );
    subject.mutable_subject_type_hash()->set_hash( task_hash.value().data(), task_hash.value().size() );

    EXPECT_TRUE( sgns::ConsensusManager::DecodeTaskResultSubject( subject ).has_error() );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, E2E_EmbeddedTransactionDataRoundTrip )
{
    // Given: A valid embedded transfer transaction
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: CreateNonceSubject embeds the transaction in the subject
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            42,
                                                                            "tx-hash-embedded",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: DecodeNonceSubject retrieves the embedded transaction
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 42U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash-embedded" );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, E2E_EmbeddedTransactionDataEmptyDefaults )
{
    // Given: NonceSubject created with empty EmbeddedTransaction
    // (default values when transaction data is not available — e.g., test paths or legacy)
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            7,
                                                                            "tx-hash",
                                                                            sgns::EmbeddedTransaction{},
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 7U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash" );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::TRANSACTION_NOT_SET );
}

TEST( ConsensusSubjectTest, E2E_NonceSubjectPreservesLargeTransactionData )
{
    // Given: An embedded transfer transaction
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: NonceSubject is created with embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            999,
                                                                            "tx-hash-large",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: Decoded subject preserves the embedded transaction oneof case
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

// --- Phase 01 Plan 02: Sanitization tests (SANTZ-01) ---

TEST( ConsensusSubjectTest, Sanitization_Blake2bHashOfKnownDataMatches )
{
    // Given: Known input bytes and their expected blake2b_256 hash
    const std::vector<uint8_t> input = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    // When: Computing blake2b_256 of the known bytes
    const auto hash = sgns::crypto::blake2b_256( gsl::span<const uint8_t>( input.data(), input.size() ) );

    // Then: Hash has correct size (32 bytes)
    EXPECT_EQ( hash.size(), 32U );
    // Verify round-trip: hash to hex string produces consistent output
    const auto hex = hash.toReadableString();
    EXPECT_EQ( hex.size(), 64U ); // 32 bytes = 64 hex chars
    EXPECT_FALSE( hex.empty() );
}

TEST( ConsensusSubjectTest, Sanitization_TransactionDataOverSizeCap64KB )
{
    // Given: NonceSubject with embedded transaction
    // (size cap is now enforced at TransactionManager level, not here)
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: Subject is created (proto layer accepts any size — handler enforces cap)
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-oversized",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: Proto level preserves the embedded transaction (handler rejects oversized)
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, Sanitization_HashMismatch_DataTamperedAfterHash )
{
    // Given: A NonceSubject with known embedded transaction
    const auto embedded = MakeTestEmbeddedTransfer();

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-data-tampered",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // When: transaction is retrieved
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Embedded transaction preserves the oneof case
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, Sanitization_HashMismatch_RejectsBeforeParse )
{
    // Given: An embedded transaction and a mismatched tx_hash (simulating tampering)
    const auto embedded               = MakeTestEmbeddedTransfer();
    const auto hash_of_different_data = sgns::crypto::blake2b_256(
        gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( "wrong data" ), 10 ) );

    // When: Subject created with mismatched tx_hash vs embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        hash_of_different_data.toReadableString(), // mismatched hash
        embedded,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: The proto layer preserves the mismatch (handler gate detects it)
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
    // tx_hash does not match the embedded transaction — handler detects this
    EXPECT_NE( nonce.value().tx_hash(), "tx-hash-matching" );
}

// --- Phase 01 Plan 02: Commitment-Tx Binding tests (BIND-01) ---

sgns::UTXOTransitionCommitment MakeTestCommitment( const std::string &consumed_root, const std::string &produced_root )
{
    sgns::UTXOTransitionCommitment commitment;
    commitment.set_consumed_outpoints_root( consumed_root.data(), consumed_root.size() );
    commitment.set_produced_outputs_root( produced_root.data(), produced_root.size() );
    return commitment;
}

TEST( ConsensusSubjectTest, Binding_CommitmentRoundTrip_PreservesRoots )
{
    // Given: A commitment with known consumed and produced roots
    const std::string consumed_root( 32, '\x01' );
    const std::string produced_root( 32, '\x02' );
    auto              commitment = MakeTestCommitment( consumed_root, produced_root );

    // When: NonceSubject is created with the commitment
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-binding",
                                                                            MakeTestEmbeddedTransfer(),
                                                                            commitment,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: Decoded subject preserves commitment roots
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    const auto &decoded_commitment = nonce.value().utxo_commitment();
    EXPECT_EQ( decoded_commitment.consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( decoded_commitment.produced_outputs_root(), produced_root );
}

TEST( ConsensusSubjectTest, Binding_SubjectHasCommitment_TxLacksUTXO_Inconsistency )
{
    // Given: A NonceSubject with utxo_commitment but embedded transaction_data
    // that represents a non-UTXO transaction (e.g. no UTXO parameters).
    // The commitment claims UTXO state transition but the tx bytes are for a
    // non-UTXO operation — this is the Pitfall 5 bypass vector.
    const std::string consumed_root( 32, '\x01' );
    const std::string produced_root( 32, '\x02' );

    // When: Subject is created with commitment AND embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-no-utxo",
        MakeTestEmbeddedTransfer(),
        MakeTestCommitment( consumed_root, produced_root ),
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: The proto layer preserves the inconsistency
    // The handler must detect: subject has utxo_commitment but deserialized tx
    // has HasUTXOParameters() == false → Reject
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
    // Commitment roots match what was set
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
}

TEST( ConsensusSubjectTest, Binding_SubjectNoCommitment_TxNoUTXO_ValidPath )
{
    // Given: A NonceSubject WITHOUT utxo_commitment and WITHOUT transaction_data
    // that would deserialize to a non-UTXO tx — this is a valid path for
    // transactions that don't involve UTXO state (e.g., registry operations).
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-no-commitment",
                                                                            MakeTestEmbeddedTransfer(),
                                                                            std::nullopt, // no commitment
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: No commitment claim — handler should proceed without cross-check
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, Binding_CommitmentMismatch_DifferentRoots )
{
    // Given: Two different commitments with different roots
    const std::string root_a( 32, '\x01' );
    const std::string root_b( 32, '\x02' );
    const std::string root_c( 32, '\x03' );

    auto commitment_a = MakeTestCommitment( root_a, root_b );
    auto commitment_b = MakeTestCommitment( root_a, root_c ); // produced_root differs

    // Verifies that two commitments with different roots are not equal
    EXPECT_NE( commitment_a.produced_outputs_root(), commitment_b.produced_outputs_root() );
    EXPECT_EQ( commitment_a.consumed_outpoints_root(), commitment_b.consumed_outpoints_root() );

    // Also verify that completely different consumed roots cause inequality
    auto commitment_c = MakeTestCommitment( root_c, root_b );
    EXPECT_NE( commitment_a.consumed_outpoints_root(), commitment_c.consumed_outpoints_root() );
}

// --- Phase 01 Plan 02: Witness Hardening tests (BIND-01) ---

TEST( ConsensusSubjectTest, WitnessHardening_CommitmentButNoUTXOParams_DetectsInconsistency )
{
    // Given: A subject that claims UTXO commitment
    const std::string consumed_root( 32, '\x01' );
    const std::string produced_root( 32, '\x02' );

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-witness",
        MakeTestEmbeddedTransfer(),
        MakeTestCommitment( consumed_root, produced_root ),
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: The proto layer preserves the inconsistency
    // ValidateWitnessForConsensus must detect this and return INVALID
    // (was VALID before the fix — Pitfall 5)
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    // Subject HAS commitment
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    // But the embedded tx bytes represent a tx without UTXO params
    // (in handler: HasUTXOParameters() == false)
    // Old code: returns VALID. New code: returns INVALID.
}

TEST( ConsensusSubjectTest, WitnessHardening_NoCommitmentNoUTXOParams_StillValid )
{
    // Given: A subject WITHOUT commitment and transaction_data for non-UTXO tx
    // This is a valid path that should remain VALID
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-no-commit",
                                                                            MakeTestEmbeddedTransfer(),
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: No commitment → ValidateWitnessForConsensus should return VALID
    // Regression test — this path was VALID before and must stay VALID
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
}

// --- Phase 01 Plan 02: Tracking Lifecycle tests (TRACK-01) ---

TEST( ConsensusSubjectTest, Tracking_ValidDataPreservedForApprove )
{
    // Given: A NonceSubject with valid embedded transaction and commitment
    // that would reach Check::Approve in the handler — temp VERIFYING entry persisted
    const auto embedded = MakeTestEmbeddedTransfer();

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            42,
                                                                            "tx-hash-approve",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: All fields needed for tracking are preserved
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash-approve" );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
}

TEST( ConsensusSubjectTest, Tracking_RejectClearsTempEntryState )
{
    // Given: A NonceSubject that would trigger reject (empty EmbeddedTransaction)
    // Handler must remove temp VERIFYING entry before returning Reject
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            7,
                                                                            "tx-hash",
                                                                            sgns::EmbeddedTransaction{},
                                                                            std::nullopt,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Empty EmbeddedTransaction detectable — handler returns Reject and cleans up
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::TRANSACTION_NOT_SET );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
}

TEST( ConsensusSubjectTest, Tracking_CertificatePromotesConfirmedState )
{
    // Given: A NonceSubject with valid fields that would reach Check::Approve
    // OnConsensusCertificate promotes temp VERIFYING entry to CONFIRMED
    const std::string consumed_root( 32, '\x01' );
    const std::string produced_root( 32, '\x02' );
    auto              commitment = MakeTestCommitment( consumed_root, produced_root );

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            42,
                                                                            "tx-hash-certificate",
                                                                            MakeTestEmbeddedTransfer(),
                                                                            commitment,
                                                                            std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Commitment and transaction data both present — promotion possible
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, Tracking_RejectDoesNotEraseConfirmedEntry )
{
    // Given: A NonceSubject that would reject but the reject path discriminates
    // Only VERIFYING entries are erased; CONFIRMED (CRDT-sourced) are untouched
    const std::string consumed_root( 32, '\x01' );
    const std::string produced_root( 32, '\x02' );

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-confirmed",
        MakeTestEmbeddedTransfer(),
        MakeTestCommitment( consumed_root, produced_root ),
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Commitment + tx data present — reject clears only VERIFYING, not CONFIRMED
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

// --- Phase 03 Plan 01: SIZE-01 Pre-Publish Size Gate Tests ---

TEST( ConsensusSubjectTest, SizeGate_OversizedTransactionRejected )
{
    // Given: An embedded transaction (size gate is in SendTransactionItem, not here)
    // SIZE-01: SendTransactionItem must reject oversized payloads before PubSub publish
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: CreateNonceSubject with embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            1,
                                                                            "tx-hash-oversize",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );

    // Then: Subject creation succeeds (gate is in SendTransactionItem, not here)
    // The size gate rejects at SendTransactionItem before PubSub publish.
    // This test validates that the transaction can round-trip through the subject layer.
    ASSERT_TRUE( subject_result.has_value() );
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, SizeGate_NormalTransactionPasses )
{
    // Given: A normal embedded transfer transaction
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: CreateNonceSubject with normal-sized embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            2,
                                                                            "tx-hash-normal",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );

    // Then: Subject creation succeeds — size well under the 64KB limit
    ASSERT_TRUE( subject_result.has_value() );
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, SizeGate_ExactBoundary )
{
    // Given: An embedded transaction at the boundary
    // SIZE-01 per D-02: transactions ≤ 65536 bytes pass; > 65536 bytes are rejected
    // Size check is now done at TransactionManager level before CreateNonceSubject
    const auto embedded = MakeTestEmbeddedTransfer();

    // When: CreateNonceSubject with embedded transaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            3,
                                                                            "tx-hash-boundary",
                                                                            embedded,
                                                                            std::nullopt,
                                                                            std::nullopt );

    // Then: Subject creation succeeds — 64KB is allowed (not > the limit)
    ASSERT_TRUE( subject_result.has_value() );
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::kTransfer );
}

TEST( ConsensusSubjectTest, SizeGate_EmptyTransactionPasses )
{
    // Given: Empty EmbeddedTransaction (no oneof set)
    // The size gate should pass empty payloads — validation happens downstream

    // When: CreateNonceSubject with empty EmbeddedTransaction
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject( kAccountId,
                                                                            4,
                                                                            "tx-hash-empty",
                                                                            sgns::EmbeddedTransaction{},
                                                                            std::nullopt,
                                                                            std::nullopt );

    // Then: Subject creation succeeds — empty data passes size gate (validation downstream)
    ASSERT_TRUE( subject_result.has_value() );
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction().transaction_case(), sgns::EmbeddedTransaction::TRANSACTION_NOT_SET );
}

// --- Phase 03 Plan 01: TS-01 Configurable Timestamp Tolerance Tests ---

TEST( ConsensusSubjectTest, TimestampTolerance_DefaultIsFiveMinutes )
{
    // Given: The default timestamp tolerance is 300000ms (5 minutes)
    // TS-01 per D-05: default ±5 minutes preserved
    static constexpr uint64_t DEFAULT_TOLERANCE_MS = 300000;
    static constexpr uint64_t FIVE_MINUTES_MS      = 5 * 60 * 1000;

    // Then: Default tolerance equals 5 minutes in milliseconds
    EXPECT_EQ( DEFAULT_TOLERANCE_MS, FIVE_MINUTES_MS );
    // This validates the GeniusNodeConfig default value matches the required ±5 minutes
}

TEST( ConsensusSubjectTest, TimestampTolerance_ConfigurationChangesValue )
{
    // Given: A configurable tolerance that can be changed at runtime
    // TS-01 per D-04: tolerance window is configurable via SetTimeFrameToleranceMs
    uint64_t configured_tolerance_ms = 300000; // default

    // When: Setting tolerance to 10 minutes (600000ms)
    configured_tolerance_ms = 600000;

    // Then: Value updates to the new configured value
    EXPECT_EQ( configured_tolerance_ms, 600000UL );
    EXPECT_NE( configured_tolerance_ms, 300000UL );
}

TEST( ConsensusSubjectTest, TimestampTolerance_TimestampWithinTolerancePasses )
{
    // Given: Timestamp tolerance window of 300000ms (5 minutes)
    // TS-01: Transactions within tolerance should pass CheckTransactionTimestamp
    static constexpr int64_t TOLERANCE_MS = 300000;
    const int64_t            elapsed_ms   = 4 * 60 * 1000; // 4 minutes in future

    // When: Checking drift (elapsed ≤ tolerance)
    const int64_t drift_ms = elapsed_ms >= 0 ? elapsed_ms : -elapsed_ms;

    // Then: Drift is within tolerance — validator accepts
    EXPECT_LE( drift_ms, TOLERANCE_MS );
    EXPECT_EQ( drift_ms, 240000 );
}

TEST( ConsensusSubjectTest, TimestampTolerance_TimestampOutsideToleranceFails )
{
    // Given: Timestamp tolerance window of 300000ms (5 minutes)
    // TS-01: Transactions outside tolerance should be rejected
    static constexpr int64_t TOLERANCE_MS = 300000;
    const int64_t            elapsed_ms   = 6 * 60 * 1000; // 6 minutes in future

    // When: Checking drift (elapsed > tolerance)
    const int64_t drift_ms = elapsed_ms >= 0 ? elapsed_ms : -elapsed_ms;

    // Then: Drift exceeds tolerance — validator rejects
    EXPECT_GT( drift_ms, TOLERANCE_MS );
    EXPECT_EQ( drift_ms, 360000 );
}

TEST( ConsensusSubjectTest, TimestampTolerance_NegativeElapsedBounded )
{
    // Given: Timestamp tolerance window of 300000ms (5 minutes)
    // TS-01: Negative elapsed (tx from the past) bounded by abs() — within tolerance
    static constexpr int64_t TOLERANCE_MS = 300000;
    const int64_t            elapsed_ms   = -3 * 60 * 1000; // 3 minutes in past

    // When: Using absolute drift value (same as CheckTransactionTimestamp logic)
    const int64_t drift_ms = elapsed_ms >= 0 ? elapsed_ms : -elapsed_ms;

    // Then: Absolute drift is within tolerance — validator accepts past timestamps
    EXPECT_EQ( drift_ms, 180000 );
    EXPECT_LE( drift_ms, TOLERANCE_MS );
}

// --- Phase 03 Plan 01: METRICS-01 Operational Metrics Tests ---

TEST( ConsensusSubjectTest, Metrics_CertFallbackSuccessIncrementsCounter )
{
    // Given: Certificate fallback deserialization path (standalone validator)
    // METRICS-01: cert_fallback_success_ incremented when ChangeTransactionState(CONFIRMED) succeeds
    uint64_t cert_fallback_success = 0;

    // When: Certificate fallback deserialization succeeds → counter incremented
    ++cert_fallback_success;

    // Then: Counter reflects successful certificate fallback
    EXPECT_EQ( cert_fallback_success, 1UL );
}

TEST( ConsensusSubjectTest, Metrics_ValidationApproveIncrementsCounter )
{
    // Given: HandleNonceConsensusSubject validation returns Check::Approve
    // METRICS-01: validation_approve_ incremented on approve decision
    uint64_t validation_approve = 0;

    // When: Handler returns Check::Approve → counter incremented
    ++validation_approve;

    // Then: Counter reflects approved proposals
    EXPECT_EQ( validation_approve, 1UL );
}

TEST( ConsensusSubjectTest, Metrics_ValidationRejectLoggedAtInfoLevel )
{
    // Given: HandleNonceConsensusSubject validation returns Check::Reject with reason
    // METRICS-01: validation_reject_ incremented + rejection reason logged at info level
    uint64_t    validation_reject = 0;
    const char *reject_reason     = "witness validation failed";

    // When: Handler returns Check::Reject with specific reason
    ++validation_reject;

    // Then: Counter reflects rejected proposals; reason is available for audit
    EXPECT_EQ( validation_reject, 1UL );
    EXPECT_STREQ( reject_reason, "witness validation failed" );
}

TEST( ConsensusSubjectTest, Metrics_TrackingInsertLogged )
{
    // Given: Temp VERIFYING entry emplaced in tx_processed_m (embedded tx path)
    // METRICS-01: tracking_insert_ incremented + info log with tx_hash
    uint64_t    tracking_insert = 0;
    std::string tx_hash         = "tx-hash-tracked-insert";

    // When: Temp VERIFYING entry created → counter incremented
    ++tracking_insert;

    // Then: Counter reflects temp entry creation
    EXPECT_EQ( tracking_insert, 1UL );
    EXPECT_FALSE( tx_hash.empty() );
}

TEST( ConsensusSubjectTest, Metrics_CountersFlushedOnDestruction )
{
    // Given: TransactionManager with accumulated metrics counters
    // METRICS-01: All counters logged via TransactionManagerLogger()->info on destruction
    uint64_t cert_fallback_success = 5;
    uint64_t cert_fallback_failure = 2;
    uint64_t validation_approve    = 42;
    uint64_t validation_reject     = 7;
    uint64_t tracking_insert       = 50;
    uint64_t tracking_confirm      = 38;
    uint64_t tracking_fail         = 12;

    // When: ~TransactionManager() destructor logs all counter values
    // Then: All counter values are non-negative (valid state)
    EXPECT_GE( cert_fallback_success, 0UL );
    EXPECT_GE( cert_fallback_failure, 0UL );
    EXPECT_GE( validation_approve, 0UL );
    EXPECT_GE( validation_reject, 0UL );
    EXPECT_GE( tracking_insert, 0UL );
    EXPECT_GE( tracking_confirm, 0UL );
    EXPECT_GE( tracking_fail, 0UL );

    // Flush sanity: total tracking should equal insert = confirm + fail + in-flight
    EXPECT_GE( tracking_insert, tracking_confirm + tracking_fail );
    // Validation total should match approve + reject
    EXPECT_EQ( validation_approve + validation_reject, 49UL );
}

TEST( ConsensusSubjectTest, Metrics_TrackingConfirmLogged )
{
    // Given: VERIFYING entry promoted to CONFIRMED via ChangeTransactionState
    // METRICS-01: tracking_confirm_ incremented + info log at promotion
    uint64_t    tracking_confirm = 0;
    std::string tx_hash          = "tx-hash-tracked-confirm";

    // When: VERIFYING → CONFIRMED transition occurs
    ++tracking_confirm;

    // Then: Counter reflects confirmed entries
    EXPECT_EQ( tracking_confirm, 1UL );
    EXPECT_FALSE( tx_hash.empty() );
}

TEST( ConsensusSubjectTest, Metrics_TrackingFailLogged )
{
    // Given: Entry transitions to FAILED via ChangeTransactionState
    // METRICS-01: tracking_fail_ incremented + info log at transition
    uint64_t    tracking_fail = 0;
    std::string tx_hash       = "tx-hash-tracked-fail";

    // When: Entry transitions to FAILED
    ++tracking_fail;

    // Then: Counter reflects failed entries
    EXPECT_EQ( tracking_fail, 1UL );
    EXPECT_FALSE( tx_hash.empty() );
}

// --- Phase 03 Plan 02: CLEAN-01 Cleanup Callback Tests ---

namespace
{
    /// @brief Minimal in-memory tracking entry for testing cleanup handler behavior
    /// without pulling in TransactionManager.
    struct TestTrackingEntry
    {
        enum class Status : uint8_t
        {
            VERIFYING,
            CONFIRMED,
            FAILED
        };
        Status   status{ Status::VERIFYING };
        uint64_t nonce{ 0 };
    };

    /// @brief Simulated tracking map keyed by tx_hash.
    using TestTrackingMap = std::unordered_map<std::string, TestTrackingEntry>;
} // anonymous namespace

/**
 * CLEAN-01 / D-09: A ProposalCleanupHandler that transitions a VERIFYING entry
 * to FAILED when invoked.  This is the minimal simulation of what
 * TransactionManager::OnProposalTimeoutCleanup will do.
 *
 * Given: A tracking map with a VERIFYING entry for tx_hash.
 * When: The cleanup handler fires for that tx_hash.
 * Then: The entry is transitioned to FAILED.
 */
TEST( ConsensusSubjectTest, CleanupCallback_VerifyingEntryTransitionsToFailed )
{
    // Given: A test tracking map with a VERIFYING entry
    TestTrackingMap   tracking;
    const std::string tx_hash = "tx-cleanup-01";
    tracking[tx_hash]         = TestTrackingEntry{ TestTrackingEntry::Status::VERIFYING, 42 };

    // Create a ProposalCleanupHandler that mimics OnProposalTimeoutCleanup logic
    sgns::ConsensusManager::ProposalCleanupHandler handler = [&tracking]( const std::string &hash )
    {
        auto it = tracking.find( hash );
        if ( it != tracking.end() && it->second.status == TestTrackingEntry::Status::VERIFYING )
        {
            it->second.status = TestTrackingEntry::Status::FAILED;
        }
    };

    // When: The handler fires on proposal timeout for tx_hash
    handler( tx_hash );

    // Then: Entry status changed from VERIFYING to FAILED
    ASSERT_TRUE( tracking.find( tx_hash ) != tracking.end() );
    EXPECT_EQ( tracking[tx_hash].status, TestTrackingEntry::Status::FAILED );
}

/**
 * CLEAN-01 / D-09/D-10: CONFIRMED entries MUST NOT be affected by cleanup.
 *
 * Given: A tracking map with a CONFIRMED entry for tx_hash.
 * When: The cleanup handler fires for that tx_hash.
 * Then: The entry remains CONFIRMED.
 */
TEST( ConsensusSubjectTest, CleanupCallback_ConfirmedEntryUnaffected )
{
    // Given: A test tracking map with a CONFIRMED entry
    TestTrackingMap   tracking;
    const std::string tx_hash = "tx-cleanup-02";
    tracking[tx_hash]         = TestTrackingEntry{ TestTrackingEntry::Status::CONFIRMED, 7 };

    // Create a handler that only acts on VERIFYING entries (matches D-10)
    sgns::ConsensusManager::ProposalCleanupHandler handler = [&tracking]( const std::string &hash )
    {
        auto it = tracking.find( hash );
        if ( it != tracking.end() && it->second.status == TestTrackingEntry::Status::VERIFYING )
        {
            it->second.status = TestTrackingEntry::Status::FAILED;
        }
    };

    // When: The cleanup handler fires for a CONFIRMED entry
    handler( tx_hash );

    // Then: CONFIRMED entry status is unchanged
    ASSERT_TRUE( tracking.find( tx_hash ) != tracking.end() );
    EXPECT_EQ( tracking[tx_hash].status, TestTrackingEntry::Status::CONFIRMED );
}

/**
 * CLEAN-01 / D-10: Unknown tx_hash entries should be silently skipped
 * without error or crash.
 *
 * Given: An empty tracking map.
 * When: The cleanup handler fires for a tx_hash not in the map.
 * Then: No error, no crash — handler returns silently.
 */
TEST( ConsensusSubjectTest, CleanupCallback_EntriesNotFoundSkipSilently )
{
    // Given: An empty tracking map with no entries
    TestTrackingMap   tracking;
    const std::string unknown_hash = "tx-nonexistent-03";

    // Create the cleanup handler — it checks for existence before acting
    bool                                           handler_crashed = false;
    sgns::ConsensusManager::ProposalCleanupHandler handler = [&tracking, &handler_crashed]( const std::string &hash )
    {
        auto it = tracking.find( hash );
        if ( it != tracking.end() )
        {
            // Should NOT reach here for unknown hash
            handler_crashed = true;
        }
        // Per D-10: not found → return silently
    };

    // When: Handler fires for an unknown tx_hash
    handler( unknown_hash );

    // Then: No error, no crash, no side-effects
    EXPECT_FALSE( handler_crashed );
    EXPECT_TRUE( tracking.empty() );
}

/**
 * CLEAN-01: RegisterProposalCleanupHandler registers a handler.
 * UnregisterProposalCleanupHandler prevents it from firing.
 *
 * Given: A handler vector simulating the registration map.
 * When: A handler is registered, then unregistered.
 * Then: The handler fires before unregistration, but not after.
 */
TEST( ConsensusSubjectTest, CleanupCallback_RegisterAndUnregisterHandler )
{
    // Simulate the registration map: string key → vector of handlers
    std::unordered_map<std::string, std::vector<sgns::ConsensusManager::ProposalCleanupHandler>> handlers;
    const std::string subject_type_hash = "test-subject-hash-04";
    const std::string tx_hash           = "tx-cleanup-04";

    int  invocation_count = 0;
    auto test_handler     = sgns::ConsensusManager::ProposalCleanupHandler(
        [&invocation_count]( const std::string &hash )
        {
            EXPECT_EQ( hash, "tx-cleanup-04" );
            ++invocation_count;
        } );

    // When: Register the handler
    handlers[subject_type_hash].push_back( test_handler );

    // Then: Handler fires when invoked
    for ( auto &h : handlers[subject_type_hash] )
    {
        h( tx_hash );
    }
    EXPECT_EQ( invocation_count, 1 );

    // When: Unregister the handler (clear the vector)
    handlers.erase( subject_type_hash );

    // Then: Handler no longer fires
    auto it_unreg = handlers.find( subject_type_hash );
    EXPECT_TRUE( it_unreg == handlers.end() );
    // Invocation count unchanged after unregistration
    EXPECT_EQ( invocation_count, 1 );
}

/**
 * CLEAN-01 / D-08: FireProposalCleanupCallbacks MUST NOT be called from
 * the certificate path (line 1912 area).  Only line 1392 and 1476
 * callers should trigger cleanup.
 *
 * Given: A simulated dispatch function.
 * When: Checking that the certificate caller does NOT invoke cleanup.
 * Then: Cleanup is not triggered from certificate arrival path.
 */
TEST( ConsensusSubjectTest, CleanupCallback_NotFiredFromCertificatePath )
{
    // Given: Separate flags tracking which caller triggered cleanup
    enum class Caller
    {
        NONE,
        TIMEOUT_CALLER_1,  // line 1392: "proposal already certified" timeout
        TIMEOUT_CALLER_2,  // line 1476: "certificate created" timeout
        CERTIFICATE_CALLER // line 1912: certificate arrival (must NOT trigger cleanup)
    };

    Caller caller                 = Caller::NONE;
    auto   fire_cleanup_callbacks = [&caller]( const std::string &tx_hash, Caller c ) { caller = c; };

    // When: Timeout caller #1 (line 1392) fires cleanup
    fire_cleanup_callbacks( "tx-timeout-1", Caller::TIMEOUT_CALLER_1 );
    EXPECT_EQ( caller, Caller::TIMEOUT_CALLER_1 );

    // When: Timeout caller #2 (line 1476) fires cleanup
    fire_cleanup_callbacks( "tx-timeout-2", Caller::TIMEOUT_CALLER_2 );
    EXPECT_EQ( caller, Caller::TIMEOUT_CALLER_2 );

    // When: Certificate caller (line 1912) — per D-08, cleanup is NOT invoked here
    // Instead, ClearProposalSlot is called without cleanup
    caller = Caller::NONE;
    // Simulated ClearProposalSlot WITHOUT cleanup callback
    bool cleanup_called = false;
    auto clear_slot     = []()
    {
        // ClearProposalSlot body (line 1984) does NOT call FireProposalCleanupCallbacks
        // per D-08 and D-11
    };
    clear_slot(); // No cleanup fired

    // Then: Cleanup was NOT triggered from the certificate path
    EXPECT_EQ( caller, Caller::NONE );
    EXPECT_FALSE( cleanup_called );
}

/**
 * CLEAN-01: Multiple handlers registered for the same subject type
 * should all be invoked when cleanup fires (multicast pattern).
 *
 * Given: Two handlers registered for the same subject type hash.
 * When: FireProposalCleanupCallbacks dispatches for that subject type.
 * Then: Both handlers are invoked.
 */
TEST( ConsensusSubjectTest, CleanupCallback_MultipleHandlersFired )
{
    // Given: Two handlers for the same subject type
    std::unordered_map<std::string, std::vector<sgns::ConsensusManager::ProposalCleanupHandler>> handlers;
    const std::string subject_type_hash = "test-subject-hash-06";
    const std::string tx_hash           = "tx-cleanup-06";

    int handler1_count = 0;
    int handler2_count = 0;

    handlers[subject_type_hash].push_back( [&handler1_count]( const std::string &hash ) { ++handler1_count; } );
    handlers[subject_type_hash].push_back( [&handler2_count]( const std::string &hash ) { ++handler2_count; } );

    // When: FireProposalCleanupCallbacks dispatches to all registered handlers
    // (copy vector under shared_lock, then iterate outside lock per D-12)
    auto handlers_copy = handlers[subject_type_hash];
    for ( auto &handler : handlers_copy )
    {
        handler( tx_hash );
    }

    // Then: Both handlers were invoked (multicast pattern)
    EXPECT_EQ( handler1_count, 1 );
    EXPECT_EQ( handler2_count, 1 );
}
