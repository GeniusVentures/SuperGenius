---
phase: 09-canonical-slot-and-certificate-storage
plan: 04
subsystem: consensus-storage
tags: [certificate-lookup, canonical-slot, secondary-index, compatibility]

requires:
  - phase: 09-03
    provides: Atomic authoritative slot certificates and winning transaction indexes
provides:
  - Typed authoritative certificate lookup by canonical slot
  - Verified transaction-hash lookup through the winning secondary index
  - Slot-aware full-subject finality with exact winning-hash compatibility
  - Fail-closed integrity handling for nonce-chain and producer-certificate consumers
affects: [10-finalization-state-machine, 12-compatibility-verification, transaction-validation]

tech-stack:
  added: []
  patterns:
    - Authoritative slot lookup validates canonical input, payload, certificate, and derived slot
    - Secondary hash lookup delegates to slot lookup and independently verifies the winning hash
    - Compatibility consumers treat only NotFound as normal absence and fail closed on corruption

key-files:
  created:
    - test/src/blockchain/certificate_compatibility_test.cpp
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/TransactionManager.cpp
    - src/account/GeniusInputValidator.cpp
    - test/src/blockchain/CMakeLists.txt
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp

key-decisions:
  - "Expose authoritative slot lookup separately; transaction-hash lookup reads only its index before delegating every authoritative validation step."
  - "A full subject is finalized when its canonical slot has a valid winner, even when that subject's transaction hash lost."
  - "Previous-nonce and producer-certificate consumers preserve hash lookup semantics while treating only NotFound as ordinary absence."

patterns-established:
  - "Verified lookup chain: canonical hash input -> index slot -> authoritative slot validation -> exact winner verification."
  - "Typed consumer handling: NotFound follows existing pending/absent behavior; every integrity or invalid-input error fails closed."

requirements-completed:
  - SLOT-01
  - CERT-04
  - COMP-01

duration: 18 min
completed: 2026-07-23
---

# Phase 09 Plan 04: Verified Certificate Lookup Compatibility Summary

**Canonical slot certificates now expose typed authoritative lookup, verified winning-hash compatibility, and slot-aware finality without hiding corrupt index relationships as absence.**

## Performance

- **Duration:** 18 min
- **Started:** 2026-07-23T14:39:01Z
- **Completed:** 2026-07-23T14:57:13Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- Added `GetCertificateBySlotId()` to Consensus and Blockchain with canonical input enforcement, complete certificate parsing/validation, and derived-slot verification.
- Reworked `GetCertificateBySubjectHash()` to read only the winning index, delegate authoritative validation to slot lookup, and verify the requested transaction is the embedded winner.
- Made full-subject certificate checks derive canonical slot identity directly, so a losing candidate observes the winner's finalized slot while losing hash lookup remains `NotFound`.
- Preserved previous-nonce and producer-UTXO hash consumers while distinguishing normal absence from integrity corruption in logs and control flow.
- Added CRDT-backed coverage for successful, absent, malformed, missing, mismatched, losing-candidate, and compatibility-consumer states.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add typed authoritative slot and verified transaction-hash lookups** - `5076fc97` (feat)
2. **Task 2: Make full-subject finality slot-aware and preserve hash consumer compatibility** - `976bf98a` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` / `src/blockchain/Consensus.cpp` - Add typed slot lookup, verified index delegation, direct subject-slot derivation, and fail-closed diagnostics.
- `src/blockchain/Blockchain.hpp` / `src/blockchain/impl/Blockchain.cpp` - Expose the authoritative slot lookup wrapper while preserving the hash wrapper.
- `src/account/TransactionManager.cpp` - Keep previous-nonce lookup on the hash API; only `NotFound` remains pending and integrity failures reject.
- `src/account/GeniusInputValidator.cpp` - Keep producer-certificate lookup on the hash API and distinguish missing producers from store corruption.
- `test/src/blockchain/CMakeLists.txt` - Register the focused compatibility target.
- `test/src/blockchain/certificate_compatibility_test.cpp` - Exercise every slot/index relationship plus winner, loser, and consumer compatibility semantics.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Assert typed lookup errors keep absence and corruption distinct.

## Decisions Made

- Canonical lookup input failures use a dedicated `InvalidInput` error rather than being collapsed into `NotFound`.
- Slot lookup is the single authoritative validator; hash lookup never parses certificate storage independently.
- Boolean hash checks can return false for absence, but integrity failures are logged at critical severity before failing closed.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The losing-candidate fixture initially used the generic subject-ID fallback, which intentionally includes the transaction hash. Registering the production nonce slot handler in that fixture established the canonical address-plus-nonce slot and made the test exercise the real compatibility path.
- CMake required regeneration from `build/OSX` before the new target became available.

## Known Stubs

None introduced. Existing TODO/placeholder comments outside the modified lookup and consumer regions are pre-existing and do not block this plan.

## User Setup Required

None - no external service configuration required.

## Verification

- `cmake --build build/OSX/Release --target certificate_compatibility_test transaction_manager_pending_lifecycle_test -j2` — PASS.
- `certificate_compatibility_test --gtest_brief=1` — PASS, 11/11 tests.
- `transaction_manager_pending_lifecycle_test --gtest_brief=1` — PASS, 3/3 tests.
- Task 1 filtered slot/hash/integrity/NotFound slice — PASS, 9/9 tests.
- Account consumer storage audit — PASS: neither `TransactionManager.cpp` nor `GeniusInputValidator.cpp` reads `/cert/` directly.
- `git diff --check` — PASS.

## Next Phase Readiness

- Phase 9 certificate storage and compatibility lookup semantics are complete.
- Phase 10 can build durable vote locks and finalized-slot state transitions on the authoritative slot API.
- No blockers.

## Self-Check: PASSED

- All nine created or modified plan files exist.
- Both task commits are present in git history.
- All task and plan-level verification commands pass.
- SLOT-01, CERT-04, and COMP-01 have executable coverage.
- Pre-existing user changes and unrelated untracked files remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
