#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gsl/span>

#include "base/hexutil.hpp"
#include "trustedpeer/CanonicalTrustCodec.hpp"
#include "trustedpeer/GenesisManifest.hpp"

namespace
{
    using sgns::base::hex_lower;
    using sgns::base::unhex;
    using sgns::trustedpeer::CanonicalTrustCodec;
    using sgns::trustedpeer::GenesisManifest;

    const std::string PEER_A( 128, 'a' );
    const std::string PEER_B( 128, 'b' );
    const std::string BOOTSTRAPPER( 128, 'c' );
    constexpr char    GOLDEN_FINGERPRINT[] = "a43ea4b21877879fa156645d776c18a6e338ff5020caf897947cbe0e421e2270";
    // Full canonical bytes of MakeManifest() beside the derived fingerprint: the bytes
    // are the single source of truth every downstream layer verifies against.
    constexpr char GOLDEN_CANONICAL_BYTES_HEX[] =
        "53474e535f54525553545f47454e455349535f563101002a00000040cccccccccccccccccccccc"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
        "cccccccccccccccccccccccccccc00000000000000010000000200000040aaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa00000040bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "bbbbbbbbbbbb000000000000000200000000000000020000000000000064";

    GenesisManifest MakeManifest( std::vector<std::string> peers = { PEER_B, PEER_A } )
    {
        GenesisManifest manifest;
        manifest.network_id                = 42;
        manifest.bootstrapper_public_key   = BOOTSTRAPPER;
        manifest.policy_version            = 1;
        manifest.peers                     = std::move( peers );
        manifest.membership_threshold      = 2;
        manifest.burn_threshold            = 2;
        manifest.initial_burn_basis_points = 100;
        return manifest;
    }

    std::string HashBytes( const std::vector<uint8_t> &bytes )
    {
        return CanonicalTrustCodec::Sha256Hex( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
    }
} // namespace

TEST( GenesisManifestTest, PeerPermutationsNormalizeToOneGoldenFingerprint )
{
    const auto forward  = MakeManifest( { PEER_A, PEER_B } );
    const auto reversed = MakeManifest( { PEER_B, PEER_A } );

    const auto forward_canonical  = forward.Canonicalized();
    const auto reversed_canonical = reversed.Canonicalized();
    ASSERT_TRUE( forward_canonical.has_value() );
    ASSERT_TRUE( reversed_canonical.has_value() );
    EXPECT_EQ( forward_canonical->peers, ( std::vector<std::string>{ PEER_A, PEER_B } ) );
    EXPECT_EQ( reversed_canonical->peers, forward_canonical->peers );

    const auto forward_fingerprint  = forward.Fingerprint();
    const auto reversed_fingerprint = reversed.Fingerprint();
    ASSERT_TRUE( forward_fingerprint.has_value() );
    ASSERT_TRUE( reversed_fingerprint.has_value() );
    EXPECT_EQ( *forward_fingerprint, GOLDEN_FINGERPRINT );
    EXPECT_EQ( *reversed_fingerprint, GOLDEN_FINGERPRINT );
}

TEST( GenesisManifestTest, CanonicalBytesNormalizeToGoldenHexPinAcrossConstructions )
{
    // Byte-level analog of the fingerprint permutation test above: both input orders
    // must produce the pinned bytes, not merely an equal fingerprint.
    const auto forward  = MakeManifest( { PEER_A, PEER_B } ).Canonicalized();
    const auto reversed = MakeManifest( { PEER_B, PEER_A } ).Canonicalized();
    ASSERT_TRUE( forward.has_value() );
    ASSERT_TRUE( reversed.has_value() );

    const auto forward_bytes  = forward->CanonicalBytes();
    const auto reversed_bytes = reversed->CanonicalBytes();
    ASSERT_TRUE( forward_bytes.has_value() );
    ASSERT_TRUE( reversed_bytes.has_value() );

    const auto to_hex = []( const std::vector<uint8_t> &bytes )
    { return hex_lower( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) ); };
    EXPECT_EQ( to_hex( *forward_bytes ), GOLDEN_CANONICAL_BYTES_HEX );
    EXPECT_EQ( to_hex( *reversed_bytes ), GOLDEN_CANONICAL_BYTES_HEX );
}

