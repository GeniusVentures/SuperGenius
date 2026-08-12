#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "securecrdt/SecureCrdtCandidate.hpp"

namespace
{
    using namespace sgns::securecrdt;

    const std::string HASH_A( 64, 'a' );
    const std::string HASH_B( 64, 'b' );
    const std::string SIGNER_A( 128, 'c' );
    const std::string SIGNER_B( 128, 'd' );

    CandidateCore MakeCore()
    {
        CandidateCore core;
        core.domain                    = "trusted-peer";
        core.network_id                = 42;
        core.kind                      = CandidateKind::TrustPolicy;
        core.version                   = 7;
        core.expected_previous_hash    = HASH_A;
        core.authorizing_policy_hash   = HASH_B;
        core.payload                   = { 'p', 'o', 'l', 'i', 'c', 'y' };
        return core;
    }

    CandidateApprovalRecord MakeApproval( CandidateCore core = MakeCore(), std::string signer = SIGNER_A )
    {
        return CandidateApprovalRecord{ CandidateApprovalRecord::ENCODING_VERSION,
                                        std::move( core ),
                                        std::move( signer ),
                                        { 0x01, 0x02, 0x03 } };
    }
} // namespace

TEST( SecureCrdtCandidateCodecTest, CanonicalCoreRoundTripsAndBindsEveryContextField )
{
    const auto core  = MakeCore();
    const auto bytes = core.CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );
    EXPECT_EQ( CandidateCore::DecodeCanonical( *bytes ), core );

    const auto hash = core.Hash();
    ASSERT_TRUE( hash.has_value() );
    EXPECT_EQ( hash->size(), 64U );

    auto mutated = core;
    mutated.network_id++;
    EXPECT_NE( mutated.Hash(), hash );
    mutated = core;
    mutated.kind = CandidateKind::BurnConfig;
    EXPECT_NE( mutated.Hash(), hash );
    mutated = core;
    mutated.expected_previous_hash = HASH_B;
    EXPECT_NE( mutated.Hash(), hash );
    mutated = core;
    mutated.authorizing_policy_hash = HASH_A;
    EXPECT_NE( mutated.Hash(), hash );
    mutated = core;
    mutated.payload.push_back( 0xff );
    EXPECT_NE( mutated.Hash(), hash );
}

TEST( SecureCrdtCandidateCodecTest, RejectsUnknownVersionKindTrailingBytesAndNonCanonicalContext )
{
    const auto bytes = MakeCore().CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );

    auto unknown_encoding = *bytes;
    unknown_encoding[CandidateCore::RECORD_DOMAIN.size()] = 2;
    EXPECT_FALSE( CandidateCore::DecodeCanonical( unknown_encoding ).has_value() );

    auto unknown_kind = *bytes;
    const auto kind = std::find( unknown_kind.begin(), unknown_kind.end(),
                                 static_cast<uint8_t>( CandidateKind::TrustPolicy ) );
    ASSERT_NE( kind, unknown_kind.end() );
    *kind = 0xff;
    EXPECT_FALSE( CandidateCore::DecodeCanonical( unknown_kind ).has_value() );

    auto trailing = *bytes;
    trailing.push_back( 0 );
    EXPECT_FALSE( CandidateCore::DecodeCanonical( trailing ).has_value() );

    auto malformed = MakeCore();
    malformed.domain = "trusted-peer/candidate";
    EXPECT_FALSE( malformed.CanonicalBytes().has_value() );
    malformed = MakeCore();
    malformed.expected_previous_hash[0] = 'A';
    EXPECT_FALSE( malformed.CanonicalBytes().has_value() );
}

