---
phase: 09-canonical-slot-and-certificate-storage
reviewed: 2026-07-24T16:14:40Z
depth: standard
files_reviewed: 40
files_reviewed_list:
  - evmrelay/include/eth/eth_receipt_source.hpp
  - evmrelay/include/eth/event_filter.hpp
  - evmrelay/src/eth/eth_receipt_source.cpp
  - evmrelay/src/eth/event_filter.cpp
  - src/account/BridgeRelayer.cpp
  - src/account/GeniusInputValidator.cpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/GeniusTransaction.cpp
  - src/account/GeniusTransaction.hpp
  - src/account/MintTransactionV2.cpp
  - src/account/MintTransactionV2.hpp
  - src/account/PublicChainInputValidator.cpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
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
  - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
  - test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp
  - test/src/crdt/crdt_datastore_test.cpp
  - test/src/startup/startup_wiring_test.cpp
findings:
  critical: 3
  warning: 1
  info: 0
  total: 4
status: issues_found
---

# Phase 09: Code Review Report

## Summary

Plans 09-10 through 09-13 close the four findings they targeted: catch-up
publication now preserves the cursor on retry, bridge and certificate reads fail
closed, mixed CRDT decisions preserve dependency barriers, and failed receipt
status is endpoint-local. The post-merge CRDT test synchronization change also
matches the production lifecycle.

Four new issues remain. Three are correctness or process-safety blockers: mint
confirmation can become durable before its UTXO effects succeed, CRDT destruction
can terminate or access a destroyed object when ownership ends on a worker, and
the intentionally concurrent RPC endpoint configuration path accesses an
unprotected container. A receipt-source bridge lifetime issue is also present.

Phase 9's documented distributed boundary is preserved in this review:
certificate preflight is not reported as a distributed compare-and-swap gap.
The phase research explicitly assigns prevention of concurrently formed
certificates to Phase 10's durable one-signature-per-slot rule.

## Narrative Findings (AI reviewer)

### Critical

#### CR-01 — Mint confirmation is made durable before transaction effects succeed

**File/line:** `src/account/TransactionManager.cpp:4636-4706`,
`src/account/TransactionManager.cpp:1791-1826`

**Issue:** `ChangeTransactionState(CONFIRMED)` first writes the in-memory
`CONFIRMED` state and, for `mint-v2`, persists the executed-burn marker. Only
after those irreversible decisions does it call `ParseTransaction`, which
creates produced UTXOs and consumes the bridge input. Any failure from
`PutProducedUTXOs` or `ConsumeUTXOs` is returned after the transaction and burn
have already been classified as complete. A repeated certificate delivery then
hits the already-`CONFIRMED` branch and skips `ParseTransaction`; catch-up also
sees the executed marker as durable `AlreadyHandled`.

The empty-input branch has the same shape: it marks the transaction confirmed
at line 4647 and then breaks at line 4664 without applying effects.

**Impact:** A transient storage failure can permanently leave a certified mint
without its UTXO state while suppressing every retry. If produced-UTXO creation
succeeds and bridge-input consumption fails, the node can also retain a partial
application that is never reconciled.

**Fix:** Introduce an idempotent/transactional application boundary. Apply and
verify all transaction effects before publishing `CONFIRMED` and the executed
burn marker, or persist an explicit applying state whose retries resume safely.
Do not treat a transaction as already confirmed until effect application has
completed. Add fault-injection regressions for produced-UTXO and bridge-input
failures followed by duplicate certificate delivery and restart.

#### CR-02 — Worker-owned CRDT destruction can terminate or use freed state

**File/line:** `src/crdt/impl/crdt_datastore.cpp:90-133`,
`src/crdt/impl/crdt_datastore.cpp:847-850`,
`src/crdt/impl/crdt_datastore.cpp:892-948`,
`src/crdt/impl/crdt_datastore.cpp:990-997`

**Issue:** Worker loops repeatedly promote a weak pointer to a temporary strong
`self`, so releasing the last external owner can either strand the object in a
worker wait or make the final strong reference disappear on an internal worker.
The destructor calls `Close()`. `RequestClose()` then starts a member
`std::thread` capturing raw `this`, while `CancelAndCloseNow()` detects the
worker thread and returns without joining it. Destruction of the still-joinable
thread member invokes `std::terminate`; independently, the coordinator's raw
capture can outlive the object and dereference freed storage.

