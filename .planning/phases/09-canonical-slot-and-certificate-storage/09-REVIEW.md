---
phase: 09-canonical-slot-and-certificate-storage
reviewed: 2026-07-24T12:33:04Z
depth: standard
files_reviewed: 35
files_reviewed_list:
  - evmrelay/include/eth/eth_receipt_source.hpp
  - evmrelay/include/eth/event_filter.hpp
  - evmrelay/src/eth/eth_receipt_source.cpp
  - evmrelay/src/eth/event_filter.cpp
  - src/account/BridgeRelayer.cpp
  - src/account/GeniusInputValidator.cpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusTransaction.cpp
  - src/account/GeniusTransaction.hpp
  - src/account/MintTransactionV2.cpp
  - src/account/MintTransactionV2.hpp
  - src/account/PublicChainInputValidator.cpp
  - src/account/TransactionManager.cpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - src/blockchain/impl/Blockchain.cpp
  - src/crdt/crdt_data_filter.hpp
  - src/crdt/crdt_datastore.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/impl/crdt_data_filter.cpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/watcher/impl/bridge_catchup_watcher.cpp
  - src/watcher/impl/bridge_catchup_watcher.hpp
  - test/src/account/bridge_event_identity_test.cpp
  - test/src/account/bridge_relayer_test.cpp
  - test/src/account/public_chain_mint_validation_test.cpp
  - test/src/account/transaction_manager_pending_lifecycle_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/certificate_compatibility_test.cpp
  - test/src/blockchain/consensus_certificate_store_test.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/blockchain/consensus_slot_key_test.cpp
  - test/src/crdt/crdt_datastore_test.cpp
findings:
  critical: 2
  warning: 4
  info: 0
  total: 6
status: issues_found
---

# Phase 09: Code Review Report

## Summary

The post-gap Phase 09 implementation closes the previously reported receipt-proof,
canonical-serialization, typed-lookup, legacy-namespace, and registry-retry gaps.
The scoped build and all nine requested test binaries pass. Two correctness
blockers remain at durability boundaries, plus four robustness issues in bridge
validation and CRDT lifecycle/filter behavior.

## Narrative Findings (AI reviewer)

### Critical

#### CR-01 — Catch-up commits the cursor after recoverable mint-submission failures

**File/line:** `src/watcher/impl/bridge_catchup_watcher.cpp:531-583`,
`src/watcher/impl/bridge_catchup_watcher.cpp:592-602`,
`src/account/GeniusNode.cpp:2973-3031`

**Issue:** Receipt resolution is now staged atomically, but publication is not.
After the chunk has been validated, a `false` return or exception from
`burn_processor_` is counted as “already processed” and processing continues.
The chunk's dedup state is then committed and `from_block` advances. The real
callback returns `false` for several conditions that are not durable duplicate
proofs: an unavailable node/account, a reserved outpoint, a failed
`MintTokens`/transaction-manager submission, and exceptions.

**Impact:** A transient node or transaction-manager failure can cause a valid
historical burn to be skipped permanently once the catch-up cursor advances.

**Fix:** Replace the boolean callback contract with an explicit result such as
`Processed`, `AlreadyHandled`, and `Retry`. Advance the chunk only when every
staged burn is either processed or proven durably handled; abort publication and
preserve the cursor on `Retry`. Add integration coverage for unavailable
transaction-manager, temporary reservation, `false`, and exception paths.

#### CR-02 — Certificate write-once checks treat datastore errors as absence

**File/line:** `src/blockchain/Consensus.cpp:1869-1936`,
`src/blockchain/Consensus.cpp:2582-2603`

**Issue:** Both local `SubmitCertificate` and the remote certificate delta
filter call `db_->Get` for the slot and transaction index, but only inspect
`has_value()`. `NOT_FOUND`, corruption, I/O failure, shutdown, and other
operational errors are therefore indistinguishable. Local submission can write
a new pair after an unknown read, while remote filtering can approve a pair
without establishing whether it conflicts with durable state.

**Impact:** A temporary or integrity-related read failure can bypass the
certificate store's write-once preflight and admit conflicting or partial
canonical state.

