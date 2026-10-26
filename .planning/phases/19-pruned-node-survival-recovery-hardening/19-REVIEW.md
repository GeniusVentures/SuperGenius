---
phase: 19-pruned-node-survival-recovery-hardening
reviewed: 2026-10-24T00:00:00Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - example/node_test/NodeExample.cpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/UTXOManager.cpp
  - src/account/UTXOManager.hpp
  - src/crdt/crdt_datastore.hpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/transaction/TransactionManager.cpp
  - src/transaction/TransactionManager.hpp
  - test/src/account/CMakeLists.txt
  - test/src/account/account_messenger_test.cpp
  - test/src/account/transaction_manager_certificate_fallback_test.cpp
  - test/src/account/transaction_manager_pending_lifecycle_test.cpp
  - test/src/account/tx_lookup_tri_state_test.cpp
  - test/src/account/tx_pruner_test.cpp
  - test/src/account/utxo_manager_test.cpp
  - test/src/crdt/globaldb_erase_test.cpp
findings:
  critical: 0
  warning: 3
  info: 6
  total: 9
status: issues_found
---
fixed: 3
skipped: 0


# Phase 19: Code Review Report

**Reviewed:** 2026-10-24
**Depth:** standard
**Files Reviewed:** 17 (16 in configured scope + `test/src/account/CMakeLists.txt`, reviewed as part of the phase diff)
**Status:** issues_found

## Summary

Reviewed the Phase 19 changes (diff `fda039d46^..HEAD`): the D-01 consumed-preserving clear (`ClearUnspentKeepConsumed`) + checkpoint-rebuild guard (own-replay discriminator, `ReadDurableSpendEvidence`, `CrosscheckRebuiltUtxos`), the D-02 typed `PRE_BROADCAST_VALIDATION` error + bounded SYNCING exit, the SC-5 `RetryDueFailedRoots` isResolved guard, the D-03 tri-state `TransactionLookupResult` + `tx` CLI, and the survival/mesh/restart test suites.

The security-critical resurrection chain holds under scrutiny: the preserve-clear keeps CONSUMED evidence in memory and on disk (verified against `SnapshotAddressUTXOsLocked`/`StoreUTXOSnapshot` semantics — the snapshot is owner-filtered from `utxo_outpoints_`, so survivors and ConsumeUTXOs-orphaned consumed entries both persist); `PutUTXO`'s existing-entry check refuses re-creation by construction; the own-replay discriminator rejects a foreign spender of the same input (negative-control test pins this) and its hash-binding assumption is gated by `CheckTransactionAuthorization`/certificate checks on every CONFIRMED parse path; the crosscheck belt is properly induced (not vacuous) in tests. The three `PRE_BROADCAST_VALIDATION` sites all sit before the first `crdt_transaction->Put`/`Commit`, so the "nothing was published" premise is correct. Test suites are real-stack with bounded waits, raw-disk proofs, and negative controls — no smoke tests.

Three warnings remain: a key-prefix collision defect in `ReadDurableSpendEvidence` (the in-code justification is factually wrong), the bounded SYNCING exit firing while the nonce gap is genuinely unresolved (its own function body still declares that state unsafe to send from), and an unbounded retry hot-loop when the pre-broadcast rollback itself fails.

## Warnings

### WR-01 — FIXED (exact-key equality): `ReadDurableSpendEvidence` prefix query can decode a sibling outpoint's record as evidence

**File:** `src/account/UTXOManager.cpp:941-963`
**Issue:** The comment claims "Exact-key prefix query: the record key identifies one outpoint, so the scan can return at most that record." That is false. `rocksdb::query` is a **prefix scan** (`src/storage/rocksdb/rocksdb.cpp:146` iterates while `iter->key().starts_with(slicePrefix)`), and the record key format is `/utxo/<addr>/<txid>:<idx>` (`BuildUTXORecordKey`, `UTXOManager.cpp:18-24`) — so the exact key for `idx=1` is a string prefix of `idx=10..19` (same for any `d` vs `d*`). When the exact record is absent but a longer sibling exists, `records.value().begin()->second` decodes the **sibling's** record and reports it as spend evidence for the queried outpoint. In the crosscheck, a CONSUMED sibling would falsely revert a legitimate READY outpoint to CONSUMED (node-side fund lock); a READY sibling silently skips. The exact-key-present case happens to work because `QueryResult` is `std::map<Buffer, Buffer>` and a proper prefix sorts before its extension, which makes the latent defect easy to miss.
**Fix:** Require an exact key match and treat longer matches as absent:

