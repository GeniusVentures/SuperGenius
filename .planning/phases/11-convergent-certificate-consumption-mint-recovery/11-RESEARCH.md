# Phase 11: Convergent Certificate Consumption & Mint Recovery - Research

**Researched:** 2026-08-24  
**Domain:** C++17 certificate recovery, transaction lifecycle, RocksDB-backed UTXOs, and bridge Mint completion  
**Confidence:** HIGH for the existing paths and required defects; MEDIUM for a test-only storage-failure seam.

## Constraints Carried Forward

- `/cert/<canonical-slot>` is the sole certificate authority. Do not add a bridge finality record or a second certificate-acceptance journal.
- Phase 10's exact certificate/transaction binding remains mandatory at every consumption point. A same-slot losing Mint must never become confirmed or apply outputs.
- Reuse `ConsensusManager`'s CRDT work journal and existing transaction/UTXO persistence. The phase may strengthen their transitions, but must not introduce an independent Mint state machine.
- Certificate-first handling is **CRDT transaction first**, then only the exact embedded certificate transaction as fallback.
- A Mint's output effects must be made durable before its existing `/bridge/executed/<chain>:<source>` marker. Missing marker after effects is retryable; no rollback and no operator-only terminal state.

## Requirements → Existing Evidence → Needed Work

| Requirement | Existing evidence | Minimal remaining work |
|---|---|---|
| CERT-05 | Every CRDT certificate callback calls `CertificateReceived`, which marks the shared CRDT work journal stalled; `RecoverPendingCertificateWork` rereads the durable record, validates the canonical key and certificate, then dispatches `ProcessCommittedCertificate`. PubSub `HandleCertificate` only validates/clears volatile proposal state and never writes or completes the record. | Preserve this as the only ingress completion path. Do not bypass it from the publisher or PubSub. Change absent-handler startup recovery from terminal `MarkDone` to retryable work and retrigger recovery after handler registration, so a certificate durable before `TransactionManager::New` reaches the same handler. |
| MINT-01 | `FetchAndProcessTransaction` derives `GetSlotID()`, loads `/cert/<slot>`, calls `CertificateMatchesTransaction`, and selects `CONFIRMED` only for the exact winner. `UTXOManager::PutUTXO` is outpoint-keyed and returns `false` for an existing output, making output insertion replay-safe. | Make `OnConsensusCertificate` use the same exact-binding gate and CRDT-first transaction lookup. Keep repeated confirmed work a no-op only after Mint completion has actually succeeded. |
| MINT-02 | The existing certificate work journal persists `Stalled` work and retries it on startup/timer. UTXOs persist to RocksDB. | Correct the current pre-application executed-marker order and error swallowing so journal retry is retained until all Mint UTXOs and the marker are durable. This gives the required equivalent atomic boundary without a new journal. |

## What Already Converges Correctly

```text
authoritative /cert/<slot> CRDT update (publisher or replica)
  -> pre-commit CertificateReceived: MarkSeen + MarkStalled
  -> post-commit RecoverPendingCertificateWork
       -> durable db->Get(/cert/<slot>)
       -> exact key / structural / quorum validation
       -> release matching active vote, clear volatile proposal
       -> TransactionManager certificate handler
       -> MarkDone only when handler succeeds

transaction CRDT update or startup scan
  -> FetchAndProcessTransaction
  -> transaction.GetSlotID()
  -> GetCertificateBySlot(slot) + CertificateMatchesTransaction
  -> VERIFYING when absent/nonmatching; CONFIRMED only for exact winner
```

This is already the desired one certificate-ingress boundary: the callback is deliberately pre-commit, and recovery—not the notification source—reads the durable authoritative value. `RecoverPendingCertificateWork()` runs both when `ConsensusManager` is created and on the consensus timer. A handler error or `Check::Stalled` leaves the journal entry retryable. [CITED: `src/blockchain/Consensus.cpp`]

`PutUTXO` inserts by `(transaction hash, output index)`, returns `false` when that outpoint already exists, and persists the address snapshot after a new insert. Therefore replaying a partially applied Mint can safely retain already stored outputs and fill only missing outputs, provided parser errors are propagated. [CITED: `src/account/UTXOManager.cpp`]

