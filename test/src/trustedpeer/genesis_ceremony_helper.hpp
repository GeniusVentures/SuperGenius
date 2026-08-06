/**
 * @file       genesis_ceremony_helper.hpp
 * @brief      Test-only helper generating an ephemeral, in-memory-only
 *             bootstrapper keypair and signing a genesis payload with the
 *             canonical GeniusSigner component. The private key is never
 *             persisted to disk (T-10-08) and is confined to test binaries.
 */
#ifndef SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP
#define SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "account/GeniusSigner.hpp"

namespace sgns::test::trustedpeer
{
    /// @brief Result of a single ephemeral genesis-ceremony signing operation.
    struct GenesisCeremonyArtifact
    {
        std::string          bootstrapper_address; ///< 128-lowercase-hex ephemeral public address.
        std::vector<uint8_t> signature;            ///< 64-byte signature, matching GeniusAccount::Sign's encoding.
    };

    /// @brief Generates a fresh, in-memory-only ephemeral keypair, derives its
    ///        128-hex-char address, and signs `payload` using GeniusSigner so
    ///        the result verifies unmodified via
    ///        sgns::multisig::VerifyPayloadSignature / GeniusAccount::VerifySignature.
    /// @param[in] payload Raw payload bytes to sign.
    /// @return {bootstrapper_address, signature} artifact.
    inline GenesisCeremonyArtifact GenerateGenesisCeremonyArtifact( const std::vector<uint8_t> &payload )
    {
        auto signer = GeniusSigner::Generate();
        return { signer.GetAddress(), signer.Sign( payload ) };
    }
} // namespace sgns::test::trustedpeer

#endif // SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP
