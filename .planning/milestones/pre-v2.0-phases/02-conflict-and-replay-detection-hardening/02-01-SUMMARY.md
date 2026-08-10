---
phase: 02-conflict-and-replay-detection-hardening
plan: 01
subsystem: testing
tags: [c++17, consensus, certificate, validation, gtest, utxo]

# Dependency graph
requires:
  - phase: 01-core-embedded-transaction-validation-path
    provides: "EmbeddedTransaction in NonceSubject, DeSerializeEmbeddedTransaction, ChangeTransactionState lifecycle"
  - phase: 02-conflict-and-replay-detection-hardening
    provides: "Certificate fallback deserialization in OnConsensusCertificate (commits 59da3f03, 57a92612)"
provides:
  - Certificate fallback test coverage (9 TEST_F cases)
  - CRDTFixture-based TransactionManager test pattern
  - CertificateFallbackTestAccess friend accessor pattern
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "CRDTFixture + direct TransactionManager construction for fast, network-free integration tests"
    - "CertificateFallbackTestAccess friend accessor for private TransactionManager method access from GTest"
    - "SerializeByteVector-based hash computation matching CheckHash verification path"

key-files:
  created:
    - test/src/account/transaction_manager_certificate_fallback_test.cpp
  modified:
    - src/account/TransactionManager.hpp
    - test/src/account/CMakeLists.txt

key-decisions:
  - "CRDTFixture instead of GeniusNode for test fixture — avoids network sync timeout"
  - "CertificateFallbackTestAccess pattern instead of direct friend on test class — GTest TEST_F generates derived classes that don't inherit friend access"
  - "TransferTransaction::SerializeByteVector for hash computation — matches CheckHash verification path exactly"
  - "GeniusAccount::New(token_id, path) random account — avoids crypto key derivation hang"

patterns-established:
  - "Test accessor pattern: friend class in namespace sgns with static methods wrapping private TM methods"
  - "Embedded transaction hash: deserialize first, then use SerializeByteVector(dag_copy) to compute blake2b_256, set data_hash, rebuild proto"
  - "UTXOManager LoadUTXOs required before ChangeTransactionState(CONFIRMED) in standalone test fixtures"

requirements-completed:
  - CONFLICT-01
  - NONCE-01

# Metrics
duration: ~90min
completed: 2026-05-30
---

# Phase 02 Plan 01: Certificate Fallback Tests Summary

**9 GTest cases verifying OnConsensusCertificate certificate fallback deserialization: happy path, edge cases, regression, and idempotency**

## Performance

- **Duration:** ~90 min
- **Tasks:** 1 of 1 (test writing only; implementation was pre-existing)
- **Files modified:** 3

## Accomplishments

- Created `transaction_manager_certificate_fallback_test.cpp` with 9 TEST_F cases covering all plan requirements
- Tests verify: valid embedded tx deserialization, empty pre-Phase-1 certificates, non-NonceSubject rejection, hash mismatch detection, tx storage with CONFIRMED status, existing-path regression, and idempotent repeated certs
- Established CRDTFixture-based TransactionManager test pattern (fast, no network required)

## Task Commits

1. **Task 1: Write certificate fallback tests** - `6c3df0d8` (test)

## Files Created/Modified

- `test/src/account/transaction_manager_certificate_fallback_test.cpp` - 9 TEST_F cases for certificate fallback path
- `test/src/account/CMakeLists.txt` - Registered new test target with genius_node + base_crdt_test
- `src/account/TransactionManager.hpp` - Added `friend class CertificateFallbackTestAccess`

## Decisions Made

- Used CRDTFixture instead of GeniusNode for test fixture — GeniusNode initialization hangs without network connectivity (CRDT sync timeout)
- Created CertificateFallbackTestAccess friend accessor in namespace sgns — GTest TEST_F generates derived classes that don't inherit friend access from the fixture
- Used TransferTransaction::SerializeByteVector for hash computation — proto's SerializeToString produces different bytes than the virtual SerializeByteVector method used by CheckHash
- Used GeniusAccount::New(token_id, path) without private key — avoids crypto key derivation hang in test environment
- Called account_->GetUTXOManager().LoadUTXOs(db_->GetDataStore()) in fixture — required for ChangeTransactionState(CONFIRMED) to succeed (ParseTransaction calls PutUTXO)

## Deviations from Plan

### Auto-fixed Issues

**1. [Private access] GTest TEST_F doesn't inherit friend access**
- **Found during:** Task 1 (test writing)
- **Issue:** TransactionManager::OnConsensusCertificate is private. Friend declaration on CertificateFallbackTest class doesn't extend to GTest-generated derived test classes.
- **Fix:** Created CertificateFallbackTestAccess class in namespace sgns with static wrapper methods, declared as friend in TransactionManager.hpp
- **Files modified:** src/account/TransactionManager.hpp, test file
- **Verification:** Build succeeds, all 9 tests pass

**2. [Static init] TransferTransaction deserializer not registered in test TU**
- **Found during:** Task 1 (test writing)
- **Issue:** `static inline bool registered = Register()` in TransferTransaction.hpp is never ODR-used in the test TU, so the compiler skips initialization. The "transfer" deserializer is absent from the map.
- **Fix:** Used DeSerializeEmbeddedTransaction through the accessor (runs in genius_node library TU where deserializer IS registered) instead of direct GetDeSerializers() lookup
- **Files modified:** test file (ComputeEmbeddedTxHash uses accessor)
- **Verification:** Deserialization succeeds, hash computed correctly

**3. [Hash mismatch] SerializeByteVector produces different bytes than proto SerializeToString**
- **Found during:** Task 1 (test writing)
- **Issue:** CheckHash() uses `SerializeByteVector(dag_copy)` which creates a TransferTx proto with utxo_params. Raw `dag_copy.SerializeToString()` produces different bytes (DAGStruct only, no TransferTx wrapper).
- **Fix:** Deserialize the embedded tx first, then use the deserialized object's SerializeByteVector(dag_copy) to compute the hash
- **Files modified:** test file (MakeMinimalEmbeddedTransfer takes TransactionManager parameter)
- **Verification:** CheckHash passes, tx stored with CONFIRMED status

**4. [UTXO not loaded] ChangeTransactionState fails without UTXOManager DB**
- **Found during:** Task 1 (test writing)
- **Issue:** ParseTransaction calls PutUTXO which requires LoadUTXOs to have been called. Standalone test fixture doesn't go through GeniusNode startup.
- **Fix:** Added account_->GetUTXOManager().LoadUTXOs(db_->GetDataStore()) in fixture constructor
- **Files modified:** test file
- **Verification:** ChangeTransactionState(CONFIRMED) succeeds, tx stored

---

**Total deviations:** 4 auto-fixed (1 access pattern, 1 static init, 1 hash computation, 1 DB initialization)
**Impact on plan:** All auto-fixes necessary for test correctness. No scope creep — only test infrastructure changes.

## Issues Encountered

- GeniusNode-based fixture hangs without network connectivity (CRDT sync timeout) — switched to CRDTFixture approach
- `GeniusAccount::New(token_id, priv_key, path)` hangs on crypto key derivation in test environment — switched to random account variant
- `static inline` variables in C++ headers may not initialize in TUs that don't ODR-use them — used accessor pattern to run code in the library TU

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Certificate fallback path is fully tested (9 cases, all passing)
- CRDTFixture-based TM test pattern available for future tests
- CertificateFallbackTestAccess accessor can be extended for additional private method access

---
*Phase: 02-Conflict and Replay Detection Hardening*
*Completed: 2026-05-30*