## Actual Gaps and Their Consequences

### 1. Certificate recovery can finish before the transaction handler exists

`Blockchain::New()` constructs `ConsensusManager` before `GeniusNode` creates `TransactionManager`. `ConsensusManager::New()` immediately calls `RecoverPendingCertificateWork()`. In `ProcessCommittedCertificate`, a missing subject handler currently calls `MarkDone(key)`. Thus, on restart, a durable certificate can lose its embedded-transaction fallback opportunity before `TransactionManager::New()` registers the nonce handler. This is a real certificate-first/restart gap when the winning transaction is not otherwise discovered from CRDT startup scanning. [CITED: `src/blockchain/impl/Blockchain.cpp`, `src/account/GeniusNode.cpp`, `src/blockchain/Consensus.cpp`]

**Minimal resolution:** a durable accepted certificate with no currently registered handler stays `Stalled`, not `Done`; after `RegisterCertificateHandler` successfully installs a handler, invoke the existing certificate-work recovery. No new journal key or certificate authority is needed. This retains generic work semantics and lets normal error/stall handling govern retry.

### 2. Certificate-first consumption currently skips the required CRDT-first lookup

`OnConsensusCertificate(tx_hash, certificate)` first searches only `tx_processed_m` through `GetTransactionByHash`; it never looks in GlobalDB. When that in-memory map is empty it immediately deserializes the embedded certificate transaction. That violates D-04's CRDT-first policy and creates a separate route from normal transaction ingestion. [CITED: `src/account/TransactionManager.cpp`]

**Minimal resolution:** add a small private transaction-by-hash recovery helper, or reuse a tightly scoped loop over `GetMonitoredNetworkIDs()` and `FetchTransaction(globaldb_m, GetTransactionPath(network, tx_hash))`. It must accept only a deserialized transaction whose `GetHash()` equals `tx_hash`; on CRDT miss, decode the embedded transaction; on both paths require `CertificateMatchesTransaction(certificate, *tx)` before confirmation. The helper must never derive finality from a subject hash or use `/cert/<tx_hash>`.

This keeps the two arrival orders convergent:

```text
transaction first: FetchAndProcessTransaction -> exact slot certificate -> CONFIRMED
certificate first: tracked tx / CRDT tx -> same ChangeTransactionState(CONFIRMED)
                                  CRDT miss -> exact embedded tx -> same ChangeTransactionState(CONFIRMED)
restart: certificate journal retry after handler registration, or transaction scan -> same two paths
```

### 3. The bridge-executed marker is persisted before Mint UTXOs and its error is discarded

`ChangeTransactionState(CONFIRMED)` currently:

1. inserts the in-memory tracking entry as `CONFIRMED`;
2. writes `/bridge/executed/<chain>:<source>` (logs but ignores a write failure);
3. calls `ParseTransaction`, which creates Mint V2 UTXOs.

A crash after step 2 records completion before the effects. This directly contradicts D-07. A marker write failure is also silently treated as success, contradicting D-08. [CITED: `src/account/TransactionManager.cpp`]

There is a second retry-loss edge: if `ParseTransaction` fails after the tracking entry becomes `CONFIRMED`, the certificate handler returns an error and the CRDT work journal correctly stalls. On its next recovery, however, `ChangeTransactionState(CONFIRMED)` sees the in-memory entry already confirmed and simply breaks/returns success without parsing again. The journal can then be marked done despite missing local effects. This must be fixed along with the order, rather than adding a parallel state record.

### 4. Mint V2 parser currently ignores UTXO persistence errors

`ParseMintTransaction(MintTransactionV2)` ignores every `PutUTXO` and `ConsumeUTXOs` result. `PutUTXO` mutates memory before `StoreUTXOs`; a RocksDB failure is therefore meaningful and must reach the certificate journal. Partial output progress is still safe because a replay sees existing outpoints as benign `false`, while a genuine persistence error is an `outcome` failure. [CITED: `src/account/TransactionManager.cpp`, `src/account/UTXOManager.cpp`]

