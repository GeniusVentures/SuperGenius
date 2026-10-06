/**
 * Header file for PriceValidator — the deterministic, pure price-validator
 * decision core (Phase 7, VAL-01): timestamp sanity (D-03/D-07-03),
 * tolerance-band check over the node's own observed window
 * (D-01/D-07-01/D-07-02), and escrow cost binding (D-02), in the locked
 * fail-fast order (D-07-11), returning a diagnostics-carrying struct
 * (D-07-10).
 *
 * Purity contract (CONS-02 prerequisite): ValidatePrice reads no clock,
 * performs no I/O, and touches no global or static mutable state. `now`
 * arrives as an input field (RESEARCH Open Q1), so identical input plus
 * config yields the identical verdict on every node.
 */
#pragma once

#include "LocalPriceManager.hpp" // PriceHistoryStats — reuse, do not redefine (D-07-12)
#include "PriceFreshness.hpp"    // kStaleMaxAge — window TTL default (D-07-02)

#include <chrono>
#include <cstdint>

namespace sgns
{
    /// @brief Default tolerance widening the observed [min, max] window,
    /// as a fraction (0.10 = 10%) — D-07-01.
    inline constexpr double kDefaultPriceTolerance{ 0.10 };

    /// @brief Default clock-skew allowance for the future-timestamp bound
    /// (D-07-03).
    inline constexpr std::chrono::seconds kDefaultValidatorClockSkew{ 30 };

    /// @brief Default maximum escrow age before the stale-timestamp reject
    /// (D-07-03, 10 minutes).
    inline constexpr std::chrono::seconds kDefaultValidatorMaxAge{ 600 };

    // Window TTL default reuses kStaleMaxAge (the quote TTL) — D-07-02;
    // no restated 300 literal here.

    /// @brief Typed accept/reject reason taxonomy — exactly one reason per
    /// input, produced in the D-07-11 check order (D-07-12).
    enum class PriceValidationReason
    {
        Accepted,       ///< In-band, cost-bound, timestamp-sane, covered
        LegacyNoPrice,  ///< claimedPrice == 0.0 (proto3 absent field) — fail-closed (D-07-08)
        TimestampFuture,///< dagTimestamp > now + clockSkew (D-07-03)
        TimestampStale, ///< dagTimestamp < now - maxAge (D-07-03)
        CostMismatch,   ///< Escrow != CalculateCostMinions(blockSize, claimedPrice) — or non-finite/negative claimedPrice (D-02)
        AboveBand,      ///< claimedPrice > max * (1 + tolerance) (D-07-01)
        BelowBand,      ///< claimedPrice < min * (1 - tolerance) (D-07-01)
        NoCoverage      ///< stats.count == 0 over the window (D-07-05/D-07-07)
    };

    /// @brief Validator thresholds — a plain aggregate with documented code
    /// defaults; every knob is overridable via the SGNS_PRICEVAL_* resolver
    /// (VAL-02, D-07-04).
    struct PriceValidatorConfig
    {
        /// @brief Band tolerance as a fraction of the observed min/max.
        double tolerancePct = kDefaultPriceTolerance; // 0.10 (10%), D-07-01

        /// @brief Lookback TTL component of the observation window: the
        /// window is [T - (windowTtl + clockSkew), T + clockSkew] (D-07-02).
        /// Default reuses the quote TTL kStaleMaxAge (300s).
        std::chrono::seconds windowTtl = kStaleMaxAge;

        /// @brief Future-timestamp allowance (default 30s, D-07-03).
        std::chrono::seconds clockSkew = kDefaultValidatorClockSkew;

        /// @brief Maximum escrow age before the stale reject (default
        /// 600s, D-07-03).
        std::chrono::seconds maxAge = kDefaultValidatorMaxAge;
    };

    /// @brief Everything the pure validator consumes (D-07-12; `now` is an
    /// input field per RESEARCH Open Q1). The caller (Phase 8) performs the
    /// QueryHistory aggregation and the escrow fetch; the validator never
    /// queries, fetches, or reads the clock.
    struct PriceValidationInput
    {
        double claimedPrice = 0.0;      ///< Task.claimed_price (poster-controlled)
        uint64_t escrowAmount = 0;      ///< EscrowTx.amount held for the task
        std::chrono::system_clock::time_point dagTimestamp{}; ///< Escrow DAGStruct.timestamp (reference time T)
        uint64_t blockSize = 0;         ///< Parsed task size in bytes
        std::chrono::system_clock::time_point now{};          ///< Validator-run time (injected — purity)
        LocalPriceManager::PriceHistoryStats stats{};         ///< QueryHistory(windowFrom, windowTo) evidence
    };

