---
phase: 10-authoritative-slot-certificate-publication
verified: 2026-08-21T16:10:00-03:00
status: passed
score: 8/8 must-haves verified
overrides_applied: 0
---

# Phase 10: Authoritative Slot Certificate Publication — Verification Report

**Phase Goal:** The network can discover one generic, slot-keyed authoritative certificate that a deterministic publisher persists before advertising and an eligible successor can recover safely.

**Verified:** 2026-08-21T16:10:00-03:00  
**Status:** passed  
**Re-verification:** Yes — after CR-01 exact-transaction binding and CR-02 registry-batch deferral fixes.

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | Certificate authority is exclusively `/cert/<canonical-slot>`; a subject-hash record cannot finalize. | VERIFIED | `GetExpectedCertificateSlotKey` derives the slot from the embedded proposal and prefixes it with `/cert/` ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2643)); `GetCertificateBySlot` reads only that key and rechecks payload/key binding ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3628)). Lifecycle coverage rejects a valid certificate placed at the legacy subject-hash suffix ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:861)). |
| 2 | Competing transactions sharing a slot cannot use a certificate for one to finalize the other. | VERIFIED | Transaction consumers derive the slot from the recovered transaction, then require nonce account, nonce, transaction hash, decoded embedded transaction hash, and slot to agree ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:1517)). The focused account regression asserts only the exact winner reaches `CONFIRMED` ([10-CR-01-FIX-SUMMARY.md](10-CR-01-FIX-SUMMARY.md)). |
| 3 | PubSub receipt never writes the authoritative CRDT record. | VERIFIED | `HandleCertificate` validates and clears volatile proposal state only; it contains no `GlobalDB::Put` call ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2796)). `CertificateReceived` only marks journal work stalled before the CRDT batch commits ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2518)); `RecoverPendingCertificateWork` subsequently reads, validates, and processes the durable record ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3534)). |
| 4 | The deterministic normal-round selected aggregator is the sole production publisher; non-selected nodes retain evidence without writing or advertising. | VERIFIED | The only production `SubmitCertificate` call is after `GetAggregatorRole`; `ActiveButNotAggregator` continues without submission and a selected round alone calls it ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2371)). Search found no other production caller; the remaining direct call is a certificate unit test. |
| 5 | Persistence precedes one best-effort full-certificate PubSub notification; failed notification is observed but never retried. | VERIFIED | `SubmitCertificate` validates, obtains `/cert/<slot>`, uses `PutConvergentImmutable`, and only then invokes `Publish`; an error is logged and returned with no retry loop ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1956)). The publisher still relies on the same callback/readback recovery path as any receiver. |
| 6 | A valid slot collision converges deterministically rather than preserving local first-seen order. | VERIFIED | Immutable CRDT writes use a reserved priority and compare lowercase SHA-256 encodings in the CRDT merge path ([crdt_set.cpp](../../../../src/crdt/impl/crdt_set.cpp)); `SubmitCertificate` applies the same existing-record comparison before write ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1983)). Local order and disconnected two-replica A→B/B→A regressions passed. |
| 7 | Publisher loss/recovery remains the existing protocol-visible round rotation and cannot overwrite/guess an occupied or indeterminate record. | VERIFIED | `ProcessCertificates` attempts no more than once per round, requires the current normal `GetAggregatorRole`, and funnels the selected candidate through the exact slot occupancy/read/write checks ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2330), [Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1956)). An unreadable existing record returns an error before any write; an occupied lower-hash record is left untouched. No lease or delivery-source authority was added. |
| 8 | Hash-starting transaction consumers recover the transaction, derive `GetSlotID()`, and never fall back to `/cert/<hash>`; registry batches are explicitly fail-closed until durable slot membership is designed. | VERIFIED | Replay and confirmation paths fetch the transaction and call `GetCertificateBySlot(transaction.GetSlotID())` ([TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:1022), [TransactionManager.cpp](../../../../src/account/TransactionManager.cpp:4497)); UTXO witness validation does the same and validates exact certificate binding ([GeniusInputValidator.cpp](../../../../src/account/GeniusInputValidator.cpp:471)). Registry finalization is intentionally no-op/deferred, while batch subjects and batch certificates are rejected ([ValidatorRegistry.cpp](../../../../src/blockchain/ValidatorRegistry.cpp:751), [ValidatorRegistry.cpp](../../../../src/blockchain/ValidatorRegistry.cpp:835)). |

