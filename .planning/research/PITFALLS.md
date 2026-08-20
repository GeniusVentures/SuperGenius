# Domain Pitfalls — Canonical Burn Finality Rebuild

**Project:** SuperGenius v3.0 Canonical Burn Finality Rebuild
**Researched:** 2026-08-20
**Confidence:** HIGH for codebase-specific findings; MEDIUM for the precise failover clock/record schema, which the roadmap must choose explicitly.

## Critical Pitfalls

### P1 — Defining the finality domain with mutable mint-proposal fields

**What goes wrong:** A slot derived from amount, token, destination, account nonce, proposer, or a locally constructed transaction hash either splits one external burn into multiple consensus domains or merges independent burns. The present `MintTransactionV2::GetSlotID()` uses chain, token, amount, destination, and first input hash; the latter is an implementation-derived surrogate rather than an explicitly specified external-burn identity. The fallback slot key uses `account_id:nonce`. Either path is unsafe as the protocol definition for a bridge burn.

**Consequences:** Two proposals for the same burn can each reach quorum and mint; conversely two real burns can suppress each other. A later refactor to serialization, input order, or destination defaults silently changes consensus behavior.

**Prevention:** Define one canonical `BurnId` from immutable, source-chain facts, with unambiguous binary encoding and domain separation. At minimum include source-chain identity and transaction/event discriminator (for log-based sources, transaction hash alone is insufficient when a transaction can contain multiple burns). Require every MintV2 proposal to carry/derive exactly that value, reject malformed or inconsistent derivations, and derive both slot key and durable-finality key solely from it. Do not fall back to account/nonce for bridge mints.

**Detection:** Unit vectors prove equal BurnId for competing proposals that differ in proposer/nonce/amount/destination representation and unequal BurnId for two events in the same external transaction. Property/fuzz tests must reject ambiguous delimiters, missing event index, and noncanonical encoding.

**Roadmap phase:** **1 — Canonical identity and deterministic slot/certificate binding.**

### P2 — Local “best proposal” arbitration is mistaken for protocol finality

**What goes wrong:** `ConsensusManager` selects a local `SlotState::best_proposal_id` as messages arrive and compares nonce transaction hashes. A peer that receives competing proposals in another order can temporarily select a different winner. More importantly, current certificate validation only checks a locally populated `slot_states_` entry; on a fresh receiver `CreateProposalState()` makes the arriving certificate winner by default. That is not a globally verifiable proof that the certificate represents the canonical proposal.

**Consequences:** Certificate acceptance depends on arrival order and restart history. A valid quorum certificate for a losing proposal can be consumed after restart, or two independently certified proposals can be applied.

**Prevention:** Specify the winner function over immutable signed/protobuf facts (for example a canonical total order of proposal IDs after checking all proposals are valid for the same BurnId). Bind the certificate to the complete winning proposal and verify the certificate's proposal ID, embedded proposal, BurnId, registry snapshot, signatures, and quorum before finality. The finality record must contain enough evidence for a node that never saw proposal gossip to recompute acceptance; it must not consult in-memory `slot_states_`.

**Detection:** Feed the same competing set in every permutation, including certificate-before-proposal and restart-with-certificate-only; require the same accepted winner. Negative tests must reject a quorum certificate whose embedded proposal is valid but is not the canonical winner for the BurnId.

**Roadmap phase:** **1 — Canonical identity and deterministic slot/certificate binding.**

### P3 — Certificate keying remains proposal/subject-keyed instead of burn-finality-keyed

**What goes wrong:** `SubmitCertificate()` writes `/cert/<subject_hash>`, and a nonce subject hash is the proposal transaction hash. Competing proposals for one BurnId therefore use different CRDT keys, so CRDT convergence cannot express “this burn is final exactly once.” Replacing that key with a shared one without ownership merely creates a multi-writer race.

**Consequences:** The system either permits multiple certificates/effects or allows competing CRDT values for the supposed canonical key. Receivers cannot know whether a missing key means “not final yet,” “wrong key,” or “publisher failed.”

**Prevention:** Introduce a single, versioned finality-record key derived from canonical BurnId, distinct from any legacy proposal-certificate key. Its immutable contents must include BurnId, winning proposal/certificate bytes or committed hashes, registry metadata, and publication/failover evidence. Define whether legacy `/cert/<subject-hash>` is retained as non-authoritative evidence or bypassed for bridge finality; never let both trigger minting.

