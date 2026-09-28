/**
 * @file       GenesisManifest.hpp
 * @brief      Reviewed canonical trusted-peer genesis identity.
 */
#ifndef SUPERGENIUS_GENESIS_MANIFEST_HPP
#define SUPERGENIUS_GENESIS_MANIFEST_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "securecrdt/SecureCrdtCandidate.hpp"

namespace sgns::trustedpeer
{
    struct GenesisManifest
    {
        static constexpr uint8_t  ENCODING_VERSION          = 1;
        static constexpr uint64_t INITIAL_BURN_BASIS_POINTS = 100;

        uint8_t                  encoding_version = ENCODING_VERSION;
        uint16_t                 network_id       = 0;
        std::string              bootstrapper_public_key;
        uint64_t                 policy_version = 1;
        std::vector<std::string> peers;
        uint64_t                 membership_threshold      = 0;
        uint64_t                 burn_threshold            = 0;
        uint64_t                 initial_burn_basis_points = INITIAL_BURN_BASIS_POINTS;

        [[nodiscard]] std::optional<GenesisManifest>      Canonicalized() const;
        [[nodiscard]] std::optional<std::vector<uint8_t>> CanonicalBytes() const;
        [[nodiscard]] std::optional<std::string>          Fingerprint() const;

        [[nodiscard]] static std::optional<GenesisManifest> DecodeCanonical( const std::vector<uint8_t> &bytes );
        [[nodiscard]] static std::optional<GenesisManifest> DecodeAndVerify( const std::vector<uint8_t> &bytes,
                                                                             const std::string &expected_fingerprint );

        bool operator==( const GenesisManifest &other ) const;
    };

    /** Single construction site of the genesis CandidateCore; the core bytes are
     *  the signed genesis proof everywhere, so duplicate constructions drift apart. */
    [[nodiscard]] sgns::securecrdt::CandidateCore GenesisCandidateCore(
        const GenesisManifest      &manifest,
        const std::vector<uint8_t> &canonical_bytes,
        const std::string          &fingerprint,
        const std::string          &domain = "trusted-peer-genesis" );
} // namespace sgns::trustedpeer

#endif // SUPERGENIUS_GENESIS_MANIFEST_HPP
