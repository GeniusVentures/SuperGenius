---
phase: 17-eligibility-policy-gating-dry-run
plan: 06
subsystem: account
tags: [prune-dry-run, gate-06, sgns-config, asio-timer, sc-5, c++17]

# Dependency graph
requires:
  - phase: 17-eligibility-policy-gating-dry-run (17-04)
    provides: PruningConfig::FromJSON (the one parse home) + PruneEligibility::New/Evaluate contracts
  - phase: 17-eligibility-policy-gating-dry-run (17-05)
    provides: PruneEvaluationResult::block_reports — the dry-run summary's second verdict family
provides:
  - GATE-06 production surface: the sgns_config.json "pruning" object read in LoadSgnsConfig (default off, absent keys byte-identical to today, resolved values INFO-logged once, fail-closed disable WARNed)
  - The operator-visible dry-run cycle: a config-gated repeating asio timer beside TransactionManager::New that evaluates per tick from live members and emits the D-03 per-cycle INFO summary (candidates + eligible/ineligible + blocks erasable/held) — zero writes anywhere
affects: [18-txpruner (replaces the dry-run timer with the real TxPruner service; reuses the per-tick construction discipline and the config wiring), operators (sgns_config.json pruning keys go live)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Node-scoped timer under replaceable dependencies: the timer member is created once and never reassigned until shutdown (io stopped, handlers drained), so an io-thread reschedule through the member can never race a generation-switch recreation — per-tick construction from live members tracks Phase-13 replacement without restarting the cadence"
    - "Absent-key byte-identity is structural: the entire config read (parse delegation, WARN, INFO) sits inside the single HasMember+IsObject guard, so the absent path executes zero new statements (SC-5)"
tech-patterns: []

key-files:
  created: []
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp

key-decisions:
  - "The dry-run timer is node-scoped, not generation-scoped: StateTransition reaches the TransactionManager::New wiring once per account generation, so StartPruneDryRunTimerIfConfigured is idempotent (an already-running timer returns) — the evaluator tracks generation switches because it is constructed per tick from the live members, and the timer member is never reassigned mid-flight"
  - "RunPruneDryRunCycle skips-and-reschedules (debug log) when tx_globaldb_/transaction_manager_/account_ is momentarily unset during a generation switch — a read-only report must survive a transient lifecycle gap rather than crash or die"
  - "An Evaluate() error (like a construction error) stops the cycle with ERROR: a broken policy state is not transient, so the timer must not spin on it every 10 minutes (fail closed)"
  - "finality_window logs as value_or(0): enabled-without-window is already disabled+WARNed by the 17-04 fail-closed rule, so window=0 only ever appears beside enabled=false"
  - "GATE-06 checkbox NOT ticked in REQUIREMENTS.md by this plan (17-01..05 phase-close precedent): tick at phase close"

patterns-established:
  - "Config-gated repeating timer wiring in GeniusNode: first executable statement is the enabled guard (source-assertable), role and mode guards inside, gc_timer_ weak-self pattern verbatim at both schedule sites, shutdown cancellation mirrored beside gc_timer_ in both ShutdownForDestruction and ReleaseRuntimeMembersAfterIoStopped"

requirements-completed: []  # GATE-06 delivered here; ticked at phase close per 17-01..05 precedent (see Decisions)

# Metrics
duration: 55min
completed: 2026-10-19
---

# Phase 17 Plan 06: GATE-06 Config Wiring + Production Dry-Run Cycle Summary

**The sgns_config.json `pruning` object now gates a read-only repeating timer in GeniusNode — default off with absent keys byte-identical to today (the whole read, WARN and INFO included, sits inside one HasMember guard), enabled-without-window fail-closed with the 17-04 WARN, Light-only at the wiring AND at evaluator construction, and a per-tick evaluator built from live members emitting the D-03 candidates+blocks INFO summary — no new service, no write of any kind**

## Performance

- **Duration:** 55 min (wall time dominated by the full-suite gate: 1131s ctest + full rebuild)
- **Started:** 2026-10-19T16:06:29Z
- **Completed:** 2026-10-19T17:01:10Z
- **Tasks:** 3
- **Files modified:** 2 (0 created)

## Accomplishments
- `LoadSgnsConfig` pruning block (GeniusNode.cpp:473-495, beside the node_type/Archive cluster): `HasMember("pruning") && IsObject()` guard, delegation of every member-key parse to `PruningConfig::FromJSON` (the 17-04 one parse home — zero per-key rapidjson parsing duplicated), `disabled_reason` WARN (carries the 17-04 fixed message verbatim), then ONE resolved-values INFO (`pruning enabled=, finality_window=, dry_run=`) in the established format. Absent keys: zero new statements execute (SC-5)
- `GeniusNode.hpp`: `#include "transaction/PruneEligibility.hpp"`, `PruningConfig pruning_config_;` beside `node_type_`, `prune_dry_run_timer_` beside `gc_timer_` (:1168), Doxygen'd `StartPruneDryRunTimerIfConfigured` / `RunPruneDryRunCycle` decls beside the GC pair
- `StartPruneDryRunTimerIfConfigured` (GeniusNode.cpp:4862): first executable statement = `if (!pruning_config_.enabled) return;` (SC-5); then the Light role gate (GATE-01 at the wiring — WARN + return, evaluator re-checks at construction, defense in depth); then `dry_run=false` WARN naming the Phase-18 TxPruner and returning (real erasure is not this phase); then the idempotence guard; then the timer on `*io_` with the FIRST cycle at `std::chrono::minutes(1)` via the gc_timer_ weak-self pattern (expires_from_now + async_wait, ec-early-return, weakSelf.lock before the member call)
- `RunPruneDryRunCycle` (:4913): per-tick `PruneEligibility::New(*tx_globaldb_, *transaction_manager_, account_->GetUTXOManager(), node_type_, pruning_config_)` — NEVER cached (Phase-13 replaceable TransactionManager); generation-gap members skip the tick and keep the cadence; construction failure ERROR + stop (fail closed); `Evaluate()` success emits ONE INFO summary — candidates / eligible / ineligible / blocks erasable / blocks held (both 17-04 and 17-05 verdict families, D-03's production mode); reschedule at `std::chrono::minutes(10)` with the same weak-self discipline
- Wiring call at the TransactionManager::New site (:1157, StateTransition INITIALIZING_TRANSACTIONS after the construction-success check); shutdown cancellation mirrored beside gc_timer_ in BOTH `ShutdownForDestruction` (cancel, ec-ignored) and `ReleaseRuntimeMembersAfterIoStopped` (cancel + reset while io still lives)
- Zero erase/write tokens in GeniusNode.cpp (grep gate: EraseEntry|EraseTransactionEntry|PutRaw|StageErase|StageBlock|DeleteKey|DeleteTransaction = 0 matches); no CRDT callback wiring anywhere (D-C6 — the cycle runs on committed state only)

## Task Commits

Each task was committed atomically:

1. **Task 1: LoadSgnsConfig pruning block + members** - `ae6ab9a5f` (feat)
2. **Task 2: Dry-run timer wiring + per-cycle INFO summary (zero-write proof)** - `cf2d4d639` (feat)
3. **Task 3: SC-5 regression gate (verification-only, no diff)** - no commit (16-03 Task 3 precedent)
4. (follow-up) canonical clang-format over the plan diff - `6b7799cbf` (style)

## Files Created/Modified
- `src/account/GeniusNode.hpp` — PruneEligibility include, pruning_config_ + prune_dry_run_timer_ members, two method decls with Doxygen
- `src/account/GeniusNode.cpp` — LoadSgnsConfig pruning block, wiring call at the TM::New site, StartPruneDryRunTimerIfConfigured + RunPruneDryRunCycle (new "── Prune Dry-Run ──" section after RunResultGC), shutdown cancels x2

## Decisions Made
- **Idempotent timer start:** the TM::New wiring site executes once per account generation (Phase-13 switching), so a naive start would destroy-and-recreate the timer under a running handler. The guard makes the timer node-scoped: created once, never reassigned until after io shutdown — no member race is representable, and the per-tick evaluator construction tracks generation replacement anyway.
- **Skip-and-reschedule on generation gaps:** a read-only report must not die (or crash) because a tick landed inside a member swap; debug-log + reschedule is the honest minimal behavior.
- **Evaluate failure stops the cycle:** the only defined failure is windowless-invalid_argument, which FromJSON's fail-closed rule already prevents at enable time — reaching it means a broken policy state; ERROR + stop (fail closed), not a 10-minute error spin.
- **Tidy gate interpretation:** the only changed-line findings are `weakSelf` naming/const (fires IDENTICALLY at the copied gc_timer_ precedent, GeniusNode.cpp:4733) and `config_json[...]` unchecked-access (the D-C5 convention every existing config block uses); copying a mandated pattern verbatim cannot introduce a "new" finding class. Everything else in the 18k file-wide findings is pre-existing legacy/generated noise, unchanged by this diff.
- **requirements.mark-complete deliberately not run** (phase-close tick precedent from 17-01..05).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Correctness] Generation-switch null-deref window in the cycle**
- **Found during:** Task 2
- **Issue:** the plan's cycle spec constructs from `tx_globaldb_/transaction_manager_/account_` unconditionally; Phase-13 generation switching can momentarily leave a member null when the 10-minute tick lands inside the swap — a null dereference on the io thread.
- **Fix:** null-guard the three members; skip the evaluation (debug log) and reschedule — the next tick sees the completed generation.
- **Files modified:** src/account/GeniusNode.cpp
- **Commit:** cf2d4d639

**2. [Rule 2 - Correctness] Timer recreation race at generation switches**
- **Found during:** Task 2
- **Issue:** the wiring site runs once per account generation; without a guard, a switch would reassign `prune_dry_run_timer_` (destroying the old timer) while an io thread could be rescheduling through the same member — a data race the gc_timer_ precedent never faces (it starts once).
- **Fix:** `if (prune_dry_run_timer_) return;` after the three config/role guards (the first-executable-statement acceptance criterion still holds — the enabled guard remains first). After first creation the member is only reset after io shutdown, so no mid-flight reassignment exists.
- **Files modified:** src/account/GeniusNode.cpp
- **Commit:** cf2d4d639

**3. [Rule 1 - Bug] Non-canonical formatting on changed lines**
- **Found during:** Task 3 format gate
- **Issue:** alignment of the timer assignment and the summary locals was off canonical form.
- **Fix:** git-clang-format applied over the plan diff; re-gate reports no modifications; rebuilt and re-verified.
- **Files modified:** src/account/GeniusNode.cpp, src/account/GeniusNode.hpp
- **Commit:** 6b7799cbf

**Total deviations:** 3 auto-fixed (2 Rule 2 correctness, 1 Rule 1 formatting)
**Impact on plan:** Local — both Rule 2 fixes are the minimal safe shapes of the plan's own "construct per tick from live members" and "config-gated timer" requirements under the Phase-13 lifecycle the plan itself cites.

## Verification Log

| Gate | Result | Notes |
|------|--------|-------|
| Task 1: build genius_node | exit 0, zero warnings — PASS | |
| Task 1: grep pruning_config_\\|prune_dry_run_timer_ (hpp/cpp) | 2 / 6 — PASS | |
| Task 1: absent-key no-op (WARN+INFO inside the HasMember guard) | both lines :483-488 inside guard at :478 — PASS | grep-able containment |
| Task 1: disabled_reason WARN carries the 17-04 fixed message | reason string logged verbatim from the config — PASS | |
| Task 2: build genius_node | exit 0, zero warnings — PASS | |
| Task 2: erase-token grep GeniusNode.cpp (EraseEntry\\|EraseTransactionEntry\\|PutRaw\\|StageErase\\|StageBlock\\|DeleteKey\\|DeleteTransaction) | 0 matches — PASS | |
| Task 2: first executable statement of StartPruneDryRunTimerIfConfigured is the enabled guard | :4866 guard; Light :4872, dry_run :4879, timer creation :4894 all follow — PASS | line-proven |
| Task 2: weak-self pattern at both schedule sites | :4900-4907 (first cycle) and :4975-4982 (reschedule) — PASS | |
| Task 2: shutdown cancels beside gc_timer_ | ShutdownForDestruction + ReleaseRuntimeMembersAfterIoStopped (cancel+reset) — PASS | |
| Task 3: full default build `cmake --build build/OSX/Release` | exit 0; zero errors; warnings only in PRE-EXISTING test files (registration_transaction_test kFundAmount lambda-captures, node_startup_test missing-field-init) — none in changed files — PASS | out-of-scope pre-existing per scope boundary |
| Task 3: full ctest suite `ctest --test-dir build/OSX/Release --output-on-failure -j8` | **100% tests passed, 0 failed out of 152**, exit 0 (1131s; requires_foundry family 9/9 green this run — no isolated re-run needed) — PASS | SC-5: default-off code compiled in, whole surface unregressed |
| Task 3: erase-caller inventory 1 — EraseEntry production callers | globaldb.cpp:797 (definition) + TransactionManager.cpp:4265 (the ONE composition) — unchanged — PASS | |
| Task 3: erase-caller inventory 2 — EraseTransactionEntry production callers | ZERO callers in src/ (only the definition, TransactionManager.cpp:4244; tests only elsewhere) — unchanged — PASS | |
| Task 3: erase-caller inventory 3 — DeleteKey/DeleteTransaction in src/transaction/ + GeniusNode.cpp | 6 matches, ALL pre-existing legacy CRDT-tombstone paths in TransactionManager — byte-identical set vs the phase-16 base commit (same 6 sites); ZERO in the pruning surface — PASS | |
| Task 3: erase-caller inventory 4 — WritePrunedEntryForTest production callers | ZERO outside PruneEligibility.{hpp,cpp} — PASS | |
| Task 3: format gate `git-clang-format --diff 9596483f7` (llvm@22) | initially 2 non-canonical regions → applied → "clang-format did not modify any files" — PASS | |
| Task 3: tidy gate (clang-tidy 22, single-arch filtered db, GeniusNode.cpp) | zero findings on changed lines beyond the copied-pattern precedents: weakSelf naming/const fires identically at gc_timer_ :4733; config_json[] unchecked-access matches every existing config block; `:1160 owner_generation` is pre-existing shifted code — PASS | 18,707 file-wide findings all pre-existing legacy/generated noise (17-04/17-05 same posture) |
| Task 3: stub scan on the plan diff (TODO/FIXME/placeholder/coming soon/not available) | 0 — PASS | |
| Task 3: post-commit deletion check (all 3 commits) | 0 tracked deletions; untracked = pre-existing supergenius_* runtime DB dirs only — PASS | |
| Build-system pre-PR checklist | no build wiring touched (source-only in-place extension) — PASS | |

## Threat Model Disposition

- **T-17-0601 (default-on foot-gun):** mitigated — default off; absent keys execute zero new statements (whole read inside one guard); enabled-without-window fail-closes in FromJSON and WARNs here; resolved values INFO-logged once; proven by 17-04 PruningConfig tests + this plan's textual containment gates.
- **T-17-0602 (NodeType-default-Light as consent):** mitigated — the config object is the consent gate; NodeType checked at the wiring (WARN + return) AND at evaluator construction (operation_not_permitted); defense in depth holds.
- **T-17-0603 (dry-run writing anything):** mitigated — the cycle calls Evaluate + INFO only; erase/stage/PutRaw/Delete token grep on GeniusNode.cpp = 0 matches; the evaluator is read-only by its 17-04 construction.
- **T-17-0604 (evaluation on the CRDT callback path):** mitigated — timer-driven cycle only; no CRDT callback wiring exists in the diff (grep-visible: the only async_wait sites are the two timer schedules).
- **T-17-0605 (stale TransactionManager after generation switch):** mitigated — per-tick construction from live members, no cached evaluator; the generation-gap null-guard (Deviation 1) and the idempotent node-scoped timer (Deviation 2) close the two lifecycle races the plan's constraint implied.

## Issues Encountered
None blocking. One environment note: the full rebuild after the GeniusNode.hpp include change is expensive (widely included header — ~15 min) and the full suite adds ~19 min; both are the SC-5 gate's cost, not defects. Pre-existing test-file warnings (registration_transaction_test, node_startup_test) were observed in the full build and left untouched per the scope boundary.

## User Setup Required
None — the pruning keys are opt-in; an operator who wants the dry-run report adds to sgns_config.json:
```json
{ "pruning": { "enabled": true, "finality_window": 100, "dry_run": true } }
```
Any config without the `pruning` object behaves exactly as before (SC-5).

## Next Phase Readiness
- Phase 18's TxPruner replaces the dry-run timer with the real service: it reuses `PruneEligibility::Evaluate()` verbatim for its work list, consults `block_reports` before any block deletion, joins the marker write to the erase WriteBatch, and inherits this plan's config surface (dry_run=false then becomes meaningful).
- The per-tick construction discipline and the shutdown cancellation pattern are the direct templates for the TxPruner's lifecycle under Phase-13 generation switching.
- Phase 17 is now 6/6 plans executed — ready for phase close (requirement checkbox ticking, PR, code review).

## Self-Check: PASSED

- Files exist: src/account/GeniusNode.hpp, src/account/GeniusNode.cpp (modified, commits verified) — FOUND
- Commits exist: ae6ab9a5f, cf2d4d639, 6b7799cbf — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
