#include "securecrdt/SecureCrdtCandidate.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "trustedpeer/CanonicalTrustCodec.hpp"

namespace sgns::securecrdt
{
    namespace
    {
        constexpr size_t HASH_BYTES              = 32;
        constexpr size_t MAX_DOMAIN_BYTES        = 128;
        constexpr size_t MAX_SIGNATURE_BYTES     = CandidateLimits::MAX_CANDIDATE_BYTES;
        constexpr size_t MAX_APPROVAL_WIRE_BYTES = CandidateLimits::MAX_CANDIDATE_BYTES * 2 + 512;

        bool IsLowerHex( const std::string &value, size_t length )
        {
            return value.size() == length &&
                   std::all_of( value.begin(),
                                value.end(),
                                []( char byte )
                                { return ( byte >= '0' && byte <= '9' ) || ( byte >= 'a' && byte <= 'f' ); } );
        }

        bool IsCanonicalDomain( const std::string &domain )
        {
            return !domain.empty() && domain.size() <= MAX_DOMAIN_BYTES &&
                   std::all_of( domain.begin(),
                                domain.end(),
                                []( char byte )
                                {
                                    return ( byte >= 'a' && byte <= 'z' ) || ( byte >= '0' && byte <= '9' ) ||
                                           byte == '-' || byte == '_' || byte == '.';
                                } );
        }

        bool IsKnownKind( CandidateKind kind )
        {
            switch ( kind )
            {
                case CandidateKind::TrustPolicy:
                case CandidateKind::BurnConfig:
                case CandidateKind::TrustedPeerGenesis:
                    return true;
            }
            return false;
        }

        std::optional<std::vector<uint8_t>> DecodeHex( const std::string &hex, size_t expected_bytes )
        {
            if ( !IsLowerHex( hex, expected_bytes * 2 ) )
            {
                return std::nullopt;
            }
            auto decoded = sgns::base::unhex( hex );
            if ( decoded.has_error() || decoded.value().size() != expected_bytes )
            {
                return std::nullopt;
            }
            return decoded.value();
        }

        std::string EncodeBytes( const std::vector<uint8_t> &bytes )
        {
            return sgns::base::hex_lower( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
        }
    } // namespace

    bool CandidateLimits::CandidateCountAllowed( size_t count )
    {
        return count <= MAX_ACTIVE_CANDIDATES_PER_PREDECESSOR;
    }

    bool CandidateLimits::ApprovalCountAllowed( size_t count )
    {
        return count <= MAX_APPROVALS_PER_CANDIDATE;
    }

    bool CandidateLimits::ApprovalBytesAllowed( size_t current_bytes, size_t additional_bytes )
    {
        return current_bytes <= MAX_ACTIVE_APPROVAL_BYTES_PER_PREDECESSOR &&
               additional_bytes <= MAX_ACTIVE_APPROVAL_BYTES_PER_PREDECESSOR - current_bytes;
    }

    std::optional<std::vector<uint8_t>> CandidateCore::CanonicalBytes() const
    {
        if ( encoding_version != ENCODING_VERSION || !IsCanonicalDomain( domain ) || !IsKnownKind( kind ) ||
             version == 0 || !IsLowerHex( expected_previous_hash, HASH_BYTES * 2 ) ||
             !IsLowerHex( authorizing_policy_hash, HASH_BYTES * 2 ) )
        {
            return std::nullopt;
        }

        const auto previous_hash = DecodeHex( expected_previous_hash, HASH_BYTES );
        const auto policy_hash   = DecodeHex( authorizing_policy_hash, HASH_BYTES );
        if ( !previous_hash || !policy_hash )
        {
            return std::nullopt;
        }

        sgns::trustedpeer::CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( RECORD_DOMAIN );
        writer.WriteU8( encoding_version );
        if ( !writer.WriteLengthPrefixedBytes(
                 gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( domain.data() ), // NOLINT
                                           domain.size() ) ) )
        {
            return std::nullopt;
        }
        writer.WriteU16( network_id );
        writer.WriteU8( static_cast<uint8_t>( kind ) );
        writer.WriteU64( version );
        if ( !writer.WriteLengthPrefixedBytes( *previous_hash ) || !writer.WriteLengthPrefixedBytes( *policy_hash ) ||
             !writer.WriteLengthPrefixedBytes( payload ) )
        {
            return std::nullopt;
        }
        auto bytes = writer.Take();
        if ( bytes.size() > CandidateLimits::MAX_CANDIDATE_BYTES )
        {
            return std::nullopt;
        }
        return bytes;
    }

