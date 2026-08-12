#include "trustedpeer/CanonicalTrustCodec.hpp"

namespace sgns::trustedpeer
{
    void CanonicalTrustCodec::Writer::WriteU8( uint8_t ) {}
    void CanonicalTrustCodec::Writer::WriteU16( uint16_t ) {}
    void CanonicalTrustCodec::Writer::WriteU32( uint32_t ) {}
    void CanonicalTrustCodec::Writer::WriteU64( uint64_t ) {}
    void CanonicalTrustCodec::Writer::WriteBytes( gsl::span<const uint8_t> ) {}
    void CanonicalTrustCodec::Writer::WriteBytes( std::string_view ) {}
    void CanonicalTrustCodec::Writer::WriteLengthPrefixedBytes( gsl::span<const uint8_t> ) {}
    const std::vector<uint8_t> &CanonicalTrustCodec::Writer::Bytes() const { return bytes_; }
    std::vector<uint8_t> CanonicalTrustCodec::Writer::Take() { return std::move( bytes_ ); }

    CanonicalTrustCodec::Reader::Reader( gsl::span<const uint8_t> bytes ) : bytes_( bytes ) {}
    std::optional<uint8_t> CanonicalTrustCodec::Reader::ReadU8() { return std::nullopt; }
    std::optional<uint16_t> CanonicalTrustCodec::Reader::ReadU16() { return std::nullopt; }
    std::optional<uint32_t> CanonicalTrustCodec::Reader::ReadU32() { return std::nullopt; }
    std::optional<uint64_t> CanonicalTrustCodec::Reader::ReadU64() { return std::nullopt; }
    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::Reader::ReadBytes( size_t ) { return std::nullopt; }
    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::Reader::ReadLengthPrefixedBytes( size_t )
    {
        return std::nullopt;
    }
    bool CanonicalTrustCodec::Reader::Exhausted() const { return false; }

    std::optional<std::vector<uint8_t>> CanonicalTrustCodec::DecodePublicKey( std::string_view )
    {
        return std::nullopt;
    }

    std::string CanonicalTrustCodec::EncodePublicKey( gsl::span<const uint8_t> ) { return {}; }

    std::optional<std::vector<std::string>> CanonicalTrustCodec::NormalizePublicKeys(
        const std::vector<std::string> & )
    {
        return std::nullopt;
    }

    std::string CanonicalTrustCodec::Sha256Hex( gsl::span<const uint8_t> ) { return {}; }
} // namespace sgns::trustedpeer
