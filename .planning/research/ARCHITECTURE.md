# Architecture Research — v3.0 Canonical Burn Finality Rebuild

**Date:** 2026-08-20
**Scope:** Minimal integration of a canonical external-burn finality protocol into the `develop` consensus, CRDT, transaction, and UTXO paths.
**Confidence:** HIGH for existing-code observations; MEDIUM for the proposed publication-record schema until the bridge event identity decision is locked.

## Recommendation

Keep consensus responsible for certificate validity, proposal arbitration, and deterministic publisher selection. Add one narrowly-scoped bridge-finality component responsible for canonical burn-slot derivation, signed CRDT publication envelopes, durable receiver state, and idempotent mint application. Do not make CRDT callback origin, transport origin, or `DeliverySource::Local` part of the protocol.

The external burn is the finality domain; the winning proposal is the certificate domain. A certificate must still embed and validate its exact proposal, but all competing proposals for the same burn contend for one finality record.

## Why the Existing Path Cannot Be Extended As-Is

- `TransactionManager` currently installs a generic `NONCE_SUBJECT_TYPE` slot handler that delegates to `GeniusTransaction::GetSlotID()` (`src/account/TransactionManager.cpp:169-184`). `MintTransactionV2::GetSlotID()` includes chain, token, amount, destination, and source hash (`src/account/MintTransactionV2.cpp:212-239`). Amount and destination must not define a burn slot.
- The current certificate key is `/cert/<subject hash>` and for a nonce subject the subject hash is the proposal transaction hash (`src/blockchain/Consensus.cpp:1533-1587`, `GetSubjectHash`). Competing bridge-mint transaction hashes therefore have separate persistent finality keys.
- `SubmitCertificate()` sends live pubsub before writing CRDT (`src/blockchain/Consensus.cpp:1533-1587`). A publisher crash in that gap exposes peers to a non-durable certificate and provides no safe recovery owner.
- A CRDT `Element` contains only `key`, `id`, and `value` (`src/crdt/proto/delta.proto`); it has no authenticated author. CRDT callback `cid`/delivery timing cannot prove who published an item. Authority must consequently be inside a signed value that receivers validate.
- `TransactionManager::MintFunds()` uses a local bridge reservation plus an ad-hoc RocksDB `/bridge/executed/<chain>:<hash>` marker (`src/account/TransactionManager.cpp:616-748`, `5248-5277`). That marker is written only after the general confirmed-state change and is not the canonical certificate record.

## Protocol Objects and Invariants

### Canonical external-burn slot

Introduce a small value type, `BridgeBurnRef`, and derive `slot_id = SHA-256(canonical_encode(BridgeBurnRef))`. Its canonical encoding must be length-delimited/binary (or a protobuf message serialized deterministically), versioned, and domain separated, rather than a colon-concatenated string.

```
BridgeBurnRef v1
  source_chain_id
  bridge_contract_or_bridge_namespace
  source_transaction_hash
  event_discriminator (log index / event ordinal when one transaction may emit many burns)
```

`slot_id` must exclude proposer address, account nonce, minted amount, token, destination, local node identity, certificate timestamp, and delivery source. Those are proposal contents validated by consensus, not the identity of the already-burned external event.

The live `BridgeRelayer` currently supplies only source chain ID and transaction hash to `MintFunds` (`src/account/BridgeRelayer.cpp:321-350`). The RPC bridge watcher already has a log index (`src/watcher/impl/bridge_rpc_watcher.cpp:156-160`). Before implementation, decide whether the supported bridge guarantees one burn per source transaction. If it does not, carry the log index/event ordinal through relayer ingress and `MintTransactionV2`; using only `(chain, tx_hash)` would collapse legitimate burns. This is the only required schema/ingress decision.

### Finality publication

Persist a signed `BridgeFinalityPublication` at exactly one key per slot:

```
/bridge/finality/v1/<slot-id>
    BridgeFinalityPublication {
      version, slot_id, bridge_burn_ref,
      certificate_bytes, certificate_digest, proposal_id,
      registry_cid, registry_epoch,
      publisher_id, publication_round, signature
    }
```

Validation must atomically reject the record unless all of the following hold:

