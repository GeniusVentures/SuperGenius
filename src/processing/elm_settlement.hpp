/**
 * @file       elm_settlement.hpp
 * @brief      ELM settlement arithmetic: per-subtask window parsing, the
 *             proportional-by-window split, and the refund remainder
 *             (elmbridge Phase 4, plan 04-04; 01-DESIGN-SETTLEMENT §2/§3
 *             implemented verbatim as a pure, deterministic unit).
 * @date       2026-09-14
 */

#ifndef SGNS_PROCESSING_ELM_SETTLEMENT_HPP
#define SGNS_PROCESSING_ELM_SETTLEMENT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <outcome/sgprocmgr-outcome.hpp>

namespace sgns::processing
{
    /// @brief One subtask's worker-attested measurement window (§1).
    struct ElmSubtaskWindow
    {
        std::string subtaskid;        ///< the queue's subtask key (envelopes carry work_item_id — the CALLER maps)
        int64_t     grab_time_usec   = 0; ///< ProcessSubTask entry, before any fetch (model download included)
        int64_t     finish_time_usec = 0; ///< envelope publication (assembly)
    };

    /// @brief The settlement input the payout path consumes (04-05 wires it
    ///        from fetched envelope artifacts keyed by subtaskid).
    struct ElmSettlementData
    {
        std::vector<ElmSubtaskWindow> windows;
    };

    /// @brief One output share of the ELM split (peer-side; the caller applies
    ///        the developer cut the same way the even-split branch does).
    struct ElmShare
    {
        std::string subtaskid; ///< owning subtask (caller resolves peer/developer metadata)
        uint64_t    minions = 0; ///< this window's share of the billable pool
    };

    /// @brief The full ELM split result: shares + refund + the arithmetic's
    ///        intermediate values (dispute reference per §2).
    struct ElmSplitResult
    {
        std::vector<ElmShare> shares;      ///< one per WELL-FORMED window (malformed excluded, §3.3)
        uint64_t              billableMinions = 0; ///< min(Σ*3/10 floor, escrow_max) (§2, cap §D-03)
        uint64_t              refundMinions   = 0; ///< escrow_max − billable (returns to the escrow source)
    };

    /// @brief Window duration in milli-hours (§2): llround(clamped-ms / 3600000.0 * 1000.0).
    ///
    /// Negative or zero deltas yield 0 (never negative). llround guards the
    /// 1299.999... → 1299 truncation (the ElmEscrowMinions convention).
    /// @param w - the window
    /// @return milli-hours, clamped >= 0
    uint64_t ElmWindowMillihours( const ElmSubtaskWindow &w );

    /// @brief The §2/§3 core: billable pool, proportional shares, refund.
    ///
    /// billable_millihours = Σ well-formed windows' milli-hours;
    /// billable_minions = min(billable_millihours * 3 / 10 (uint64 floor),
    /// escrow_max_minions) — the identical integer path as ElmEscrowMinions
    /// ($0.0003/h × 10^6 minions/$ at kUsdPerGnusRate == 1.0);
    /// share_i = billable_minions * window_millihours_i / billable_millihours
    /// (uint128, floor); the split remainder distributes one minion at a time
    /// to the LARGEST-window entries, ties broken by lexicographic subtaskid
    /// (§3.2). Malformed/zero windows are excluded from Σ and from shares
    /// (§3.3). refund = escrow_max − billable.
    ///
    /// Invariant: Σ(shares) + refund == escrow_max_minions (uint128-safe for
    /// every input; conservation is the caller's final check).
    ///
    /// @param windows - the per-subtask windows (well-formed subset used)
    /// @param escrow_max_minions - the escrow hold amount (the cap)
    /// @return the split; EMPTY shares when no window is well-formed (the
    ///         caller's all-malformed refusal path)
    ElmSplitResult ElmWindowsToShares( const std::vector<ElmSubtaskWindow> &windows,
                                       uint64_t                             escrow_max_minions );

    /// @brief Parse one window out of a fetched envelope artifact's bytes
    ///        (the malformed-entry filter's input; NEVER throws).
    ///
    /// The envelope carries work_item_id (NOT subtaskid) — the caller supplies
    /// the subtask key. Reads the numeric grab_time_usec/finish_time_usec
    /// keys; malformed JSON, missing keys, or non-numeric stamps → nullopt.
    /// @param envelopeBytes - the fetched artifact bytes
    /// @param subtaskid - the queue-side key to stamp onto the window
    /// @return the window, or nullopt when the envelope is malformed
    std::optional<ElmSubtaskWindow> ElmWindowFromEnvelopeJson( const std::vector<uint8_t> &envelopeBytes,
                                                               const std::string          &subtaskid );
} // namespace sgns::processing

#endif // SGNS_PROCESSING_ELM_SETTLEMENT_HPP