TEST( SecureCrdtCandidateKeyTest, ExactPathRoundTripsAndRejectsPathConfusion )
{
    const auto id = CandidateId::FromCore( MakeCore() );
    ASSERT_TRUE( id.has_value() );
    const CandidateKey key{ *id, SIGNER_A };
    const auto canonical = key.ToHierarchicalKey();
    EXPECT_EQ( canonical.GetKey(),
               "/trusted-peer/candidate/v7/" + id->content_hash + "/approval/" + SIGNER_A );
    const auto parsed = CandidateKey::Parse( canonical );
    ASSERT_TRUE( parsed.has_value() );
    EXPECT_EQ( parsed->id, *id );
    EXPECT_EQ( parsed->signer, SIGNER_A );

    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey(
                      "/trusted-peer/candidate/v7/" + id->content_hash + "/approval" ) ).has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey(
                      "/trusted-peer/candidate/v7/" + id->content_hash + "/approval/" + SIGNER_A + "/extra" ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey(
                      "/trusted-peer/candidate/7/" + id->content_hash + "/approval/" + SIGNER_A ) ).has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey(
                      "/trusted-peer/candidate/v7/" + std::string( 64, 'A' ) + "/approval/" + SIGNER_A ) )
                      .has_value() );
    EXPECT_FALSE( CandidateKey::Parse( sgns::crdt::HierarchicalKey(
                      "/trusted-peer/candidate/v7/" + id->content_hash + "/approval/" +
                      std::string( 128, 'C' ) ) ).has_value() );
}

TEST( SecureCrdtCandidateKeyTest, ApprovalRecordRejectsCandidateAndSignerMismatchBeforeVerification )
{
    const auto record = MakeApproval();
    const auto bytes  = record.CanonicalBytes();
    const auto id     = CandidateId::FromCore( record.core );
    ASSERT_TRUE( bytes.has_value() );
    ASSERT_TRUE( id.has_value() );

    EXPECT_TRUE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id, SIGNER_A } ).has_value() );

    auto candidate_b = record.core;
    candidate_b.payload.push_back( 'b' );
    const auto id_b = CandidateId::FromCore( candidate_b );
    ASSERT_TRUE( id_b.has_value() );
    EXPECT_FALSE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id_b, SIGNER_A } ).has_value() );
    EXPECT_FALSE( CandidateApprovalRecord::DecodeCanonical( *bytes, CandidateKey{ *id, SIGNER_B } ).has_value() );
}

TEST( SecureCrdtCandidateBoundsTest, AcceptsExactlyMaximumCandidateBytesAndRejectsOneMore )
{
    auto core = MakeCore();
    core.payload.clear();
    const auto empty = core.CanonicalBytes();
    ASSERT_TRUE( empty.has_value() );
    ASSERT_LT( empty->size(), CandidateLimits::MAX_CANDIDATE_BYTES );

    core.payload.resize( CandidateLimits::MAX_CANDIDATE_BYTES - empty->size() );
    const auto exact = core.CanonicalBytes();
    ASSERT_TRUE( exact.has_value() );
    ASSERT_EQ( exact->size(), CandidateLimits::MAX_CANDIDATE_BYTES );
    EXPECT_TRUE( CandidateCore::DecodeCanonical( *exact ).has_value() );

    core.payload.push_back( 0 );
    EXPECT_FALSE( core.CanonicalBytes().has_value() );
    auto oversized = *exact;
    oversized.push_back( 0 );
    EXPECT_FALSE( CandidateCore::DecodeCanonical( oversized ).has_value() );
}

TEST( SecureCrdtCandidateBoundsTest, CandidateAndApprovalCountCapsAreInclusive )
{
    EXPECT_TRUE( CandidateLimits::CandidateCountAllowed( 31 ) );
    EXPECT_TRUE( CandidateLimits::CandidateCountAllowed( 32 ) );
    EXPECT_FALSE( CandidateLimits::CandidateCountAllowed( 33 ) );

    EXPECT_TRUE( CandidateLimits::ApprovalCountAllowed( 255 ) );
    EXPECT_TRUE( CandidateLimits::ApprovalCountAllowed( 256 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalCountAllowed( 257 ) );
}

TEST( SecureCrdtCandidateBoundsTest, ApprovalByteAccountingIsOverflowSafeAt64MiBBoundary )
{
    constexpr size_t cap = CandidateLimits::MAX_ACTIVE_APPROVAL_BYTES_PER_PREDECESSOR;
    EXPECT_TRUE( CandidateLimits::ApprovalBytesAllowed( cap - 1, 1 ) );
    EXPECT_TRUE( CandidateLimits::ApprovalBytesAllowed( cap, 0 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalBytesAllowed( cap, 1 ) );
    EXPECT_FALSE( CandidateLimits::ApprovalBytesAllowed( std::numeric_limits<size_t>::max(), 1 ) );
}