1. The embedded `ConsensusCertificate` passes existing semantic/signature/quorum validation and is bound to the embedded proposal.
2. The embedded proposal's `MintTransactionV2` is hash-bound to its nonce subject; recomputing its `BridgeBurnRef` yields `slot_id`.
3. `proposal_id`, registry CID/epoch, and certificate digest agree with the certificate.
4. The registry snapshot named by the certificate contains `publisher_id`, the envelope signature verifies as that validator, and `publisher_id` is the deterministic owner for `(slot_id, proposal_id, registry snapshot, publication_round)`.
5. The round is eligible: round zero is the primary; later rounds become eligible only after the protocol-defined interval from the certificate timestamp. Use the same fixed interval and sorted active-validator ordering on every node. This is a liveness rule, not local transport evidence.

The owner function can reuse the existing deterministic active-validator ordering and hash rotation pattern in `ConsensusManager::GetOrderedActiveValidators` / `GetAggregatorRole` (`src/blockchain/Consensus.cpp:483-545`), but it must be factored as an explicit `GetPublicationOwner(...)` helper. Aggregation and publication are separate roles: whichever validator assembled a valid quorum certificate is not automatically entitled to write it.

### Durable local application state

Keep a local, non-replicated `BridgeFinalityStore` record under `/bridge/finality-work/v1/<slot-id>`:

```
CERTIFIED(proposal_id, certificate_digest) → APPLYING → APPLIED(mint_tx_hash)
```

This record is the receiver-side exactly-once gate. It is not a second protocol authority and never selects a winner. It must be written synchronously before mint side effects and retain the proposal ID/digest permanently after application. A different valid certificate for the same slot is rejected as a finality conflict; a byte-identical replay is a no-op.

`UTXOManager` already persists UTXO updates in RocksDB batches (`src/account/UTXOManager.cpp:767-865`), but it does not share an atomic transaction with the current bridge marker. The bridge-finality application method must therefore be crash-replayable:

1. persist `CERTIFIED` (or recover it from the CRDT publication),
2. persist `APPLYING`,
3. apply the exact mint transaction from the certificate through the existing transaction/UTXO path,
4. persist `APPLIED` only after that path is durable,
5. on startup, inspect `APPLYING` and reconcile against the deterministic mint transaction/UTXO outpoint before either completing or replaying.

This replaces the ad-hoc `/bridge/executed` check as the semantic source of idempotency. UTXO reservation remains useful as a local construction guard, not as cross-node finality.

## Recommended Component Boundaries

| Component | Responsibility | Minimal integration point |
|---|---|---|
| `BridgeBurnRef` | Canonical external identity, encode/decode, slot derivation | `MintTransactionV2` and bridge ingress; expose a single `GetBridgeBurnSlot()` helper. |
| `BridgeFinalityPublication` | Signed CRDT value and publication-owner proof | New bridge-specific protobuf/message; no change to generic CRDT element metadata. |
| `ConsensusManager` | Validate certificate/proposal, keep in-memory proposal competition, calculate publication owner, call only the eligible local publisher | `Consensus.hpp/.cpp`; retain existing generic handlers for non-bridge subjects. |
| `BridgeFinalityStore` | Validate CRDT envelope, durable state machine, recovery scan, exactly-once application gate | New small `src/account/BridgeFinality.*` adjacent to `TransactionManager`; use existing RocksDB/GlobalDB facilities. |
| `TransactionManager` | Construct bridge proposal, register bridge finality receiver, apply the exact certified `MintTransactionV2` | Replace mint-specific generic slot callback and `/bridge/executed` marker usage; keep ordinary transfers untouched. |
| `UTXOManager` | Persist/recover UTXO state for a winning mint | No protocol changes. Reuse `PutUTXO`/`ConsumeUTXOs` and its durable storage. |
| CRDT / `GlobalDB` | Transport and local persistence of the one signed publication | Register a bridge-finality filter/callback; no CRDT data-model rewrite. |

Do not turn this into a generic TransactionManager or CRDT refactor. The bridge module may use `GlobalDB::GetDataStore()` and the existing work journal, but it owns only `/bridge/finality...` keys.

## Data Flow