TEST( GenesisManifestTest, GoldenCanonicalBytesPinDecodesBackToTheManifest )
{
    // The pinned bytes are the source of truth: they must decode into exactly the
    // canonical manifest, proving the fingerprint is derived, never the reverse.
    const auto pinned_bytes = unhex( GOLDEN_CANONICAL_BYTES_HEX );
    ASSERT_TRUE( pinned_bytes.has_value() ) << pinned_bytes.error().message();

    const auto decoded = GenesisManifest::DecodeCanonical( pinned_bytes.value() );
    ASSERT_TRUE( decoded.has_value() );

    const auto canonical = MakeManifest().Canonicalized();
    ASSERT_TRUE( canonical.has_value() );
    EXPECT_EQ( *decoded, *canonical );
}

TEST( GenesisManifestTest, IndependentConstructionsProduceByteIdenticalCanonicalBytes )
{
    // Replay proof: two independent construction->canonicalize->encode cycles over the
    // same inputs must agree on the bytes themselves, not only on their common hash.
    const auto first  = MakeManifest( { PEER_A, PEER_B } ).Canonicalized();
    const auto second = MakeManifest( { PEER_A, PEER_B } ).Canonicalized();
    ASSERT_TRUE( first.has_value() );
    ASSERT_TRUE( second.has_value() );

    const auto first_bytes  = first->CanonicalBytes();
    const auto second_bytes = second->CanonicalBytes();
    ASSERT_TRUE( first_bytes.has_value() );
    ASSERT_TRUE( second_bytes.has_value() );

    EXPECT_EQ( *first_bytes, *second_bytes );
}

TEST( GenesisManifestTest, NormalizesUppercasePeersAndRejectsDuplicatesAfterNormalization )
{
    const std::string uppercase_peer( 128, 'A' );
    const auto        canonical = MakeManifest( { uppercase_peer, PEER_B } ).Canonicalized();
    ASSERT_TRUE( canonical.has_value() );
    EXPECT_EQ( canonical->peers, ( std::vector<std::string>{ PEER_A, PEER_B } ) );

    EXPECT_FALSE( MakeManifest( { PEER_A, uppercase_peer } ).Canonicalized().has_value() );
}

TEST( GenesisManifestTest, RejectsEmptyMalformedAndOverCapPeerSets )
{
    EXPECT_FALSE( MakeManifest( {} ).Canonicalized().has_value() );
    EXPECT_FALSE( MakeManifest( { std::string( 127, 'a' ) } ).Canonicalized().has_value() );
    EXPECT_FALSE( MakeManifest( { std::string( 128, 'g' ) } ).Canonicalized().has_value() );

    std::vector<std::string> too_many;
    too_many.reserve( CanonicalTrustCodec::MAX_TRUSTED_PEERS + 1 );
    for ( size_t index = 0; index <= CanonicalTrustCodec::MAX_TRUSTED_PEERS; ++index )
    {
        std::string key( 128, '0' );
        key[126] = "0123456789abcdef"[( index / 16 ) % 16];
        key[127] = "0123456789abcdef"[index % 16];
        too_many.push_back( std::move( key ) );
    }
    EXPECT_FALSE( MakeManifest( std::move( too_many ) ).Canonicalized().has_value() );
}

TEST( GenesisManifestTest, RejectsMalformedBootstrapperAndInvalidInitialPolicy )
{
    auto manifest                    = MakeManifest();
    manifest.bootstrapper_public_key = std::string( 127, 'c' );
    EXPECT_FALSE( manifest.Canonicalized().has_value() );

    manifest                  = MakeManifest();
    manifest.encoding_version = 2;
    EXPECT_FALSE( manifest.Canonicalized().has_value() );

    manifest                = MakeManifest();
    manifest.policy_version = 0;
    EXPECT_FALSE( manifest.Canonicalized().has_value() );

    manifest                      = MakeManifest();
    manifest.membership_threshold = 0;
    EXPECT_FALSE( manifest.Canonicalized().has_value() );

    manifest                = MakeManifest();
    manifest.burn_threshold = 3;
    EXPECT_FALSE( manifest.Canonicalized().has_value() );

    manifest                           = MakeManifest();
    manifest.initial_burn_basis_points = 101;
    EXPECT_FALSE( manifest.Canonicalized().has_value() );
}

