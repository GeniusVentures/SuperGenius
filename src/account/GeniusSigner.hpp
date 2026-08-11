/**
 * @file       GeniusSigner.hpp
 * @brief      In-memory Genius keypair and canonical signature operations.
 */
#ifndef SGNS_GENIUS_SIGNER_HPP
#define SGNS_GENIUS_SIGNER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sgns
{
    /**
     * @brief Owns a Genius keypair without imposing account persistence or
     *        lifecycle concerns.
     *
     * GeniusAccount composes this type, while short-lived signing workflows
     * can use it directly without touching secure storage.
     *
     * The keypair is held as a raw secp256k1 secret key so that this component
     * depends only on libsecp256k1, not on the crypto3-based ProofSystem.
     */
    class GeniusSigner
    {
    public:
        static constexpr size_t PRIVATE_KEY_SIZE = 32;

        /// Big-endian secp256k1 secret key, matching libsecp256k1's own encoding.
        using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;

        /**
         * @brief Generate a fresh, in-memory-only keypair.
         */
        static GeniusSigner Generate();

        /**
         * @brief Take ownership of an existing secret key.
         */
        explicit GeniusSigner( const PrivateKey &private_key );

        /**
         * @brief Return the 128-character hexadecimal Genius public address.
         *
         * The address is the uncompressed public key (X followed by Y, both
         * big-endian) without the leading 0x04 tag, hex encoded in lower case.
         * Empty when the secret key is not a valid secp256k1 scalar.
         */
        [[nodiscard]] std::string GetAddress() const;

        /**
         * @brief Sign bytes using the canonical Genius signature encoding.
         */
        [[nodiscard]] std::vector<uint8_t> Sign( const std::vector<uint8_t> &data ) const;

        /**
         * @brief Verify a canonical Genius signature.
         */
        static bool VerifySignature( const std::string          &address,
                                     std::string_view            signature,
                                     const std::vector<uint8_t> &data );

        /**
         * @brief Verify a canonical Genius signature supplied as bytes.
         */
        static bool VerifySignature( const std::string          &address,
                                     const std::vector<uint8_t> &signature,
                                     const std::vector<uint8_t> &data );

    private:
        static constexpr size_t SIGNATURE_SIZE = 64;

        PrivateKey  private_key_;
        std::string address_; ///< Derived once on construction; empty on an invalid key
    };
} // namespace sgns

#endif // SGNS_GENIUS_SIGNER_HPP