    /// @brief Verdict plus diagnostics (D-07-10): the computed band, the
    /// echoed observed stats, and the window bounds, so Phase 8 logging and
    /// tests assert details without re-deriving them (Pitfall 7).
    struct PriceValidationResult
    {
        bool accepted = false;
        PriceValidationReason reason = PriceValidationReason::Accepted;
        double bandLow = 0.0;   ///< stats.min * (1 - tolerance)
        double bandHigh = 0.0;  ///< stats.max * (1 + tolerance)
        double observedMin = 0.0; ///< Echo of stats.min
        double observedMax = 0.0; ///< Echo of stats.max
        std::chrono::system_clock::time_point windowFrom{}; ///< Observation window lower bound
        std::chrono::system_clock::time_point windowTo{};   ///< Observation window upper bound
    };

    /// @brief The observation window bounds for a DAG timestamp.
    struct ObservationWindow
    {
        std::chrono::system_clock::time_point from{};
        std::chrono::system_clock::time_point to{};
    };

    /// @brief The ONE shared window formula (D-07-02): from = T - (windowTtl
    /// + clockSkew), to = T + clockSkew. The Phase 8 caller uses these
    /// bounds for QueryHistory so caller and validator can never drift
    /// apart on the window definition (Pitfall 3).
    /// @param dagTimestamp Escrow DAG timestamp T
    /// @param config Thresholds supplying windowTtl and clockSkew
    inline ObservationWindow PriceObservationWindow(
        std::chrono::system_clock::time_point dagTimestamp,
        const PriceValidatorConfig &config )
    {
        return { dagTimestamp - ( config.windowTtl + config.clockSkew ),
                 dagTimestamp + config.clockSkew };
    }

    /// @brief Validate a poster's claimed price against the node's own
    /// observed evidence — the price-gaming decision core (VAL-01).
    ///
    /// Fail-fast chain in the locked D-07-11 order, exactly one reason per
    /// input:
    /// 1. Legacy: claimedPrice == 0.0 (proto3 absent field; -0.0 == 0.0 is
    ///    true) -> LegacyNoPrice (D-07-08, fail-closed, no grace flag).
    /// 2. Timestamp sanity: dagTimestamp > now + clockSkew ->
    ///    TimestampFuture; dagTimestamp < now - maxAge -> TimestampStale
    ///    (D-07-03; boundaries on the accept side via strict comparisons).
    /// 3. Cost binding: non-finite or negative claimedPrice -> CostMismatch
    ///    BEFORE the oracle (NaN passes ScaledInteger::FromDouble's
    ///    comparison guard — Pitfall 1); then
    ///    CalculateCostMinions(blockSize, claimedPrice) must equal
    ///    escrowAmount by exact integer equality — never an epsilon (D-02).
    /// 4. Coverage: stats.count == 0 -> NoCoverage (key on count, never
    ///    min — Pitfall 2, D-07-05/D-07-07).
    /// 5. Band: [min * (1 - tolerance), max * (1 + tolerance)], both edges
    ///    inclusive (D-07-01, Pitfall 4).
    ///
    /// @param input Poster-controlled values plus the caller-queried stats
    /// @param config Thresholds (defaults per D-07-01..D-07-04)
    /// @return accepted flag, typed reason, and diagnostics
    /// @note Pure: no clock reads, no I/O, no shared state — identical
    /// inputs yield the identical verdict on every node (VAL-01).
    PriceValidationResult ValidatePrice( const PriceValidationInput &input,
                                         const PriceValidatorConfig &config = {} );

    // ---- VAL-02: SGNS_PRICEVAL_* environment overrides (D-07-04) ----
    // RED-stage stubs: defaults only; the getenv parsing lands with GREEN.

    /// @brief Resolve the effective validator config from code defaults
    /// plus the SGNS_PRICEVAL_* environment overrides (VAL-02, D-07-04).
    inline PriceValidatorConfig ResolvePriceValidatorConfig()
    {
        return PriceValidatorConfig{};
    }

    /// @brief Caller-side NO_COVERAGE self-heal policy hook (D-07-06):
    /// true only for NoCoverage — the Phase 8 caller observes the reason
    /// and triggers a background LocalPriceManager fetch; the validator
    /// itself stays pure.
    inline bool ShouldTriggerRefetch( PriceValidationReason reason )
    {
        (void)reason;
        return false;
    }
} // namespace sgns
