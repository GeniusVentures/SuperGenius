/**
 * @file processing_clocks_elm.hpp
 * @brief Deterministic three-clock derivation for ELM (elm_processing) jobs.
 * @copyright 2026
 */

#ifndef SGNS_PROCESSING_CLOCKS_ELM_HPP
#define SGNS_PROCESSING_CLOCKS_ELM_HPP

#include <chrono>
#include <cstdint>

namespace sgns::processing
{
    /// @brief USD charged per hour of ELM processing (FUND-01).
    /// Deterministic branch: no live price lookup anywhere on this path.
    inline constexpr double kUsdPerHourElm = 0.0003;

    /// @brief Named USD-per-GNUS rate used at hold time (OD-2).
    /// Defined exactly once; recorded into the escrow CRDT sibling key by
    /// GeniusNode::CreateElmRateRecordCRDTTransaction so settlement disputes
    /// can be resolved against the rate actually used, not a live price.
    inline constexpr double kUsdPerGnusRate = 1.0;

    /// @brief Lock grace period added to the deadline to form the lock timeout (D-08/D-09).
    /// Planner-discretion floor; always exceeds publication latency so a
    /// deadline-expiring job can still publish its result before unlock.
    inline constexpr std::chrono::seconds kLockGraceSeconds{ 60 };

    /// @brief Hard cap on declared maximum_processing_hours (D-05; schema+gate enforced upstream).
    inline constexpr double kMaxProcessingHoursCap = 24.0;

    /// @brief The three clocks every ELM escrow derivation produces.
    struct ElmClocks
    {
        std::chrono::milliseconds deadline;     ///< Wall-clock processing deadline == hours exactly (D-08)
        std::chrono::milliseconds lockTimeout;  ///< deadline + kLockGraceSeconds (D-08)
        uint64_t                 escrowMinions; ///< Max escrow in minion units (10^-6 GNUS)
    };

    /**
     * @brief Single integer escrow path shared by DeriveElmClocks and GetElmProcessCost (D-08/D-09).
     *
     * milli-hours = std::llround(hours * 1000.0); minions = milli_hours * 3 / 10
     * ($0.0003/hour x 10^6 minions/$ at kUsdPerGnusRate == 1.0, floored).
     * llround guards against 1299.999... -> 1299 truncation (Pitfall 3); the
     * final /10 floor is requestor-favorable (D-02).
     *
     * @param maximum_processing_hours Declared hours; sub-milli-hour bills 0.
     * @return Escrow amount in minions.
     */
    uint64_t ElmEscrowMinions( double maximum_processing_hours );

    /**
     * @brief The single derivation source for the three ELM clocks (FUND-02).
     *
     * deadline == hours exactly (no grace inflation, D-08);
     * lockTimeout == deadline + kLockGraceSeconds;
     * escrowMinions == ElmEscrowMinions(hours) (the same integer path as
     * GeniusNode::GetElmProcessCost, so cost and lock can never diverge).
     *
     * @param maximum_processing_hours Declared hours, contract (0, kMaxProcessingHoursCap].
     *        The schema bounds and the ProcessingManager C++ gate guarantee this
     *        upstream; a debug assert checks it, no clamping (D-05).
     * @return The derived clocks.
     */
    ElmClocks DeriveElmClocks( double maximum_processing_hours );
} // namespace sgns::processing

#endif // SGNS_PROCESSING_CLOCKS_ELM_HPP
