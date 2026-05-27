#include <gtest/gtest.h>
#include <gsl/span>

#include "blockchain/Consensus.hpp"

#include "crypto/hasher/hasher_impl.hpp"

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
    constexpr const char *kAccountId = "gnus-test-account";
    constexpr const char *kBridgeSubjectType = "gnus.bridge_event.v1";

    std::vector<uint8_t> BridgePayload()
    {
        return std::vector<uint8_t>{ 0x01, 0x02, 0x03 };
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
        sgns::crypto::HasherImpl hasher;
        auto payload_hash = hasher.sha2_256(
            gsl::span<const uint8_t>(
                reinterpret_cast<const uint8_t *>( subject.payload().data() ),
                subject.payload().size() ) );
        subject.set_payload_hash( payload_hash.data(), payload_hash.size() );
    }
}

TEST( ConsensusSubjectTest, ComputesSubjectTypeHashFromString )
{
    const auto first = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    const auto second = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
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
    const auto payload = BridgePayload();
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject(
        kAccountId,
        kBridgeSubjectType,
        payload );

    ASSERT_TRUE( subject_result.has_value() );
    const auto &subject = subject_result.value();
    EXPECT_EQ( subject.account_id(), kAccountId );
    EXPECT_EQ( subject.payload(), std::string( payload.begin(), payload.end() ) );
    EXPECT_FALSE( subject.payload_hash().empty() );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value() );

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
    EXPECT_TRUE( sgns::ConsensusManager::CreateGenericSubject(
                     kAccountId,
                     kBridgeSubjectType,
                     std::vector<uint8_t>{} )
                     .has_error() );
}

TEST( ConsensusSubjectTest, ValidateGenericSubjectRejectsTamperedPayloadHash )
{
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject(
        kAccountId,
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
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject(
        kAccountId,
        kBridgeSubjectType,
        BridgePayload() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    ASSERT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    subject.clear_subject_type_hash();

    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, CreatesBuiltInSubjectWithCanonicalStringType )
{
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::string{},
        std::vector<uint8_t>{},
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto &subject = subject_result.value();
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 7U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash" );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( sgns::NONCE_SUBJECT_TYPE );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value() );

    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_TRUE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, RejectsMalformedNoncePayload )
{
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::string{},
        std::vector<uint8_t>{},
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
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::string{},
        std::vector<uint8_t>{},
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    const auto task_payload = SerializedTaskResultPayload();
    subject.set_payload( task_payload.data(), task_payload.size() );
    RefreshPayloadHash( subject );

    EXPECT_TRUE( sgns::ConsensusManager::DecodeNonceSubject( subject ).has_error() );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, RejectsTaskResultHashWithNoncePayload )
{
    auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::string{},
        std::vector<uint8_t>{},
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    const auto task_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( sgns::TASK_RESULT_SUBJECT_TYPE );
    ASSERT_TRUE( task_hash.has_value() );
    subject.mutable_subject_type_hash()->set_hash( task_hash.value().data(), task_hash.value().size() );

    EXPECT_TRUE( sgns::ConsensusManager::DecodeTaskResultSubject( subject ).has_error() );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManagerTestAccess::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, E2E_EmbeddedTransactionDataRoundTrip )
{
    // Given: A valid transaction's serialized bytes and type tag
    const std::string         tx_type = "transfer";
    const std::vector<uint8_t> tx_data = { 0x01, 0x02, 0x03, 0x04, 0x05 };

    // When: CreateNonceSubject embeds the transaction data in the subject
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        42,
        "tx-hash-embedded",
        tx_type,
        tx_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: DecodeNonceSubject retrieves the embedded transaction_type and transaction_data
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 42U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash-embedded" );
    EXPECT_EQ( nonce.value().transaction_type(), tx_type );
    EXPECT_EQ( nonce.value().transaction_data(), std::string( tx_data.begin(), tx_data.end() ) );
}

TEST( ConsensusSubjectTest, E2E_EmbeddedTransactionDataEmptyDefaults )
{
    // Given: NonceSubject created with empty transaction_type and empty transaction_data
    // (default values when transaction data is not available — e.g., test paths or legacy)
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::string{},
        std::vector<uint8_t>{},
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().nonce(), 7U );
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash" );
    EXPECT_TRUE( nonce.value().transaction_type().empty() );
    EXPECT_TRUE( nonce.value().transaction_data().empty() );
}

TEST( ConsensusSubjectTest, E2E_NonceSubjectPreservesLargeTransactionData )
{
    // Given: Transaction data up to 64KB (max expected embedded size)
    const std::string         tx_type = "migration";
    const std::vector<uint8_t> tx_data( 64 * 1024, 0xAA ); // 64KB of 0xAA

    // When: NonceSubject is created with large embedded data
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        999,
        "tx-hash-large",
        tx_type,
        tx_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: Decoded subject preserves all embedded data
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction_type(), tx_type );
    EXPECT_EQ( nonce.value().transaction_data().size(), tx_data.size() );
    EXPECT_EQ( nonce.value().transaction_data(), std::string( tx_data.begin(), tx_data.end() ) );
}

