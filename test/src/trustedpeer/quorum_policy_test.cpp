#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "account/GeniusSigner.hpp"
#include "multisig/MultiSig.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"
#include "trustedpeer/CanonicalTrustCodec.hpp"
#include "trustedpeer/QuorumPolicy.hpp"

namespace
{
    using sgns::trustedpeer::CanonicalTrustCodec;
    using sgns::trustedpeer::QuorumPolicyState;

    const std::string PEER_A( 128, 'a' );
    const std::string PEER_B( 128, 'b' );
    const std::string PEER_C( 128, 'c' );
    const std::string CURRENT_HASH( 64, '1' );

    QuorumPolicyState MakePolicy( std::vector<std::string> peers = { PEER_B, PEER_A } )
    {
        QuorumPolicyState policy;
        policy.network_id                = 42;
        policy.version                   = 2;
        policy.expected_previous_hash    = CURRENT_HASH;
        policy.authorizing_policy_hash   = CURRENT_HASH;
        policy.peers                     = std::move( peers );
        policy.membership_threshold      = 2;
        policy.burn_threshold            = 2;
        return policy;
    }

    std::vector<std::string> MakePeers( size_t count )
    {
        std::vector<std::string> peers;
        peers.reserve( count );
        for ( size_t index = 0; index < count; ++index )
        {
            std::string key( 128, '0' );
            key[124] = "0123456789abcdef"[( index >> 12U ) & 0x0fU];
            key[125] = "0123456789abcdef"[( index >> 8U ) & 0x0fU];
            key[126] = "0123456789abcdef"[( index >> 4U ) & 0x0fU];
            key[127] = "0123456789abcdef"[index & 0x0fU];
            peers.push_back( std::move( key ) );
        }
        return peers;
    }
} // namespace

TEST( QuorumPolicyTest, ExactMembershipAndBurnFloorsMatchLockedBoundaryVectors )
{
    const std::vector<std::pair<size_t, uint64_t>> membership = {
        { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 2 }, { 4, 3 }, { 100, 51 }, { 101, 51 },
    };
    const std::vector<std::pair<size_t, uint64_t>> burn = {
        { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 2 }, { 4, 3 }, { 100, 67 }, { 101, 68 },
    };

    for ( const auto &[signer_count, expected] : membership )
    {
        EXPECT_EQ( sgns::securecrdt::MembershipQuorumFloor( signer_count ), expected ) << signer_count;
    }
    for ( const auto &[signer_count, expected] : burn )
    {
        EXPECT_EQ( sgns::securecrdt::BurnQuorumFloor( signer_count ), expected ) << signer_count;
    }
}

TEST( QuorumPolicyTest, ValidatorsRejectEmptyZeroOversizedAndBelowFloorThresholds )
{
    EXPECT_TRUE( sgns::securecrdt::ValidateMembershipQuorumThreshold( 1, 0 ).has_error() );
    EXPECT_TRUE( sgns::securecrdt::ValidateBurnQuorumThreshold( 1, 0 ).has_error() );

    for ( const size_t signer_count : { 1U, 2U, 3U, 4U, 100U, 101U } )
    {
        const auto membership_floor = sgns::securecrdt::MembershipQuorumFloor( signer_count );
        const auto burn_floor       = sgns::securecrdt::BurnQuorumFloor( signer_count );

        EXPECT_TRUE( sgns::securecrdt::ValidateMembershipQuorumThreshold( 0, signer_count ).has_error() );
        EXPECT_TRUE( sgns::securecrdt::ValidateBurnQuorumThreshold( 0, signer_count ).has_error() );
        EXPECT_TRUE(
            sgns::securecrdt::ValidateMembershipQuorumThreshold( signer_count + 1, signer_count ).has_error() );
        EXPECT_TRUE( sgns::securecrdt::ValidateBurnQuorumThreshold( signer_count + 1, signer_count ).has_error() );
        EXPECT_TRUE(
            sgns::securecrdt::ValidateMembershipQuorumThreshold( membership_floor, signer_count ).has_value() );
        EXPECT_TRUE( sgns::securecrdt::ValidateBurnQuorumThreshold( burn_floor, signer_count ).has_value() );
        if ( membership_floor > 1 )
        {
            EXPECT_TRUE( sgns::securecrdt::ValidateMembershipQuorumThreshold( membership_floor - 1, signer_count )
                             .has_error() );
        }
        if ( burn_floor > 1 )
        {
            EXPECT_TRUE(
                sgns::securecrdt::ValidateBurnQuorumThreshold( burn_floor - 1, signer_count ).has_error() );
        }
    }
}

