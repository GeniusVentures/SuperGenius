---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 25
subsystem: node lifecycle and transaction persistence
tags: [genius-node, transaction-manager, restart, callback-ownership, tdd]

requires:
  - phase: 13-23
    provides: passive burn convergence and current production startup-controller behavior
provides:
  - serialized and idempotent GeniusNode lifecycle transitions with stale trust-post suppression
  - stop-before-replace TransactionManager ownership across GlobalDB, account, blockchain, and bridge callbacks
  - historical same-path trust and transaction restart proof with exactly-one manager construction and start
affects: [phase-13-verification, plan-13-26, transaction-manager-lifetime, persisted-restart]

tech-stack:
  added: []
  patterns: [recursive lifecycle serialization, transition epoch validation, generation-owned callbacks, stop-before-replace]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-25-SUMMARY.md
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/src/multiaccount/multi_account_sync.cpp

key-decisions:
  - "GeniusNode serializes complete transitions with a recursive mutex so existing same-thread nested transitions remain valid while concurrent duplicate work is excluded."
  - "Posted trust transitions carry their source state and lifecycle epoch; ConfirmedReady may initialize transactions only from the two trust waiting states."
  - "TransactionManager replacement stops and releases prior bridge, state, slot-hash, GlobalDB, and account callback ownership before TransactionManager::New installs a generation-owned replacement."

patterns-established:
  - "Lifecycle post validation: capture epoch plus source state, validate both at delivery, then enter the target transition."
  - "Manager handoff: invalidate bridge work, unregister state/slot ownership, Stop, destroy old manager callbacks, then construct and start the new generation."

requirements-completed: [BOOT-04, BURN-02, TEST-01]

duration: 1h 31m
completed: 2026-08-14
---

# Phase 13 Plan 25: Idempotent Persisted-Ready Transaction Startup Summary

**Persisted-ready GeniusNode restart now constructs and starts exactly one TransactionManager while preserving historical transaction CIDs and generation-owned GlobalDB, account, blockchain, bridge, and burn-provider callbacks.**

## Performance

- **Duration:** 1h 31m
- **Started:** 2026-08-14T14:24:11Z
- **Completed:** 2026-08-14T15:54:41Z
- **Tasks:** 1 TDD task
- **Files modified:** 3 source/test files plus this summary

## Accomplishments

- Serialized `GeniusNode::StateTransition` across the complete state-specific operation while preserving synchronous nested transitions and suppressing same-state re-entry.
- Restricted `ConfirmedReady` transaction initialization to trust/burn waiting sources and made every posted trust transition reject stale epoch or source-state snapshots.
- Centralized TransactionManager handoff so bridge observers, state callbacks, blockchain slot-hash ownership, and the old manager's GlobalDB/account callbacks are released before replacement registration.
- Added construction/start and callback-owner generation diagnostics, with generation checks on manager state and slot-hash callbacks.
- Added the exact historical restart regression that reopens the same `historical_base_path`, reloads durable trust and a confirmed transaction/CID, and observes one new transaction exactly once without using the separate fresh-client workaround.

## Task Commits

The TDD task was committed atomically as RED then GREEN:

1. **Task 1 RED: Restore persisted historical restart counterexample** - `bb9c34a6` (test)
2. **Task 1 GREEN: Make GeniusNode transitions and manager ownership idempotent** - `b044289a` (fix)

## Verification Evidence

- RED build failed at the intended lifecycle contract: the test required five construction/start/owner-generation diagnostics absent from `GeniusNode` before GREEN.
- `cmake --build build/OSX/Release --target multi_account_test node_startup_test -j8` passed.
- Exact automated chain passed at exit `0`: the historical and retained fresh-client cases passed 2/2, followed by `NodeStartupTest.GenesisNodeDefaultBurnRateIsOnePercent` at 1/1.
- `MultiAccountTest.PersistedHistoricalTrustAndTransactionsRestartWithSingleManagerOwnership` passed independently at 1/1 in 8.8 seconds.
- Complete `multi_account_test` passed all 5 enabled tests in 146.9 seconds. Its unrelated pre-existing `DISABLED_CRDTFilterDuplicateTx` remains disabled; there is no disabled counterpart of the new historical test.
- Structural acceptance checks reported `acceptance_structure=PASS`, found one enabled exact historical case, found the retained fresh-client case, and verified transition epoch/source gating plus unregister/Stop/reset ordering.

## Files Created/Modified

- `src/account/GeniusNode.hpp` - Adds serialized transition state, lifecycle epoch, manager counters, callback owner generations, and the centralized ownership-release helper.
- `src/account/GeniusNode.cpp` - Enforces idempotent serialized transitions, stale trust-post rejection, generation-owned callbacks, and stop-before-replace manager construction.
- `test/src/multiaccount/multi_account_sync.cpp` - Adds the true same-path historical trust/transaction restart counterexample while retaining fresh-client registry recovery separately.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-25-SUMMARY.md` - Records implementation, TDD, and acceptance evidence.

## Decisions Made

- Transition serialization covers the full state action, not only `state_` mutation, because duplicate manager construction occurs inside the action body.
- A recursive mutex is intentional: established database, transaction, and processing paths synchronously enter their next state on the same thread.
- Manager callback generations supplement weak pointers so a retained or delayed prior owner cannot act as the current node manager.
- Lifecycle counters increment only after `TransactionManager::New` returns a valid manager and immediately after the single `Start` call.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test fixture bug] Used the fixture's configured transaction chain label**
- **Found during:** Task 1 GREEN
- **Issue:** The new historical test initially supplied descriptive strings as chain IDs, so its first mint was rejected before exercising restart behavior.
- **Fix:** Used the fixture's established `test` chain label for both historical and post-restart mint transactions.
- **Files modified:** `test/src/multiaccount/multi_account_sync.cpp`
- **Verification:** Historical-only, exact filtered, exact chained, and complete multi-account runs all passed.
- **Committed in:** `b044289a`

---

**Total deviations:** 1 auto-fixed bug.
**Impact on plan:** The correction keeps the test on the production transaction path and changes no product scope, dependency, storage schema, topic, or authority boundary.

## Issues Encountered

- Sandboxed test attempts could not open local libp2p listeners and exited during network initialization. All network-backed acceptance gates were rerun with listener permission without weakening or skipping coverage.
- Two GREEN commit approval reviews timed out before process launch. Splitting explicit staging from the standalone commit completed the required normal-hook commit safely.

## Known Stubs

None. The historical case uses real persisted trust/transaction stores, real confirmed transactions and CIDs, and live manager/provider/callback ownership.

## Threat Flags

None. This plan hardens existing lifecycle and callback boundaries; it adds no endpoint, authentication path, file-access boundary, schema, package, topic, or transport.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-10 is closed: persisted-ready startup cannot schedule or execute duplicate transaction initialization.
- The restart half of WR-07 is closed with true same-path historical trust and transaction evidence.
- Plan 13-26 can include the exact historical case in the final sanitizer-aware closure gate; Plans 13-24 and 13-26 remain incomplete.

## Self-Check: PASSED

- All three planned source/test files and this summary exist.
- RED commit `bb9c34a6` and GREEN commit `b044289a` exist in repository history in the required order.
- Every task acceptance criterion and plan verification command passed.
- No goal-blocking stub or unmodeled security surface was introduced.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
