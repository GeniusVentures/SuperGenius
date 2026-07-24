---
phase: 09-canonical-slot-and-certificate-storage
plan: 09
subsystem: consensus-certificate-compatibility
tags: [certificate-storage, error-mapping, nonce-replay, utxo-witness]

requires:
  - phase: 09-05
    provides: Canonical slot-keyed certificate storage and subject-hash index lookup
  - phase: 09-08
    provides: Strict certificate filtering and dependency-aware CRDT retry
provides:
  - Typed distinction between certificate absence, integrity failure, and operational datastore failure
  - Private friend-only certificate read injection for exact storage-failure regression coverage
  - Real previous-nonce consumer coverage through TransactionManager and Blockchain
  - Real producer-UTXO witness coverage through GeniusInputValidator and Blockchain
affects: [10-finalization-state-machine, transaction-admission, utxo-validation]

tech-stack:
  added: []
  patterns:
    - Map raw datastore errors before applying certificate index-to-slot relationship rules
    - Only typed certificate absence is retryable; integrity and operational failures fail closed
    - Exercise compatibility through production consumers rather than lookup-only contract assertions

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/account/TransactionManager.cpp
    - src/account/GeniusInputValidator.cpp
    - test/src/blockchain/certificate_compatibility_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp

key-decisions:
  - "Only storage::DatabaseError::NOT_FOUND maps to CertificateStoreError::NotFound; corruption maps to IntegrityError and every other category maps conservatively to StorageError."
  - "An existing hash index whose authoritative slot is absent remains an integrity failure, while operational slot-read errors propagate unchanged."
  - "Previous-nonce admission creates a dependency only for NotFound; producer-witness validation rejects every non-success lookup."
  - "Test access remains private and friend-only in ConsensusManager and Blockchain."

patterns-established:
  - "Certificate read flow: raw read -> typed storage mapping -> payload validation -> index/slot relationship validation."
  - "Consumer flow: success continues, NotFound follows consumer-specific absence behavior, IntegrityError and StorageError fail closed with distinct diagnostics."

requirements-completed:
  - SLOT-01
  - SLOT-02
  - CERT-04
  - COMP-01

duration: 2h 16m
completed: 2026-07-23
---

# Phase 09 Plan 09: Certificate Read Semantics and Consumer Compatibility Summary

**Certificate reads now preserve absence, corruption, and datastore outages as distinct outcomes, with executable nonce-chain and producer-witness consumers proving the canonical v2 index path.**

## Performance

- **Duration:** 2h 16m
- **Started:** 2026-07-23T22:06:27Z
- **Completed:** 2026-07-24T00:22:30Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments

- Added `CertificateStoreError::StorageError` and one conservative datastore-error mapper shared by slot and subject-hash certificate reads.
- Preserved relationship semantics: a dangling index is an integrity error, while corruption and operational errors remain distinguishable at both index and authoritative-slot reads.
- Added a private, friend-only read seam and nine focused compatibility cases spanning actual absence, injected corruption, injected I/O failure, and dangling-index variants.
- Executed the real previous-nonce admission path for canonical winner, absent index, corruption, and I/O failure; only absence becomes Pending with the exact certificate dependency.
- Built a signed producer certificate plus valid UTXO commitment/witness and exercised the real input validator for success, absence, corruption, and I/O failure.
- Split consumer diagnostics between missing data, corruption, and operational unavailability without weakening fail-closed behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Map certificate datastore reads to absence, integrity, and operational errors**
   - `bbfd3e82` — typed read mapping, private reader injection, and compatibility regressions
2. **Task 2: Execute real previous-nonce and producer-UTXO lookup branches**
   - `1d193498` — consumer diagnostics and real Blockchain-backed outcome matrices

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Declares `StorageError`, the shared read mapper, and the private injected reader.
- `src/blockchain/Consensus.cpp` - Maps raw read errors and keeps raw-read classification separate from certificate relationship validation.
- `src/blockchain/Blockchain.hpp` - Grants the narrow existing lifecycle test accessor private consensus-manager access.
- `src/account/TransactionManager.cpp` - Distinguishes pending absence from rejecting integrity and operational failures.
- `src/account/GeniusInputValidator.cpp` - Distinguishes missing producer, corruption, and operational unavailability while rejecting all three.
- `test/src/blockchain/certificate_compatibility_test.cpp` - Covers slot/index read failures and dangling-index slot outcomes.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Builds authoritative certificates and a valid Merkle witness to execute both production consumers.

## Decisions Made

- Unknown or non-database read errors are treated as operational storage failures, never normal absence.
- Corruption and storage unavailability both reject admission, but retain distinct log diagnostics for operations and incident response.
- The producer-witness test uses one canonical leaf with an empty branch so the proof is minimal while still passing every real commitment, signature, ownership, and balance check.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Repository metadata writes required the approved elevated git path; file scope was explicitly audited and only plan-owned files were staged.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Prescribed dual-target build — PASS: `transaction_manager_pending_lifecycle_test` and `certificate_compatibility_test`.
- Focused certificate read-error matrix — PASS: 9/9.
- Focused real-consumer matrix — PASS: 2/2 tests covering eight winner/absent/corrupt/I/O outcomes.
- Full `certificate_compatibility_test` — PASS: 17/17.
- Full `transaction_manager_pending_lifecycle_test` — PASS: 8/8.
- `git diff --check` — PASS.

## Next Phase Readiness

- Canonical certificate storage now exposes precise, rejecting read semantics to both compatibility consumers.
- Phase 09 requirements and executable evidence are complete and ready for milestone audit/finalization work.
- No blockers.

## Self-Check: PASSED

- All seven plan-owned source and test files exist.
- Task commits `bbfd3e82` and `1d193498` are present.
- `StorageError`, the shared error mapper, private read seam, and separate consumer diagnostics are present in production code.
- Both complete test targets pass after implementation.
- Protected user-owned dirty and untracked paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
