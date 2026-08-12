#include "trustedpeer/GenesisManifest.hpp"

#include <algorithm>

#include <gsl/span>

#include "trustedpeer/CanonicalTrustCodec.hpp"

namespace sgns::trustedpeer
{
    std::optional<GenesisManifest> GenesisManifest::Canonicalized() const
    {
        if ( encoding_version != ENCODING_VERSION || policy_version != 1 ||
             initial_burn_basis_points != INITIAL_BURN_BASIS_POINTS )
        {
            return std::nullopt;
        }

        const auto bootstrapper     = CanonicalTrustCodec::DecodePublicKey( bootstrapper_public_key );
        const auto normalized_peers = CanonicalTrustCodec::NormalizePublicKeys( peers );
        if ( !bootstrapper || !normalized_peers )
        {
            return std::nullopt;
        }

        const uint64_t peer_count       = normalized_peers->size();
        const uint64_t membership_floor = peer_count / 2 + 1;
        const uint64_t burn_floor       = peer_count - peer_count / 3;
        if ( membership_threshold < membership_floor || membership_threshold > peer_count ||
             burn_threshold < burn_floor || burn_threshold > peer_count )
        {
            return std::nullopt;
        }

        GenesisManifest canonical         = *this;
        canonical.bootstrapper_public_key = CanonicalTrustCodec::EncodePublicKey(
            gsl::span<const uint8_t>( bootstrapper->data(), bootstrapper->size() ) );
        canonical.peers = *normalized_peers;
        return canonical;
    }

    std::optional<std::vector<uint8_t>> GenesisManifest::CanonicalBytes() const
    {
        const auto canonical = Canonicalized();
        if ( !canonical )
        {
            return std::nullopt;
        }

        const auto bootstrapper = CanonicalTrustCodec::DecodePublicKey( canonical->bootstrapper_public_key );
        if ( !bootstrapper )
        {
            return std::nullopt;
        }

        CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( CanonicalTrustCodec::GENESIS_DOMAIN );
        writer.WriteU8( canonical->encoding_version );
        writer.WriteU16( canonical->network_id );
        if ( !writer.WriteLengthPrefixedBytes(
                 gsl::span<const uint8_t>( bootstrapper->data(), bootstrapper->size() ) ) )
        {
            return std::nullopt;
        }
        writer.WriteU64( canonical->policy_version );
        writer.WriteU32( static_cast<uint32_t>( canonical->peers.size() ) );
        for ( const auto &peer : canonical->peers )
        {
            const auto decoded_peer = CanonicalTrustCodec::DecodePublicKey( peer );
            if ( !decoded_peer || !writer.WriteLengthPrefixedBytes(
                                      gsl::span<const uint8_t>( decoded_peer->data(), decoded_peer->size() ) ) )
            {
                return std::nullopt;
            }
        }
        writer.WriteU64( canonical->membership_threshold );
        writer.WriteU64( canonical->burn_threshold );
        writer.WriteU64( canonical->initial_burn_basis_points );
        return writer.Take();
    }

    std::optional<std::string> GenesisManifest::Fingerprint() const
    {
        const auto bytes = CanonicalBytes();
        if ( !bytes )
        {
            return std::nullopt;
        }
        return CanonicalTrustCodec::Sha256Hex( gsl::span<const uint8_t>( bytes->data(), bytes->size() ) );
    }

    std::optional<GenesisManifest> GenesisManifest::DecodeCanonical( const std::vector<uint8_t> &bytes )
    {
        CanonicalTrustCodec::Reader reader( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
        const auto                  domain = reader.ReadBytes( CanonicalTrustCodec::GENESIS_DOMAIN.size() );
        if ( !domain || !std::equal( domain->begin(), domain->end(), CanonicalTrustCodec::GENESIS_DOMAIN.begin() ) )
        {
            return std::nullopt;
        }

        const auto encoding     = reader.ReadU8();
        const auto network      = reader.ReadU16();
        const auto bootstrapper = reader.ReadLengthPrefixedBytes( CanonicalTrustCodec::PUBLIC_KEY_BYTES );
        const auto policy       = reader.ReadU64();
        const auto peer_count   = reader.ReadU32();
        if ( !encoding || *encoding != ENCODING_VERSION || !network || !bootstrapper ||
             bootstrapper->size() != CanonicalTrustCodec::PUBLIC_KEY_BYTES || !policy || !peer_count ||
             *peer_count == 0 || *peer_count > CanonicalTrustCodec::MAX_TRUSTED_PEERS )
        {
            return std::nullopt;
        }

        std::vector<std::string> decoded_peers;
        decoded_peers.reserve( *peer_count );
        for ( uint32_t index = 0; index < *peer_count; ++index )
        {
            const auto peer = reader.ReadLengthPrefixedBytes( CanonicalTrustCodec::PUBLIC_KEY_BYTES );
            if ( !peer || peer->size() != CanonicalTrustCodec::PUBLIC_KEY_BYTES )
            {
                return std::nullopt;
            }
            decoded_peers.push_back(
                CanonicalTrustCodec::EncodePublicKey( gsl::span<const uint8_t>( peer->data(), peer->size() ) ) );
        }

        const auto membership   = reader.ReadU64();
        const auto burn         = reader.ReadU64();
        const auto initial_burn = reader.ReadU64();
        if ( !membership || !burn || !initial_burn || !reader.Exhausted() )
        {
            return std::nullopt;
        }

        GenesisManifest manifest;
        manifest.encoding_version        = *encoding;
        manifest.network_id              = *network;
        manifest.bootstrapper_public_key = CanonicalTrustCodec::EncodePublicKey(
            gsl::span<const uint8_t>( bootstrapper->data(), bootstrapper->size() ) );
        manifest.policy_version            = *policy;
        manifest.peers                     = std::move( decoded_peers );
        manifest.membership_threshold      = *membership;
        manifest.burn_threshold            = *burn;
        manifest.initial_burn_basis_points = *initial_burn;

        const auto canonical = manifest.Canonicalized();
        if ( !canonical || canonical->peers != manifest.peers )
        {
            return std::nullopt;
        }
        const auto canonical_bytes = canonical->CanonicalBytes();
        if ( !canonical_bytes || *canonical_bytes != bytes )
        {
            return std::nullopt;
        }
        return canonical;
    }

    std::optional<GenesisManifest> GenesisManifest::DecodeAndVerify( const std::vector<uint8_t> &bytes,
                                                                     const std::string          &expected_fingerprint )
    {
        const bool canonical_fingerprint = expected_fingerprint.size() == 64 &&
                                           std::all_of( expected_fingerprint.begin(),
                                                        expected_fingerprint.end(),
                                                        []( char value )
                                                        {
                                                            return ( value >= '0' && value <= '9' ) ||
                                                                   ( value >= 'a' && value <= 'f' );
                                                        } );
        if ( !canonical_fingerprint )
        {
            return std::nullopt;
        }

        const auto manifest = DecodeCanonical( bytes );
        if ( !manifest || CanonicalTrustCodec::Sha256Hex( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) ) !=
                              expected_fingerprint )
        {
            return std::nullopt;
        }
        return manifest;
    }

    bool GenesisManifest::operator==( const GenesisManifest &other ) const
    {
        return encoding_version == other.encoding_version && network_id == other.network_id &&
               bootstrapper_public_key == other.bootstrapper_public_key && policy_version == other.policy_version &&
               peers == other.peers && membership_threshold == other.membership_threshold &&
               burn_threshold == other.burn_threshold && initial_burn_basis_points == other.initial_burn_basis_points;
    }
} // namespace sgns::trustedpeer
