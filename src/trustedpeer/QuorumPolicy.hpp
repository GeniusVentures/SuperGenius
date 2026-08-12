/**
 * @file       QuorumPolicy.hpp
 * @brief      Canonical versioned trusted-peer quorum policy contract.
 */
#ifndef SUPERGENIUS_QUORUM_POLICY_HPP
#define SUPERGENIUS_QUORUM_POLICY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgns::trustedpeer
{
    struct QuorumPolicyState
    {
        static constexpr uint8_t          ENCODING_VERSION = 1;
        static constexpr std::string_view POLICY_DOMAIN    = "SGNS_TRUST_POLICY_V1";

        uint8_t                  encoding_version = ENCODING_VERSION;
        uint16_t                 network_id       = 0;
        uint64_t                 version          = 0;
        std::string              expected_previous_hash;
        std::string              authorizing_policy_hash;
        std::vector<std::string> peers;
        uint64_t                 membership_threshold = 0;
        uint64_t                 burn_threshold       = 0;

        [[nodiscard]] std::optional<QuorumPolicyState>        Canonicalized() const;
        [[nodiscard]] std::optional<std::vector<uint8_t>>     CanonicalBytes() const;
        [[nodiscard]] std::optional<std::string>              Hash() const;
        [[nodiscard]] static std::optional<QuorumPolicyState> DecodeCanonical( const std::vector<uint8_t> &bytes );

        bool operator==( const QuorumPolicyState &other ) const;
    };

    [[nodiscard]] bool ValidateQuorumPolicy( const QuorumPolicyState &policy );
    [[nodiscard]] bool ValidatePolicySuccessor( const QuorumPolicyState &current, const QuorumPolicyState &candidate );
} // namespace sgns::trustedpeer

#endif // SUPERGENIUS_QUORUM_POLICY_HPP