TEST( QuorumPolicyTest, PolicyValidationNormalizesOrderAndRejectsInvalidSignerSets )
{
    const auto canonical = MakePolicy().Canonicalized();
    ASSERT_TRUE( canonical.has_value() );
    EXPECT_EQ( canonical->peers, ( std::vector<std::string>{ PEER_A, PEER_B } ) );
    EXPECT_TRUE( sgns::trustedpeer::ValidateQuorumPolicy( *canonical ) );

    EXPECT_FALSE( MakePolicy( {} ).Canonicalized().has_value() );
    EXPECT_FALSE( MakePolicy( { PEER_A, std::string( 128, 'A' ) } ).Canonicalized().has_value() );
    EXPECT_FALSE( MakePolicy( { std::string( 127, 'a' ) } ).Canonicalized().has_value() );
    EXPECT_FALSE( MakePolicy( { std::string( 128, 'g' ) } ).Canonicalized().has_value() );
    EXPECT_FALSE( MakePolicy( MakePeers( CanonicalTrustCodec::MAX_TRUSTED_PEERS + 1 ) ).Canonicalized().has_value() );

    auto invalid_hash                       = MakePolicy();
    invalid_hash.expected_previous_hash     = std::string( 63, '1' );
    EXPECT_FALSE( invalid_hash.Canonicalized().has_value() );
    invalid_hash                            = MakePolicy();
    invalid_hash.authorizing_policy_hash[0] = 'A';
    EXPECT_FALSE( invalid_hash.Canonicalized().has_value() );
}

TEST( QuorumPolicyTest, PolicyThresholdsEnforceBoundsAndBothExactFloors )
{
    for ( const size_t signer_count : { 1U, 2U, 3U, 4U, 100U, 101U } )
    {
        auto policy                 = MakePolicy( MakePeers( signer_count ) );
        policy.membership_threshold = sgns::securecrdt::MembershipQuorumFloor( signer_count );
        policy.burn_threshold       = sgns::securecrdt::BurnQuorumFloor( signer_count );
        EXPECT_TRUE( policy.Canonicalized().has_value() ) << signer_count;

        policy.membership_threshold = 0;
        EXPECT_FALSE( policy.Canonicalized().has_value() ) << signer_count;
        policy.membership_threshold = signer_count + 1;
        EXPECT_FALSE( policy.Canonicalized().has_value() ) << signer_count;

        policy.membership_threshold = sgns::securecrdt::MembershipQuorumFloor( signer_count );
        policy.burn_threshold       = signer_count + 1;
        EXPECT_FALSE( policy.Canonicalized().has_value() ) << signer_count;
    }
}

TEST( QuorumPolicyTest, CanonicalBytesBindAllFieldsAndDecoderRejectsMalformedOrdering )
{
    const auto canonical = MakePolicy().Canonicalized();
    ASSERT_TRUE( canonical.has_value() );
    const auto bytes = canonical->CanonicalBytes();
    const auto hash  = canonical->Hash();
    ASSERT_TRUE( bytes.has_value() );
    ASSERT_TRUE( hash.has_value() );
    EXPECT_EQ( *hash, CanonicalTrustCodec::Sha256Hex( *bytes ) );

    const auto decoded = QuorumPolicyState::DecodeCanonical( *bytes );
    ASSERT_TRUE( decoded.has_value() );
    EXPECT_EQ( *decoded, *canonical );

    auto trailing = *bytes;
    trailing.push_back( 0 );
    EXPECT_FALSE( QuorumPolicyState::DecodeCanonical( trailing ).has_value() );
    auto truncated = *bytes;
    truncated.pop_back();
    EXPECT_FALSE( QuorumPolicyState::DecodeCanonical( truncated ).has_value() );

    constexpr size_t HASH_BYTES = 32;
    const size_t first_peer = QuorumPolicyState::POLICY_DOMAIN.size() + 1 + 2 + 8 + 4 + HASH_BYTES + 4 + HASH_BYTES + 4 + 4;
    const size_t second_peer = first_peer + CanonicalTrustCodec::PUBLIC_KEY_BYTES + 4;
    auto duplicate = *bytes;
    std::copy_n( duplicate.begin() + static_cast<ptrdiff_t>( first_peer ),
                 CanonicalTrustCodec::PUBLIC_KEY_BYTES,
                 duplicate.begin() + static_cast<ptrdiff_t>( second_peer ) );
    EXPECT_FALSE( QuorumPolicyState::DecodeCanonical( duplicate ).has_value() );
    auto reversed = *bytes;
    std::swap_ranges( reversed.begin() + static_cast<ptrdiff_t>( first_peer ),
                      reversed.begin() + static_cast<ptrdiff_t>( first_peer + CanonicalTrustCodec::PUBLIC_KEY_BYTES ),
                      reversed.begin() + static_cast<ptrdiff_t>( second_peer ) );
    EXPECT_FALSE( QuorumPolicyState::DecodeCanonical( reversed ).has_value() );
}

