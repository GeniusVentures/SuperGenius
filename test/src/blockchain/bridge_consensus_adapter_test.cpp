#include <gtest/gtest.h>
#include <gsl/span>

#include <eth/abi_decoder.hpp>

#include "account/BridgeConsensusAdapter.hpp"
#include "crypto/hasher/hasher_impl.hpp"

namespace
{
    constexpr const char *kAccountId = "gnus-bridge-account";

    template <typename Array>
    Array make_filled( uint8_t seed )
    {
        Array value{};
        for ( size_t i = 0; i < value.size(); ++i )
        {
            value[i] = static_cast<uint8_t>( seed + i );
        }
        return value;
    }

    eth::BridgeEventClaim MakeClaim()
    {
        eth::BridgeEventClaim claim;
        claim.src_chain_id     = 1;
        claim.dest_chain_id    = 56;
        claim.block_number     = 123456;
        claim.block_hash       = make_filled<eth::Hash256>( 0x10 );
        claim.tx_hash          = make_filled<eth::Hash256>( 0x30 );
        claim.log_index        = 5;
        claim.bridge_contract  = make_filled<eth::Address>( 0x50 );
        claim.event_topic0     = eth::abi::event_signature_hash( "BridgeSourceBurned(address,uint256,uint256,address)" );
        claim.topics           = { claim.event_topic0, make_filled<eth::Hash256>( 0x70 ) };
        claim.data             = { 0x01, 0x02, 0x03 };
        claim.sender           = make_filled<eth::Address>( 0x80 );
        claim.token_id_or_nonce = intx::uint256{ 42 };
        claim.amount           = intx::uint256{ 1000 };
        claim.recipient        = make_filled<eth::Address>( 0xA0 );
        claim.observed_at      = 987654;
        claim.finality_depth   = 64;
        return claim;
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
} // namespace

TEST( BridgeConsensusAdapterTest, CreatesAndDecodesBridgeSubject )
{
    const auto claim = MakeClaim();
    auto       subject_result = sgns::CreateBridgeEventConsensusSubject( kAccountId, claim );
    ASSERT_TRUE( subject_result.has_value() );

    const auto &subject = subject_result.value();
    EXPECT_EQ( subject.account_id(), kAccountId );
    EXPECT_TRUE( sgns::ConsensusManager::SubjectTypeMatches(
        subject,
        std::string( sgns::kBridgeEventSubjectType ) ) );

    auto decoded = sgns::DecodeBridgeEventConsensusSubject( subject );
    ASSERT_TRUE( decoded.has_value() );
    EXPECT_EQ( decoded.value().tx_hash, claim.tx_hash );
    EXPECT_EQ( decoded.value().log_index, claim.log_index );
    EXPECT_EQ( decoded.value().amount, claim.amount );
    EXPECT_EQ( decoded.value().recipient, claim.recipient );
}

TEST( BridgeConsensusAdapterTest, RejectsMalformedBridgePayload )
{
    auto subject_result = sgns::CreateBridgeEventConsensusSubject( kAccountId, MakeClaim() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    subject.set_payload( "\xff\xff\xff", 3 );
    RefreshPayloadHash( subject );

    EXPECT_TRUE( sgns::DecodeBridgeEventConsensusSubject( subject ).has_error() );
}

TEST( BridgeConsensusAdapterTest, RejectsSubjectTypeHashMismatch )
{
    auto subject_result = sgns::CreateBridgeEventConsensusSubject( kAccountId, MakeClaim() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    auto other_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( "gnus.other_bridge_event.v1" );
    ASSERT_TRUE( other_hash.has_value() );
    subject.mutable_subject_type_hash()->set_hash( other_hash.value().data(), other_hash.value().size() );

    EXPECT_TRUE( sgns::DecodeBridgeEventConsensusSubject( subject ).has_error() );
}

TEST( BridgeConsensusAdapterTest, RejectsPayloadHashMismatch )
{
    auto subject_result = sgns::CreateBridgeEventConsensusSubject( kAccountId, MakeClaim() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    subject.set_payload_hash( "bad" );

    EXPECT_TRUE( sgns::DecodeBridgeEventConsensusSubject( subject ).has_error() );
}

TEST( BridgeConsensusAdapterTest, DispatchesDecodedClaimToHandler )
{
    const auto claim = MakeClaim();
    auto       subject_result = sgns::CreateBridgeEventConsensusSubject( kAccountId, claim );
    ASSERT_TRUE( subject_result.has_value() );

    bool handler_called = false;
    auto handler = sgns::MakeBridgeEventConsensusHandler(
        [&handler_called, &claim](
            const eth::BridgeEventClaim       &decoded,
            const sgns::ConsensusManager::Subject &subject ) -> outcome::result<sgns::ConsensusManager::Check>
        {
            handler_called = true;
            EXPECT_EQ( subject.account_id(), kAccountId );
            EXPECT_EQ( decoded.tx_hash, claim.tx_hash );
            EXPECT_EQ( decoded.amount, claim.amount );
            return sgns::ConsensusManager::Check::Approve;
        } );

    auto result = handler( subject_result.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), sgns::ConsensusManager::Check::Approve );
    EXPECT_TRUE( handler_called );
}