**Detection:** Assert that two same-BurnId proposals map to exactly one finality key and that different events never map to it. Test that a legacy/alternate certificate path alone cannot apply a MintV2 after the migration boundary.

**Roadmap phase:** **2 — Durable finality record, publication authority, and failover.**

### P4 — Every PubSub/CRDT receiver writes the canonical key

**What goes wrong:** CRDT callbacks run on every replica that receives an element. If callback receipt causes a `Put()` of the shared finality/certificate key, all replicas become writers. `GlobalDB::Put()` and `AtomicTransaction` atomically create a local delta; neither supplies a distributed compare-and-set, lease, or writer election.

**Consequences:** Concurrent incompatible values and nondeterministic merge/order behavior return; retries and resync amplify the race. The fact that all writers saw identical bytes does not establish authority, and a malicious/buggy replica can write first.

**Prevention:** Split roles sharply: receivers validate, persist local consumption state, and apply only; exactly one protocol-eligible publisher writes/advertises the finality record. Authorize the writer from data peers can verify (BurnId, certified winner, certificate registry snapshot, and defined failover epoch), never callback source, PubSub origin, or “who saw it first.” A successor writes only after the same deterministic takeover predicate holds.

**Detection:** Instrument finality writes with BurnId, publisher identity, proposal ID, and takeover epoch. In a 3+ node test, deliver a certificate to every node concurrently and assert one publication attempt/record, no receiver-side re-put, and identical finality bytes on all replicas.

**Roadmap phase:** **2 — Durable finality record, publication authority, and failover.**

### P5 — `DeliverySource::Local`, callback timing, or sender identity is used as authority

**What goes wrong:** Local ingress provenance tells a node only which callback path ran. It is neither signed protocol data nor durable across restart, and other nodes cannot verify it. Suppressing remote receivers based on it also leaves the system stuck if the presumed local writer dies before persistence/publication.

**Consequences:** A receiver can neither safely take over nor prove why it is forbidden. The system gains a single point of liveness failure while still lacking a safety proof.

**Prevention:** Make publisher selection and takeover a pure function of durable, signed protocol facts. Explicitly define: ordered eligible publishers, the initial owner, failover trigger/timebase, successor ordering, what evidence makes a takeover valid, and what happens when no owner is available. Ensure the rule remains recomputable after a cold restart and by a peer that received only the finality record.

**Detection:** Kill/disconnect the initial owner at each boundary: before durable write, after durable write but before advertisement, and after advertisement before local application. For each case assert either safe eventual takeover or an explicit safe stall—never a second final record/certificate/mint.

**Roadmap phase:** **2 — Durable finality record, publication authority, and failover.**

### P6 — Advertising before durable commit, or treating CRDT publication as synchronous durability

**What goes wrong:** Current `ConsensusManager::SubmitCertificate()` calls PubSub `Publish()` before `db_->Put()`. A crash or write failure can advertise a certificate that no publisher can recover locally. Conversely, `GlobalDB::Put()` creates and persists a CRDT DAG delta before later network propagation; it is not proof that other replicas received it. Conflating these events makes restart and retry behavior ambiguous.

**Consequences:** Peers may act on unavailable evidence, retries may construct a new certificate/record, and failover may race with a publisher that committed but did not advertise.

**Prevention:** Enforce a recoverable state machine: validate certificate → determine eligible owner/failover epoch → durably write immutable finality record (with error handling) → advertise that exact record/evidence → mark local publication progress. On restart, recover record first and resume advertisement of the same bytes; never regenerate a competing certificate. Define whether the durable store is the CRDT datastore, a local RocksDB journal, or both, and test its actual crash boundary.

**Detection:** Fault-inject failure after each transition and reboot. Assert no peer applies solely from an unpersisted advertisement and a committed-unadvertised record is eventually served/advertised unchanged.

**Roadmap phase:** **2 — Durable finality record, publication authority, and failover.**

### P7 — Treating the CRDT work journal as canonical finality or an exactly-once transaction log

**What goes wrong:** `CRDTWorkJournal` stores local callback processing leases under `/crdt/work/`; callback dispatch marks entries processing and may auto-complete them after return. It is useful retry bookkeeping, but not shared protocol state and not tied atomically to UTXO/mint effects. A callback can be marked done even when the higher-level effect is only partially durable.