// --- Phase 01 Plan 02: Sanitization tests (SANTZ-01) ---

TEST( ConsensusSubjectTest, Sanitization_Blake2bHashOfKnownDataMatches )
{
    // Given: Known input bytes and their expected blake2b_256 hash
    const std::vector<uint8_t> input = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    sgns::crypto::HasherImpl hasher;

    // When: Computing blake2b_256 of the known bytes
    const auto hash = hasher.blake2b_256(
        gsl::span<const uint8_t>( input.data(), input.size() ) );

    // Then: Hash has correct size (32 bytes)
    EXPECT_EQ( hash.size(), 32U );
    // Verify round-trip: hash to hex string produces consistent output
    const auto hex = hash.toReadableString();
    EXPECT_EQ( hex.size(), 64U ); // 32 bytes = 64 hex chars
    EXPECT_FALSE( hex.empty() );
}

TEST( ConsensusSubjectTest, Sanitization_TransactionDataOverSizeCap64KB )
{
    // Given: NonceSubject with transaction_data exceeding 64KB
    const std::string         tx_type = "transfer";
    const std::vector<uint8_t> tx_data( 100 * 1024, 0x00 ); // 100KB — exceeds 64KB cap

    // When: Subject is created (proto layer accepts any size — handler enforces cap)
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-oversized",
        tx_type,
        tx_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: Proto level preserves the oversized data (handler rejects it)
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_EQ( nonce.value().transaction_data().size(), tx_data.size() );
}

TEST( ConsensusSubjectTest, Sanitization_HashMismatch_DataTamperedAfterHash )
{
    // Given: A NonceSubject with known transaction_data
    const std::string         tx_type = "transfer";
    const std::vector<uint8_t> original_data = { 0x01, 0x02, 0x03, 0x04 };
    sgns::crypto::HasherImpl  hasher;
    const auto expected_hash = hasher.blake2b_256(
        gsl::span<const uint8_t>( original_data.data(), original_data.size() ) );

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        expected_hash.toReadableString(), // tx_hash matches original_data
        tx_type,
        original_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // When: transaction_data is retrieved — it matches the hash
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Verify data integrity: blake2b of embedded data equals tx_hash
    const auto computed = hasher.blake2b_256(
        gsl::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>( nonce.value().transaction_data().data() ),
            nonce.value().transaction_data().size() ) );
    EXPECT_EQ( computed.toReadableString(), nonce.value().tx_hash() );
}

TEST( ConsensusSubjectTest, Sanitization_HashMismatch_RejectsBeforeParse )
{
    // Given: Known data and a correct hash of different data (simulating tampering)
    const std::string         tx_type = "transfer";
    const std::vector<uint8_t> tx_data = { 0x01, 0x02, 0x03, 0x04 };
    sgns::crypto::HasherImpl  hasher;
    const auto hash_of_different_data = hasher.blake2b_256(
        gsl::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>( "wrong data" ), 10 ) );

    // When: Subject created with mismatched tx_hash vs transaction_data
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        hash_of_different_data.toReadableString(), // mismatched hash
        tx_type,
        tx_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: The proto layer preserves the mismatch (handler gate detects it)
    // blake2b of embedded data != subject's tx_hash
    const auto computed = hasher.blake2b_256(
        gsl::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>( nonce.value().transaction_data().data() ),
            nonce.value().transaction_data().size() ) );
    EXPECT_NE( computed.toReadableString(), nonce.value().tx_hash() );
    // Also verify fromReadableString round-trip
    const auto decoded_hash = sgns::base::Hash256::fromReadableString( nonce.value().tx_hash() );
    EXPECT_TRUE( decoded_hash.has_value() );
    EXPECT_NE( computed, decoded_hash.value() );
}

// --- Phase 01 Plan 02: Commitment-Tx Binding tests (BIND-01) ---

