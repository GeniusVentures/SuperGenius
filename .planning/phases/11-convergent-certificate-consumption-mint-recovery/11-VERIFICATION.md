---
phase: 11-convergent-certificate-consumption-mint-recovery
verified: 2026-08-24T15:43:42Z
status: gaps_found
score: 2/4 must-haves verified
overrides_applied: 0
gaps:
  - truth: "A byte-identical certificate replay is harmless, while different certificate contents for an occupied slot fail closed and never overwrite the authority or unlock the slot."
    status: failed
    reason: "A valid different certificate whose SHA-256 sorts lower is allowed to replace an occupied canonical /cert/<slot> record. The durable work journal has no accepted-content identity and MarkDone removes its entry, so the later value is accepted as fresh work."
    artifacts:
      - path: "src/blockchain/Consensus.cpp"
        issue: "SubmitCertificate selects/replaces with a lower-hash different certificate; CertificateReceived reopens work solely by slot key."
      - path: "src/crdt/impl/crdt_work_journal.cpp"
        issue: "MarkDone deletes the only slot work record, and MarkSeen recreates it without comparing certificate bytes."
    missing:
      - "A durable fail-closed acceptance boundary that prevents a different occupied-slot certificate from replacing or being dispatched after the first accepted certificate."
  - truth: "A durably accepted certificate causes its embedded winning mint transaction at most once per node, including after duplicate delivery or restart."
    status: failed
    reason: "After the conflicting certificate path reopens slot work, its own exactly-bound Mint can pass OnConsensusCertificate and persist different UTXO outpoints before writing the same bridge marker key."
    artifacts:
      - path: "src/account/TransactionManager.cpp"
        issue: "CertificateMatchesTransaction binds each candidate to its own certificate but does not prove that certificate is the already accepted slot authority; a second conflicting Mint has a different transaction hash/outpoints."
    missing:
      - "Conflict regression covering a completed first certificate followed by a different valid same-slot certificate, proving no second Mint effect and no authority replacement."
---

# Phase 11: Convergent Certificate Consumption & Mint Recovery Verification Report

**Phase Goal:** Every node converges on one accepted slot certificate and applies its exact certified mint at most once across duplicate delivery and crash recovery.
**Verified:** 2026-08-24T15:43:42Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | Local completion, PubSub, CRDT synchronization, and restart recovery all pass certificates through one idempotent acceptance path for the same canonical slot. | ✓ VERIFIED | `CertificateReceived` only marks `/cert/<slot>` work Seen/Stalled; `RecoverPendingCertificateWork` serializes durable `db_->Get`, canonical-key validation, certificate validation, and `ProcessCommittedCertificate` dispatch ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2550), [Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3563)). `RegisterCertificateHandler` invokes that same recovery only after its map lock is released ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:286)). PubSub `HandleCertificate` has no datastore write or consumer dispatch; it only validates/clears volatile proposal state ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2837)). The lifecycle target passed 23 tests, including handler-late and concurrent recovery dispatch cases. |
| 2 | A byte-identical certificate replay is harmless, while different certificate contents for an occupied slot fail closed and never overwrite the authority or unlock the slot. | ✗ FAILED | The intended byte-identical behavior is covered by the immutable write test, but different contents do not fail closed: `SubmitCertificate` hashes both valid values and returns only when the existing hash sorts lower; otherwise it calls `PutConvergentImmutable`, whose own tests prove that a different lower-hash value replaces the current one ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2014), [crdt_datastore_test.cpp](../../../../test/src/crdt/crdt_datastore_test.cpp:193)). Recovery has only a slot-key journal identity, so it cannot distinguish the replacement from a replay. |
| 3 | A durably accepted certificate causes its embedded winning mint transaction at most once per node, including after duplicate delivery or restart. | ✗ FAILED | `MarkDone` removes the sole journal entry ([crdt_work_journal.cpp](../../../../src/crdt/impl/crdt_work_journal.cpp:92)). A later same-slot CRDT callback recreates it with `MarkSeen`/`MarkStalled` without comparing certificate bytes ([crdt_work_journal.cpp](../../../../src/crdt/impl/crdt_work_journal.cpp:25), [Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2550)), then durable recovery dispatches the replacement. Its different exact Mint can pass per-certificate binding and has different UTXO outpoints, so `ChangeTransactionState` does not make it a no-op. Existing 20/20 coverage tests duplicates of one certificate, not this sequence. |
| 4 | Recovery durably distinguishes certified, applying, and applied mint work (or an equivalent atomic boundary), so a crash neither repeats the mint effect nor silently loses certified work. | ✓ VERIFIED | The existing durable certificate work journal remains `Stalled` for missing handlers, failed durable reads, validation failures, handler errors, and `Check::Stalled`; only a successful handler reaches `MarkDone` ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3563), [Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3615)). Mint V2 enters retryable `VERIFYING`, persists UTXOs with propagated errors, persists `/bridge/executed/<chain>:<source>`, and only then marks `CONFIRMED` ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:1929), [TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:5410)). Failed UTXO persistence rolls its in-memory insertion back, and every UTXO writer retains the registry lock through snapshot persistence ([UTXOManager.cpp](../../../../src/account/UTXOManager.cpp:156), [UTXOManager.cpp](../../../../src/account/UTXOManager.cpp:252)). |