TEST( QuorumPolicyTest, SuccessorRequiresExactVersionPredecessorAndCurrentAuthorizerHash )
{
    auto current                         = MakePolicy();
    current.version                     = 7;
    current.expected_previous_hash      = std::string( 64, '0' );
    current.authorizing_policy_hash     = std::string( 64, '0' );
    const auto current_hash             = current.Hash();
    ASSERT_TRUE( current_hash.has_value() );

    auto candidate                      = MakePolicy( { PEER_A, PEER_B, PEER_C } );
    candidate.version                   = 8;
    candidate.expected_previous_hash    = *current_hash;
    candidate.authorizing_policy_hash   = *current_hash;
    EXPECT_TRUE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );

    candidate.version = 9;
    EXPECT_FALSE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );
    candidate.version                = 8;
    candidate.expected_previous_hash = std::string( 64, '2' );
    EXPECT_FALSE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );
    candidate.expected_previous_hash  = *current_hash;
    candidate.authorizing_policy_hash = std::string( 64, '3' );
    EXPECT_FALSE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );
    candidate.authorizing_policy_hash = *current_hash;
    candidate.network_id += 1;
    EXPECT_FALSE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );
}

TEST( QuorumPolicyTest, NewlyProposedPeersCannotAuthorizeThePolicyThatAddsThem )
{
    const auto current_a = sgns::GeniusSigner::Generate();
    const auto current_b = sgns::GeniusSigner::Generate();
    const auto proposed_a = sgns::GeniusSigner::Generate();
    const auto proposed_b = sgns::GeniusSigner::Generate();

    auto current                     = MakePolicy( { current_a.GetAddress(), current_b.GetAddress() } );
    current.version                 = 4;
    current.expected_previous_hash  = std::string( 64, '0' );
    current.authorizing_policy_hash = std::string( 64, '0' );
    const auto current_hash         = current.Hash();
    ASSERT_TRUE( current_hash.has_value() );

    auto candidate = MakePolicy( { current_a.GetAddress(), current_b.GetAddress(), proposed_a.GetAddress(),
                                   proposed_b.GetAddress() } );
    candidate.version                   = 5;
    candidate.expected_previous_hash    = *current_hash;
    candidate.authorizing_policy_hash   = *current_hash;
    candidate.membership_threshold      = 3;
    candidate.burn_threshold            = 3;
    const auto candidate_bytes          = candidate.CanonicalBytes();
    ASSERT_TRUE( candidate_bytes.has_value() );
    ASSERT_TRUE( sgns::trustedpeer::ValidatePolicySuccessor( current, candidate ) );

    const sgns::multisig::CollectedSignatures new_peer_signatures = {
        { proposed_a.GetAddress(), proposed_a.Sign( *candidate_bytes ) },
        { proposed_b.GetAddress(), proposed_b.Sign( *candidate_bytes ) },
    };
    const sgns::multisig::MultiSig current_authorizer( current.peers, current.membership_threshold );
    const sgns::multisig::MultiSig proposed_authorizer( candidate.peers, candidate.membership_threshold );
    EXPECT_FALSE( current_authorizer.EvaluateQuorum( new_peer_signatures, *candidate_bytes ).has_quorum );
    EXPECT_FALSE( proposed_authorizer.EvaluateQuorum( new_peer_signatures, *candidate_bytes ).has_quorum );

    auto candidate_with_unsafe_self_threshold = candidate;
    candidate_with_unsafe_self_threshold.membership_threshold = 2;
    const auto unsafe_bytes = candidate_with_unsafe_self_threshold.CanonicalBytes();
    ASSERT_TRUE( unsafe_bytes.has_value() );
    const sgns::multisig::MultiSig unsafe_proposed_authorizer(
        candidate_with_unsafe_self_threshold.peers, candidate_with_unsafe_self_threshold.membership_threshold );
    const sgns::multisig::CollectedSignatures unsafe_new_peer_signatures = {
        { proposed_a.GetAddress(), proposed_a.Sign( *unsafe_bytes ) },
        { proposed_b.GetAddress(), proposed_b.Sign( *unsafe_bytes ) },
    };
    EXPECT_TRUE( unsafe_proposed_authorizer.EvaluateQuorum( unsafe_new_peer_signatures, *unsafe_bytes ).has_quorum );
    EXPECT_FALSE( current_authorizer.EvaluateQuorum( unsafe_new_peer_signatures, *unsafe_bytes ).has_quorum );
}
