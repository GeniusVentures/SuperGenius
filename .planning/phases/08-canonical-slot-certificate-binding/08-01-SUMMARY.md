---
phase: 08-canonical-slot-certificate-binding
plan: 01
subsystem: consensus
tags: [c++17, consensus, certificates, crdt, gtest]
requires:
  - phase: 07-deferred-validation-and-pending-proposal-lifecycle
    provides: Pending-proposal lifecycle and consensus test seams
provides:
  - Canonical Mint slot-identity regression coverage through the existing handler
  - Fail-closed certificate proposal and legacy CRDT-key binding at ingress
  - Non-authoritative future certificate-slot-key predicate for later publication migration
affects: [09-durable-one-vote-finality, 10-authoritative-slot-certificate-publication, 11-convergent-certificate-consumption-and-mint-recovery]
tech-stack:
  added: []
  patterns:
    - Derive the certificate slot from its embedded proposal
    - Treat /cert/<subject-hash> as compatibility evidence only at key-aware CRDT ingress
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_slot_key_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
key-decisions:
  - Keep the legacy subject-hash CRDT key non-authoritative while validating it at key-aware ingress.
  - Reject a certificate before publish, callback, cleanup, or finality effects whenever its embedded proposal binding is invalid.
  - Use in-memory secure storage in the lifecycle fixture before creating its signing account.
requirements-completed: [SLOT-01, SLOT-02, SLOT-03]
metrics:
  duration: 48m
  completed: 2026-08-20
  tasks_completed: 2
  files_modified: 4
---

# Phase 08 Plan 01: Canonical Slot & Certificate Binding Summary

Canonical Mint slot identity is now regression-tested, and certificate ingress rejects an invalid embedded proposal or incompatible legacy CRDT key before it can cause observable consensus effects.

## Accomplishments

- Added handler-level Mint tests proving transaction-envelope mutations do not change its canonical slot, while each slot-defining Mint fact does.
- Added one certificate-binding predicate shared by semantic validation, key-aware CRDT filtering, and callback receipt handling.
- Preserved keyless `HandleCertificate` acceptance for valid certificates; only ingress that supplies a legacy key checks its compatibility.
- Added a lifecycle regression that proves rejected CRDT-key evidence leaves certificate state and finality effects untouched, while valid keyless handling remains accepted.
- Followed up on review finding CR-01: intrinsic proposal and canonical-slot checks now precede registry availability, and keyless handling requires `Approve` before any proposal-state cleanup.

## Task Commits

1. **Task 1: Canonical Mint slot identity tests** — `8d66d670` (`test(08-01): prove canonical Mint slot identity`)
2. **Task 2: Certificate binding regressions** — `5ded0222` (`test(08-01): add certificate binding regression`)
3. **Task 2: Certificate ingress implementation** — `38f0e40e` (`feat(08-01): enforce certificate ingress binding`)
4. **Review follow-up: unavailable-registry fail-closed fix** — `35daf3d0` (`fix(08): reject stalled certificates before cleanup`)

## Decisions Made

- The current `/cert/<subject-hash>` key remains the persistence and lookup contract. The newly exposed canonical slot key is a validated future-migration predicate only; it does not migrate authority in this phase.
- `ProcessCertificates` clears a pending proposal only after certificate validation and submission succeed, so failed binding checks cannot erase recoverable state.
- The lifecycle fixture explicitly selects `MemorySecureStorage` before `GeniusAccount::NewFromPrivateKey`, avoiding platform keychain use during the regression test.
- Registry unavailability remains a deferred `Stalled` condition only for already intrinsic-valid certificates; `HandleCertificate` does not treat it as authorization to create or clear local proposal state.

## Verification

- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` — passed.
- `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` — passed, 2/2 tests.
- `git diff --check` — passed.
- `git diff --quiet HEAD -- src/account/MintTransactionV2.cpp` — passed; Mint implementation remains unchanged.
- Review follow-up focused Release verification: both consensus targets built and `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` passed, 2/2 tests.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 3 - Blocking] Used the configured Release build directory**
   - **Found during:** Task 2 verification
   - **Issue:** The repository's configured test artifacts are under `build/OSX/Release`, not the generic `build` location named in the plan.
   - **Fix:** Built and tested the planned targets from `build/OSX/Release`.
   - **Files modified:** None.

2. **[Rule 1 - Bug] Closed the lifecycle manager timer thread in the existing bounded-pool test**
   - **Found during:** Task 2 verification
   - **Issue:** That test left the manager's joinable round-timer thread running, which caused process termination after the test.
   - **Fix:** Used the test suite's existing `Close(manager)` pattern before fixture teardown.
   - **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
   - **Commit:** `5ded0222`

## Known Stubs

None.

## Self-Check: PASSED

- Summary file exists.
- All three task commits are present in repository history.
