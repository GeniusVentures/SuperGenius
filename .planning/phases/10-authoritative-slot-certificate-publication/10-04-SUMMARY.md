---
phase: 10-authoritative-slot-certificate-publication
plan: "04"
status: complete
---

# Plan 10-04: Transaction-derived authoritative certificate lookup Summary

**Transaction hash consumers now recover the CRDT transaction before resolving finality through its canonical slot.**

## Accomplishments

- Replaced transaction-hash certificate checks in `TransactionManager` with strict `GetSlotID()`-derived slot lookups.
- Made prior-transaction replay protection recover and validate the preceding transaction from CRDT; unavailable or mismatched evidence retains the existing retryable pending dependency.
- Made Genius UTXO witness validation recover the producer transaction from CRDT and fail closed before looking up its canonical-slot certificate.
- Added the minimal documented `Blockchain::GetGlobalDB()` accessor so the existing validator interface can recover transaction evidence without broadening `IInputValidator`.
- Registered the existing CRDT-backed account test target, moved its account creation behind `MemorySecureStorage`, and added missing-transaction and legacy `/cert/<transaction-hash>` rejection regressions.

## Verification

- `cmake -S build/OSX -B build/OSX/Release`
- `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test consensus_pending_lifecycle_test consensus_slot_key_test validator_registry_certificate_lookup_test pubsub_counts_test --parallel 2` — passed.
- `ctest --test-dir build/OSX/Release -R '^transaction_manager_certificate_fallback_test$' --output-on-failure` — passed (11 tests).
- `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` — passed.
- `ctest --test-dir build/OSX/Release -R '^(validator_registry_certificate_lookup_test|pubsub_counts_test)$' --output-on-failure` — passed.
- `ctest --test-dir build/OSX/Release -R '^consensus_slot_key_test$' --output-on-failure` — passed.
- `git diff --check` — passed.

## Task Commits

1. **Task 1: Register and extend CRDT-backed account consumer regressions** — `318559db` (`test`)
2. **Task 2: Resolve transaction hashes to canonical slots before certificate lookup** — `61aff953` (`feat`)

## Coverage Note

The account fixture directly proves missing transaction and legacy hash-record rejection through replay protection. Constructing an accepted quorum certificate together with a complete UTXO witness is outside this focused fixture; the production witness path is compiled by this target and fails closed on unavailable producer evidence.

