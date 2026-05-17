#include <gtest/gtest.h>

#define private public
#include "blockchain/Consensus.hpp"
#undef private

namespace
{
    constexpr const char *kAccountId = "gnus-test-account";
    constexpr const char *kBridgeSubjectType = "gnus.bridge_event.v1";

    std::vector<uint8_t> BridgePayload()
    {
        return std::vector<uint8_t>{ 0x01, 0x02, 0x03 };
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
    EXPECT_EQ( subject.subject_type(), kBridgeSubjectType );
    ASSERT_TRUE( subject.has_generic() );
    EXPECT_EQ( subject.generic().payload(), std::string( payload.begin(), payload.end() ) );
    EXPECT_FALSE( subject.generic().payload_hash().empty() );
    EXPECT_FALSE( subject.subject_id().empty() );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( kBridgeSubjectType );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value() );

    const auto computed_id = sgns::ConsensusManager::ComputeSubjectId( subject );
    ASSERT_TRUE( computed_id.has_value() );
    EXPECT_EQ( subject.subject_id(), computed_id.value() );
    EXPECT_TRUE( sgns::ConsensusManager::ValidateSubject( subject ) );
    EXPECT_TRUE( sgns::ConsensusManager::CheckSubject( subject ) );
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
    ASSERT_TRUE( sgns::ConsensusManager::ValidateSubject( subject ) );
    subject.mutable_generic()->set_payload_hash( "bad" );

    const auto subject_id = sgns::ConsensusManager::ComputeSubjectId( subject );
    ASSERT_TRUE( subject_id.has_value() );
    subject.set_subject_id( subject_id.value() );

    EXPECT_FALSE( sgns::ConsensusManager::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManager::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, ValidateGenericSubjectRejectsTamperedSubjectTypeHash )
{
    const auto subject_result = sgns::ConsensusManager::CreateGenericSubject(
        kAccountId,
        kBridgeSubjectType,
        BridgePayload() );
    ASSERT_TRUE( subject_result.has_value() );

    auto subject = subject_result.value();
    ASSERT_TRUE( sgns::ConsensusManager::ValidateSubject( subject ) );
    subject.mutable_subject_type_hash()->set_hash( "bad" );

    const auto subject_id = sgns::ConsensusManager::ComputeSubjectId( subject );
    ASSERT_TRUE( subject_id.has_value() );
    subject.set_subject_id( subject_id.value() );

    EXPECT_FALSE( sgns::ConsensusManager::ValidateSubject( subject ) );
    EXPECT_FALSE( sgns::ConsensusManager::CheckSubject( subject ) );
}

TEST( ConsensusSubjectTest, CreatesBuiltInSubjectWithCanonicalStringType )
{
    const auto subject_result = sgns::ConsensusManager::CreateNonceSubject(
        kAccountId,
        7,
        "tx-hash",
        std::nullopt,
        std::nullopt );
    ASSERT_TRUE( subject_result.has_value() );

    const auto &subject = subject_result.value();
    ASSERT_TRUE( subject.has_nonce() );
    const auto subject_type = sgns::ConsensusManager::GetSubjectType( subject );
    ASSERT_TRUE( subject_type.has_value() );
    EXPECT_EQ( subject_type.value(), sgns::kNonceSubjectType );

    const auto type_hash = sgns::ConsensusManager::ComputeSubjectTypeHash( sgns::kNonceSubjectType );
    ASSERT_TRUE( type_hash.has_value() );
    EXPECT_EQ( subject.subject_type_hash().hash(), type_hash.value() );

    EXPECT_TRUE( sgns::ConsensusManager::ValidateSubject( subject ) );
    EXPECT_TRUE( sgns::ConsensusManager::CheckSubject( subject ) );
}