**Score:** 8/8 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/crdt/globaldb/*`, `src/crdt/impl/*` | Replicated immutable convergent write primitive | VERIFIED | `PutConvergentImmutable` carries its merge semantics in replicated deltas, rather than relying on read-then-write. |
| `src/blockchain/Consensus.cpp` | Slot authority, selected-only publication, persistence-before-PubSub, recovery | VERIFIED | All authority ingress and durable recovery paths validate the canonical record key. |
| `src/account/TransactionManager.cpp`, `src/account/GeniusInputValidator.cpp` | Transaction-derived, exact-winner lookup | VERIFIED | Missing transaction evidence fails closed; same-slot losing transaction is not accepted. |
| `/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub` | Concrete result from one completed Publish attempt | VERIFIED | `GossipPubSub::Publish` returns `result<void>`, rejects a stopped service synchronously, serializes acceptance with shutdown, and bounds completion waiting to one second ([gossip_pubsub.cpp](/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:885)). |
| `src/blockchain/ValidatorRegistry.cpp` | No unsafe callback-local batch slot association | VERIFIED | CR-02 removes the temporary map and rejects/defer batches rather than consulting a legacy subject-hash certificate record. |

### Requirements Coverage

| Requirement | Status | Evidence |
|---|---|---|
| CERT-01 | SATISFIED | Only validated `/cert/<slot>` retrieval; legacy subject-hash storage is rejected. |
| CERT-02 | SATISFIED | Receiver/PubSub paths do not write; normal-round selected aggregation is the only production write path. |
| CERT-03 | SATISFIED | Immutable durable write precedes exactly one PubSub attempt; actual publish failure is logged. |
| CERT-04 | SATISFIED | Existing deterministic rotation retries normal rounds; occupancy/read errors fail closed and concurrent encodings converge by hash. |
| COMP-01 | SATISFIED | Consumers load transactions and derive slots; no hash-to-slot authority or `/cert/<transaction-hash>` fallback remains. |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Focused consensus and transaction consumers | `ctest --test-dir build/OSX/Release -R '^(consensus_pending_lifecycle_test|consensus_slot_key_test|transaction_manager_certificate_fallback_test)$' --output-on-failure` | All three passed; lifecycle 22.27s, account target 19.65s | PASS |
| Stopped PubSub transport result | `build/OSX/Release/test_bin/pubsub_counts_test --gtest_color=no` | 2/2 passed, including `PublishReportsStoppedTransport` | PASS |
| Local deterministic immutable merge | `build/OSX/Release/test_bin/crdt_test --gtest_filter='*ConvergentImmutable*' --gtest_color=no` | 2/2 passed | PASS |
| Two-node deterministic convergence | `build/OSX/Release/test_bin/globaldb_integration_test --gtest_filter='*ConvergentImmutableConcurrentWrites*' --gtest_color=no` | 2/2 passed with local-network permission | PASS |
| Scope/whitespace | `git diff --check` | Clean | PASS |

The two-replica test correctly requires local network sockets. It fails under the filesystem/network sandbox with `Operation not permitted`; the same unmodified target passed in both synchronization orders once local-network execution was approved.

### Scope Check

No bridge-specific finality record, receiver-side certificate `Put`, publisher lease, subject-hash authority, PubSub notification retry, or registry-batch identity redesign was introduced. The public compatibility methods named `GetCertificateBySubjectHash` now treat their parameter as a slot and have no production callers; they do not construct a legacy key. Registry batch finality is intentionally deferred and fails closed, matching the later user scope decision rather than claiming a callback-local solution is recoverable.

## Conclusion

Phase 10 achieves its goal. Certificate authority is generic and canonical-slot-keyed; the selected normal-round publisher persists the exact validated certificate before one observable best-effort notification, and recipients recover only from durable CRDT state. The exceptional concurrent-write case converges by deterministic certificate hash, while exact transaction binding prevents a same-slot losing mint from consuming the winner's certificate. The remaining convergence-through-one-consumption-path and exactly-once mint work is correctly left to Phase 11.

---

_Verified: 2026-08-21T16:10:00-03:00_  
_Verifier: the agent (gsd-verifier)_