**Fix:** Classify both reads with the typed certificate-store error mapper.
Treat only exact `NOT_FOUND` as absence; reject/fail closed on all integrity and
operational errors before any write or merge. Add injected-error tests for both
local submission and remote filtering, including asymmetric slot/index errors.

### Warnings

#### WR-01 — Bridge mint replay protection fails open when its durable read fails

**File/line:** `src/account/TransactionManager.cpp:652-675`

**Issue:** `MintFunds` rejects a persisted duplicate only when the underlying
datastore `get` returns a value. Any read error falls through to UTXO creation,
reservation, and proposal enqueueing.

**Impact:** During corruption or storage unavailability, the restart-persistent
executed-burn guard becomes ineffective and duplicate mint work can be created.

**Fix:** Distinguish `NOT_FOUND` from all other datastore errors and return a
storage failure before mutating UTXO or queue state. Add fault-injection tests
that assert no mutation for corruption and I/O errors.

#### WR-02 — A mixed reject/retry delta loses the dependency retry

**File/line:** `src/crdt/impl/crdt_data_filter.cpp:133-219`,
`src/crdt/impl/crdt_data_filter.cpp:221-289`,
`src/crdt/impl/crdt_datastore.cpp:1390-1409`

**Issue:** When one matching delta filter returns `RetryDependency` and another
returns `Reject`, the aggregate becomes `Reject`. Only the rejecting
namespace is removed, the dependency is discarded, and the remaining delta is
returned as `Reject`. The datastore parks only `RetryDependency`; it merges all
other returned deltas. Consequently, the retry-dependent namespace can merge
without its dependency after being combined with an unrelated rejected
namespace.

**Impact:** An attacker can co-package rejected data with dependency-stalled
data and bypass the latter's validation barrier.

**Fix:** Preserve retry semantics for every retained retry-dependent namespace.
For example, sanitize rejected namespaces and return `RetryDependency` with the
remaining original delta, or terminally remove both classes. Add a two-filter
mixed-decision regression that proves stalled keys remain invisible.

#### WR-03 — One failed-status RPC response vetoes the weighted endpoint policy

**File/line:** `src/account/PublicChainInputValidator.cpp:437-473`

**Issue:** Transport, parse, transaction-hash, and log mismatches contribute
zero weight and allow later endpoints to vote. A missing or false receipt status
instead returns `false` immediately.

**Impact:** One stale or malicious endpoint placed before otherwise sufficient
independent endpoint weight can deny a valid mint, contrary to the preserved
weighted verification policy.

**Fix:** Treat failed receipt status as an endpoint failure (`continue`) and
let the configured weight threshold decide the result. Add a disagreement test
with one failed-status endpoint followed by at least 75 successful weight.

#### WR-04 — Shutdown reads shared queues without synchronization and may return before close completes

**File/line:** `src/crdt/impl/crdt_datastore.cpp:884-924`

**Issue:** The opening shutdown log reads `pending_jobs_`,
`selfCreatedJobList_`, `rootCIDJobList_`, `pendingRootQueue_`, and
`activeRootCID_` before acquiring `dagWorkerMutex_` while worker threads can
still mutate them. If shutdown is initiated by an internal worker,
`CancelAndCloseNow` starts a detached helper and returns before worker joins and
draining finish.

**Impact:** The unsynchronized container access is undefined behavior, and the
worker-thread path breaks the synchronous “closed on return” contract relied on
to prevent post-close callbacks.

**Fix:** Snapshot queue state under `dagWorkerMutex_` and make the close
contract explicit. A synchronous API must not return until another safe owner
has completed joins and drain; otherwise expose a distinct asynchronous close
operation and require a completion barrier. Add a worker-initiated shutdown
test that asserts no callback or queued work occurs after completion.

## Verification

- Built the nine requested targets successfully:
  `consensus_certificate_store_test`, `certificate_compatibility_test`,
  `consensus_pending_lifecycle_test`, `consensus_slot_key_test`,
  `bridge_event_identity_test`, `bridge_relayer_test`,
  `public_chain_mint_validation_test`,
  `transaction_manager_pending_lifecycle_test`, and `crdt_test`.
- Ran all nine binaries sequentially; all exited successfully.
- `git diff --check` passed before this report was written.

---

*Reviewed: 2026-07-24 | Depth: standard | Files: 35 | Findings: 2 critical, 4 warnings, 0 info*
