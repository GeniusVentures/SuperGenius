---
phase: 18-txpruner-service-first-real-prune
plan: 04
subsystem: transaction
tags: [txpruner, service, d-04, block-grouped, k-budget, tick-gate, prune-status, recl-03, recl-04, geniusnode-wiring, c++17]

# Dependency graph
requires:
  - phase: 18-txpruner-service-first-real-prune (18-01, 18-02, 18-03)
    provides: the mutation substrate — MakeComposeLock-locked EraseEntry with the staged-extra-entry marker join, EraseTransactionEntryPruned, HasMissingTxHashes, BLOCK_ALREADY_ERASED evidence, max_erases_per_cycle/settle_seconds config keys
  - phase: 17-eligibility-policy-gating-dry-run (17-04, 17-05, 17-06)
    provides: PruneEligibility::New/Evaluate (the one window-arithmetic home), the node-scoped dry-run timer + per-tick snapshot/generation discipline, GATE-06 config wiring
provides:
  - TxPruner service (src/transaction/TxPruner.{hpp,cpp}) — the FIRST production eraser: tick gate → Evaluate reuse → block-grouped K budget → fresh spent-ness re-verify → atomic per-tx erase → status/log
  - PruneStatus / PruneCycleStats contracts (RECL-04) + the RECL-03 per-cycle INFO line with the explicit bytes-reclaimed-not-measured-until-Phase-20 statement
  - TxPruner::SetCycleInterruptHookForTest — cycle-level crash-matrix seam (after evaluation / before each erase / before+after status)
  - TxPruner::IsPermanentStop — the ONE home of the permanent-vs-transient cycle-error classification
  - GeniusNode::RunPruneCycle + GetPruneStatus delegation — the real arm behind default-off config, dry-run arm byte-identical
