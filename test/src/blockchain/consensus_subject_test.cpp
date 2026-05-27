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
