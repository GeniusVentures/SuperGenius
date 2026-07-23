---
phase: 09-canonical-slot-and-certificate-storage
plan: 01
subsystem: consensus
tags: [canonical-slots, sha256, bridge-mint, fail-closed]

requires: []
provides:
  - Strict canonical readable preimages and 64-lowercase-hex SHA-256 slot IDs
  - Candidate-independent bridge mint slots keyed by chain, burn hash, and receipt-local index
  - Result-returning consensus slot derivation with no recognized-subject proposal fallback
affects: [09-02-certificate-storage, 10-vote-locks, 11-bridge-reservations, 12-race-verification]

tech-stack:
  added: []
  patterns:
    - Domain objects validate canonical slot preimages before hashing
    - Recognized consensus slot handlers propagate typed derivation errors

key-files:
  created: []
  modified:
    - src/account/GeniusTransaction.hpp
    - src/account/GeniusTransaction.cpp
    - src/account/MintTransactionV2.hpp
    - src/account/MintTransactionV2.cpp
    - src/account/TransactionManager.cpp
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_slot_key_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp

key-decisions:
  - "Expose readable slot preimages separately while making SHA-256 digests the only operational slot IDs."
  - "Treat registered slot-handler failure as terminal; only unregistered subject types use hashed canonical subject identity."

patterns-established:
  - "Canonical slot seam: validate readable identity, then hash exact bytes to 64 lowercase hexadecimal characters."
  - "Fail-closed admission: derive a canonical slot before proposals or slot states can be mutated."

requirements-completed:
  - SLOT-01
  - SLOT-02
  - SLOT-03
  - SLOT-04

duration: 16 min
completed: 2026-07-23
---

# Phase 09 Plan 01: Canonical Slot Identity Summary

**Strict normal and bridge-mint preimages now resolve to one fail-closed SHA-256 finality slot across consensus admission, continuation, restoration, and certificate paths.**

## Performance

- **Duration:** 16 min
- **Started:** 2026-07-23T12:50:28Z
- **Completed:** 2026-07-23T13:06:27Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- Normal transactions preserve canonical source-address-plus-nonce semantics through a fixed-size SHA-256 slot representation.
- Mint slots use exactly `mint-v2:<source_chain_id>:<burn_tx_hash>:<receipt_log_index>` and ignore proposer, nonce, transaction hash, token, amount, and destination.
- Consensus rejects recognized canonical-slot failures before proposal or slot-state insertion and hashes only unregistered subject identities as fallback.
- Adversarial tests cover aliases, missing or ambiguous inputs, candidate-controlled field variation, proposal-ID variation, and exact known digests.

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement strict canonical preimages and hashed transaction slot IDs** - `c852bf9f` (feat)
2. **Task 2: Make consensus use one fail-closed slot derivation path** - `89b63aa1` (fix)

## Files Created/Modified

- `src/account/GeniusTransaction.hpp` - Declares canonical nonce preimage and SHA-256 slot APIs.
- `src/account/GeniusTransaction.cpp` - Validates lowercase public-key addresses and hashes canonical slot bytes.
- `src/account/MintTransactionV2.hpp` - Exposes result-returning mint slot preimage derivation.
- `src/account/MintTransactionV2.cpp` - Enforces exact chain/hash/input identity and receipt-local index composition.
- `src/account/TransactionManager.cpp` - Propagates embedded-transaction and nonce-slot failures without fallback.
- `src/blockchain/Consensus.hpp` - Makes slot handlers and `GetSlotKey` result-returning.
- `src/blockchain/Consensus.cpp` - Validates one slot before every proposal/certificate state seam.
- `test/src/blockchain/consensus_slot_key_test.cpp` - Covers SLOT-01 through SLOT-04 and fail-closed adversarial cases.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - Adapts lifecycle test access to the typed slot contract.

## Decisions Made

- Readable preimages remain available through `GetSlotPreimage()`, while `GetSlotID()` is always the SHA-256 digest of validated canonical bytes.
- A registered domain handler owns validity for its subject type; its error is never replaced with proposal ID, subject hash, or nonce zero.
- Unregistered subject types retain compatibility by hashing their serialized canonical subject identity into the same fixed-size slot form.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Embedded deserialization represented null transactions as success**

- **Found during:** Task 1 (malformed mint identity verification)
- **Issue:** A deserializer returning `nullptr` was wrapped as a successful `outcome::result`, allowing malformed embedded transactions to be dereferenced.
- **Fix:** Centralized embedded deserializer dispatch and convert serialization failures or null transaction objects into `invalid_argument`.
- **Files modified:** `src/account/TransactionManager.cpp`
- **Verification:** Noncanonical uppercase, prefixed, and non-hex burn hashes now return typed errors; focused slot tests pass.
- **Committed in:** `c852bf9f`

---

**Total deviations:** 1 auto-fixed (1 bug).
**Impact on plan:** The fix is required for fail-closed malformed-subject handling and adds no scope beyond the canonical slot boundary.

## Issues Encountered

- The first malformed non-hex test exposed the null-success deserialization bug; it was fixed before the Task 1 commit.

## User Setup Required

None - no external service configuration required.

## Verification

- `cmake --build build/OSX/Release --target consensus_slot_key_test consensus_pending_lifecycle_test -j2` — PASS
- `build/OSX/Release/test_bin/consensus_slot_key_test --gtest_brief=1` — PASS, 11/11 tests
- `build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_brief=1` — PASS, 7/7 tests
- Operational slot assertions require `^[0-9a-f]{64}$`; readable `mint-v2:` text appears only as a diagnostic preimage.

## Next Phase Readiness

- Ready for 09-02 certificate storage to use canonical slot IDs as authoritative keys.
- No blockers.

## Self-Check: PASSED

- All key modified files exist.
- Both task commits are present.
- All task and plan-level focused verification commands pass.
- SLOT-01, SLOT-02, SLOT-03, and SLOT-04 have executable coverage.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