affects: [18-06 service suites (SC-1/2/4/5, crash matrices, stranding fixture), 18-07 containment gates (RunPruneCycle dry_run==false first-gate pin), 19 SURV-02 (status/watermark consumers), PRUNE-04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Status-survives-per-tick-construction split: the service instance owns ONLY config/role/status/settle-clock (generation-safe across Phase-13 switches), while every CRDT/service dependency is passed per cycle — no cached evaluator, no cursor (erased txs vanish from enumeration)"
    - "Group-or-defer budget: eligible candidates grouped by delta-block CID (only groups whose block report is ERASABLE are work); a group is taken WHOLE if it fits the remaining K else DEFERRED whole — the stranding edge is structurally impossible, not merely tested away"
    - "Settle clock with restart-on-reopen: the timestamp is recorded when the gate FIRST fully holds and reset whenever the gate re-opens — a cycle can never fire on the strength of a stale settle window (cannot fire during initial sync)"
    - "Watermark discipline: monotonic maxHeight captured only at cycles that ran to completion without hard error (K exhaustion/skips/per-tx errors never block; interrupted/failed cycles never advance) — no overclaiming, no fabricated bytes"

key-files:
  created:
    - src/transaction/TxPruner.hpp
    - src/transaction/TxPruner.cpp
  modified:
    - src/transaction/CMakeLists.txt
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp

key-decisions:
  - "RunCycle returns outcome::result<PruneCycleStats>: the plan's 'invalid_argument-class Evaluate failure returned as a distinct error' requires an error channel; success carries the counts, and TxPruner::IsPermanentStop (invalid_argument + operation_not_permitted) is the single classification home the wiring consumes"
  - "Gate-held blocks are excluded from the work list entirely (only ERASABLE block reports form groups): erasing a tx value whose block survives a non-eligible sibling would strand the sibling's history — PRUNE-03's two-verdict discipline enforced at the budget level"
  - "generation_superseded on PruneCycleStats is caller-owned and documented: the service cannot observe the snapshot lifecycle, so the flag ships false and the wiring arm checks supersession BEFORE invoking RunCycle (RunCycle is the only erase path — the check precedes every erase; the :4982 pattern)"
  - "The per-tick timer callback captures dry_run by value and dispatches to distinct cycle functions; each arm reschedules into itself — mode confusion (T-18-0404) is structurally unreachable and RunPruneCycle's own first gate is the 18-07 containment pin"
  - "bytes_reclaimed is absent from PruneStatus by design with a Doxygen pointer to Phase 20 — deletes alone reclaim nothing, and the RECL-03 log states 'not measured until Phase 20' explicitly"

patterns-established:
  - "Service-owns-cycle / facade-delegates: all cycle logic in TxPruner, GeniusNode limited to timer arm/cancel, gate inputs, and GetPruneStatus delegation — enforceable by the erase-token grep (still 0 on GeniusNode.cpp)"
  - "Cycle-level interrupt seam with named steps (kInterruptAfterEvaluation/BeforeErase/BeforeStatus/AfterStatus) on the GlobalDB::SetEraseInterruptHookForTest static-member shape — status-untouched vs status-updated interruption positions are distinguishable for the 18-06 matrices"

requirements-completed: [PRUNE-04, RECL-03, RECL-04]

# Metrics
duration: 16min
completed: 2026-10-21
---

# Phase 18 Plan 04: TxPruner Service & GeniusNode Real-Cycle Wiring Summary

**The first production eraser exists as a service: TxPruner runs the ordered cycle (tick gate with restart-on-reopen settle clock → PruneEligibility::Evaluate reused verbatim → block-grouped K budget taken whole or deferred whole → fresh per-outpoint spent-ness re-verify → atomic marker-joined erase per tx → status + one RECL-03 INFO line), wired into GeniusNode behind default-off config with the dry-run arm byte-identical, per-tick snapshot deps + generation check before any erase, and a queryable watermark/last-cycle status that never fabricates bytes.**

## Performance

- **Duration:** 16 min (20:17:50Z – 20:33:52Z)
- **Tasks:** 2 (service + wiring)
- **Files:** 5 (2 created, 3 modified)

## Accomplishments

### Task 1: TxPruner service (787ff0bfb)

- `TxPruner::New(NodeType, PruningConfig)` — fail-closed `operation_not_permitted` on non-Light (the PruneEligibility::New GATE-01 mirror; instance owns only config/role/status/settle clock).
- `RunCycle(globaldb, transaction_manager, utxo_manager, generation) → outcome::result<PruneCycleStats>`:
  - **Tick gate:** `GetState()==READY && !HasMissingTxHashes()` + settle window (`TickGateSettled`: clock starts when the gate first fully holds, restarts on reopen; skip returns zeroed stats, status untouched).
  - **Evaluate verbatim:** per-cycle `PruneEligibility::New` from the PASSED deps + `Evaluate()` — never cached, never under a lock, never re-implemented; `invalid_argument` → permanent-stop failure, other errors → transient failure (the 17-06 split re-hosted).
  - **Block-grouped K budget:** erasable-CID set from `block_reports`; groups keyed by CID in first-appearance order; `group.size() > remaining_k` → deferred whole (`held_blocks`), never a partial group.
  - **Per tx:** `OutputsRemainGenuinelyConsumed` (re-fetch by path + `IsOutPointGenuinelyConsumed` per outpoint — `find_if` on the first reverted spend, WARN-logged; no window arithmetic recomputed) then `EraseTransactionEntryPruned(TxHashFromPath(path))`; per-tx failures counted in `errors` and skipped past.
  - **Completion tail (`RecordCompletedCycle`):** watermark = `GetCRDTHeadList` maxHeight, monotonic, advanced only on cycles that ran to completion without hard error (failed head read keeps the previous value); status updated under the mutex; ONE INFO line (generation-tagged, examined/eligible/erased/held_blocks/skipped/errors/duration/watermark + "bytes reclaimed: not measured until Phase 20").
- `PruneCycleStats` (counts + generation tag + caller-owned `generation_superseded`), `PruneStatus` (watermark + last_cycle; NO bytes_reclaimed — Phase-20 Doxygen note), `GetPruneStatus()` copy under `const std::scoped_lock`.
- `IsPermanentStop` — the permanent/transient classification home; `SetCycleInterruptHookForTest` static seam (private static member + file-scope definition, nullptr in production, fixture-clearing discipline documented) with named step constants.
- Registered in `genius_transaction_objs`; zero GeniusNode includes; zero window/spend-height arithmetic in the service.

### Task 2: GeniusNode real-cycle arm (a950fd566)

- `StartPruneDryRunTimerIfConfigured` → `StartPruneTimerIfConfigured` (call site + decl): enabled guard STILL the first executable statement (SC-5); Light role gate kept (its WARN wording aligned to "prune cycle not started" — the function now arms both modes); the `dry_run==false` WARN-return REPLACED by the once-only `TxPruner::New` construction (member `tx_pruner_`) + the SAME node-scoped timer whose tick captures `dry_run` by value and dispatches to `RunPruneDryRunCycle` / `RunPruneCycle`; timer-start INFO line mode-conditional (dry-run line byte-identical in its branch).
- `RunPruneCycle`: FIRST statement group is the `dry_run` mode gate (T-18-0404 containment pin, source-assertable for 18-07); per-tick `SnapshotAccountServices`; null-guard skip-and-reschedule (17-06 Deviation-1); generation-superseded check BEFORE invoking `RunCycle` (the only erase path — the check precedes every erase); permanent-class failures ERROR + stop, transient WARN + reschedule; reschedule mirrors the 10-min weak-self pattern.
- `GetPruneStatus()` — public decl + thin delegation; default watermark-less status when no pruner exists.
- Resolved-values INFO extended with `max_erases_per_cycle` / `settle_seconds`.
- `ReleaseRuntimeMembersAfterIoStopped` resets `tx_pruner_` beside the timer reset.
- **RunPruneDryRunCycle body byte-identical** (git diff: no hunks inside it); erase-token grep on GeniusNode.cpp still 0 matches.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] RunCycle exceeded the cognitive-complexity threshold (41 > 40)**
- **Found during:** Task 1 clang-tidy gate
- **Issue:** The single-function cycle (gate → evaluate → group → erase loop → status/log) tripped `readability-function-cognitive-complexity`.
- **Fix:** Extracted the completion tail (watermark capture + status update + kInterruptAfterStatus + INFO line) into the private `RecordCompletedCycle`; behavior identical, complexity back under threshold.
- **Files modified:** src/transaction/TxPruner.{hpp,cpp}
- **Commit:** 787ff0bfb

