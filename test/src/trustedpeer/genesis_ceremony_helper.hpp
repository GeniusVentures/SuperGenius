/**
 * @file       genesis_ceremony_helper.hpp
 * @brief      Test-only helper generating an ephemeral, in-memory-only
 *             bootstrapper keypair and signing a genesis payload with it,
 *             replicating GeniusAccount::Sign's exact secp256k1/SHA256(SHA256)
 *             routine (src/account/GeniusAccount.cpp:845-873) against a
 *             locally-created secp256k1_context. Never references
 *             GeniusAccount and never persists the ephemeral private key to
 *             disk (T-10-08) -- confined to test binaries only.
 */
#ifndef SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP
#define SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP

// Keep these include files here to prevent errors within crypto3's headers
// (mirrors src/account/GeniusAccount.cpp's include ordering, lines 1-4).
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include <ProofSystem/EthereumKeyGenerator.hpp>

#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sgns::test::trustedpeer
{
    /// @brief Result of a single ephemeral genesis-ceremony signing operation.
    struct GenesisCeremonyArtifact
    {
        std::string          bootstrapper_address; ///< 128-lowercase-hex ephemeral public address.
        std::vector<uint8_t> signature;             ///< 64-byte signature, matching GeniusAccount::Sign's encoding.
    };

    /// @brief Generates a fresh, in-memory-only ephemeral keypair, derives its
    ///        128-hex-char address, and signs `payload` with it exactly the
    ///        way GeniusAccount::Sign does (same secp256k1 context flags,
    ///        SHA256(SHA256(payload)) digest, and byte-reversed compact
    ///        signature serialization) so the result verifies unmodified via
    ///        sgns::multisig::VerifyPayloadSignature / GeniusAccount::VerifySignature.
    /// @param[in] payload Raw payload bytes to sign.
    /// @return {bootstrapper_address, signature} artifact.
    inline GenesisCeremonyArtifact GenerateGenesisCeremonyArtifact( const std::vector<uint8_t> &payload )
    {
        ethereum::EthereumKeyGenerator ephemeral; // default ctor: fresh keypair, in-memory only, never persisted

        GenesisCeremonyArtifact artifact;
        artifact.bootstrapper_address = ephemeral.GetEntirePubValue();

        using Context = std::unique_ptr<secp256k1_context, decltype( &secp256k1_context_destroy )>;
        Context context( secp256k1_context_create( SECP256K1_CONTEXT_SIGN ), &secp256k1_context_destroy );

        const auto              private_key = ephemeral.get_private_key();
        std::array<uint8_t, 32> secret_key{};
        nil::marshalling::bincode::field<ethereum::scalar_field_type>::field_element_to_bytes<
            std::array<uint8_t, 32>::iterator>( private_key.private_key_data(), secret_key.begin(), secret_key.end() );
        std::reverse( secret_key.begin(), secret_key.end() );

        // Mirrors GeniusAccount::Sign's historical SHA256(SHA256(data)) signing protocol.
        const std::array<uint8_t, 32> first_hash = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( payload );
        const std::array<uint8_t, 32> message_hash =
            nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( first_hash );

        secp256k1_ecdsa_signature signature;
        secp256k1_ecdsa_sign( context.get(), &signature, message_hash.data(), secret_key.data(), nullptr, nullptr );

        constexpr size_t          kSignatureExpSize = 64;
        std::array<uint8_t, kSignatureExpSize> compact_signature{};
        secp256k1_ecdsa_signature_serialize_compact( context.get(), compact_signature.data(), &signature );

        std::vector<uint8_t> signed_vector( kSignatureExpSize );
        std::reverse_copy( compact_signature.begin(), compact_signature.begin() + 32, signed_vector.begin() );
        std::reverse_copy( compact_signature.begin() + 32, compact_signature.end(), signed_vector.begin() + 32 );

        artifact.signature = std::move( signed_vector );
        return artifact;
    }
} // namespace sgns::test::trustedpeer

#endif // SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP
