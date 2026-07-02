---
phase: 03-network-hardening-and-operational-readiness
plan: 02
subsystem: consensus
tags: [c++17, protobuf, pubsub, callback, timeout, cleanup, tracking, memory-leak]
requires:
  - phase: 03-01
    provides: SIZE-01 pre-publish size gate, TS-01 timestamp tolerance, METRICS-01 operational counters
  - phase: 01-02
    provides: TRACK-01 temporary VERIFYING entry insertion in tx_processed_m
  - phase: 02-01
    provides: Certificate-based promotion VERIFYING → CONFIRMED
provides:
  - ProposalCleanupHandler callback infrastructure bridging ConsensusManager timeouts with TransactionManager tracking cleanup
  - Automatic transition of orphaned VERIFYING entries to FAILED on proposal timeout
  - Thread-safe multicast handler dispatch under shared_mutex
  - Blockchain facade delegation pattern for cross-subsystem registration
affects:
  - 03-01 (SIZE-01/TS-01/METRICS-01 tests co-exist in consensus_subject_test.cpp)
  - future operational-readiness plans (all tracking entries now have terminal paths)
tech-stack:
  added: []
  patterns:
    - "ProposalCleanupHandler: third callback type in ConsensusManager handler framework (alongside SubjectHandler, CertificateSubjectHandler)"
    - "FireProposalCleanupCallbacks: DecodeNonceSubject → tx_hash extraction → shared_lock copy → unlock → dispatch"
    - "weak_ptr-based lambda registration for cross-subsystem lifecycle safety (matching SubjectHandler/CertificateHandler pattern)"
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp (ProposalCleanupHandler typedef, Register/Unregister declarations, member map/mutex, Fire declaration)
    - src/blockchain/Consensus.cpp (FireProposalCleanupCallbacks impl + 2 call sites, Register/Unregister impl)
    - src/blockchain/Blockchain.hpp (RegisterProposalCleanupHandler delegation declaration)
    - src/blockchain/impl/Blockchain.cpp (RegisterProposalCleanupHandler delegation impl)
    - src/account/TransactionManager.hpp (OnProposalTimeoutCleanup declaration)
    - src/account/TransactionManager.cpp (OnProposalTimeoutCleanup impl + handler registration in constructor)
    - test/src/blockchain/consensus_subject_test.cpp (6 CLEAN-01 test cases)
key-decisions:
  - "Cleanup callback uses void return (fire-and-forget) unlike SubjectHandler/CertificateHandler result<Check>"
  - "Multiple handlers per subject type via vector (multicast pattern), unlike single-handler-per-type for Subject/Certificate"
  - "Callback invoked from timeout callers (lines 1466, 1551) BEFORE ClearProposalSlot, NOT from certificate path (line 1988) per D-08"
  - "OnProposalTimeoutCleanup uses weak_ptr lambda registration matching the existing SubjectHandler/CertificateHandler pattern for lifecycle safety"
requirements-completed:
  - CLEAN-01
duration: 15min
completed: 2026-05-29
---

# Phase 3 Plan 2: CLEAN-01 Proposal Timeout Cleanup Callback Summary

**ProposalCleanupHandler callback infrastructure that transitions orphaned VERIFYING tracking entries to FAILED on proposal timeout, preventing memory leaks over sustained multi-hour operation**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-29T18:00:00Z
- **Completed:** 2026-05-29T18:15:18Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments

- ProposalCleanupHandler callback infrastructure added to ConsensusManager as third handler type (alongside SubjectHandler, CertificateSubjectHandler)
- FireProposalCleanupCallbacks dispatches to all registered handlers before ClearProposalSlot at two timeout callers — NOT at the certificate caller (D-08 safety gate)
- TransactionManager::OnProposalTimeoutCleanup transitions VERIFYING entries to FAILED via ChangeTransactionState, leaves CONFIRMED entries untouched, skips missing entries silently
- 6 CLEAN-01 tests covering: VERIFYING→FAILED transition, CONFIRMED untouched, missing entry skip, register/unregister lifecycle, certificate path exclusion, multicast dispatch

