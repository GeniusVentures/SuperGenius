# Technology Stack: v3.0 Canonical Burn Finality Rebuild

**Project:** SuperGenius — Canonical Burn Finality Rebuild
**Researched:** 2026-08-20
**Recommendation confidence:** HIGH for reuse/no-new-dependency; MEDIUM for the exact finality-record schema, which must be decided in the phase protocol contract.

## Decision

**Add no external dependency and introduce no new network protocol transport.** Implement the feature with the existing C++17, Protobuf, consensus, CRDT/GlobalDB, RocksDB, libp2p PubSub, and GoogleTest/CTest stack.

The existing stack already has all necessary capabilities:

| Existing facility | Concrete source | Reuse for |
|---|---|---|
| C++17 and CMake | `build/CommonCompilerOptions.cmake`, root CMake targets | Small protocol/domain types and focused changes in current services. |
| Protobuf consensus envelope | `src/blockchain/impl/proto/Consensus.proto`, `src/blockchain/Consensus.hpp` | Preserve a certificate's embedded exact proposal and its signed vote bundle. Add fields/messages only if the chosen finality record cannot be deterministically derived from the existing certificate and proposal. |
| Deterministic proposal arbitration | `ConsensusManager::RegisterSlotKeyHandler()` and `GetSlotKey()` in `src/blockchain/{Consensus.hpp,Consensus.cpp}`; current registration in `src/account/TransactionManager.cpp:169` | Supply one bridge-burn slot identity for all competing bridge-mint proposals. |
| Validator signatures and certificate verification | `CreateCertificate`, `ValidateCertificate`, `TallyVotes`, and `GetAggregatorRole` in `src/blockchain/Consensus.cpp`; `src/blockchain/ConsensusAuth.hpp` | Keep certificate-to-proposal cryptographic binding and derive publisher eligibility from the certificate/proposal registry facts. |
| Existing dissemination | `ConsensusManager::Publish()` / `SubmitCertificate()` in `src/blockchain/Consensus.cpp`; `crdt::GlobalDB` in `src/crdt/globaldb/globaldb.{hpp,cpp}`; existing libp2p/IPFS PubSub | Advertise verified certificate/finality data and synchronize it between peers. No separate RPC, leader-election service, or lock server is required. |
| CRDT filters/callbacks | `src/crdt/impl/crdt_callback_manager.cpp`, `src/crdt/globaldb/globaldb.hpp` | Validate and consume received state. Treat callbacks only as notifications of replicated data, never as proof that the local peer is the authorized writer. |
| Durable local state | `src/storage/rocksdb/{rocksdb.hpp,rocksdb.cpp,rocksdb_batch.hpp,rocksdb_batch.cpp}` | Store a finality/application record and atomically record local application steps that must survive restart. |
| Crash retry journal | `src/crdt/globaldb/crdt_work_journal.hpp`, `src/crdt/impl/crdt_work_journal.cpp` | Resume unfinished certificate/finality consumption after callback interruption or restart. |
| Multi-node and restart tests | `test/src/bridge_e2e/bridge_e2e_test.cpp`, `test/src/multiaccount/multi_account_sync.cpp`, `test/src/transaction_sync/transaction_crash_test.cpp`, `test/src/crdt/globaldb_integration.cpp` | Exercise real PubSub/CRDT ingress, delayed delivery, loss of the original publisher, and restart recovery. |

## Required Stack-Level Changes

These are design changes inside existing modules, not package additions.

### 1. Make the slot an external-burn identity

Reuse the slot-key registration seam rather than inventing another consensus engine. The current mint path delegates `NONCE_SUBJECT_TYPE` slot selection to `GeniusTransaction::GetSlotID()` through `TransactionManager.cpp`; `MintTransactionV2::GetSlotID()` in `src/account/MintTransactionV2.cpp` currently incorporates chain/token/amount/destination/source hash. That is not the milestone contract: proposal-controlled mint details must not split the finality domain.

