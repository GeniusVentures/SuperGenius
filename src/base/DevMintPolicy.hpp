/**
 * @file       DevMintPolicy.hpp
 * @brief      Compile-time designated-net policy for burn-less dev mints (D-03).
 * @details    One home for the dev-mint net rule, beside the net-id constants
 *             (sgns_version.hpp) it is built from. Contract: burn-less mints
 *             may certify through FULL consensus (proposal, quorum,
 *             certificate) ONLY on the designated nets — staging and local
 *             dev; every validator on every other net refuses them regardless
 *             of what the proposer declares. The designated set is
 *             compile-time by design: configuration can never widen it, and
 *             only a code change (this file) can add an allowance. The
 *             predicate reads ONLY version::GetNetworkID() — the validator's
 *             own process-global net identity, never a transaction or
 *             proposal field (proposer claims are attacker-chosen).
 * @date       2026-10-21
 */

#ifndef SGNS_DEV_MINT_POLICY_HPP
#define SGNS_DEV_MINT_POLICY_HPP

#include <algorithm>
#include <array>
#include <cstdint>

#include "base/sgns_version.hpp"

namespace sgns
{
    /// Nets where burn-less dev mints may certify through full consensus
    /// (D-03). Built from the named net-id constants — the only net literals.
    inline constexpr std::array<uint16_t, 2> kDevMintPermittedNets{ version::STAGING_NET_ID,
                                                                    version::DEV_NET_ID };

    /**
     * @brief True iff THIS process's own net may certify burn-less dev mints.
     * @return Membership of version::GetNetworkID() in kDevMintPermittedNets.
     */
    inline bool DevMintBurnlessPermitted()
    {
        const auto net = version::GetNetworkID();
        return std::find( kDevMintPermittedNets.begin(), kDevMintPermittedNets.end(), net ) !=
               kDevMintPermittedNets.end();
    }
} // namespace sgns

#endif // SGNS_DEV_MINT_POLICY_HPP