    std::optional<std::string> CandidateCore::Hash() const
    {
        const auto bytes = CanonicalBytes();
        if ( !bytes )
        {
            return std::nullopt;
        }
        return sgns::trustedpeer::CanonicalTrustCodec::Sha256Hex(
            gsl::span<const uint8_t>( bytes->data(), bytes->size() ) );
    }

    std::optional<CandidateCore> CandidateCore::DecodeCanonical( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.size() > CandidateLimits::MAX_CANDIDATE_BYTES )
        {
            return std::nullopt;
        }

        sgns::trustedpeer::CanonicalTrustCodec::Reader reader( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
        const auto                                     record_domain = reader.ReadBytes( RECORD_DOMAIN.size() );
        const auto                                     encoding      = reader.ReadU8();
        const auto domain_bytes  = reader.ReadLengthPrefixedBytes( MAX_DOMAIN_BYTES );
        const auto network       = reader.ReadU16();
        const auto kind_value    = reader.ReadU8();
        const auto version_value = reader.ReadU64();
        const auto previous_hash = reader.ReadLengthPrefixedBytes( HASH_BYTES );
        const auto policy_hash   = reader.ReadLengthPrefixedBytes( HASH_BYTES );
        const auto payload_bytes = reader.ReadLengthPrefixedBytes( CandidateLimits::MAX_CANDIDATE_BYTES );
        if ( !record_domain || !std::equal( record_domain->begin(), record_domain->end(), RECORD_DOMAIN.begin() ) ||
             !encoding || *encoding != ENCODING_VERSION || !domain_bytes || !network || !kind_value || !version_value ||
             *version_value == 0 || !previous_hash || previous_hash->size() != HASH_BYTES || !policy_hash ||
             policy_hash->size() != HASH_BYTES || !payload_bytes || !reader.Exhausted() )
        {
            return std::nullopt;
        }

        CandidateCore core;
        core.encoding_version = *encoding;
        core.domain.assign( domain_bytes->begin(), domain_bytes->end() );
        core.network_id              = *network;
        core.kind                    = static_cast<CandidateKind>( *kind_value );
        core.version                 = *version_value;
        core.expected_previous_hash  = EncodeBytes( *previous_hash );
        core.authorizing_policy_hash = EncodeBytes( *policy_hash );
        core.payload                 = std::move( *payload_bytes );

        const auto canonical = core.CanonicalBytes();
        if ( !canonical || *canonical != bytes )
        {
            return std::nullopt;
        }
        return core;
    }

    bool CandidateCore::operator==( const CandidateCore &other ) const
    {
        return encoding_version == other.encoding_version && domain == other.domain && network_id == other.network_id &&
               kind == other.kind && version == other.version &&
               expected_previous_hash == other.expected_previous_hash &&
               authorizing_policy_hash == other.authorizing_policy_hash && payload == other.payload;
    }

    std::optional<CandidateId> CandidateId::FromCore( const CandidateCore &core )
    {
        const auto hash = core.Hash();
        if ( !hash )
        {
            return std::nullopt;
        }
        return CandidateId{ core.domain, core.version, *hash };
    }

    bool CandidateId::operator==( const CandidateId &other ) const
    {
        return domain == other.domain && version == other.version && content_hash == other.content_hash;
    }

    sgns::crdt::HierarchicalKey CandidateKey::ToHierarchicalKey() const
    {
        if ( !IsCanonicalDomain( id.domain ) || id.version == 0 || !IsLowerHex( id.content_hash, HASH_BYTES * 2 ) ||
             !IsLowerHex( signer, sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES * 2 ) )
        {
            return {};
        }
        return sgns::crdt::HierarchicalKey( id.domain )
            .ChildString( "candidate" )
            .ChildString( "v" + std::to_string( id.version ) )
            .ChildString( id.content_hash )
            .ChildString( "approval" )
            .ChildString( signer );
    }

