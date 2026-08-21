---
phase: 10-authoritative-slot-certificate-publication
reviewed: 2026-08-21T19:21:00Z
depth: rereview
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
  - src/account/TransactionManager.hpp
  - src/account/GeniusInputValidator.cpp
  - src/blockchain/ValidatorRegistry.hpp
  - src/blockchain/ValidatorRegistry.cpp
  - test/src/crdt/crdt_datastore_test.cpp
  - test/src/crdt/globaldb_integration.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/pubsub_counts/pubsub_counts.cpp
  - test/src/account/transaction_manager_certificate_fallback_test.cpp
  - test/src/account/CMakeLists.txt
  - thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.hpp
  - thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp
findings:
  critical: 0
  warning: 0
  info: 0
  total: 0
status: clean
---

# Phase 10: Code Re-review Report

**Reviewed:** 2026-08-21T19:21:00Z  
**Depth:** rereview  
**Files Reviewed:** 24  
**Status:** clean

## Summary

The corrective changes resolve all three findings from the initial review.

- CR-01 is closed. Authoritative records are still read only through
  `/cert/<transaction GetSlotID()>`; every transaction-facing slot lookup now
  also uses `TransactionManager::CertificateMatchesTransaction()`. That helper
  requires the nonce subject's account, nonce, and `tx_hash`, plus the decoded
  embedded transaction's hash and canonical slot, to match the consumer's
  transaction. The confirmation paths, predecessor/replay paths, conflict
  recovery, and producer UTXO-witness validation all use that binding. No
  production caller reads a legacy `/cert/<subject-hash>` record; the retained
  compatibility method treats its argument as a slot and has no callers.
- CR-02 is closed. The callback-local subject-to-slot map and registry lookup
  seam are gone. Registry batching is explicitly deferred: finalization does
  not construct a batch, registry-batch subjects/certificates are rejected,
  and certificate-based registry updates containing a batch subject fail
  closed. This leaves no restart- or arrival-order-dependent finality state.
- WR-01 is closed. `Publish()` serializes admission with `Stop()` and bounds
  completion waiting to one second, returning a concrete error if shutdown
  prevents the posted work from running. The certificate path persists through
  CRDT before the one PubSub attempt; on that attempt's failure it logs and
  returns, while subsequent processing observes the durable slot record and
  clears the proposal rather than retrying notification.

The convergent immutable CRDT path remains deterministic (lowest SHA-256 of
serialized certificate bytes wins) and its direct and two-replica tests cover
both arrival orders. `git diff --check` passes.

## Findings

No critical, warning, or informational findings.

_Reviewed: 2026-08-21T19:21:00Z_  
_Reviewer: the agent (Phase 10 code re-review)_