```cpp
auto records = db->query( key_buf );
// ...
const auto &first = *records.value().begin();
if ( !( first.first == key_buf ) )
{
    // A longer sibling key (e.g. ":11" for a ":1" probe) matched the prefix —
    // no record exists for THIS outpoint.
    return std::nullopt;
}
const auto &record_value = first.second;
```

### WR-02 — FIXED (gap-guarded exit): Bounded SYNCING exit fires while the nonce gap is genuinely unresolved, and the send gate cannot catch the stale nonce

**File:** `src/transaction/TransactionManager.cpp:3704-3719`
**Issue:** The timeout exit at the top of `SyncNonce` does not discriminate "wedged with no possible advance" from "still legitimately behind" — yet the same function's `proposed_nonce < expected_next_nonce` branch (lines 3769-3785) states: *"If we're behind at all, we need to catch up — even a gap of 1 means there's transaction data in CRDT that we don't have, and we cannot safely propose new transactions until we're caught up."* After the exit returns READY, the next queued send is gated only against the **local** confirmed nonce (`SendTransactionItem`, lines 2057-2069 and the nonce check at 2119), so a nonce the network already consumed for txs this node never received passes the gate and is Put/Commit-published to CRDT (lines 2163-2184) before any consensus-side rejection. `pending_nonces_` (the safety the comment cites) only holds *this node's own in-flight reservations* (`GeniusAccount.cpp:913-921`) — it offers no protection against network-side nonces inside the gap. On a pruned node (the exact Phase 19 scenario) the gap may be permanent, so every send becomes a 60-second-throttled stale-nonce publish cycle. Note also that `durable_nonce` (line 3710) is read for the log line only — nothing "falls back" to it; the log message overstates what the code does.
**Fix:** Do not arm the exit while the sync is making a legitimate behind-gap claim, e.g. only allow the timeout exit when the gap cannot be closed locally:

```cpp
if ( syncing_since_.has_value() )
{
    const auto window  = std::chrono::milliseconds( syncing_exit_timeout_for_test_.load() );
    const auto elapsed = std::chrono::steady_clock::now() - syncing_since_.value();
    if ( elapsed > window && account_m->GetProposedNonce() >= last_seen_expected_next_nonce_ )
    {
        // wedge class only: proposed is NOT behind the network
        ...exit as today...
    }
}
```

(Track the last observed `expected_next_nonce` per SYNCING episode; when proposed is behind, keep requesting heads and let the existing missing-tx bounded recovery — or a wider, explicitly documented survival policy — own the exit.) At minimum, the comment and log text must match the actual behavior (state change only, no nonce reassignment).

### WR-03 — FIXED (3-attempt cap): Pre-broadcast rollback failure leaves the queue front in place — deterministic re-failure loop with no cap

**File:** `src/transaction/TransactionManager.cpp:590-604`
**Issue:** When `RollbackTransactions` fails in the new `PRE_BROADCAST_VALIDATION` branch, the code logs, `break`s, and leaves the transaction at the front of `tx_queue_m` in the READY state. Pre-broadcast validation failures are deterministic (size/witness/commitment preflight over the same bytes), so every subsequent ~300 ms tick re-runs send → same preflight failure → rollback retry: an unbounded hot loop of ERROR/WARN logging and wasted work for as long as the underlying rollback failure persists (e.g. storage down). The pre-existing SYNCING branch shares the `break`-without-pop shape but at least exits READY into `SyncNonce`'s slower cadence; the new branch stays in the send path.
**Fix:** Cap the retry or drop the batch when its rollback keeps failing, e.g. after N consecutive pre-broadcast rollback failures, force-pop with a terminal `RemoveTransactionFromProcessedMaps` (accepting the leaked reservation, already released by the FAILED transition attempt) so later queue entries are not starved.

## Info

### IN-01: SC-5 guard clears the retry ladder but leaves the `pending_jobs_` FAILED entry behind