Define a canonical `BurnId` (or similarly named value object) at the bridge/transaction boundary and have the existing handler return `bridge-burn:<canonical-id>`. Its minimum source identity must be agreed explicitly and canonically serialized. The observed bridge infrastructure already exposes source-chain identity, contract and event/log index in `src/watcher/impl/bridge_rpc_watcher.cpp`; however, the active `BridgeRelayer`/`MintFunds` path currently passes only `chain_id` and `transaction_hash` (`src/account/BridgeRelayer.cpp`, `src/account/TransactionManager.cpp`). If multiple valid burn logs can occur in one external transaction, the boundary must carry the log index (and canonical contract identifier) before this milestone can honestly claim one-burn-at-most-once. Do not silently assume transaction hash is globally one-burn-one-event.

### 2. Keep certificate binding; make finality slot-scoped

Continue using `ConsensusCertificate` with its embedded `ConsensusProposal`; `ValidateCertificate()` already checks proposal id, registry binding, subject validity, deterministic proposal id, signatures, and quorum (`src/blockchain/Consensus.cpp`). The canonical slot decides which proposal is eligible to finalize, but must not replace the certificate's referenced proposal or rewrite certificate payloads.

The existing in-memory `SlotState` and tie-breaker in `Consensus.cpp` are useful for proposal competition but not sufficient as protocol finality: they disappear on restart and `ValidateCertificateBestProposal()` currently depends on each receiver's local proposal arrival order. Persisted/received finality therefore needs an explicit deterministic comparison rule based only on verifiable proposal/certificate fields. Any slot-finality record should carry the canonical slot id and the full (or cryptographically bound) winning certificate/proposal id; receivers must verify both before applying it.

### 3. Reorder and narrow certificate publication

`ConsensusManager::SubmitCertificate()` currently PubSub-publishes the certificate before `db_->Put()` writes `/cert/<subject-hash>` (`src/blockchain/Consensus.cpp`). This is incompatible with persistence-before-advertisement. Change the existing method or extract a small publisher component so its order is:

1. verify certificate and deterministic publisher/failover eligibility;
2. durably persist the certificate/finality record;
3. advertise it through the existing PubSub/CRDT path;
4. process all peers' replicated result through the common finality-consumer path.

The current rotating aggregator calculation (`GetOrderedActiveValidators()` + `GetAggregatorRole()` in `Consensus.cpp`) is a candidate building block for deterministic authority and later-round failover. It is not by itself a complete publication rule, because it is proposal-id based and its failure interval/takeover evidence have to be protocol-defined. The phase should define a certificate/slot-based owner selection and a deterministic round/timeout handoff that every peer can recompute from the same certificate, proposal, registry snapshot, and time/round inputs.

### 4. Use CRDT for replication, not distributed compare-and-set

Use `GlobalDB::Put()` / `AtomicTransaction` only to write and replicate data the authorized publisher has already chosen. `AtomicTransaction` (`src/crdt/impl/atomic_transaction.cpp`) combines a local multi-key delta and publishes it atomically, but it does **not** provide a cross-peer conditional put, lease, or single-writer lock. It cannot safely elect an author when peers concurrently write the same CRDT certificate key.

The finality consumer must never react to receipt of a certificate by writing that same CRDT key again. `CRDTCallbackManager::PutDataCallback()` supplies a local `(key, value, cid)` notification, and its work journal records processing state; neither gives network-verifiable publication ownership. Leave receivers read/validate/apply-only. A failover publisher writes only after the protocol's reproducible takeover predicate holds, not because it saw a remote callback or a local `DeliverySource`-style indicator.

### 5. Reuse RocksDB for durable exactly-once application

The current bridge code has a local `/bridge/executed/<chain>:<tx>` marker in `TransactionManager` (`src/account/TransactionManager.hpp:559`, checks in `MintFunds()`, write on confirmation near `TransactionManager.cpp:5254`). It is a useful migration point but not a sufficient canonical finality record: its key lacks a log/event discriminator and it is written only after transaction confirmation.

Use the existing `storage::rocksdb::Batch` to atomically persist a namespaced finality/application record and its application state (for example, `accepted`, `mint-enqueued`, `mint-confirmed` plus proposal/certificate binding) where one local transition must not be torn by a restart. The final schema and recovery state machine belong in the phase specification. Do not build a second database, a distributed lock table, or a broad TransactionManager refactor.

`CRDTWorkJournal` is appropriate for retrying receiver-side consumption after crash/temporary validation stall. It is not a replacement for the finality record: work entries track callback processing leases, while canonical finality and mint idempotency require application-level facts.

## Do Not Add