    std::optional<CandidateKey> CandidateKey::Parse( const sgns::crdt::HierarchicalKey &key )
    {
        const auto segments = key.GetList();
        if ( segments.size() != 6 || !IsCanonicalDomain( segments[0] ) || segments[1] != "candidate" ||
             segments[2].size() < 2 || segments[2][0] != 'v' || segments[4] != "approval" ||
             !IsLowerHex( segments[3], HASH_BYTES * 2 ) ||
             !IsLowerHex( segments[5], sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES * 2 ) )
        {
            return std::nullopt;
        }

        uint64_t   version_value = 0;
        const auto version_text  = std::string_view( segments[2] ).substr( 1 );
        const auto parsed        = std::from_chars( version_text.data(),
                                             version_text.data() + version_text.size(),
                                             version_value );
        if ( parsed.ec != std::errc() || parsed.ptr != version_text.data() + version_text.size() ||
             version_value == 0 || std::to_string( version_value ) != version_text )
        {
            return std::nullopt;
        }
        return CandidateKey{ CandidateId{ segments[0], version_value, segments[3] }, segments[5] };
    }

    std::optional<std::vector<uint8_t>> CandidateApprovalRecord::CanonicalBytes() const
    {
        if ( encoding_version != ENCODING_VERSION ||
             !IsLowerHex( signer, sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES * 2 ) || signature.empty() ||
             signature.size() > MAX_SIGNATURE_BYTES )
        {
            return std::nullopt;
        }
        const auto core_bytes   = core.CanonicalBytes();
        const auto signer_bytes = DecodeHex( signer, sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES );
        if ( !core_bytes || !signer_bytes )
        {
            return std::nullopt;
        }

        sgns::trustedpeer::CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( RECORD_DOMAIN );
        writer.WriteU8( encoding_version );
        if ( !writer.WriteLengthPrefixedBytes( *core_bytes ) || !writer.WriteLengthPrefixedBytes( *signer_bytes ) ||
             !writer.WriteLengthPrefixedBytes( signature ) )
        {
            return std::nullopt;
        }
        return writer.Take();
    }

    std::optional<CandidateApprovalRecord> CandidateApprovalRecord::DecodeCanonical( const std::vector<uint8_t> &bytes,
                                                                                     const CandidateKey         &key )
    {
        if ( bytes.size() > MAX_APPROVAL_WIRE_BYTES )
        {
            return std::nullopt;
        }

        sgns::trustedpeer::CanonicalTrustCodec::Reader reader( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
        const auto                                     record_domain = reader.ReadBytes( RECORD_DOMAIN.size() );
        const auto                                     encoding      = reader.ReadU8();
        const auto core_bytes   = reader.ReadLengthPrefixedBytes( CandidateLimits::MAX_CANDIDATE_BYTES );
        const auto signer_bytes = reader.ReadLengthPrefixedBytes(
            sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES );
        const auto signature_bytes = reader.ReadLengthPrefixedBytes( MAX_SIGNATURE_BYTES );
        if ( !record_domain || !std::equal( record_domain->begin(), record_domain->end(), RECORD_DOMAIN.begin() ) ||
             !encoding || *encoding != ENCODING_VERSION || !core_bytes || !signer_bytes ||
             signer_bytes->size() != sgns::trustedpeer::CanonicalTrustCodec::PUBLIC_KEY_BYTES || !signature_bytes ||
             signature_bytes->empty() || !reader.Exhausted() )
        {
            return std::nullopt;
        }

        const auto core = CandidateCore::DecodeCanonical( *core_bytes );
        if ( !core )
        {
            return std::nullopt;
        }
        CandidateApprovalRecord record;
        record.encoding_version = *encoding;
        record.core             = *core;
        record.signer           = EncodeBytes( *signer_bytes );
        record.signature        = std::move( *signature_bytes );

        const auto id        = CandidateId::FromCore( record.core );
        const auto canonical = record.CanonicalBytes();
        if ( !id || !( *id == key.id ) || record.signer != key.signer || !canonical || *canonical != bytes )
        {
            return std::nullopt;
        }
        return record;
    }
} // namespace sgns::securecrdt
