---
phase: 19-pruned-node-survival-recovery-hardening
plan: 02
subsystem: transaction
tags: [transaction-manager, syncing-wedge, pre-broadcast, nonce-safety, bounded-exit, outcome-result]

# Dependency graph
requires:
  - phase: 18-risk-consumer-hardening
    provides: GATE-05 bounded-recovery ForTest seam precedent (missing_tx_max_attempts_for_test_) copied for the window knob
provides:
  - TransactionManager::Error::PRE_BROADCAST_VALIDATION + READY-preserving TickOnce branch (D-02 half 1)
  - Bounded SYNCING exit on the last durable confirmed nonce + SetSyncingExitTimeoutForTest seam (D-02 half 2)
  - PreBroadcast + SyncingExit regression suites incl. the nonce-collision proof
  - Closed live-incident todo failed-send-wedges-tm-in-syncing
affects: [19-06 (restart matrix rides the bounded SYNCING exit), SURV-01]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [typed error enum classification at the only boundary that knows (callee-side, before Commit), bounded-wait-with-durable-anchor state exit that never touches reservation state]

key-files:
  created: []
  modified:
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
    - .planning/todos/completed/failed-send-wedges-tm-in-syncing.md

key-decisions:
  - "Classification surface is the Error enum + its outcome category (adopted open question 2): errc comparison cannot work — invalid_argument/message_size fire on BOTH sides of crdt_transaction->Commit"
  - "Bounded-exit window 60s default (well above confirm cadence, far below infinite), static-atomic ForTest override mirroring the missing-tx seam shape exactly"
  - "SYNCING entry timestamp set at the one :581 wiring site (not in ChangeState) and cleared on both READY exits; read directly by the friend test access to prove the clears"
  - "Tests enter SYNCING through the REAL send-failure path (committed-CRD-transaction Put failure), not a direct state poke — arming syncing_since_ via the production wiring point"

patterns-established:
  - "Pre/post-broadcast classification: only the callee knows which side of Commit it failed on; the typed condition carries that fact to the caller's state decision"
  - "Fallback-exit nonce safety: change ONLY the sync anchor and the state; pending reservations stay held so GetNextNonceLocked's skip keeps nonces collision-free by construction"

requirements-completed: [] # SURV-01 intentionally left open — the restart-matrix proof is 19-06's deliverable (see Deviations)

# Metrics
duration: 22min
completed: 2026-10-23
---

# Phase 19 Plan 02: TM-SYNCING Send-Wedge Fix (D-02) Summary

**Pre-broadcast send failures now return a typed `Error::PRE_BROADCAST_VALIDATION` and leave the TransactionManager READY (rollback + nonce release, no SYNCING), and a SYNCING state with no confirming nonce advance exits within a bounded 60s window to the last durable confirmed nonce — reservations untouched — killing the live-incident wedge class on both halves.**

## Performance

- **Duration:** 22 min
- **Started:** 2026-10-23T18:50:32Z
- **Completed:** 2026-10-23T19:12Z
- **Tasks:** 3
- **Files modified:** 4

## Accomplishments
- `Error::PRE_BROADCAST_VALIDATION` (enum + category message); the three preflight sites (size `:2060`-class, commitment, witness) return the typed condition; the three post-Commit sites keep their errc codes byte-identical (they genuinely entered SYNCING territory)
- TickOnce: the PRE_BROADCAST branch sits between the untouched retryable branch and the SYNCING transition — WARN naming tx + error, `RollbackTransactions`, pop, break, NO state change (the FAILED transition releases the reserved nonce for free at `:5196`-class site)
- `syncing_since_` armed immediately before the `ChangeState(State::SYNCING)` call; SyncNonce checks the window at function TOP (before `GetConfirmedNonce`): elapsed past the knob ⇒ WARN (window + durable anchor value + READY return), reset, READY; both READY exits clear the timestamp
- Seam: `k_syncing_exit_timeout` = 60s + `syncing_exit_timeout_for_test_` static atomic + `Set/GetSyncingExitTimeoutForTest` — the `missing_tx_max_attempts_for_test_` shape verbatim including the "No production caller may invoke this" doc
- Suites (all double-run green): `PreBroadcastFailureKeepsManagerReadyWithNonceRelease` (oversized-payload driver; FAILED + READY + nonce reuse + subsequent SENDING), `PreBroadcastNegativeControlNonTypedFailureStillEntersSyncing`, `SyncingExitFallsBackToDurableNonceAfterWindow` (READY within bounded wait, anchor unchanged, held reservation survives — next reserved nonce strictly greater), `SyncingExitNormalSyncPathDoesNotDependOnWindow` (normal exit beats the window and clears the arm)
- Folded todo `failed-send-wedges-tm-in-syncing` moved to completed with the D-02 resolution note naming both halves and the four regression tests

## Task Commits

Each task was committed atomically:

1. **Task 1: typed pre-broadcast error + READY-preserving TickOnce branch** - `6cbe22b45` (feat)
2. **Task 2: bounded SYNCING exit anchored to durable confirmed nonce** - `4d104d43d` (feat)
3. **Task 3: PreBroadcast + SyncingExit suites; close send-wedge todo** - `451f609cd` (test)