```
verified external burn
  │  BridgeRelayer / RPC watcher supplies BridgeBurnRef + proposal fields
  ▼
MintTransactionV2 (contains same canonical burn reference)
  │  slot = H(BridgeBurnRef), independent of proposer/nonce/amount/destination
  ▼
ConsensusManager proposal arbitration
  │  all proposals for slot choose deterministic best proposal; votes bind proposal_id
  ▼
valid quorum ConsensusCertificate (still contains the winning proposal)
  │
  ├─ live consensus message: hint only; validates but does not mint or write finality
  │
  └─ eligible publication owner
        │  create signed BridgeFinalityPublication
        ▼
     local CRDT persistence / DAG node first
        ▼
     CRDT head advertisement and rebroadcast
        ▼
all receivers' bridge-finality CRDT callback
  │  validate envelope + certificate + slot + publisher schedule
  │  never write the CRDT key
  ▼
BridgeFinalityStore: CERTIFIED → APPLYING → APPLIED
  ▼
TransactionManager applies certificate-embedded winning mint once
  ▼
UTXOManager persists minted/consumed bridge UTXO state
```

`GlobalDB::Put()` ultimately creates a CRDT DAG node and queues local processing before later head broadcast/rebroadcast (`src/crdt/impl/crdt_datastore.cpp:1333-1339`, `1461+`). The publication method must treat a successful local persistence result as the precondition for advertising the live certificate/head. Reorder `SubmitCertificate` accordingly for bridge finality: persist publication first, then advertise; if advertising fails, retain the durable record for normal CRDT rebroadcast and failover recovery.

## Event Ordering and Recovery

### Certificate competition

1. For bridge `MintTransactionV2`, `ConsensusManager` asks the bridge slot helper for its slot. For non-bridge nonce subjects, preserve current subject/transaction behavior.
2. It votes only for the deterministic current winner in that slot. A certificate is accepted only if it references that winner, while retaining the full proposal-binding checks.
3. Once a valid finality publication exists for the slot, clear competing in-memory proposals and suppress all new certificates for that slot—even where proposal transaction hashes differ.

### Publisher loss / crash

- Before persistence: nobody sees a finality record; the next eligible publication round may publish the same valid certificate.
- After persistence, before network advertisement: restart finds the local record/CRDT head and rebroadcasts it; later scheduled owners may safely republish the same certificate envelope if needed.
- After advertisement, before local mint application: every receiver persists `CERTIFIED` before application and restarts through `APPLYING` reconciliation.
- Delayed CRDT delivery: a receiver can see the certificate pubsub hint first. It must wait for a valid finality publication (or become the eligible failover owner) rather than treating delivery provenance as authority.

The existing `CRDTWorkJournal` provides durable callback retry state (`src/crdt/globaldb/crdt_work_journal.hpp`; recovery in `ConsensusManager::RecoverPendingCertificateWork`). Reuse this pattern for the bridge-finality callback. Note that the callback manager marks work `Processing` before dispatch and may auto-complete it if still `Processing` afterwards (`src/crdt/impl/crdt_callback_manager.cpp:141-198`); the bridge callback must explicitly mark stalled/retryable on an unresolved dependency and must not rely on an in-memory queue alone.

### Receiver convergence

The receiver's CRDT filter verifies the envelope before accepting it; its callback invokes the finality store exactly once. It must not call `db_->Put`, reconstruct a certificate key, or elect an author based on whether the callback happened locally. Replays, duplicate heads, restart recovery, and different arrival orders all converge on the same `(slot_id, proposal_id, certificate_digest)` local record.

## Concrete Existing-Code Changes

