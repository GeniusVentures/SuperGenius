#include "trustedpeer/QuorumPolicy.hpp"

#include <algorithm>
#include <limits>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"
#include "trustedpeer/CanonicalTrustCodec.hpp"

namespace sgns::trustedpeer
{
    namespace
    {
        constexpr size_t POLICY_HASH_BYTES = 32;

        bool IsCanonicalHash( const std::string &hash )
        {
            return hash.size() == POLICY_HASH_BYTES * 2 && sgns::base::IsLowerHex( hash );
        }

        std::optional<std::vector<uint8_t>> DecodeHash( const std::string &hash )
        {
            if ( !IsCanonicalHash( hash ) )
            {
                return std::nullopt;
            }
            auto decoded = sgns::base::unhex( hash );
            if ( decoded.has_error() || decoded.value().size() != POLICY_HASH_BYTES )
            {
                return std::nullopt;
            }
            return decoded.value();
        }
    } // namespace

    std::optional<QuorumPolicyState> QuorumPolicyState::Canonicalized() const
    {
        if ( encoding_version != ENCODING_VERSION || version == 0 || !IsCanonicalHash( expected_previous_hash ) ||
             !IsCanonicalHash( authorizing_policy_hash ) )
        {
            return std::nullopt;
        }

        const auto normalized_peers = CanonicalTrustCodec::NormalizePublicKeys( peers );
        if ( !normalized_peers ||
             sgns::securecrdt::ValidateMembershipQuorumThreshold( membership_threshold, normalized_peers->size() )
                 .has_error() ||
             sgns::securecrdt::ValidateBurnQuorumThreshold( burn_threshold, normalized_peers->size() ).has_error() )
        {
            return std::nullopt;
        }

        QuorumPolicyState canonical = *this;
        canonical.peers             = *normalized_peers;
        return canonical;
    }

    std::optional<std::vector<uint8_t>> QuorumPolicyState::CanonicalBytes() const
    {
        const auto canonical = Canonicalized();
        if ( !canonical )
        {
            return std::nullopt;
        }

        const auto previous_hash   = DecodeHash( canonical->expected_previous_hash );
        const auto authorizer_hash = DecodeHash( canonical->authorizing_policy_hash );
        if ( !previous_hash || !authorizer_hash )
        {
            return std::nullopt;
        }

        CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( POLICY_DOMAIN );
        writer.WriteU8( canonical->encoding_version );
        writer.WriteU16( canonical->network_id );
        writer.WriteU64( canonical->version );
        if ( !writer.WriteLengthPrefixedBytes( *previous_hash ) ||
             !writer.WriteLengthPrefixedBytes( *authorizer_hash ) )
        {
            return std::nullopt;
        }
        writer.WriteU32( static_cast<uint32_t>( canonical->peers.size() ) );
        for ( const auto &peer : canonical->peers )
        {
            const auto decoded_peer = CanonicalTrustCodec::DecodePublicKey( peer );
            if ( !decoded_peer || !writer.WriteLengthPrefixedBytes( *decoded_peer ) )
            {
                return std::nullopt;
            }
        }
        writer.WriteU64( canonical->membership_threshold );
        writer.WriteU64( canonical->burn_threshold );
        return writer.Take();
    }

    std::optional<std::string> QuorumPolicyState::Hash() const
    {
        const auto bytes = CanonicalBytes();
        if ( !bytes )
        {
            return std::nullopt;
        }
        return CanonicalTrustCodec::Sha256Hex( *bytes );
    }

    std::optional<QuorumPolicyState> QuorumPolicyState::DecodeCanonical( const std::vector<uint8_t> &bytes )
    {
        CanonicalTrustCodec::Reader reader( bytes );
        const auto                  domain = reader.ReadBytes( POLICY_DOMAIN.size() );
        if ( !domain || !std::equal( domain->begin(), domain->end(), POLICY_DOMAIN.begin() ) )
        {
            return std::nullopt;
        }

        const auto encoding        = reader.ReadU8();
        const auto network         = reader.ReadU16();
        const auto version_value   = reader.ReadU64();
        const auto previous_hash   = reader.ReadLengthPrefixedBytes( POLICY_HASH_BYTES );
        const auto authorizer_hash = reader.ReadLengthPrefixedBytes( POLICY_HASH_BYTES );
        const auto peer_count      = reader.ReadU32();
        if ( !encoding || *encoding != ENCODING_VERSION || !network || !version_value || *version_value == 0 ||
             !previous_hash || previous_hash->size() != POLICY_HASH_BYTES || !authorizer_hash ||
             authorizer_hash->size() != POLICY_HASH_BYTES || !peer_count || *peer_count == 0 ||
             *peer_count > CanonicalTrustCodec::MAX_TRUSTED_PEERS )
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
            decoded_peers.push_back( CanonicalTrustCodec::EncodePublicKey( *peer ) );
        }

        const auto membership = reader.ReadU64();
        const auto burn       = reader.ReadU64();
        if ( !membership || !burn || !reader.Exhausted() )
        {
            return std::nullopt;
        }

        QuorumPolicyState policy;
        policy.encoding_version        = *encoding;
        policy.network_id              = *network;
        policy.version                 = *version_value;
        policy.expected_previous_hash  = sgns::base::hex_lower( *previous_hash );
        policy.authorizing_policy_hash = sgns::base::hex_lower( *authorizer_hash );
        policy.peers                   = std::move( decoded_peers );
        policy.membership_threshold    = *membership;
        policy.burn_threshold          = *burn;

        const auto canonical = policy.Canonicalized();
        if ( !canonical || canonical->peers != policy.peers )
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

    bool QuorumPolicyState::operator==( const QuorumPolicyState &other ) const
    {
        return encoding_version == other.encoding_version && network_id == other.network_id &&
               version == other.version && expected_previous_hash == other.expected_previous_hash &&
               authorizing_policy_hash == other.authorizing_policy_hash && peers == other.peers &&
               membership_threshold == other.membership_threshold && burn_threshold == other.burn_threshold;
    }

    bool ValidateQuorumPolicy( const QuorumPolicyState &policy )
    {
        return policy.Canonicalized().has_value();
    }

    bool ValidatePolicySuccessor( const QuorumPolicyState &current, const QuorumPolicyState &candidate )
    {
        const auto current_canonical   = current.Canonicalized();
        const auto candidate_canonical = candidate.Canonicalized();
        const auto current_hash        = current.Hash();
        if ( !current_canonical || !candidate_canonical || !current_hash ||
             current_canonical->version == std::numeric_limits<uint64_t>::max() )
        {
            return false;
        }
        return candidate_canonical->network_id == current_canonical->network_id &&
               candidate_canonical->version == current_canonical->version + 1 &&
               candidate_canonical->expected_previous_hash == *current_hash &&
               candidate_canonical->authorizing_policy_hash == *current_hash;
    }
} // namespace sgns::trustedpeer
