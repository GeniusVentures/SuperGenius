---
phase: 11-convergent-certificate-consumption-mint-recovery
reviewed: 2026-08-24T13:37:45Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - src/blockchain/Blockchain.hpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/account/transaction_manager_certificate_fallback_test.cpp
findings:
  critical: 2
  warning: 1
  info: 0
  total: 3
status: issues_found
---

# Phase 11: Code Review Report

**Reviewed:** 2026-08-24T13:37:45Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

The review traced the certificate callback, durable readback, transaction selection, Mint parsing, UTXO persistence, marker write, and terminal tracking paths. The new marker ordering is not a durable boundary after a UTXO write failure, and the CRDT-first path can apply a forged transaction whose hash field merely claims to be the certified hash. Recovery dispatch is also not exclusively claimed, so concurrent retries can re-run successful Mint lifecycle side effects.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01 [BLOCKER]: Failed UTXO persistence is converted into terminal Mint completion on retry

**File:** `src/account/TransactionManager.cpp:1969-1972` (with the called persistence behavior in `src/account/UTXOManager.cpp:159-178`)

**Issue:** `PutUTXO` inserts the outpoint into its in-memory maps before calling `StoreUTXOs`. If that storage call fails, this handler correctly returns an error once, but the failed in-memory insertion remains. On the journal retry, `PutUTXO` returns `false` at the existing-outpoint branch without attempting `StoreUTXOs`; `BOOST_OUTCOME_TRY` treats that `false` result as success. The retry therefore writes `/bridge/executed/...` and marks the Mint `CONFIRMED` at `TransactionManager.cpp:5412-5417` even though its output was never durable. A restart then observes the marker/terminal state and loses the Mint UTXO permanently.

**Fix:** Make an output's "already present" result prove durable progress, not only in-memory presence. The robust repair is to make `UTXOManager::PutUTXO` atomic with respect to persistence (roll back its map insertion if `StoreUTXOs` fails), or add a recovery-safe API that re-persists and verifies an existing outpoint before it is accepted as idempotent progress. Add a fault-injection regression for `StoreUTXOs` failure followed by retry and restart; it must assert that the marker cannot be written until the output is read back from durable storage.

### CR-02 [BLOCKER]: CRDT-first certificate handling accepts a forged hash field and can mint altered payloads

**File:** `src/account/TransactionManager.cpp:1719-1736`

**Issue:** The new CRDT lookup accepts a candidate when its stored `data_hash` field equals `tx_hash`, but never calls `CheckHash()`. `GetHash()` only returns that stored field (`GeniusTransaction.cpp:98-101`), while `CheckHash()` is the recomputation that binds it to the serialized transaction (`GeniusTransaction.cpp:46-57`). This matters because the subsequent binding check also compares only hash/slot fields (`TransactionManager.cpp:1529-1542`), and a Mint V2 slot covers only the first output and first input (`MintTransactionV2.cpp:217-232`). A signed CRDT payload with a forged `data_hash`, the certified source/nonce and slot fields, but extra outputs or altered non-slot fields can therefore be selected ahead of the certificate's embedded transaction and applied as the winning Mint.

**Fix:** Require an integrity check before returning a CRDT candidate, and reject/stall rather than process a malformed value:

```cpp
if (transaction.value()->GetHash() == tx_hash && transaction.value()->CheckHash()) {
    return std::optional<std::shared_ptr<GeniusTransaction>>{transaction.value()};
}
```

Also add a regression that stores a Mint V2 with a deliberately forged `data_hash` but the certified slot/hash strings and a changed second output; certificate recovery must not apply it. Ideally make `CertificateMatchesTransaction` enforce `transaction.CheckHash()` as defense in depth for every candidate source.

## Warnings

### WR-01 [WARNING]: Certificate recovery has no per-entry claim, allowing concurrent Mint replays

**File:** `src/blockchain/Consensus.cpp:3539-3583` and `src/account/TransactionManager.cpp:5397-5420`

**Issue:** `RecoverPendingCertificateWork` processes every stalled entry directly; it never changes the entry to `Processing` or otherwise claims it before invoking the handler. It can run from the round-timer path and synchronously from `RegisterCertificateHandler` (`Consensus.cpp:287-289`) at the same time. Both calls can observe the same stalled certificate and execute `ChangeTransactionState`. The Mint branch drops `tx_mutex_m` before parsing, so both executions can run `ParseTransaction` and independently increment tracking/UTXO-state versions even though the UTXO outpoint itself is deduplicated. This violates the intended exactly-once lifecycle semantics and makes the durability race in CR-01 easier to trigger.

**Fix:** Add an atomic, lease-backed per-key claim to the work journal (or a `ConsensusManager` recovery mutex) before durable readback/dispatch. Only the claimant may call the handler; it should mark the entry stalled on error and done on success. Add a two-thread regression that races timer-style recovery with handler-registration recovery and asserts one handler invocation and one UTXO-state version advance.

---

_Reviewed: 2026-08-24T13:37:45Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