1. **`MintTransactionV2` / bridge ingress:** replace `GetSlotID()`'s amount/destination-based string with `BridgeBurnRef`-based slot derivation. Extend the mint protobuf/relayer input only if an event discriminator is required.
2. **`TransactionManager`:** register the bridge slot/finality adapter at initialization (today's registration is at `TransactionManager.cpp:126-184`); route `MintFunds` through `BridgeFinalityStore`; delete the bridge-only `/bridge/executed` marker logic once the durable finality state exists.
3. **`ConsensusManager`:** add a tiny bridge-finality policy hook (slot derivation, publication owner, finality-exists lookup), not a new global transaction lifecycle. Make bridge certificate persistence occur before live advertisement. Keep certificate validation proposal-bound.
4. **CRDT registration:** add a bridge-finality filter/callback for `/bridge/finality/v1/...`, separate from the generic `/cert/...` callback so non-bridge consensus paths retain their data format.
5. **Recovery:** initialize the bridge store before transaction consensus handlers; scan its local `CERTIFIED/APPLYING` records and unfinished CRDT work after GlobalDB is ready.

## Implementation Order

1. **Protocol foundation:** specify `BridgeBurnRef`, canonical encoding, slot key, proposal winner comparator, publication round/owner function, and finality record/envelope fields. Add pure unit tests for equality, exclusion of amount/destination/nonce/proposer, signature bytes, and owner rotation.
2. **Consensus arbitration:** integrate bridge slot derivation and certificate-best-proposal validation. Update `test/src/blockchain/consensus_slot_key_test.cpp`, whose current expectations encode the rejected amount/destination-sensitive behavior.
3. **Publication path:** implement signed envelope validation plus persist-before-advertise in the eligible-owner path. Test invalid publisher, primary owner, deterministic failover, and no write by an unqualified callback recipient.
4. **Receiver state machine:** implement `BridgeFinalityStore`, CRDT callback/filter, transaction application gate, and crash recovery. Replace `/bridge/executed` as the bridge semantic guard while retaining UTXO reservation mechanics.
5. **Production multi-node regressions:** run the actual pubsub + CRDT + `TransactionManager` ingress path for competing proposals, delayed CRDT after certificate, primary publisher loss, restart at each persistence boundary, duplicate delivery, and one mint effect across nodes. Do not test authority by directly invoking local-only helpers.

## Anti-Patterns to Avoid

| Anti-pattern | Why it fails | Required alternative |
|---|---|---|
| CRDT callback writes the same finality key on every recipient | Multi-writer race creates divergent values/heads and no authority proof | Only the scheduled, signed publication owner writes; receivers validate and apply. |
| `DeliverySource::Local` / callback origin selects the writer | Not network-verifiable, not durable, and unavailable to restart/replay peers | Signed publisher identity plus registry-snapshot schedule. |
| Store certificates by proposal transaction hash | Lets multiple proposals for one burn independently finalize | Store bridge finality by canonical burn slot while retaining certificate-to-proposal binding. |
| Use amount/destination/token to form slot identity | A malicious or inconsistent proposal can evade burn contention | Use only immutable external-event identity. |
| Advertise certificate before durable publication | Publisher loss produces a visible but unrecoverable finality hint | Persist signed envelope, then advertise; rebroadcast/recover from durable state. |
| Treat UTXO reservation as distributed finality | It is local state and does not select a winner across nodes | Use finality record for protocol/application idempotency; UTXO only for ledger effects. |

## Verification Notes

- Use the existing certificate fallback tests as the closest receiver-path fixture (`test/src/account/transaction_manager_certificate_fallback_test.cpp`), but add bridge-slot assertions and durable state checks rather than accepting a certificate after fallback deserialization alone.
- Use the existing consensus callback/filter and work-journal test setup for CRDT timing/restart behavior. Test CRDT delivery separately from consensus-message arrival because those paths are intentionally unordered.
- Exercise a real multi-node fixture; unit tests alone cannot prove that no receiver-side callback writes occur or that rebroadcast repairs publisher loss.

## Sources

- `src/blockchain/Consensus.hpp`, `src/blockchain/Consensus.cpp` — proposal arbitration, certificate validation, current `/cert` persistence, aggregation rotation, and CRDT recovery.
- `src/account/TransactionManager.cpp`, `src/account/MintTransactionV2.cpp`, `src/account/BridgeRelayer.cpp` — current mint proposal, slot derivation, bridge ingress, execution marker, and certificate application.
- `src/crdt/proto/delta.proto`, `src/crdt/impl/crdt_datastore.cpp`, `src/crdt/impl/crdt_callback_manager.cpp`, `src/crdt/globaldb/crdt_work_journal.hpp` — unauthenticated CRDT metadata, persistence/broadcast path, callback work semantics, and recovery facility.
- `src/account/UTXOManager.cpp` — durable UTXO batching and local reservation behavior.