**Consequences:** Restart recovery may skip a needed application, reapply an already-applied effect, or interpret an expired lease as authority to republish.

**Prevention:** Use the work journal only to schedule/retry consumption. Model finality and application separately with explicit durable states such as `finalized`, `applying`, and `applied`, keyed by BurnId and bound to the finality-record hash. Recovery reconciles these states with actual transaction/UTXO evidence. Journal lease expiration must never grant publication authority.

**Detection:** Crash while callback is processing, after finality persisted, during mint application, and after application before journal cleanup. Verify recovery derives the outcome from finality/application state—not the callback journal alone.

**Roadmap phase:** **3 — Exactly-once mint application and restart recovery.**

### P8 — “Executed” marker is non-atomic, late, and too weakly identified

**What goes wrong:** `MintFunds()` checks `/bridge/executed/<chain>:<transaction-hash>`, but writes the marker only after the transaction has entered `CONFIRMED`; the write error is logged and execution continues. It also derives the write key from `dag_st.uncle_hash()`. The marker excludes event/log identity and is not atomically committed with transaction/UTXO state.

**Consequences:** A crash between mint effect and marker permits another mint after restart; a transaction with multiple burns aliases them; errors can silently remove the only replay guard. Concurrent local callers can pass the pre-check before either writes the marker.

**Prevention:** Replace/augment it with the BurnId-keyed finality/application state machine. Ensure state transitions and the durable mint/UTXO effect are atomic where the storage boundary permits, or use a write-ahead intent plus idempotent reconciliation that has a proven crash protocol. Treat failure to persist the guard as failure of finality/application, not merely a log message. Serialize same-BurnId local application under a keyed lock in addition to durable idempotency.

**Detection:** Run same-BurnId mint requests concurrently in one process and across nodes; inject RocksDB failures and crashes at each state transition; verify exactly one persistent UTXO/transaction effect and one completed BurnId record.

**Roadmap phase:** **3 — Exactly-once mint application and restart recovery.**

### P9 — Accepting certificate fallback data without enforcing the bridge-finality contract

**What goes wrong:** `TransactionManager::OnConsensusCertificate()` currently accepts several bad/unknown fallback cases so certificate propagation can continue (undecodable nonce subject, missing embedded transaction, deserialization failure, or hash mismatch). This is defensible for legacy generic consensus traffic but unsafe if any of those paths can confirm/apply a bridge mint. It also confirms the transaction before conflict handling discovers a second confirmed conflict.

**Consequences:** A malformed, legacy, or mismatched certificate can bypass canonical BurnId/certificate binding and reach account state. Resolving a conflict after confirmation is not equivalent to preventing the second effect.

**Prevention:** For MintV2/bridge subjects, fail closed before any state change unless the canonical finality record and its complete certificate binding verify. Keep legacy tolerant behavior explicitly scoped to non-bridge subjects, with type-specific tests. Perform the BurnId finality/idempotency check before `ChangeTransactionState(CONFIRMED)` and before parsing/application.

**Detection:** Test invalid embedded MintV2, certificate/subject hash mismatch, wrong BurnId, losing proposal, wrong registry epoch, and a second valid-looking proposal; assert no balance, UTXO, checkpoint, or applied marker changes.

**Roadmap phase:** **1 — Certificate binding** and **3 — guarded application.**

### P10 — Failover clocks are not deterministic or are too short for the network

**What goes wrong:** Wall-clock timestamps, local timeout starts, and arbitrary retry intervals differ across nodes. A quick timeout causes honest owners and successors to publish concurrently; a never-expiring owner stalls forever. The existing aggregator role is round/proposal-oriented and cannot be assumed to solve finality-record failover without an explicit mapping.

**Consequences:** Split-brain publication under clock skew/partition, or permanent liveness loss after owner failure.

**Prevention:** Choose and document a protocol-observable time/round basis (for example certificate round plus deterministic retry epochs), bounded skew assumptions, successor order from the certificate's registry snapshot, and a monotonic takeover epoch included in the record. Safety must not depend on a failed owner deleting anything. Prefer safe temporary stall to accepting a conflicting record under ambiguous timeout evidence.