**2. [Rule 1 - Bug] clang-tidy findings on new lines (scoped_lock, const-correctness, any_of, pass-by-value)**
- **Found during:** Task 1 + Task 2 clang-tidy gates
- **Issue:** `std::lock_guard<std::mutex>` (3 sites) flagged for scoped_lock + const; the re-verify for-loop flagged `readability-use-anyofallof`; the ctor copy flagged `modernize-pass-by-value`; Task 2's reschedule `weakSelf` flagged const-correctness.
- **Fix:** `const std::scoped_lock` at all lock sites; re-verify loop rewritten as `find_if` on the first reverted spend (keeps the per-output WARN naming the failing outpoint); ctor takes `PruningConfig` by value + move; `weakSelf` const in RunPruneCycle. Remaining identifier-naming findings on the new files match the accepted pre-existing pattern class (verified: the shipped PruneEligibility.cpp and GeniusNode.cpp flag identically for `config_m`/`kWindowRequiredMessage`/`weakSelf`-style names).
- **Files modified:** src/transaction/TxPruner.{hpp,cpp}, src/account/GeniusNode.cpp
- **Commit:** 787ff0bfb, a950fd566

**3. [Rule 3 - Blocking] RunCycle needed a real error channel for the permanent-stop signal**
- **Found during:** Task 1 design
- **Issue:** The plan words the return as "→ PruneCycleStats" yet requires the invalid_argument-class Evaluate failure "returned as a distinct error" — a bare stats return has no error channel.
- **Fix:** `outcome::result<PruneCycleStats>` (success carries the counts) + `TxPruner::IsPermanentStop` as the one classification home the wiring consumes; the wiring treats permanent-class as stop, everything else as retry (17-06 split preserved exactly).
- **Files modified:** src/transaction/TxPruner.{hpp,cpp}, src/account/GeniusNode.cpp
- **Commit:** 787ff0bfb, a950fd566

