/**
 * @file       genesis_ceremony_helper.hpp
 * @brief      Test-only helper generating an ephemeral, in-memory-only
 *             bootstrapper keypair and signing a genesis payload with the
 *             canonical GeniusSigner component. The private key is never
 *             persisted to disk (T-10-08) and is confined to test binaries.
 */
#ifndef SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP
#define SGNS_TEST_TRUSTEDPEER_GENESIS_CEREMONY_HELPER_HPP

// boost/range/concepts.hpp must precede crypto3's marshalling.hpp: it declares
// boost::SinglePassRangeConcept, which marshalling.hpp uses via
// BOOST_CONCEPT_ASSERT but does not include itself.
#include <boost/range/concepts.hpp>

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