**Detection:** Simulate delayed propagation, clock skew, duplicated messages, network partition/rejoin, and owner restart. Assert one finality content and monotonic owner/epoch behavior; test both pre- and post-takeover first-owner recovery.

**Roadmap phase:** **2 — Durable finality record, publication authority, and failover.**

## Test-Validity Pitfalls

### T1 — Tests call helpers or inject certificate state directly

**What goes wrong:** Unit tests can prove a key function but miss production callback ordering, PubSub delivery, CRDT merge, persistence, and restart behavior. Existing certificate fallback tests directly invoke `OnConsensusCertificate()`; they cannot validate publication ownership.

**Prevention:** Keep deterministic unit tests for encoding/validation, but make the acceptance gate a multi-node fixture using production `MintTokens`, proposal/vote/certificate propagation, CRDT ingress, and durable databases. Do not expose a test-only “author” switch that production cannot verify.

**Roadmap phase:** **4 — Multi-node fault-injection and regression verification.**

### T2 — Sleep-based eventual assertions hide races and do not test absence of a second effect

**What goes wrong:** Codebase concerns identify pervasive fixed sleeps. A test that waits once for a balance increase proves only that at least one mint occurred; it cannot prove exactly once, canonical publication, or recovery safety. The current replay test's unchanged-balance comparison reads the same value twice rather than observing a post-replay settling interval.

**Prevention:** Use predicate-based waits with explicit observation windows, event/latch hooks for controlled delivery, and exact deltas/UTXO counts keyed by BurnId. Record per-node finality bytes and publication attempts. Tests should fail on a second effect even if the first succeeds.

**Roadmap phase:** **4 — Multi-node fault-injection and regression verification.**

### T3 — Restart tests reuse memory, ports, or data directories

**What goes wrong:** Recreating a node in the same process can retain static state, outstanding PubSub work, or an unflushed database; creating it with a fresh directory tests only clean start. Port collisions and incomplete teardown can turn a failover test into a non-networked unit test.

**Prevention:** Give each node an isolated, inspectable RocksDB directory and deterministic ports; explicitly close/flush/destroy the failed node, then construct a new process-equivalent node over the same directory. Verify preconditions (connected peers, owner selected, record absent/present as intended) before advancing a fault step.

**Roadmap phase:** **4 — Multi-node fault-injection and regression verification.**

## Phase-Specific Warnings

| Phase topic | Likely pitfall | Required mitigation |
|---|---|---|
| 1. Canonical identity and binding | Slot uses proposal-local fields; in-memory first-seen winner masquerades as finality | Versioned BurnId, deterministic winner, certificate validation independent of arrival order/restart |
| 2. Finality publication/failover | Multi-writer CRDT put, `DeliverySource` authorization, PubSub-before-persistence | One verifiable owner, immutable BurnId record, durable-before-advertise, deterministic takeover epochs |
| 3. Application/recovery | Marker written late/non-atomically; callback journal treated as finality | BurnId-keyed application state, atomic/WAL-backed effect protocol, keyed concurrency control, reconciliation |
| 4. Multi-node verification | Direct helper tests, sleeps, fake restart | Production ingress, delivery barriers, crash matrix, exact effect count and record-byte assertions |

## Sources and Evidence

- `src/account/MintTransactionV2.cpp` — current slot composition.
- `src/account/TransactionManager.cpp` — slot registration, certificate fallback/confirmation, bridge reservation, and late `/bridge/executed/` marker.
- `src/blockchain/Consensus.cpp` / `Consensus.hpp` — local slot arbitration, `/cert/<subject-hash>` persistence, PubSub-before-CRDT ordering, callback handling, and recovery journal use.
- `src/crdt/globaldb/globaldb.cpp`, `src/crdt/impl/crdt_datastore.cpp`, and `src/crdt/impl/crdt_callback_manager.cpp` — local CRDT atomicity/callback semantics, not distributed writer exclusion.
- `test/src/blockchain/consensus_slot_key_test.cpp`, `test/src/bridge_e2e/bridge_e2e_test.cpp`, and `test/src/account/transaction_manager_certificate_fallback_test.cpp` — current unit/E2E coverage limits.
- `.planning/codebase/CONCERNS.md` — known sleep-based test flakiness and persistence atomicity concerns.