## Files Created/Modified
- `src/transaction/TransactionManager.hpp` — Error value + doc, `syncing_since_` member, knob + seam declarations
- `src/transaction/TransactionManager.cpp` — category case, three typed preflight returns, TickOnce branch + entry timestamp, seam definitions, SyncNonce bounded exit + clears
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` — access-class forwarders (`SetSyncingExitTimeout`/`Restore`/`SyncingWindowIsArmed`), RAII window guard, 4 new tests; Doxygen header extended (1174 lines)
- `.planning/todos/completed/failed-send-wedges-tm-in-syncing.md` — resolution note appended (moved out of pending/)

## Decisions Made
- Adopted open-question default: the Error enum + its category IS the classification surface (checker-verified alternative — an errc table — cannot disambiguate across Commit)
- Deterministic preflight driver = the size gate (70KB uncle_hash): the fixture's controllable validator returns `RequiresConsensusUTXOData()==false`, so the witness gate has no driver here — plan's documented fallback choice
- Tests arm `syncing_since_` through the real failure wiring (the seam's purpose is only shrinking the window), so the tests prove the wiring point, not just the exit
- Single-tick READY assertion is the wedge discriminator (SYNCING would resolve only on a LATER sync pass)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Test name did not match the checker's NonceRelease backstop**
- **Found during:** Task 3 verify (`*MissingCreatingTx*:*NonceRelease*` matched only the old test)
- **Issue:** `...ReadyAndReleasesNonce` does not contain the substring `NonceRelease`; the T1/T2 backstop filter ran vacuous on the new coverage
- **Fix:** Renamed to `PreBroadcastFailureKeepsManagerReadyWithNonceRelease` (matches `*PreBroadcast*` AND `*NonceRelease*`)
- **Files modified:** test/src/account/transaction_manager_pending_lifecycle_test.cpp
- **Verification:** backstop filter now runs 2 tests, green
- **Committed in:** `451f609cd`

**2. [Rule 1 - Process] SURV-01 requirement left open despite the plan frontmatter listing it**
- **Found during:** state updates
- **Issue:** `requirements.mark-complete SURV-01` (mechanical, per frontmatter) marked "restart matrix on a pruned node passes — READY with zero pruned-CID fetch attempts..." complete; that proof is 19-06's restart-matrix deliverable — this plan only removes one of its failure modes. Sibling precedent: 19-01 listed GATE-05 and left it open for the same reason
- **Fix:** Reverted both the checkbox and the traceability row to Pending; 19-02 recorded as *affects* SURV-01 instead
- **Files modified:** .planning/REQUIREMENTS.md
- **Verification:** SURV-01 Pending in both places

---

**Total deviations:** 2 auto-fixed (1 Rule 3 blocking, 1 Rule 1 process)
**Impact on plan:** None on code; state files stay truthful for wave 2.

## Issues Encountered
- clang-tidy: one new `bugprone-throwing-static-initialization` on the `syncing_exit_timeout_for_test_` static — the same class already fires 4x in this file (`kGeniusValidatorRegistered` x3, `transaction_parsers`); std::atomic's non-noexcept ctor makes the check conservative. Accepted as established idiom (the seam shape is mandated by the plan)
- Cognitive-complexity findings (TickOnce 42, SendTransactionItem 63) identical before/after — no threshold crossed by the new code
- A2 assumption VERIFIED before relying on the free nonce release: `SetPeerConfirmedNonce` is called in TransactionManager.cpp ONLY at the CONFIRMED arm of `ChangeTransactionState` (both sites are confirmation paths); `RollBackPeerConfirmedNonce` only at cert/validation/FAILED-from-CONFIRMED paths. No send-path baseline advance exists beyond `ReserveNextNonce` at enqueue ⇒ no explicit rollback needed in the READY-preserving branch
- gtest filter note: `--gtest_filter='*PreBroadcast*:*SyncingExit*'` green twice consecutively; full binary 22/22; `ctest -R transaction_manager_pending_lifecycle` passed (7.49s)

## Threat Mitigations (per plan threat_model)
- **T-19-0201** (nonce collision via fallback): half-2 diff contains zero ReleaseNonce/ReserveNextNonce/pending_nonces_ lines (grep-proven); collision test holds a reservation across the fallback fire and asserts the next reserved nonce is strictly greater — DONE
- **T-19-0202** (misclassification): typed condition instead of errc comparison; post-Commit sites unchanged; negative-control test proves non-typed failures still enter SYNCING — DONE
- **T-19-0203** (wedged-TM DoS): the incident regression pair (PreBroadcast stays READY + SyncingExit bounds the wait) — DONE
- **T-19-0204** (silent recovery): the WARN names window (ms), durable anchor value (or "none"), and the READY return — DONE

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 19-06's restart matrix can rely on the bounded SYNCING exit: a pruned-node restart that wedged SYNCING now self-recovers to READY within 60s on the durable anchor
- The PRE_BROADCAST branch keeps preflight-failing sends from ever polluting nonce recovery, so the restart matrix's outgoing-nonce-chain assertions see only real publication attempts
- Wave-2 note for 19-05/19-06: the `SyncingExitWindowGuard` RAII idiom (test file) is the pattern for any test that shrinks the window — the knob is process-wide static and MUST be restored

## Self-Check: PASSED

All 4 modified files exist; all 3 task commits (6cbe22b45, 4d104d43d, 451f609cd) present in git log; `PRE_BROADCAST` present in hpp; `SyncingExit` present in cpp; test artifact 1174 lines (min 80 satisfied); todo absent from todos/pending.

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