| Do not add | Why |
|---|---|
| New database, Kafka/queue, Redis lock, ZooKeeper/etcd, or external leader-election service | Existing RocksDB supplies durable local transitions and the validator registry/consensus supplies deterministic network authority. An external coordinator would create a second, unverified trust domain. |
| New P2P transport, HTTP/RPC control plane, or bespoke gossip protocol | Existing `GossipPubSub`, consensus channels, and GlobalDB propagation already reach the production nodes. |
| A CRDT compare-and-set/last-writer-wins certificate lock | The supplied CRDT transaction is atomic only for one local delta, not a distributed authority mechanism; concurrent writers remain the failure being eliminated. |
| A `DeliverySource`, callback-origin boolean, or local sender heuristic | It is neither durable nor verifiable by another peer and cannot safely decide publication/failover. |
| Broad CRDT, registry, consensus, or TransactionManager rewrite | The available extension seams are sufficient. Keep changes concentrated around bridge identity, slot arbitration/finality, publication, and the mint application boundary. |
| New cryptographic library | Existing signing/verifying (`GeniusAccount`, `ConsensusAuth`, SHA/BLAKE helpers) is enough for canonical serialization/hash binding. |

## Implementation Implications

1. Introduce a small canonical bridge-burn identity/value type and thread it from real-time watcher and catch-up scan to `MintFunds`/the consensus subject. Normalize textual chain/contract/hash values before bytes are signed or keys are constructed.
2. Change the registered mint slot-key handler to use that identity only. Do not let amount, recipient, nonce, local proposer, or mint transaction hash select the finality slot.
3. Define an explicit slot-finality schema and deterministic winner/publisher/failover predicate. It must be recomputable after restart and by a peer that did not receive proposals in the same order.
4. Centralize all certificate ingress (local quorum, PubSub, replicated CRDT recovery) behind one validation → durable-finality → idempotent-application path. Receivers must not republish/rewrite certificate CRDT keys.
5. Make the successful durable store occur before advertisement. Use RocksDB batch writes for related local finality/application transitions; keep `GlobalDB` writes for replication only after authorization.
6. Extend existing CTest fixtures with production paths: at least two contending mints for one BurnId, out-of-order proposal/certificate delivery, original publisher loss with deterministic takeover, and node restart before/after application. Unit tests alone are insufficient.

## Alternatives Considered

| Category | Recommended | Alternative | Why not |
|---|---|---|---|
| Finality ownership | Deterministic rule derived from validated consensus/registry data | Callback-origin flag or first receiver | Local timing/provenance is not protocol evidence and fails after restart. |
| Persistence | Existing RocksDB + its batch API | New SQL/coordination store | Adds operational and trust complexity without solving cross-peer authority. |
| Replication | Existing GlobalDB/CRDT + PubSub | New event bus/queue | Duplicates the deployed synchronization plane. |
| Conflict control | Slot-specific deterministic winner before publication | Multi-writer CRDT key resolution | Multi-writer race is the known failure mode and jeopardizes certificate binding. |

## Sources and Evidence

- `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` — proposal slots, certificate validation, aggregator rotation, certificate CRDT keying and publication order.
- `src/account/TransactionManager.cpp`, `src/account/MintTransactionV2.cpp`, and `src/account/BridgeRelayer.cpp` — current mint identity flow and the current over-broad slot key.
- `src/watcher/impl/bridge_rpc_watcher.cpp` — external claim fields available for a canonical identity, including log index and contract.
- `src/crdt/globaldb/globaldb.hpp`, `src/crdt/atomic_transaction.hpp`, and `src/crdt/impl/atomic_transaction.cpp` — CRDT atomic-delta semantics and their non-CAS boundary.
- `src/storage/rocksdb/rocksdb_batch.{hpp,cpp}` — local atomic batch facility.
- `src/crdt/impl/crdt_callback_manager.cpp` and `src/crdt/globaldb/crdt_work_journal.hpp` — callback and durable retry behavior.
- `test/src/bridge_e2e/bridge_e2e_test.cpp`, `test/src/multiaccount/multi_account_sync.cpp`, `test/src/transaction_sync/transaction_crash_test.cpp` — existing production-path test foundations.

No external library documentation lookup was required: this milestone is constrained to existing in-repository components, and the dependency question is answered directly by the baseline's concrete facilities.
