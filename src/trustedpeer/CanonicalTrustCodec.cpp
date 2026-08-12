#include "trustedpeer/CanonicalTrustCodec.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

#include "base/hexutil.hpp"
#include "crypto/sha/sha256.hpp"

namespace sgns::trustedpeer
{
    namespace
    {
        std::optional<uint8_t> HexNibble( char value )
        {
            if ( value >= '0' && value <= '9' )
            {
                return static_cast<uint8_t>( value - '0' );
            }
            if ( value >= 'a' && value <= 'f' )
            {
                return static_cast<uint8_t>( value - 'a' + 10 );
            }
            if ( value >= 'A' && value <= 'F' )
            {
                return static_cast<uint8_t>( value - 'A' + 10 );
            }
            return std::nullopt;
        }
    } // namespace

    void CanonicalTrustCodec::Writer::WriteU8( uint8_t value )
    {
        bytes_.push_back( value );
    }

    void CanonicalTrustCodec::Writer::WriteU16( uint16_t value )
    {
        bytes_.push_back( static_cast<uint8_t>( value >> 8U ) );
        bytes_.push_back( static_cast<uint8_t>( value ) );
    }

    void CanonicalTrustCodec::Writer::WriteU32( uint32_t value )
    {
        bytes_.push_back( static_cast<uint8_t>( value >> 24U ) );
        bytes_.push_back( static_cast<uint8_t>( value >> 16U ) );
        bytes_.push_back( static_cast<uint8_t>( value >> 8U ) );
        bytes_.push_back( static_cast<uint8_t>( value ) );
    }

    void CanonicalTrustCodec::Writer::WriteU64( uint64_t value )
    {
        for ( int shift = 56; shift >= 0; shift -= 8 )
        {
            bytes_.push_back( static_cast<uint8_t>( value >> static_cast<unsigned>( shift ) ) );
        }
    }

    void CanonicalTrustCodec::Writer::WriteBytes( gsl::span<const uint8_t> bytes )
    {
        bytes_.insert( bytes_.end(), bytes.begin(), bytes.end() );
    }

    void CanonicalTrustCodec::Writer::WriteBytes( std::string_view bytes )
    {
        const auto *begin = reinterpret_cast<const uint8_t *>( bytes.data() ); // NOLINT
        WriteBytes( gsl::span<const uint8_t>( begin, bytes.size() ) );
    }

    bool CanonicalTrustCodec::Writer::WriteLengthPrefixedBytes( gsl::span<const uint8_t> bytes )
    {
        if ( bytes.size() > std::numeric_limits<uint32_t>::max() )
        {
            return false;
        }
        WriteU32( static_cast<uint32_t>( bytes.size() ) );
        WriteBytes( bytes );
        return true;
    }

    const std::vector<uint8_t> &CanonicalTrustCodec::Writer::Bytes() const
    {
        return bytes_;
    }

    std::vector<uint8_t> CanonicalTrustCodec::Writer::Take()
    {
        return std::move( bytes_ );
    }

    CanonicalTrustCodec::Reader::Reader( gsl::span<const uint8_t> bytes ) : bytes_( bytes )
    {
    }

    std::optional<uint8_t> CanonicalTrustCodec::Reader::ReadU8()
    {
        const auto bytes = ReadBytes( 1 );
        return bytes ? std::optional<uint8_t>( bytes->front() ) : std::nullopt;
    }

    std::optional<uint16_t> CanonicalTrustCodec::Reader::ReadU16()
    {
        const auto bytes = ReadBytes( 2 );
        if ( !bytes )
        {
            return std::nullopt;
        }
        return static_cast<uint16_t>( static_cast<uint16_t>( ( *bytes )[0] ) << 8U |
                                      static_cast<uint16_t>( ( *bytes )[1] ) );
    }

    std::optional<uint32_t> CanonicalTrustCodec::Reader::ReadU32()
    {
        const auto bytes = ReadBytes( 4 );
        if ( !bytes )
        {
            return std::nullopt;
        }
        uint32_t value = 0;
        for ( const auto byte : *bytes )
        {
            value = ( value << 8U ) | byte;
        }
        return value;
    }

    std::optional<uint64_t> CanonicalTrustCodec::Reader::ReadU64()
    {
        const auto bytes = ReadBytes( 8 );
        if ( !bytes )
        {
            return std::nullopt;
        }
        uint64_t value = 0;
        for ( const auto byte : *bytes )
        {
            value = ( value << 8U ) | byte;
        }
        return value;
    }

    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::Reader::ReadBytes( size_t length )
    {
        const auto total_size = static_cast<size_t>( bytes_.size() );
        if ( offset_ > total_size || length > total_size - offset_ )
        {
            return std::nullopt;
        }
        std::vector<uint8_t> result( bytes_.begin() + static_cast<ptrdiff_t>( offset_ ),
                                     bytes_.begin() + static_cast<ptrdiff_t>( offset_ + length ) );
        offset_ += length;
        return result;
    }

    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::Reader::ReadLengthPrefixedBytes( size_t maximum_length )
    {
        const auto length = ReadU32();
        if ( !length || *length > maximum_length )
        {
            return std::nullopt;
        }
        return ReadBytes( *length );
    }

    bool CanonicalTrustCodec::Reader::Exhausted() const
    {
        return offset_ == static_cast<size_t>( bytes_.size() );
    }

    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::DecodePublicKey( std::string_view public_key )
    {
        if ( public_key.size() != PUBLIC_KEY_BYTES * 2 )
        {
            return std::nullopt;
        }

        std::vector<uint8_t> decoded;
        decoded.reserve( PUBLIC_KEY_BYTES );
        for ( size_t index = 0; index < public_key.size(); index += 2 )
        {
            const auto high = HexNibble( public_key[index] );
            const auto low  = HexNibble( public_key[index + 1] );
            if ( !high || !low )
            {
                return std::nullopt;
            }
            decoded.push_back( static_cast<uint8_t>( ( *high << 4U ) | *low ) );
        }
        return decoded;
    }

    std::string CanonicalTrustCodec::EncodePublicKey( gsl::span<const uint8_t> public_key )
    {
        if ( public_key.size() != PUBLIC_KEY_BYTES )
        {
            return {};
        }
        return sgns::base::hex_lower( public_key );
    }

    std::optional<std::vector<std::string>> CanonicalTrustCodec::NormalizePublicKeys(
        const std::vector<std::string> &public_keys )
    {
        if ( public_keys.empty() || public_keys.size() > MAX_TRUSTED_PEERS )
        {
            return std::nullopt;
        }

        std::vector<std::string> normalized;
        normalized.reserve( public_keys.size() );
        std::unordered_set<std::string> unique;
        for ( const auto &public_key : public_keys )
        {
            const auto decoded = DecodePublicKey( public_key );
            if ( !decoded )
            {
                return std::nullopt;
            }
            const auto encoded = EncodePublicKey( gsl::span<const uint8_t>( decoded->data(), decoded->size() ) );
            if ( encoded.empty() || !unique.insert( encoded ).second )
            {
                return std::nullopt;
            }
            normalized.push_back( encoded );
        }
        std::sort( normalized.begin(), normalized.end() );
        return normalized;
    }

    std::string CanonicalTrustCodec::Sha256Hex( gsl::span<const uint8_t> canonical_bytes )
    {
        return sgns::crypto::sha256( canonical_bytes ).toHex();
    }
} // namespace sgns::trustedpeer
