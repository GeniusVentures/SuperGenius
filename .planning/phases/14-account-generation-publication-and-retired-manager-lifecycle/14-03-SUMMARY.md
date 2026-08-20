---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "03"
subsystem: account transaction-manager production integration
tags: [cxx17, transaction-manager, genius-node, lifecycle, migration]
requires:
  - phase: 14-01
    provides: manager admission gate and retirement ledger
  - phase: 14-02
    provides: generation-tagged GeniusNode account lifecycle
provides:
  - checked direct transaction submission for production GeniusNode callers
  - drained-before-stop account replacement sequencing
  - audited migrated production manager-caller partition
affects: [14-04, 14-05, manager-caller-inventory]
tech-stack:
  added: []
  patterns: [checked submission, drain callback, declaration-driven caller audit]
key-files:
  created: []
  modified: [src/account/TransactionManager.hpp, src/account/TransactionManager.cpp, src/account/GeniusNode.hpp, src/account/GeniusNode.cpp, .planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-manager-caller-inventory.tsv, .planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-manager-caller-audit.py]
key-decisions:
  - "Direct transaction-and-proof submission crosses TransactionManager admission through the typed SubmitTransaction result rather than the legacy enqueue shim."
  - "Selection closes admission outside the node lifecycle mutex and waits for the retired manager's terminal ledger to drain before destructive teardown."
requirements-completed: [D-05, D-06, D-13, D-14, D-15]
duration: 20 min
completed: 2026-08-20T14:30:18Z
---

# Phase 14 Plan 03: Production manager caller migration Summary

Production GeniusNode, BridgeRelayer, and migration manager callers now have target-mapped lifecycle ownership, while direct transaction submission and account replacement preserve typed retirement and admitted-work terminalization.

## Accomplishments

- Reconciled the production caller manifest with source and compile-command targets; the audit now detects caller drift, duplicate ownership, missing target coverage, and unmigrated rows.
- Replaced GeniusNode's direct `EnqueueTransaction` call with checked `SubmitTransaction` result propagation.
- Made account selection close old admission outside the node lock and delay old-manager stopping until all admitted operations have drained.
- Marked every 14-03 caller row migrated; BridgeRelayer's existing `MintFunds` result handling and migration's friend-owned startup/read/submission path already met the migrated contract.

## Verification

- Caller current/owner/migrated inventory checks — PASS.
- `cmake --build build/OSX/Release --target genius_node genius_node_test migration -j8` — PASS.
- Structural guard confirmed `SendTransactionAndProof` uses `SubmitTransaction`, contains no `EnqueueTransaction`, closes admission during selection, and retains generation-bound manager construction — PASS.

## Task Commits

1. **Task 1: Reconcile production caller rows with declarations and owning targets** — `20dad8ff` (chore)
2. **Task 2: Migrate production callers and freshly build their targets** — `19577940` (feat)

## Files Created/Modified

- `src/account/TransactionManager.hpp` / `src/account/TransactionManager.cpp` — typed direct submission and private drain notification for the lifecycle owner.
- `src/account/GeniusNode.hpp` / `src/account/GeniusNode.cpp` — checked transaction forwarding and drain-before-teardown replacement sequencing.
- `14-manager-caller-inventory.tsv` — current production rows marked migrated without changing later-plan rows.
- `14-manager-caller-audit.py` — deterministic current, owner, and migration completeness checks.

## Decisions Made

- `SubmitTransaction` is the production checked replacement for direct `EnqueueTransaction`; the compatibility shim remains for Plan 14-05 to retire.
- The retiring manager remains strongly owned through drain; its `Stop()` occurs only in the generation teardown reached after its terminal ledger is empty.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added a private drained callback before old-manager teardown**
- **Found during:** Task 2
- **Issue:** `SelectAccount()` closed admission while holding `lifecycle_mutex_` and then immediately stopped the retired manager, which could cancel admitted work before its required terminal delivery.
- **Fix:** Added `TransactionManager::OnDrained` for its GeniusNode friend, moved admission closure outside the node mutex, and start replacement teardown only from the drained callback.
- **Files modified:** `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`, `src/account/GeniusNode.hpp`, `src/account/GeniusNode.cpp`
- **Verification:** All mapped production targets compile; structural lifecycle guard passes.
- **Committed in:** `19577940`

---

**Total deviations:** 1 auto-fixed (missing critical lifecycle sequencing).
**Impact on plan:** Required to uphold D-05/D-06/D-15 without widening bridge ownership, cancellation, or global transaction authority scope.

## Known Stubs

None. The scan found only pre-existing defaults and TODOs outside this plan's new lifecycle path.

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: transaction-admission | `src/account/TransactionManager.cpp` | New checked submission is an account mutation boundary; it uses the existing admission gate and returns `MANAGER_RETIRED` after closure. |

## Next Phase Readiness

- Plans 14-04 and 14-05 can use the audited inventory to migrate test callers and remove the compatibility seam without touching the completed production partition.

## Self-Check: PASSED

- Required modified files and both task commits exist.
- The three mapped production targets build successfully.
