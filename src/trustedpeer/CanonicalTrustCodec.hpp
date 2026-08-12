/**
 * @file       CanonicalTrustCodec.hpp
 * @brief      Bounded canonical byte primitives for trusted-policy identities.
 */
#ifndef SUPERGENIUS_CANONICAL_TRUST_CODEC_HPP
#define SUPERGENIUS_CANONICAL_TRUST_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gsl/span>

namespace sgns::trustedpeer
{
    class CanonicalTrustCodec
    {
    public:
        static constexpr std::string_view GENESIS_DOMAIN = "SGNS_TRUST_GENESIS_V1";
        static constexpr size_t           MAX_TRUSTED_PEERS = 256;
        static constexpr size_t           PUBLIC_KEY_BYTES = 64;

        class Writer
        {
        public:
            void WriteU8( uint8_t value );
            void WriteU16( uint16_t value );
            void WriteU32( uint32_t value );
            void WriteU64( uint64_t value );
            void WriteBytes( gsl::span<const uint8_t> bytes );
            void WriteBytes( std::string_view bytes );
            void WriteLengthPrefixedBytes( gsl::span<const uint8_t> bytes );

            [[nodiscard]] const std::vector<uint8_t> &Bytes() const;
            [[nodiscard]] std::vector<uint8_t>        Take();

        private:
            std::vector<uint8_t> bytes_;
        };

        class Reader
        {
        public:
            explicit Reader( gsl::span<const uint8_t> bytes );

            [[nodiscard]] std::optional<uint8_t>              ReadU8();
            [[nodiscard]] std::optional<uint16_t>             ReadU16();
            [[nodiscard]] std::optional<uint32_t>             ReadU32();
            [[nodiscard]] std::optional<uint64_t>             ReadU64();
            [[nodiscard]] std::optional<std::vector<uint8_t>> ReadBytes( size_t length );
            [[nodiscard]] std::optional<std::vector<uint8_t>> ReadLengthPrefixedBytes( size_t maximum_length );
            [[nodiscard]] bool                                Exhausted() const;

        private:
            gsl::span<const uint8_t> bytes_;
            size_t                   offset_ = 0;
        };

        [[nodiscard]] static std::optional<std::vector<uint8_t>> DecodePublicKey( std::string_view public_key );
        [[nodiscard]] static std::string EncodePublicKey( gsl::span<const uint8_t> public_key );
        [[nodiscard]] static std::optional<std::vector<std::string>> NormalizePublicKeys(
            const std::vector<std::string> &public_keys );
        [[nodiscard]] static std::string Sha256Hex( gsl::span<const uint8_t> canonical_bytes );
    };
} // namespace sgns::trustedpeer

#endif // SUPERGENIUS_CANONICAL_TRUST_CODEC_HPP