`WorkerInitiatedShutdownCompletesBeforeBarrierAndRunsNoPostCloseWork` does not
cover this path because the test deliberately retains the external `receiver`
owner and later joins from the test thread.

**Impact:** Normal shared-ownership teardown can leak indefinitely, terminate
the process, or cause use-after-free during shutdown.

**Fix:** Make the asynchronous shutdown operation own a separate lifetime-safe
control state, and ensure `CrdtDatastore` destruction occurs only after all
workers and the coordinator have completed. The destructor must never spawn an
unjoinable raw-`this` coordinator. Add a subprocess/death-safe regression in
which a worker callback releases the last external datastore owner and prove
clean destruction with no post-close work.

#### CR-03 — Runtime RPC endpoint publication races with consensus and validation reads

**File/line:** `src/account/PublicChainInputValidator.cpp:155-168`,
`src/account/PublicChainInputValidator.cpp:226-255`,
`src/account/PublicChainInputValidator.cpp:412-425`,
`src/account/GeniusNode.cpp:711-727`,
`src/account/GeniusNode.cpp:2849-2859`,
`src/account/GeniusNode.cpp:3132-3172`

**Issue:** `SetRpcEndpoints` and `AddRpcEndpoints` mutate the shared
`rpc_endpoints_` unordered map and its endpoint vectors without synchronization.
`GetSlotHash`, receipt verification, and the inline URL/chain accessors read the
same state without synchronization. This is not merely a hypothetical caller
misuse: `InitializeAndStartBridge` posts endpoint discovery asynchronously
because it may block for about 15 seconds, while `ConfigureRpcEndpoint` is
explicitly allowed to publish operator endpoints during that fetch and
consensus vote creation reads slot hashes concurrently.

**Impact:** Concurrent rehash/vector growth and iteration is undefined behavior.
It can crash, corrupt endpoint metadata, produce inconsistent signed slot
hashes, or make receipt quorum depend on a torn configuration snapshot.

**Fix:** Protect endpoint and transport-factory state with a reader/writer lock,
or publish immutable per-chain snapshots through atomic shared pointers. Each
validation or vote operation must hold/use one stable snapshot for its entire
decision. Add a deterministic concurrency regression that blocks asynchronous
provider initialization while operator configuration and slot/quorum reads run.

### Warnings

#### WR-01 — Receipt source retains a callback to a destroyed bridge

**File/line:** `evmrelay/include/eth/eth_receipt_source.hpp:61-87`,
`evmrelay/src/eth/eth_receipt_source.cpp:39-48`,
`evmrelay/src/eth/eth_receipt_source.cpp:73-90`

**Issue:** `EthReceiptSourceBridge` installs a source callback that captures raw
`this`, but the class has no destructor that clears the callback or removes its
remaining source/service subscriptions. Because both collaborators are stored
by reference, either can validly outlive the bridge and deliver a later receipt
batch through the dangling callback.

**Impact:** Destroying a bridge before its receipt source can cause a
use-after-free; stale filters and service watches also remain active.

**Fix:** Add explicit teardown that first prevents new callback dispatch,
clears the source handler, and unwatches every subscription with synchronization
appropriate to the source's dispatch model. Alternatively, use a lifetime token
captured weakly by the handler. Add a test that destroys the bridge, emits a
batch from the still-live source, and verifies no callback or watch remains.

## Verification

- Reviewed the 40 requested files at standard depth, with cross-file tracing of
  Plans 09-10 through 09-13 and the post-merge CRDT lifecycle test change.
- Confirmed the prior four review findings are closed in current source and
  covered by the focused tests recorded in the plan summaries.
- Checked the documented Phase 9/Phase 10 boundary before excluding distributed
  empty-state certificate races from the findings.
- No source or test files were edited, and no commits were created.

## Self-Check

- Frontmatter file count and `files_reviewed_list` both contain all 40 requested
  paths.
- Severity counts sum to four and match the narrative sections.
- Every finding includes a concrete file/line, impact, and proposed fix.
- The protected unrelated `src/account/GeniusNode.cpp` logger hunk and all other
  user-owned dirty paths were left untouched.

---
*Reviewed: 2026-07-24 | Depth: standard | Files: 40 | Findings: 3 critical, 1 warning, 0 info*
