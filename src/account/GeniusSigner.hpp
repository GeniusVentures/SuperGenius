/**
 * @file       GeniusSigner.hpp
 * @brief      In-memory Genius keypair and canonical signature operations.
 */
#ifndef SGNS_GENIUS_SIGNER_HPP
#define SGNS_GENIUS_SIGNER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ProofSystem/EthereumKeyGenerator.hpp>

namespace sgns
{
    /**
     * @brief Owns a Genius keypair without imposing account persistence or
     *        lifecycle concerns.
     *
     * GeniusAccount composes this type, while short-lived signing workflows
     * can use it directly without touching secure storage.
     */
    class GeniusSigner
    {
    public:
        /**
         * @brief Generate a fresh, in-memory-only keypair.
         */
        static GeniusSigner Generate();

        /**
         * @brief Take ownership of an existing keypair.
         */
        explicit GeniusSigner( ethereum::EthereumKeyGenerator keypair );

        /**
         * @brief Return the 128-character hexadecimal Genius public address.
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

        ethereum::EthereumKeyGenerator keypair_;
    };
} // namespace sgns

#endif // SGNS_GENIUS_SIGNER_HPP
