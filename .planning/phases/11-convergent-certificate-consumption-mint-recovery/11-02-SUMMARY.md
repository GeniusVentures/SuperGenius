---
phase: 11-convergent-certificate-consumption-mint-recovery
plan: "02"
subsystem: transaction-manager
tags: [c++, crdt, consensus-certificate, mint-v2, finality]
requires:
  - phase: 11-01
    provides: durable committed-certificate dispatch and retryable work journal
provides:
  - CRDT-first exact-hash transaction recovery for certificate ingress
  - Exact certificate-to-transaction binding before confirmation
  - Winner/loser Mint certificate regression coverage
affects: [11-03, certificate-recovery, mint-finality]
tech-stack:
  added: []
  patterns: [tracked-then-exact-CRDT-then-embedded candidate selection, certificate-bound confirmation]
key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - test/src/account/transaction_manager_certificate_fallback_test.cpp
key-decisions:
  - "Certificate-first processing selects tracked state, exact CRDT evidence, then only a validated embedded fallback."
  - "Every certificate-first candidate must satisfy Phase 10's exact account, nonce, hash, embedded-hash, and slot binding before confirmation."
  - "A non-NOT_FOUND CRDT read failure is returned so the Phase 11 certificate work journal can retry it."
patterns-established:
  - "CRDT key paths are lookup hints only; decoded transaction hashes remain mandatory authority checks."
requirements-completed: [CERT-05, MINT-01]
duration: 10 min
completed: 2026-08-24
---

# Phase 11 Plan 02: Certificate-First Winner Selection Summary

**Certificate-first Mint consumption now recovers the exact CRDT winner before a tightly validated embedded fallback, so same-slot contenders cannot inherit finality.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-08-24T13:07:20Z
- **Completed:** 2026-08-24T13:17:35Z
- **Tasks:** 2/2
- **Files modified:** 3

## Accomplishments

- Added monitored-network CRDT transaction recovery that accepts only a decoded transaction whose intrinsic hash equals the requested certificate transaction hash.
- Updated certificate ingress to use tracked transaction, exact CRDT candidate, then a CheckHash-validated embedded transaction fallback, always through `ChangeTransactionState(CONFIRMED)`.
- Enforced `CertificateMatchesTransaction` before every certificate-first confirmation and covered the same-slot Mint winner/loser path.

## Task Commits

1. **Task 1: Specify exact certificate-first winner selection and convergence** — `1e0b3c71` (`test`)
2. **Task 2: Implement guarded CRDT-first certificate consumption through the existing lifecycle** — `54989ee6` (`feat`)

## Files Created/Modified

- `src/account/TransactionManager.hpp` — Declares exact-hash CRDT transaction recovery.
- `src/account/TransactionManager.cpp` — Selects and binds the certificate candidate before the established confirmation lifecycle.
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — Exercises CRDT-first recovery, hash mismatch rejection, constrained fallback, and contested Mint slots.

## Decisions Made

- Used the normal monitored-network transaction paths rather than a certificate-by-subject lookup; canonical slot certificates remain the sole authority.
- Treat CRDT `NOT_FOUND` as an embedded-fallback miss, but propagate other read failures as retryable handler errors.

## TDD Gate Compliance

- RED commit present: `1e0b3c71` — the focused target failed because the exact CRDT recovery helper did not yet exist.
- GREEN commit present after RED: `54989ee6` — focused target passed after candidate ordering and binding checks were implemented.
- No refactor commit was required.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Made legacy embedded-transfer fixtures satisfy exact certificate binding**
- **Found during:** Task 2
- **Issue:** The existing fixture omitted the embedded transaction's source address and nonce, so its certificates could not meet the now-required Phase 10 binding proof.
- **Fix:** Parameterized the minimal embedded-transfer fixture with its certificate-bound source address and nonce.
- **Files modified:** `test/src/account/transaction_manager_certificate_fallback_test.cpp`
- **Verification:** Focused CTest passed 16/16.
- **Committed in:** `54989ee6`

**2. [Rule 2 - Missing Critical] Added a mismatched CRDT payload regression**
- **Found during:** Task 2
- **Issue:** The plan requires a decoded CRDT value with a mismatched intrinsic hash to be ignored, but the first test set covered only exact recovery.
- **Fix:** Added a normal-path CRDT persistence case that stores a different transaction under the requested key and asserts no candidate is returned.
- **Files modified:** `test/src/account/transaction_manager_certificate_fallback_test.cpp`
- **Verification:** Focused CTest passed 16/16.
- **Committed in:** `54989ee6`

**Total deviations:** 2 auto-fixed (1 Rule 1 bug, 1 Rule 2 missing critical coverage).
**Impact on plan:** Both fixes enforce the specified certificate-binding and CRDT integrity boundaries without adding a second finality or Mint route.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 11-03 can rely on certificate-first and transaction-first processing converging on the same exact-bound transaction lifecycle.
- No new authority record, subject-hash certificate lookup, or Mint completion path was introduced.

## Self-Check: PASSED

- `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`, and `test/src/account/transaction_manager_certificate_fallback_test.cpp` exist.
- Task commits `1e0b3c71` and `54989ee6` exist in git history.
- `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test --parallel 4` and focused CTest passed (16/16).
