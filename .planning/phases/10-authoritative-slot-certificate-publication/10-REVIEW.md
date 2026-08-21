---
phase: 10-authoritative-slot-certificate-publication
reviewed: 2026-08-21T18:49:11Z
depth: standard
files_reviewed: 24
files_reviewed_list:
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/crdt_datastore.hpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/crdt/crdt_set.hpp
  - src/crdt/impl/crdt_set.cpp
  - src/blockchain/Consensus.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/impl/Blockchain.cpp
  - src/account/AccountMessenger.cpp
  - src/account/TransactionManager.cpp
  - src/account/GeniusInputValidator.cpp
  - src/blockchain/ValidatorRegistry.hpp
  - src/blockchain/ValidatorRegistry.cpp
  - test/src/crdt/crdt_datastore_test.cpp
  - test/src/crdt/globaldb_integration.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/pubsub_counts/pubsub_counts.cpp
  - test/src/blockchain/validator_registry_certificate_lookup_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/account/transaction_manager_certificate_fallback_test.cpp
  - test/src/account/CMakeLists.txt
  - thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.{hpp,cpp}
findings:
  critical: 2
  warning: 1
  info: 0
  total: 3
status: issues_found
---

# Phase 10: Code Review Report

**Reviewed:** 2026-08-21T18:49:11Z  
**Depth:** standard  
**Files Reviewed:** 24  
**Status:** issues_found

## Summary

The authority-path changes have the intended positive shape: CRDT ingress verifies the canonical `/cert/<slot>` key, receipt only journals/stalls work before commit, the selected round aggregator persists before one best-effort PubSub notification, and the convergent immutable CRDT priority deterministically selects the lowest serialized-content hash. The external PubSub API now also exposes a real error result.

However, migrating a certificate key from a subject hash to a shared canonical slot removes the old implicit proof that the record certifies the particular transaction being consumed. Several newly migrated transaction and witness paths now treat *any* valid certificate in that slot as finality for the transaction they were given. That confirms losing competing Mints—the exact outcome this milestone is intended to prevent. The registry update migration additionally introduces an unpersisted subject-hash-to-slot lookup that makes batch-update validation fail after a restart or on a validator that did not process the original certificate callback. Finally, the synchronous PubSub result implementation can wait forever during a Stop/Publish race.

## Critical Issues

### CR-01: Slot existence is treated as certification of every competing transaction

**Files:** `src/account/TransactionManager.cpp:1807`, `src/account/TransactionManager.cpp:3327`, `src/account/GeniusInputValidator.cpp:466`, `src/account/TransactionManager.cpp:4472`

**Issue:** `CheckCertificateForSlot()` establishes only that an approved certificate occupies a canonical slot. It intentionally does not compare the certificate's embedded proposal/transaction with the candidate transaction. After the migration, the transaction manager uses that broad predicate to mark an incoming transaction `CONFIRMED` (lines 1807 and 3327), and the input validator accepts the slot certificate's UTXO commitment for the referenced producer transaction (lines 466-482).

For competing Mints A and B with the same burn-derived slot, a certificate for A makes `CheckCertificateForSlot(B.GetSlotID())` true. If B arrives or is reprocessed afterward, it is marked confirmed despite never being the certified proposal. The witness path can likewise use A's certified subject/commitment while validating B's transaction hash. The legacy subject-hash key prevented this accidentally; the new shared slot requires an explicit exact-subject check at every consumer boundary.

`ConsensusManager::CheckCertificateForSubject(const Subject&)` already demonstrates the required second step: derive the slot, load the authoritative certificate, and compare the complete computed subject identity. The transaction-facing paths must build/retain the expected nonce subject (or, at minimum, verify the decoded certificate nonce's `tx_hash` and embedded transaction hash equal the transaction being consumed) before confirming, chaining a previous transaction, or accepting its witness. A slot-only predicate may still be used to answer whether a slot is occupied, but not whether a particular transaction won it.

**Missing regression:** create two valid, same-slot Mint V2 transactions with distinct transaction/subject hashes, write a certificate for only one, and prove that only the winner can reach `CONFIRMED` or supply a valid producer certificate/witness. The current lookup test checks only that a slot record exists, so it cannot detect this failure.

### CR-02: Registry-batch certificate resolution depends on ephemeral callback state

**Files:** `src/blockchain/ValidatorRegistry.cpp:752-760`, `src/blockchain/ValidatorRegistry.cpp:789-815`, `src/blockchain/ValidatorRegistry.cpp:976-989`, `src/blockchain/ValidatorRegistry.cpp:1332-1340`

**Issue:** The change replaces direct certificate lookup with `pending_certificate_slots_by_subject_`, an in-memory map populated only by `OnFinalizedCertificate()` (line 812). It is neither durable nor rebuilt by `InitializeCache()` (which only restores the current registry at lines 2014-2045). Yet both batch-certificate handling and persisted registry-update verification require this map and reject when it is absent.

Consequently, a validator restarted after receiving the source certificates has the authoritative `/cert/<slot>` records but no subject-to-slot associations. It rejects a subsequently replicated registry batch/update containing those subject hashes. The same happens to a late-joining validator that receives an update without having run the original certificate callback. This changes registry-update validity based on local uptime and message order, compromising registry convergence and liveness.

Do not make a certificate lookup required for registry validation depend on ephemeral receipt metadata. Either defer this registry-batch migration as Phase 10's D-14 boundary requires, or introduce a protocol/durable representation from which every validator can deterministically obtain the source certificate slot and rebuild it on startup. Add a restart and late-join test that validates the same batch/update using only durable CRDT data.

## Warnings

### WR-01: PubSub `Publish()` can block forever when shutdown races a send

**Files:** `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:880-920`, `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:617-620`, `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:750-762`

**Issue:** The new result contract posts a closure to the strand and calls `future.get()` without a timeout. `IsStarted()` is checked before the post. A concurrent `Stop()` can set `m_started` false, queue/perform its stop work, and stop the I/O context after the check but before the publish closure executes. A task posted to the stopped context will not set `completion`, so `future.get()` waits indefinitely.

This turns the best-effort certificate notification into a potential consensus-thread liveness failure during shutdown/restart—the caller cannot log the error and return as D-06/D-08 require.

Use a synchronized lifecycle/strand handoff so shutdown either drains or explicitly completes pending publish promises with `SERVICE_NOT_RUNNING`; alternatively use a bounded wait that returns a shutdown/transport error, and ensure `Stop()` resolves queued publish completions. Add a deterministic Stop-versus-Publish regression that asserts `Publish()` returns an error rather than hanging.

---

_Reviewed: 2026-08-21T18:49:11Z_  
_Reviewer: the agent (gsd-code-reviewer)_  
_Depth: standard_