**File:** `src/crdt/impl/crdt_datastore.cpp:1854-1861`
**Issue:** The resolved-root guard calls only `ClearFailedRootRetryLocked(cid)`; `pending_jobs_[cid]` stays FAILED forever. The other resolved-site (lines 749-754) erases both. This matches the give-up path's semantics (line 1797 also leaves the FAILED entry) so it is likely benign today, but the two "root is resolved" sites now diverge — future accounting built on `pending_jobs_` will see phantom failed jobs.
**Fix:** Erase the `pending_jobs_` entry under the same lock when clearing the ladder entry for a resolved root, mirroring lines 749-754.

### IN-02: own-replay guard reads spender evidence unsynchronized and leans on signature gating — document the assumption

**File:** `src/transaction/TransactionManager.cpp:2692-2718`
**Issue:** Two properties worth pinning. (1) The `IsOutPointConsumed` / `GetOutPointSpender` sequence takes and releases `utxos_mutex_` per call — a concurrent `ConsumeUTXOs` between the reads can flip the verdict (TOCTOU). This mirrors the pre-existing `any_of` check's shape and TickOnce's serialized strand makes it unlikely, but the guard is new security-sensitive code. (2) `GetHash()` is `dag_st.data_hash()` — a proto field, not recomputed — so the discriminator's soundness rests entirely on `CheckTransactionAuthorization`/certificate validation binding hash to content on every CONFIRMED path (which this review confirmed is the case today).
**Fix:** Add a single `UTXOManager` helper (e.g. `EveryConsumedInputSpentBy(inputs, spender)` under one `shared_lock`) and use it here; add a comment at the guard stating the signature-gating assumption explicitly.

### IN-03: `TransactionLookupResult::verdict` is uninitialized by construction

**File:** `src/transaction/TransactionManager.hpp:504-515`
**Issue:** The opaque-enum member has no default; a future construction site that forgets `verdict` ships an indeterminate value (UB on read). The comment mandates manual initialization, which only holds until the second construction site appears.
**Fix:** Provide a small factory (`static TransactionLookupResult Found(std::shared_ptr<GeniusTransaction>)` / `FromVerdict(...)`) in the .cpp so no caller can default-construct a half-built result, or add `verdict = PrunedLookup::NOT_FOUND;` at every construction site as a lint-enforced pattern.

### IN-04: Checkpoint-mismatch rebuild cannot restore unspent outputs whose creating tx was pruned locally

**File:** `src/transaction/TransactionManager.cpp:3372-3383`
**Issue:** After the preserve-clear, the rebuild is `QueryTransactions()` — a **local** CRDT enumeration only. A READY UTXO created by a locally pruned transaction (marker present, value gone) has just been erased from disk by the clear and nothing can re-create it: the balance is lost on this node until a peer re-delivers. This is not a regression (the old `SetUTXOs` wipe had the same hole) and the checkpoint record stores only the Merkle root (no UTXO set to restore), but for a phase titled "pruned-node survival" the interaction is worth an explicit plan note.
**Fix:** None required for this phase; record the known availability limit (or a future checkpoint-carried UTXO set) in the phase docs.

### IN-05: Crosscheck revert erases the UTXO type discriminator on bridge-typed outpoints

**File:** `src/transaction/TransactionManager.cpp:3522-3525`
**Issue:** `CrosscheckRebuiltUtxos` reverts through `ConsumeUTXOs(..., UTXOType::UTXO_NORMAL, ...)`. For a READY entry whose `entry.type` is `UTXO_BRIDGE`, the type match in `ConsumeUTXOs` fails, `utxo_found` is false, and the entry is replaced by a zero-amount NORMAL tombstone — the outpoint ends CONSUMED (goal achieved) but the bridge typing and genuine amount metadata are clobbered, and the `false` return value is ignored.
**Fix:** Read the durable record's type (or the registry entry's type) and pass it through; or note the degradation as accepted in the belt's contract.

### IN-06: Blind 8-second sleep in the stalled-delta pruned-block test

**File:** `test/src/crdt/globaldb_erase_test.cpp:888`
**Issue:** `std::this_thread::sleep_for( std::chrono::seconds( 8 ) )` with the stated justification that no observable predicate exists. Under heavy CI load the worker pass may not have run within the window, making the zero-fetch assertion pass vacuously (weakens the test rather than flaking it).
**Fix:** Expose a ForTest counter for RetryStalledDeltas passes (the file already uses that seam pattern elsewhere) and wait on "at least one pass ran" instead of the wall clock.

---

_Reviewed: 2026-10-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