**Minimal resolution:** propagate `outcome` errors from Mint V2 output insertion/bridge-input consumption, while treating an already-present output as the existing idempotent success case. Do not roll back prior outputs on an error: replay is the recovery method the user selected.

## Recommended Implementation Shape

### A. Keep certificate work stalled until a transaction handler can consume it

**Files:** `src/blockchain/Consensus.hpp`, `src/blockchain/Consensus.cpp`, `test/src/blockchain/consensus_pending_lifecycle_test.cpp`.

- In `ProcessCommittedCertificate`, no registered certificate subject handler is a retryable availability condition, not final completion.
- Ensure `RegisterCertificateHandler` invokes `RecoverPendingCertificateWork` only after releasing its handler-map lock.
- Preserve the current outcome behavior: handler errors / `Check::Stalled` keep work stalled; `Approve` or `Reject` ends that work. A certificate validation/key conflict remains fail-closed and does not unlock a slot.
- Test a certificate committed before handler registration: the journal must remain unfinished; registering a handler must dispatch it exactly once from durable readback; a replay must be harmless. Retain the existing pre-commit callback test and active-vote release assertions.

### B. Make certificate-first transaction selection reuse normal transaction evidence

**Files:** `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`, `test/src/account/transaction_manager_certificate_fallback_test.cpp`.

- Add a private CRDT transaction recovery helper (all monitored networks, exact returned hash). It is a lookup helper, not a certificate locator.
- In `OnConsensusCertificate`: tracked transaction → CRDT transaction → exact embedded fallback, in that order.
- Before calling `ChangeTransactionState(CONFIRMED)` on *any* selected transaction, require `CertificateMatchesTransaction`; reject the candidate/fail closed if it does not bind to the certificate's embedded winner and canonical slot.
- The fallback must retain `CheckHash()` and subject-hash checks; malformed or mismatched embedded data must not be processed.
- Add a regression where a serialized exact winner exists in CRDT but not `tx_processed_m`: certificate consumption must use it (not fallback). Retain/add a same-slot winning-vs-losing Mint assertion through this certificate path, not only `FetchAndProcessTransaction`.

### C. Make `CONFIRMED` mean durable Mint completion, not attempted completion

**Files:** primarily `src/account/TransactionManager.cpp`; likely only existing private declarations/test friend access in `TransactionManager.hpp`; focused tests in `test/src/account/transaction_manager_certificate_fallback_test.cpp`.

For `mint-v2` specifically, use this order:

```text
validated exact certificate
  -> keep transaction retryable (not terminal CONFIRMED)
  -> ParseMintTransaction: persist each output idempotently
  -> persist /bridge/executed/<chain>:<source>
  -> mark transaction CONFIRMED and finish certificate work
```

- Return an error from a failed marker write, leaving no completed state. Existing `ProcessCommittedCertificate` then calls `MarkStalled`, so timer/startup recovery retries.
- Do not set `tx_processed_m` to terminal `CONFIRMED` before `ParseTransaction` and marker persistence succeed. If an in-memory intermediate state is required while a call is running, retain the existing `VERIFYING` status; do **not** add a durable certificate-acceptance or Mint-work journal.
- Repeated successful confirmation remains a no-op. Retrying an incomplete Mint re-runs parser effects, but output outpoints make it non-duplicating and it retries the missing marker.
- Restrict this sequencing change to certified Mint V2 work unless code inspection of another transaction type proves it shares the bridge marker. Avoid broad transaction-state-machine refactoring.

## Test Strategy