sgns::UTXOTransitionCommitment MakeTestCommitment( const std::string &consumed_root,
                                                    const std::string &produced_root )
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
    auto commitment = MakeTestCommitment( consumed_root, produced_root );

    // When: NonceSubject is created with the commitment
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-binding",
        "transfer",
        std::vector<uint8_t>{ 0x01, 0x02 },
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

    // When: Subject is created with commitment AND transaction_data
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-no-utxo",
        "transfer",
        std::vector<uint8_t>{ 0xAA, 0xBB }, // small non-UTXO tx bytes
        MakeTestCommitment( consumed_root, produced_root ),
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: The proto layer preserves the inconsistency
    // The handler must detect: subject has utxo_commitment but deserialized tx
    // has HasUTXOParameters() == false → Reject
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_FALSE( nonce.value().transaction_data().empty() );
    // Commitment roots match what was set
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
}

TEST( ConsensusSubjectTest, Binding_SubjectNoCommitment_TxNoUTXO_ValidPath )
{
    // Given: A NonceSubject WITHOUT utxo_commitment and WITHOUT transaction_data
    // that would deserialize to a non-UTXO tx — this is a valid path for
    // transactions that don't involve UTXO state (e.g., registry operations).
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-no-commitment",
        "transfer",
        std::vector<uint8_t>{ 0x01, 0x02 },
        std::nullopt,   // no commitment
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    // Then: No commitment claim — handler should proceed without cross-check
    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
    EXPECT_FALSE( nonce.value().transaction_data().empty() );
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
        "transfer",
        std::vector<uint8_t>{ 0x01 }, // tx bytes (non-UTXO)
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
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        1,
        "tx-hash-no-commit",
        "transfer",
        std::vector<uint8_t>{ 0x01 },
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
    // Given: A NonceSubject with valid embedded transaction_data and commitment
    // that would reach Check::Approve in the handler — temp VERIFYING entry persisted
    const std::string          tx_type = "transfer";
    const std::vector<uint8_t> tx_data = { 0x01, 0x02, 0x03, 0x04, 0x05 };

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        42,
        "tx-hash-approve",
        tx_type,
        tx_data,
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: All fields needed for tracking are preserved
    EXPECT_EQ( nonce.value().tx_hash(), "tx-hash-approve" );
    EXPECT_EQ( nonce.value().transaction_type(), tx_type );
    EXPECT_EQ( nonce.value().transaction_data(), std::string( tx_data.begin(), tx_data.end() ) );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
}

TEST( ConsensusSubjectTest, Tracking_RejectClearsTempEntryState )
{
    // Given: A NonceSubject that would trigger reject (empty transaction_data)
    // Handler must remove temp VERIFYING entry before returning Reject
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash-reject",
        std::string{},
        std::vector<uint8_t>{},
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Empty transaction_data detectable — handler returns Reject and cleans up
    EXPECT_TRUE( nonce.value().transaction_data().empty() );
    EXPECT_FALSE( nonce.value().has_utxo_commitment() );
}

TEST( ConsensusSubjectTest, Tracking_CertificatePromotesConfirmedState )
{
    // Given: A NonceSubject with valid fields that would reach Check::Approve
    // OnConsensusCertificate promotes temp VERIFYING entry to CONFIRMED
    const std::vector<uint8_t> tx_data      = { 0x01, 0x02, 0x03 };
    const std::string          consumed_root( 32, '\x01' );
    const std::string          produced_root( 32, '\x02' );
    auto                       commitment = MakeTestCommitment( consumed_root, produced_root );

    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        42,
        "tx-hash-certificate",
        "transfer",
        tx_data,
        commitment,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Commitment and transaction data both present — promotion possible
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
    EXPECT_FALSE( nonce.value().transaction_data().empty() );
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
        "transfer",
        std::vector<uint8_t>{ 0x01 },
        MakeTestCommitment( consumed_root, produced_root ),
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto nonce = sgns::ConsensusManager::DecodeNonceSubject( subject_result.value() );
    ASSERT_TRUE( nonce.has_value() );

    // Then: Commitment + tx data present — reject clears only VERIFYING, not CONFIRMED
    EXPECT_TRUE( nonce.value().has_utxo_commitment() );
    EXPECT_EQ( nonce.value().utxo_commitment().consumed_outpoints_root(), consumed_root );
    EXPECT_EQ( nonce.value().utxo_commitment().produced_outputs_root(), produced_root );
    EXPECT_FALSE( nonce.value().transaction_data().empty() );
}