TEST( GenesisManifestTest, CanonicalDecoderRoundTripsAndConsumesTheWholeInput )
{
    const auto canonical = MakeManifest().Canonicalized();
    ASSERT_TRUE( canonical.has_value() );
    const auto bytes = canonical->CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );

    const auto decoded = GenesisManifest::DecodeCanonical( *bytes );
    ASSERT_TRUE( decoded.has_value() );
    EXPECT_EQ( *decoded, *canonical );

    auto trailing = *bytes;
    trailing.push_back( 0 );
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( trailing ).has_value() );

    auto truncated = *bytes;
    truncated.pop_back();
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( truncated ).has_value() );
}

TEST( GenesisManifestTest, DecoderRejectsUnknownVersionOverflowDuplicateAndNonCanonicalOrder )
{
    const auto bytes_result = MakeManifest().CanonicalBytes();
    ASSERT_TRUE( bytes_result.has_value() );

    auto unknown_version                                        = *bytes_result;
    unknown_version[CanonicalTrustCodec::GENESIS_DOMAIN.size()] = 2;
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( unknown_version ).has_value() );

    auto             length_overflow         = *bytes_result;
    constexpr size_t bootstrap_length_offset = 24;
    std::fill_n( length_overflow.begin() + bootstrap_length_offset, 4, UINT8_MAX );
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( length_overflow ).has_value() );

    constexpr size_t first_peer_offset  = 108;
    constexpr size_t second_peer_offset = 176;
    auto             duplicate          = *bytes_result;
    std::copy_n( duplicate.begin() + first_peer_offset,
                 CanonicalTrustCodec::PUBLIC_KEY_BYTES,
                 duplicate.begin() + second_peer_offset );
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( duplicate ).has_value() );

    auto reversed = *bytes_result;
    std::swap_ranges( reversed.begin() + first_peer_offset,
                      reversed.begin() + first_peer_offset + CanonicalTrustCodec::PUBLIC_KEY_BYTES,
                      reversed.begin() + second_peer_offset );
    EXPECT_FALSE( GenesisManifest::DecodeCanonical( reversed ).has_value() );
}

TEST( GenesisManifestTest, FingerprintBindsEveryReviewedFieldAndRejectsTampering )
{
    const auto bytes_result = MakeManifest().CanonicalBytes();
    const auto fingerprint  = MakeManifest().Fingerprint();
    ASSERT_TRUE( bytes_result.has_value() );
    ASSERT_TRUE( fingerprint.has_value() );

    const std::vector<size_t> field_offsets = {
        23,
        28,
        99,
        108,
        247,
        255,
        263,
    };
    for ( const size_t offset : field_offsets )
    {
        auto tampered     = *bytes_result;
        tampered[offset] ^= 0x01;
        EXPECT_NE( HashBytes( tampered ), *fingerprint ) << "field offset " << offset;
        EXPECT_FALSE( GenesisManifest::DecodeAndVerify( tampered, *fingerprint ).has_value() )
            << "field offset " << offset;
    }
}

TEST( GenesisManifestTest, VerifyRequiresCanonicalLowercaseFingerprint )
{
    const auto bytes = MakeManifest().CanonicalBytes();
    ASSERT_TRUE( bytes.has_value() );
    EXPECT_TRUE( GenesisManifest::DecodeAndVerify( *bytes, GOLDEN_FINGERPRINT ).has_value() );
    EXPECT_FALSE( GenesisManifest::DecodeAndVerify( *bytes, std::string( 64, 'A' ) ).has_value() );
    EXPECT_FALSE( GenesisManifest::DecodeAndVerify( *bytes, std::string( 63, 'a' ) ).has_value() );
}