| Scenario | Test seam | Expected assertion |
|---|---|---|
| Local publisher, PubSub notification, and remote CRDT receipt | Existing consensus lifecycle fixture | All only become eligible through `CertificateReceived` → durable readback → shared certificate handler; PubSub itself does not process or write authority. |
| Restart/startup before handler registration | `consensus_pending_lifecycle_test` friend accessor | Durable valid `/cert/<slot>` remains stalled without a handler; handler registration wakes recovery; handler is called once and journal clears only afterward. |
| Certificate first, transaction already in CRDT | `transaction_manager_certificate_fallback_test` | No in-memory tracked tx initially; handler selects exact CRDT payload, reaches confirmed lifecycle; embedded fallback is not required. |
| Certificate first, CRDT miss | Same account fixture | Exact embedded winner produces the same confirmation path; malformed/mismatched embedded transaction does not apply. |
| Same-slot losing Mint | Same account fixture with `MakeCompetingMintV2` and a signed winner certificate | Winner effects appear once; loser stays unconfirmed/does not create its output even though slots equal. |
| Duplicate certificate/replay | Same fixture | Repeated certificate work leaves one output outpoint and one completion marker; no balance double-count. |
| Crash-equivalent after effects but before marker | Same fixture, delete/withhold only the existing marker after durable UTXOs, recreate/replay manager/certificate | Replay observes output already present, does not duplicate it, restores marker, and then completes. A narrow injectable marker-writer failure seam is acceptable if the existing RocksDB fixture cannot force a write failure. |
| UTXO write failure / retryability | `UTXOManager::ReleaseStorage` or a test-only persistence-failure seam | Parser failure keeps certificate work stalled and transaction nonterminal; after storage recovery, replay succeeds. |

Focused build/test targets already exist:

```text
cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test consensus_pending_lifecycle_test --parallel 4
ctest --test-dir build/OSX/Release -R '^(transaction_manager_certificate_fallback_test|consensus_pending_lifecycle_test)$' --output-on-failure
```

`utxo_manager_test` is registered but its current executable is absent in the local build tree; build it only if Phase 11 adds UTXOManager-focused coverage. Do not treat this stale build artifact as a source defect. [VERIFIED: local CTest registration probe]

## Anti-Patterns and Scope Fences

- Do **not** add `/bridge/certified`, a per-slot certificate acceptance table, or any second finality journal. The CRDT certificate work journal plus the existing UTXO/marker persistence is the required recovery boundary.
- Do **not** let PubSub `HandleCertificate` write, confirm, or bypass durable CRDT readback. PubSub remains notification/volatile cleanup only.
- Do **not** mark a no-handler certificate work item done at startup; that hides a valid durable certificate from the later transaction handler.
- Do **not** treat a pre-parse `CONFIRMED` map entry or a successful `PutUTXO` in-memory insertion as completed Mint work. Completion requires durable outputs and the post-effect marker.
- Do **not** roll back partial Mint outputs after a local failure. Outpoint idempotence makes replay safer than compensating rollback.
- Do **not** make a marker-write error log-only. It must return an error that retains journal retryability.
- Do **not** relax Phase 10 binding: no `GetCertificateBySubjectHash`, no `/cert/<tx-hash>` fallback, and no same-slot transaction substitution.

## Recommended Plan Breakdown

1. **Certificate dispatch recovery:** leave no-handler work retryable and rerun the existing recovery path after registration; add lifecycle ordering/replay tests. (CERT-05)
2. **Convergent certificate transaction selection:** add exact CRDT-first recovery and enforce exact binding before confirmation; add certificate-first and competing-Mint tests. (CERT-05, MINT-01)
3. **Mint durability completion boundary:** propagate Mint V2 storage errors, apply UTXOs before marker, make marker failure retryable, and only transition terminally afterward; add restart/duplicate/failure recovery tests. (MINT-01, MINT-02)

The first two tasks can share the existing certificate/transaction fixtures, while task 3 depends on the corrected handler error propagation. They should execute serially in one worktree because both touch `TransactionManager.cpp` and its test fixture.

## Planning Verdict

Phase 11 is necessary but can remain narrow. The user was correct that existing transaction/UTXO persistence already supplies most of the recovery design: no new finality record is warranted. The required work is to stop prematurely declaring success at three concrete boundaries—handler unavailable at startup, certificate-first lookup bypassing CRDT, and Mint marker/parse ordering—and to let the existing CRDT work journal retry those failures until the idempotent UTXO effects and marker are durable.
