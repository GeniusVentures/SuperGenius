/**
 * @file       QuorumThresholdValidation.hpp
 * @brief      Shared majority-floor quorum-threshold validation helper
 *             (D-07, security-critical). Both TrustedPeerRegistry::New and
 *             BurnConfig::New call this at construction time to reject any
 *             locally-configured quorum_threshold below ceil(0.51*N) --
 *             preventing a malicious node operator from locally lowering a
 *             registry's threshold to trivially self-confirm an under-signed
 *             value.
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_SECURECRDT_QUORUMTHRESHOLDVALIDATION_HPP
#define SGNS_SECURECRDT_QUORUMTHRESHOLDVALIDATION_HPP

#include <cstddef>
#include <cstdint>

#include "outcome/outcome.hpp"
#include "securecrdt/SecureCrdt.hpp"

namespace sgns::securecrdt
{
    /**
     * @brief Validates that `threshold` is at or above the majority-safety
     *        floor for a signer set of size `signer_set_size`
     *        (ceil(0.51*signer_set_size), computed via integer arithmetic).
     * @param[in] threshold Configured quorum threshold to validate.
     * @param[in] signer_set_size Construction-time membership size of the
     *            registry being built (e.g. genesis_peers.size()).
     * @return outcome::success() if threshold >= the majority floor,
     *         outcome::failure(SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR)
     *         otherwise.
     */
    inline outcome::result<void> ValidateQuorumThreshold( uint64_t threshold, size_t signer_set_size )
    {
        const uint64_t minimum_safe_threshold = ( static_cast<uint64_t>( signer_set_size ) * 51 + 99 ) / 100;
        if ( threshold < minimum_safe_threshold )
        {
            return outcome::failure( SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR );
        }
        return outcome::success();
    }
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_QUORUMTHRESHOLDVALIDATION_HPP
