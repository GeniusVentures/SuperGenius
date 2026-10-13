/**
 * @file       QuorumThresholdValidation.hpp
 * @brief      Exact quorum-policy floor and bounds validation helpers.
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
    inline uint64_t MembershipQuorumFloor( size_t signer_set_size )
    {
        if ( signer_set_size == 0 )
        {
            return 0;
        }
        return static_cast<uint64_t>( signer_set_size ) / 2 + 1;
    }

    inline uint64_t BurnQuorumFloor( size_t signer_set_size )
    {
        const auto count = static_cast<uint64_t>( signer_set_size );
        return count - count / 3;
    }

    inline outcome::result<void> ValidateThresholdAtFloor( uint64_t threshold, size_t signer_set_size, uint64_t floor )
    {
        const auto count = static_cast<uint64_t>( signer_set_size );
        if ( count == 0 || threshold == 0 || threshold > count || threshold < floor )
        {
            return outcome::failure( SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR );
        }
        return outcome::success();
    }

    inline outcome::result<void> ValidateMembershipQuorumThreshold( uint64_t threshold, size_t signer_set_size )
    {
        return ValidateThresholdAtFloor( threshold, signer_set_size, MembershipQuorumFloor( signer_set_size ) );
    }

    inline outcome::result<void> ValidateBurnQuorumThreshold( uint64_t threshold, size_t signer_set_size )
    {
        return ValidateThresholdAtFloor( threshold, signer_set_size, BurnQuorumFloor( signer_set_size ) );
    }

    /// @brief Strict-majority safety floor, ceil(0.51 * signer_set_size):
    ///        the minimum threshold that can never be met by a signer set
    ///        alone without cooperation of more than half of its members.
    ///        Returns 0 for an empty signer set (an empty set can never
    ///        authorize anything -- ValidateThresholdAtFloor rejects it).
    inline uint64_t StrictMajorityQuorumFloor( size_t signer_set_size )
    {
        return ( static_cast<uint64_t>( signer_set_size ) * 51 + 99 ) / 100;
    }

    /**
     * @brief Validates `threshold` against the strict-majority floor
     *        ceil(0.51 * signer_set_size). Used by registries whose bootstrap
     *        authority is an external peer set (e.g. NetworkRegistry's
     *        TrustedPeerRegistry-majority bootstrap, D-06): the bootstrap
     *        record must carry signatures from strictly more than half of the
     *        authorizing set.
     * @param[in] threshold Configured required-signature count.
     * @param[in] signer_set_size Size of the authorizing signer set.
     * @return success, or SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR when
     *         the set is empty, the threshold is 0/above the set size, or
     *         below the strict-majority floor.
     */
    inline outcome::result<void> ValidateQuorumThreshold( uint64_t threshold, size_t signer_set_size )
    {
        return ValidateThresholdAtFloor( threshold, signer_set_size, StrictMajorityQuorumFloor( signer_set_size ) );
    }

} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_QUORUMTHRESHOLDVALIDATION_HPP
