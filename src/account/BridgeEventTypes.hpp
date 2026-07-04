/**
 * @file       BridgeEventTypes.hpp
 * @brief      Lightweight bridge event types and signature constants.
 *             No heavy dependencies — safe to include from tests and watchers.
 * @date       2026-06-22
 */
#ifndef SGNS_BRIDGE_EVENT_TYPES_HPP
#define SGNS_BRIDGE_EVENT_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "account/TokenID.hpp"

namespace sgns
{
    /**
     * @brief Parsed burn event parameters shared between real-time watch
     *        (OnWatchEvent) and startup catch-up scan (PerformStartupCatchupScan).
     */
    struct BurnEventParams
    {
        TokenID     token_id;     ///< Token identifier from event [1]
        uint64_t    amount;       ///< Burn amount from event [2]
        std::string destination;  ///< 128-char hex recipient from event [5] (decompressed if v2)
    };

    /// @brief Canonical Solidity event signatures for bridge events.
    ///        Single source of truth — shared by watch registration, catch-up scan,
    ///        and RPC endpoint validation.
    /// @note  The old signature with 5 parameters it not supported as it was wrong.
    inline constexpr std::string_view kBridgeSourceBurnedSig =
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)";
    inline constexpr std::string_view kBridgeOutInitiatedSig =
        "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)";

} // namespace sgns

#endif // SGNS_BRIDGE_EVENT_TYPES_HPP
