---
phase: 14-account-generation-publication-and-retired-manager-lifecycle
plan: "01"
subsystem: account transaction manager lifecycle
tags: [cxx17, concurrency, lifecycle, transaction-manager]
requires: []
provides: [manager-admission-gate, retirement-ledger, caller-inventory]
affects: [14-03, 14-04, 14-05]
tech-stack:
  added: []
  patterns: [move-only admission, terminal ledger, frozen retirement snapshot]
key-files:
  created: [14-01-PUBLIC-SURFACE.tsv, 14-manager-caller-inventory.tsv, 14-public-surface-audit.py, 14-manager-caller-audit.py]
  modified: [src/account/TransactionManager.hpp, src/account/TransactionManager.cpp, test/src/multiaccount/multi_account_sync.cpp]
decisions:
  - "Admission is the single lifecycle linearization point; accepted work stays in a transaction-ID ledger until terminalization."
requirements-completed: [D-05, D-06, D-13, D-14, D-15]
duration: 25 min
completed: 2026-08-20T13:48:32Z
---

# Phase 14 Plan 01: Manager lifecycle core Summary

TransactionManager now rejects post-close mutation admission, preserves accepted work through an idempotent terminal ledger, and publishes frozen retirement diagnostics under an immutable manager generation.

## Tasks Completed

1. Added four deterministic lifecycle RED cases and an exact public declaration inventory.
2. Implemented admission, draining, terminal completion, retirement snapshots, and staged caller ownership inventory.

## Verification

- RED: all four exact lifecycle cases failed normally with their required markers and no setup/crash/timeout failure.
- GREEN: `multi_account_test` built successfully; the four focused lifecycle cases passed (4/4).
- Public-surface audit and caller-assignment/current-inventory checks passed.

## Deviations from Plan

### Auto-fixed Issues

1. [Rule 3 - Blocking test infrastructure] Linked `base_crdt_test` to `multi_account_test`.
- **Found during:** Task 1
- **Issue:** The deterministic manager fixture required the existing CRDT fixture symbols, which the target did not link.
- **Fix:** Added the existing fixture library to the scoped test target; no new dependency was introduced.
- **Files modified:** `test/src/multiaccount/CMakeLists.txt`
- **Commit:** f99a8705

**Total deviations:** 1 auto-fixed. **Impact:** focused lifecycle tests are self-contained and execute with the repository's existing local CRDT fixture.

## Known Stubs

None.

## Self-Check: PASSED

- Required implementation, test, inventory, and audit files exist.
- Task commits `f99a8705` and `0c6b338b` exist.