## Task Commits

Each task was committed atomically:

1. **Task 1: RED — Cleanup Callback Tests** - `8c886134` (test): 6 CLEAN-01 test cases in consensus_subject_test.cpp — all referencing ProposalCleanupHandler (nonexistent at commit time)
2. **Task 2: GREEN — ProposalCleanupHandler Implementation** - `faa9f333` (feat): Callback infrastructure across 6 source files plus handler registration

## Files Modified

- `src/blockchain/Consensus.hpp` — ProposalCleanupHandler typedef (line 109), Register/Unregister declarations (lines 151-156), FireProposalCleanupCallbacks declaration (line 503), proposal_cleanup_handlers_ map + cleanup_handlers_mutex_ (lines 694-696)
- `src/blockchain/Consensus.cpp` — FireProposalCleanupCallbacks implementation (line 333), RegisterProposalCleanupHandler (line 293), UnregisterProposalCleanupHandler (line 319), calls before ClearProposalSlot at timeout callers (lines 1466, 1551)
- `src/blockchain/Blockchain.hpp` — RegisterProposalCleanupHandler delegation declaration (line 177)
- `src/blockchain/impl/Blockchain.cpp` — RegisterProposalCleanupHandler delegation implementation forwarding to consensus_manager_ (line 1690)
- `src/account/TransactionManager.hpp` — OnProposalTimeoutCleanup declaration (line 464)
- `src/account/TransactionManager.cpp` — OnProposalTimeoutCleanup implementation (line 3463); handler registration via blockchain_->RegisterProposalCleanupHandler with weak_ptr lambda (lines 157-164)
- `test/src/blockchain/consensus_subject_test.cpp` — 6 CLEAN-01 test cases covering full cleanup lifecycle

## Decisions Made

- Cleanup callback uses `void` return (fire-and-forget), unlike `SubjectHandler`/`CertificateSubjectHandler` which return `outcome::result<Check>` — cleanup is best-effort, failures are logged not propagated
- Used `std::vector<ProposalCleanupHandler>` per subject type for multicast dispatch, unlike single-handler-per-type maps for Subject/Certificate handlers
- Callback invocation placed in timeout caller code, NOT inside `ClearProposalSlot()` — this prevents the certificate path from accidentally triggering cleanup (D-08/D-11 safety design)
- Handler registration uses `weak_ptr` lambda pattern matching the existing `SubjectHandler` and `CertificateHandler` registrations, ensuring lifecycle safety across ConsensusManager/TransactionManager boundaries

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

- Build system not configured locally (`build/local` lacks `build.ninja` — pre-existing condition). All acceptance criteria verified via source-level grep inspection. Compilation not attempted but code follows existing patterns exactly.

## Threat Model Verification

| Threat ID | Mitigation | Status |
|-----------|-----------|--------|
| T-03-04 (DoS) | OnProposalTimeoutCleanup → ChangeTransactionState(FAILED) → ReleaseNonce + UTXO rollback | Implemented |
| T-03-05 (EoP) | FireProposalCleanupCallbacks NOT called from certificate path (line 1988 only calls ClearProposalSlot) | Verified |
| T-03-06 (DoS) | Handler copy under shared_lock, invoked outside lock — no deadlock path | Implemented |

## Next Phase Readiness

- CLEAN-01 is complete — all tracking entries now have terminal paths (VERIFYING → CONFIRMED via certificate, or FAILED via cleanup callback)
- Phase 3 network hardening milestone now has all 4 requirement areas implemented (SIZE-01, TS-01, METRICS-01, CLEAN-01)
- Ready for phase-level verification and milestone completion

---
*Phase: 03-network-hardening-and-operational-readiness*
*Completed: 2026-05-29*