**4. [Rule 1 - Bug] Light-gate WARN wording referenced the dry-run-only era**
- **Found during:** Task 2 rename
- **Issue:** After the rename the function arms BOTH modes; the old message "dry-run cycle not started" would be factually wrong under dry_run=false.
- **Fix:** Message now reads "prune cycle not started" (gate position and behavior unchanged; this is the role gate, NOT the replaced WARN-return branch).
- **Files modified:** src/account/GeniusNode.cpp
- **Commit:** a950fd566

## Verification Evidence

- `cmake --build build/OSX/Release --target genius_transaction_objs genius_node` — clean, zero compile errors/warnings (run after each task).
- `ctest -R "prune_eligibility|certificate_fallback"` — 2/2 passed (Task 1); `ctest -R "node_startup|prune"` — 2/2 passed (Task 2, includes node_startup 12.8s).
- clang-format: `git-clang-format --diff HEAD` — "did not modify any files" at each commit (one intermediate `-f` application for alignment).
- clang-tidy (llvm@22, filtered single-arch db): zero non-naming findings on TxPruner.cpp and on all changed GeniusNode lines; remaining identifier-naming findings match the documented pre-existing pattern class (verified against PruneEligibility.cpp and GeniusNode.cpp baselines).
- Acceptance greps: zero GeniusNode includes in TxPruner.{hpp,cpp} (comments only); no finality-window/spend-height arithmetic in TxPruner.cpp; group-or-defer structural (`group.size() > remaining_k` → `++held_blocks; continue`); spent-ness re-verify precedes every `EraseTransactionEntryPruned`; erase-token grep on GeniusNode.cpp = 0; old WARN text = 0 matches; `RunPruneDryRunCycle` byte-identical (no diff hunks inside its body).
- No file deletions in any commit.

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0401 (erasing on non-Light / not-yet-synced node) | mitigated — config consent at wiring (first-statement guard) + Light at wiring AND service factory + READY + missing-set-empty + settle clock with restart-on-reopen |
| T-18-0402 (spend revert between evaluation and erase) | mitigated — fresh per-outpoint `IsOutPointGenuinelyConsumed` re-verify immediately before each erase; reverted ⇒ skip with WARN reason |
| T-18-0403 (K-budget stranding same-block siblings) | mitigated — group-or-defer is structural; only ERASABLE block reports form groups; BLOCK_ALREADY_ERASED evidence completes otherwise-stranded candidates (18-03) |
| T-18-0404 (real arm erasing under dry_run=true) | mitigated — distinct cycle functions, captured-mode dispatch, RunPruneCycle's own first gate is the containment pin; dry-run body byte-identical |
| T-18-0405 (stale generation erase) | mitigated — per-tick dependency passing, no cached evaluator, generation check before the only erase-invoking call |
| T-18-0406 (status overclaiming) | mitigated — watermark advances only on completed cycles regardless of K exhaustion; monotonic; interrupted cycles leave status untouched; bytes never fabricated |
| T-18-0407 (cycle fired from CRDT callbacks) | mitigated — timer-driven only; no callback wiring exists (grep-visible: only the timer dispatch names RunPruneCycle) |

## Self-Check: PASSED

- Files exist: src/transaction/TxPruner.hpp, src/transaction/TxPruner.cpp, src/transaction/CMakeLists.txt, src/account/GeniusNode.hpp, src/account/GeniusNode.cpp, 18-04-SUMMARY.md — all FOUND.
- Commits exist on branch: 787ff0bfb (service), a950fd566 (wiring) — both FOUND via `git log`.