**Score:** 2/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/blockchain/Consensus.cpp` | Retryable no-handler dispatch and one recovery path | ⚠️ PARTIAL | Its shared recovery path is substantive and wired, but it permits a lower-hash different certificate to replace occupied authority and re-enter the same slot’s finished work. |
| `src/account/TransactionManager.hpp` / `src/account/TransactionManager.cpp` | Exact CRDT-first certificate consumption and Mint completion boundary | ✓ VERIFIED | Private exact-CRDT helper and marker seam are declared/defined; production flow uses them before terminal confirmation. |
| `src/account/UTXOManager.cpp` / `src/account/UTXOManager.hpp` | Durable idempotent UTXO effects under all writers | ✓ VERIFIED | `PutUTXO`, `ConsumeUTXOs`, `DeleteUTXO`, `SetUTXOs`, and public `StoreUTXOs` serialize mutation/snapshot/persist behavior and restore on failures. |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | No-handler, callback/readback, replay, and recovery-race coverage | ✓ VERIFIED | 23-test executable passed; contains `DurableCertificateWaitsForHandlerRegistrationBeforeFinalizing` and `CertificateRecoverySerializesHandlerRegistrationAndTimerDispatch`. |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` | Exact winner, duplicate/restart, marker-gap, and concurrent-ingress coverage | ✓ VERIFIED | 20-test executable passed; XML records zero failures/errors, including marker-failure retry and same-slot loser tests. |
| `test/src/account/utxo_manager_test.cpp` | Writer-concurrency durability regression | ✓ VERIFIED | 27-test executable passed, including `ConsumeSnapshotSerializesWithMintPersistenceAndReload`. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `RegisterCertificateHandler` | `RecoverPendingCertificateWork` | Post-insertion call after mutex release | ✓ WIRED | Direct call follows the scoped `certificate_handlers_mutex_` lock ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:286)). |
| `ProcessCommittedCertificate` | `CRDTWorkJournal::MarkStalled` | No-handler and handler-error branches | ✓ WIRED | Missing handler, active-vote release failure, and handler error/stall all retain the exact work key ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3615)). |
| `OnConsensusCertificate` | `FetchExactTransactionFromCRDT` and `CertificateMatchesTransaction` | Exact CRDT recovery before fallback and guard before confirmation | ✓ WIRED | Candidate selection and both confirmation branches call the proof-binding guard ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:3779)). |
| `ParseMintTransaction` | `UTXOManager::PutUTXO` / `ConsumeUTXOs` | Error-propagating Mint V2 effect persistence | ✓ WIRED | Both persistence calls use `BOOST_OUTCOME_TRY` ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:1929)). |
| `ChangeTransactionState(CONFIRMED)` | bridge marker | UTXOs → marker → terminal map state | ✓ WIRED | `ParseTransaction`, `PersistBridgeExecutedMarker`, then `tx_processed_m = CONFIRMED` are in that order ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:5410)). |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|---|---|---|---|---|
| Certificate recovery | Unfinished journal entry and exact certificate | Durable `CRDTWorkJournal` entry → `db_->Get(/cert/<slot>)` → validated protobuf | No — work identity is only the slot key, so a later different canonical value is indistinguishable from an idempotent replay | ✗ HOLLOW |
| Certificate-first Mint | Candidate transaction | tracked map → monitored-network CRDT transaction bytes → exact embedded fallback | Yes — each candidate is deserialized, intrinsic-hash checked, then certificate-bound | ✓ FLOWING |
| Mint completion | UTXO outpoints and bridge marker | RocksDB-backed `PutUTXO` / `ConsumeUTXOs` → `/bridge/executed/<chain>:<source>` datastore put | Yes — persistence errors propagate to the certificate journal; replay sees durable outpoints as safe progress | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Focused code compiles | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test transaction_manager_certificate_fallback_test utxo_manager_test --parallel 4` | All three targets built successfully. | ✓ PASS |
| Certificate-before-handler and serialized recovery | `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` | 1/1 CTest passed; 23 GoogleTest cases, 29.44s. | ✓ PASS |
| Exact winner plus marker/UTXO recovery | `ctest --test-dir build/OSX/Release -R '^transaction_manager_certificate_fallback_test$' --output-on-failure` | Verbose run passed 20/20 in 29.75s; post-build XML records 20 tests, 0 failures, 0 errors. | ✓ PASS |
| UTXO writer concurrency | `ctest --test-dir build/OSX/Release -R '^utxo_manager_test$' --output-on-failure` | 1/1 CTest passed; 27 tests, 1.04s. | ✓ PASS |
| Immutable authority replay/collision | `build/OSX/Release/test_bin/crdt_test --gtest_filter='*ConvergentImmutable*' --gtest_color=no` | 2/2 passed, but the tested behavior selects a different lower-hash value for the same key — direct evidence against this phase’s fail-closed occupied-slot contract. | ✗ FAIL (contract) |

### Probe Execution

No phase-declared or conventional `scripts/*/tests/probe-*.sh` probe exists. This is a C++ test phase; focused CTest/GoogleTest behavior was run instead.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| CERT-05 | 11-01, 11-02 | All ingress/restart paths converge through an idempotent canonical-certificate acceptance path; conflicts never unlock/overwrite. | ✗ BLOCKED | Different valid occupied-slot contents can replace authority and recreate completed journal work. |
| MINT-01 | 11-02, 11-03 | Each node applies the winning certified Mint at most once under duplicate delivery/restart. | ✗ BLOCKED | A replacement certificate can bind to a different transaction hash and bypass the first Mint’s outpoint idempotence. |
| MINT-02 | 11-03 | Durable recovery distinguishes incomplete from completed work and cannot lose/repeat a Mint effect. | ✓ SATISFIED | Stalled certificate journal, UTXO-before-marker-before-confirmed ordering, rollback on failed UTXO snapshot, and writer-concurrency regression. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|---|---:|---|---|---|
| `src/blockchain/Consensus.cpp` | 2014-2037 | Lower-hash different certificate is admitted into occupied canonical slot | 🛑 BLOCKER | Violates the fail-closed authority contract and can reopen completed slot work. |
| `src/crdt/impl/crdt_work_journal.cpp` | 25-47, 92-105 | Slot-only journal identity is deleted on completion then recreated on next callback | 🛑 BLOCKER | Cannot distinguish a byte replay from a different certificate at the same slot. |

The Phase 11 commit range adds no `TBD`, `FIXME`, or `XXX` markers. Existing unrelated TODO comments predate the phase and do not alter this finding. Searches found no `/bridge/certified` record, separate certificate-acceptance/finality journal, receiver-side certificate write, or subject-hash authority path.

### Human Verification Required

None. The phase plans contain no deferred human checks, and the four roadmap success criteria are covered by code inspection plus runnable focused regressions.

### Gaps Summary

**Root cause: an accepted slot has no durable certificate-content identity.** The journal uses only `/cert/<slot>`. Once a handler succeeds, `MarkDone` removes that record. A later different certificate at the same key is therefore treated exactly like new work. This is observable, not uncertain: `SubmitCertificate` explicitly permits the lower-hash different value through the convergent immutable write, and the immutable unit tests assert replacement. The failing behavior is not deferred to Phase 12: its goal is multi-node regression proof, not implementation of an acceptance/conflict boundary.

Required closure test: complete certificate A and its Mint, then deliver durable valid certificate B for the same canonical slot (including a lower content hash), run recovery/restart, and assert the authoritative certificate cannot change, active-vote state is not unlocked again, and B creates neither UTXO nor balance effect.

---

_Verified: 2026-08-24T15:43:42Z_
_Verifier: the agent (gsd-verifier)_
