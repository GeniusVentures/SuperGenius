# Project Research Summary

**Project:** SuperGenius v3.0 Canonical Burn Finality Rebuild
**Domain:** Cross-chain bridge burn-to-mint finality in a C++17, CRDT-backed validator network
**Researched:** 2026-08-20
**Confidence:** MEDIUM-HIGH

## Executive Summary

This milestone is a protocol-safety and recovery rebuild, not a new bridge product. One verified external burn must map to exactly one canonical finality domain, one certificate-bound winning proposal, one authoritative published record, and one mint effect. The current path cannot establish that guarantee because its MintV2 slot and certificate persistence namespace include proposal-controlled fields, its in-memory winner selection is arrival-order-sensitive after restart, and CRDT callbacks cannot prove a writer's authority.

The recommended design is intentionally small: introduce a versioned canonical `BridgeBurnRef`, use it alone to derive the bridge slot and finality key, keep the existing certificate's exact proposal/signature/quorum binding, and publish a signed `BridgeFinalityPublication` only from the deterministic eligible owner. Persist before advertising. CRDT/GlobalDB remains the replication layer, not a distributed lock or election system. A local `BridgeFinalityStore` provides a separate durable `CERTIFIED -> APPLYING -> APPLIED` application gate with restart reconciliation.

The primary safety risks are an ambiguous burn identity, an arrival-order-dependent winner, multi-writer CRDT publication, non-verifiable local publication authority, and a crash between mint effect and replay marker. Mitigate them with a protocol specification before implementation, verification based only on signed/durable data, deterministic owner/failover epochs, a persisted application state machine, and production-path multi-node fault tests. When liveness evidence is ambiguous, safe temporary stall is preferable to accepting a conflicting record or second mint.

## Key Findings

### Recommended Stack

**Decision: add no dependency and no new transport.** The existing C++17/CMake, Protobuf, `ConsensusManager`, validator registry/signature checks, libp2p PubSub, `GlobalDB`/CRDT, RocksDB batch API, CRDT work journal, and GoogleTest/CTest fixtures are sufficient. New coordination databases, CAS/lease layers, leader-election services, queues, transports, and cryptographic libraries would add a second authority domain without closing the actual protocol gap.

**Core technologies:**

- C++17 and CMake — implement narrowly scoped value types, validation, and storage adapters in the current build.
- Protobuf and existing consensus certificates — retain the exact embedded proposal, deterministic proposal ID, vote signatures, registry binding, and quorum validation.
- `ConsensusManager` slot-key handlers and validator ordering — derive bridge-slot arbitration and a deterministic publication-owner function; do not create another consensus engine.
- `GlobalDB`/CRDT plus PubSub — persist and replicate an already-authorized signed finality publication; callbacks are notifications, not authority evidence.
- RocksDB and `rocksdb::Batch` — durably record finality/application transitions and support crash recovery; do not introduce a second datastore.
- `CRDTWorkJournal` — retry callback consumption only; never treat a work lease as canonical finality or permission to publish.
- Existing CTest multi-node/restart fixtures — test real consensus, PubSub, CRDT, durable-store, and mint ingress paths together.

### Atomic Requirement Categories

The roadmap should preserve the following atomic requirement groupings; none is safe to omit or merge into a vague "bridge finality" task.

| Category | Atomic requirements | Acceptance boundary |
|---|---|---|
| Identity and arbitration | FIN-01 through FIN-03 | A versioned canonical `BridgeBurnRef` identifies one source event; slot derivation excludes proposer, nonce, mint hash, amount, destination, and delivery order; every delivery permutation selects the same winner. |
| Certificate binding and bridge boundary | FIN-04, FIN-11, FIN-12 | A finality candidate verifies its embedded proposal, proposal ID, burn reference, signatures, registry snapshot, quorum, and winner status before any state change; invalid bridge inputs fail closed while non-bridge consensus semantics stay unchanged. |
| Canonical finality and publication | FIN-05 through FIN-08 | Exactly one versioned `/bridge/finality/v1/<slot-id>` record is accepted; publisher identity and failover round are independently verifiable; persistence occurs before advertisement; successor publication cannot create contradictory finality. |
| Unified consumption and exactly-once application | FIN-09 and FIN-10 | Local quorum, PubSub, CRDT callback/sync, and restart replay share validation and durable-state transitions; duplicate delivery or recovery cannot cause a second MintV2 effect. |
| Regression proof | TEST-01 through TEST-07 | Local multi-node tests establish contention convergence, reordered/delayed ingress, publisher loss, restart recovery, invalid-data rejection, and the distinct-burn control case. |

### Architecture Approach

Keep generic consensus and CRDT facilities intact. Add a bridge-specific protocol adapter at the boundary between bridge ingress and transaction consensus, a signed bridge-finality envelope, and a small durable finality store. This separates four concerns that must not be conflated: source-event identity, certificate validity/winner selection, publication authority/replication, and local mint idempotency.

**Major components:**

1. **`BridgeBurnRef`** — canonical binary/length-delimited, versioned external-event identity; derive `slot_id = H(BridgeBurnRef)` and use the same identity for the durable finality key.
2. **`MintTransactionV2` and bridge ingress** — carry or deterministically reconstruct `BridgeBurnRef`; remove bridge-slot dependence on proposal-local fields while retaining them as certificate-bound mint content.
3. **`ConsensusManager` bridge policy hook** — validate certificate-to-proposal binding, apply a deterministic signed-data winner comparator, calculate publication owner from the certificate registry snapshot, and allow only the eligible publisher to start publication.
4. **`BridgeFinalityPublication`** — a signed, immutable CRDT value at `/bridge/finality/v1/<slot-id>` containing version, slot/burn reference, certificate bytes/digest, proposal ID, registry CID/epoch, publisher ID, and monotonic publication round.
5. **`BridgeFinalityStore`** — local non-replicated `CERTIFIED -> APPLYING -> APPLIED(mint_tx_hash)` state, keyed by slot/burn identity and permanently bound to proposal ID/certificate digest; recover `APPLYING` by reconciling the deterministic mint/UTXO evidence.
6. **Bridge-specific CRDT filter/callback** — validate and consume the signed publication; receiver callbacks never re-`Put` the canonical key or infer authority from delivery source.
7. **`TransactionManager` / `UTXOManager` adapter** — apply the exact certificate-embedded winning MintV2 only through the finality gate; retain UTXO reservation as a local ledger guard, not distributed finality; retire the bridge-only `/bridge/executed/<chain>:<tx>` marker as the semantic replay authority.

**Required data flow:**

```text
verified external burn
  -> BridgeBurnRef / MintTransactionV2
  -> deterministic bridge slot and certified winning proposal
  -> eligible owner persists signed BridgeFinalityPublication
  -> advertises existing CRDT/PubSub state
  -> every receiver validates and consumes the same publication
  -> BridgeFinalityStore: CERTIFIED -> APPLYING -> APPLIED
  -> exact certified mint is applied once through existing transaction/UTXO paths
```

### Critical Pitfalls

1. **Proposal-local slot identity** — Never use amount, token, destination, account nonce, proposer, mint hash, or callback order to identify a burn. Use canonical source-chain facts and an event discriminator.
2. **In-memory/first-seen proposal state as finality** — A receiver that missed proposal gossip or restarted must recompute acceptance from certificate and finality-record evidence, not `slot_states_` arrival history.
3. **Proposal-keyed storage or receiver-side CRDT writes** — `/cert/<subject-hash>` cannot be the bridge-finality namespace, and `GlobalDB::Put()`/`AtomicTransaction` is not cross-peer compare-and-set. One eligible owner writes; receivers validate/apply only.
4. **Local callback provenance as authority** — `DeliverySource::Local`, PubSub origin, and timing are neither signed nor durable. Owner selection and takeover must be pure functions of burn/certificate/registry/round data.
5. **Advertise-before-persist and weak completion markers** — Persist finality before live advertisement; make application recovery explicit. Do not let the current late, weakly-keyed `/bridge/executed` marker be the exactly-once proof.
6. **Clock-based failover without protocol definition** — Specify a shared eligibility time/round basis, validator order, monotonic takeover epoch, and skew/partition behavior. A timeout that only a local node observes is unsafe.
7. **Tests that bypass production** — Direct handler calls, fixed sleeps, shared restart state, or a single observed balance change cannot prove the contract. Use controls and an observation window that detect a second effect.

## Decisions Requiring User Confirmation Before Phase Planning

These are concrete protocol choices, not implementation details. Phase 1 must stop for confirmation if they are still unresolved.

1. **Burn identity scope:** Is a source transaction guaranteed to contain at most one supported bridge burn? If not, v3.0 must add `bridge_contract_or_namespace` and `log_index`/event ordinal to ingress, `MintTransactionV2`, and the canonical `BridgeBurnRef`. Recommendation: support multiple events correctly; do not assume `(chain_id, transaction_hash)` is unique enough.
2. **Canonical winner rule:** Select the precise total order of valid competing proposals (recommended: an explicitly specified order over validated, canonical proposal IDs, with deterministic tie behavior) and define what evidence a cold receiver needs to verify that certificate's proposal won.
3. **Finality-record schema/version:** Confirm the proposed signed `BridgeFinalityPublication` fields and the treatment of legacy `/cert/<subject-hash>` data. Recommendation: bridge finality is authoritative only under `/bridge/finality/v1/<slot-id>`; legacy certificate storage may remain diagnostic/non-authoritative but must never trigger a bridge mint.
4. **Failover timebase and parameters:** Define a protocol-observable owner rotation and eligibility condition: validator ordering from the certificate registry snapshot, primary owner, fixed round interval/bounded skew assumption, successor order, and monotonic round. Recommendation: a verified certificate timestamp plus fixed published round schedule, with conservative intervals and safe stall under ambiguous evidence.
5. **Mint/UTXO crash atomicity:** Confirm whether existing storage boundaries can atomically commit finality application and UTXO/transaction effect. If not, accept the recommended WAL-style `APPLYING` reconciliation protocol and define its deterministic evidence of completed mint.

## Implications for Roadmap

### Phase 1: Canonical Burn Identity, Arbitration, and Binding

**Rationale:** Every later record, owner, and idempotency key depends on a protocol-correct finality domain and winner rule. Implementing publication first would merely move the existing ambiguity to a shared key.

**Delivers:** A versioned `BridgeBurnRef`; canonical encoding/normalization; slot derivation independent of proposal-local fields; bridge-only deterministic winner comparator; and fail-closed certificate/proposal/burn/registry/quorum validation that a cold receiver can perform.

**Addresses:** FIN-01 through FIN-04, FIN-11, FIN-12.

**Avoids:** P1, P2, P9. Explicitly update current slot-key test expectations that encode amount/destination-sensitive behavior.

**Planning flag:** **Research required.** This phase locks consensus-visible bytes, external event scope, and winner evidence; inspect actual protobuf/watcher fields and obtain the confirmations above before code.

### Phase 2: Signed Canonical Finality Publication and Safe Failover

**Rationale:** Once one certificate-bound winner is defined, establish the one canonical replicated record and make publication recoverable. Ownership, ordering, and failover form one protocol rule and cannot be safely split.

**Delivers:** Versioned finality key/envelope; certificate/owner-envelope validation; deterministic owner and takeover round computation from the registry snapshot; bridge-specific CRDT filter/callback; and persist-before-advertise publication/rebroadcast recovery.

**Addresses:** FIN-05 through FIN-08.

**Uses:** Existing Protobuf, `ConsensusManager` validator ordering, GlobalDB/CRDT, PubSub, and RocksDB. No generic CRDT or new transport.

**Avoids:** P3 through P6 and P10. Receivers must be strictly read/validate/consume-only; only an eligible owner may issue `GlobalDB::Put()` for the finality record.

**Planning flag:** **Research required.** Validate actual persistence/advertisement semantics, certificate timestamps/round data, registry snapshot availability, and the safest takeover predicate from the codebase.

### Phase 3: Unified Ingress, Exactly-Once Mint Application, and Restart Recovery

**Rationale:** A replicated canonical record is not sufficient until every ingress route and local crash boundary gates application through the same durable state machine.

**Delivers:** `BridgeFinalityStore`; `CERTIFIED -> APPLYING -> APPLIED` persistence/reconciliation; keyed same-burn local serialization; bridge-specific certificate ingress routing; startup/work-journal recovery; and replacement of the bridge execution marker as the semantic replay guard.

**Addresses:** FIN-09 and FIN-10.

**Avoids:** P7 through P9. Work-journal transitions schedule retries only; they cannot decide finality or publisher ownership. Apply checks happen before `CONFIRMED` state/mint effects.

**Planning flag:** **Research required.** Confirm the exact transaction/UTXO durable commit boundary and construct fault points around it. Keep changes adjacent to bridge paths; no TransactionManager rewrite.

### Phase 4: Production-Path Multi-Node Fault Regression

**Rationale:** The milestone’s core claim is behavior under adversarial order and loss, which unit tests cannot establish. This phase turns the contract into deterministic proof against regressions.

**Delivers:** Controlled local multi-node fixtures with isolated data directories/ports, delivery barriers or hooks, and tests for concurrent contention, delivery permutations, certificate-before-CRDT, duplicate delivery, pre-/post-persistence publisher loss, owner restart, receiver restart, invalid certificate/owner data, and distinct burns.

**Addresses:** TEST-01 through TEST-07.

**Avoids:** T1 through T3. Assertions record finality bytes, publisher/round attempts, exact UTXO/transaction deltas, and a settling window that catches a second mint; live Sepolia remains an optional smoke test only.

**Planning flag:** **Standard pattern with targeted codebase inspection.** Existing bridge E2E, CRDT integration, transaction crash, and multi-account fixtures are precedents; research is focused on harness control seams rather than an external technology decision.

### Phase Ordering Rationale

- Identity and certificate proof must precede persistence, because a shared key is unsafe until it names one unambiguous external event and one verifiable winner.
- Publication authority and failover must be specified together, because a publisher-loss test without an authorized successor is either unsafe or permanently stalled.
- Application state must follow canonical publication, because it is local idempotency rather than a network authority and must bind to the final record’s proposal/certificate digest.
- Multi-node fault tests are both a final phase and a requirement of every phase’s acceptance criteria; add focused unit/negative tests as each protocol function is introduced, then prove the full path in Phase 4.

## Test Strategy

Use layered verification, with end-to-end production-path tests as the release gate:

1. **Pure protocol tests:** canonical byte vectors, normalization, same-burn equality/different-event inequality, exclusion of mutable fields, deterministic winner permutations, certificate binding, envelope signatures, owner ordering, and failover-round eligibility.
2. **Component tests:** reject wrong BurnId, proposal/certificate mismatch, losing proposal, invalid signatures/quorum/registry epoch, unauthorized owner, duplicate/conflicting finality record, and receiver re-publication attempts.
3. **Crash/recovery tests:** inject failures before durable finality persistence, after persistence/before advertisement, during `APPLYING`, after durable mint/before `APPLIED`, and during callback work processing. Restart against the same flushed per-node data directory and reconcile exact effect state.
4. **Three-or-more-node acceptance fixture:** drive real `MintTokens`, proposal/vote/certificate propagation, PubSub, GlobalDB callbacks, and `TransactionManager` application. Control delay/reordering/duplication and node loss. Require one finality payload, one eligible publication sequence, and exactly one mint effect per BurnId; separately prove two distinct burns with similar mint fields each mint once.

Non-negotiable testing rules: no direct local-author helper as authority proof, no sleep-only completion checks, no reused memory or fresh data directory masquerading as restart, and no test that merely observes the first balance increase without detecting another during a settling interval.

## Non-Negotiable Guardrails

- No porting, rebasing, or repairing the rejected Phase 9–12 production implementation; use it only as forensic evidence.
- No new dependency, new transport, external coordination service, CRDT CAS/lease, or generic CRDT/TransactionManager/registry refactor unless a demonstrated gap changes this research conclusion.
- `BridgeBurnRef` and its finality key are derived only from immutable, canonical source-event facts; bridge mints never fall back to account/nonce identity.
- A canonical slot groups contenders but never substitutes for certificate-to-exact-proposal binding.
- A bridge certificate/finality record fails closed before transaction confirmation, checkpoint, balance, UTXO, or applied-state mutation unless all bridge-specific checks pass.
- CRDT callback source, PubSub sender, local timing, and `DeliverySource::Local` are never authorization inputs.
- Receivers never write the canonical finality record because they received it. All publication and failover authorization is reproducible after cold restart from signed/durable protocol facts.
- Persist the immutable record before live advertisement; recover/rebroadcast the same bytes, never manufacture a second certificate/record for the burn.
- The work journal is retry bookkeeping only; the finality store and application state are the exactly-once authority.
- Safety wins under ambiguous ownership/failover evidence: stall and diagnose rather than accept a second authority or mint.
- Non-bridge consensus behavior and existing external-burn verification remain unchanged except where bridge subject dispatch explicitly invokes this protocol.

## Confidence Assessment

| Area | Confidence | Notes |
|---|---|---|
| Stack | HIGH | Direct codebase evidence shows existing C++17, Protobuf, consensus, CRDT/GlobalDB, RocksDB, and test facilities cover the required capabilities. |
| Features | HIGH | Atomic requirements and acceptance signals derive directly from the stated milestone safety contract and observed current failure modes. |
| Architecture | MEDIUM-HIGH | Component boundaries and flows map cleanly to existing seams; the envelope field set and exact recovery atomicity require phase-level verification. |
| Pitfalls | HIGH | Failure modes are evidenced by the current slot/key/publication/marker/callback implementations and existing test limitations. |

**Overall confidence:** MEDIUM-HIGH. The direction and scope are well supported; protocol-visible serialization, winner proof, failover schedule, and mint-commit recovery mechanics must be locked before implementation.

### Gaps to Address

- **External event cardinality:** verify supported bridge contracts/events and decide whether one source transaction can contain multiple burns. If yes, propagate contract namespace and log/event index end-to-end.
- **Winner verifiability:** define whether the certificate or finality envelope must carry sufficient competing-proposal evidence, or whether consensus provides a durable canonical proposal set. Do not rely on in-memory arrival history.
- **Failover semantics:** select the protocol-observable clock/round inputs, interval, skew/partition model, first-owner restart behavior, and the exact rule for accepting same-content re-publication after a later round.
- **Storage atomicity:** prove the current transaction/UTXO persistence boundaries and specify the recovery reconciliation record before changing `/bridge/executed` handling.
- **CRDT conflict semantics:** verify that the proposed immutable signed envelope/key use and filter behavior cannot surface a conflicting accepted value; test conflict rejection explicitly rather than assuming CRDT merge provides it.

## Sources

### Primary (HIGH confidence)

- [STACK.md](STACK.md) — existing facility inventory, no-new-dependency position, and concrete extension seams.
- [FEATURES.md](FEATURES.md) — FIN-01 through FIN-12 and TEST-01 through TEST-07 acceptance contract.
- [ARCHITECTURE.md](ARCHITECTURE.md) — proposed protocol objects, component boundaries, data flow, recovery states, and implementation order.
- [PITFALLS.md](PITFALLS.md) — codebase-specific safety, durability, failover, and test-validity hazards.
- [PROJECT.md](../PROJECT.md) — milestone goal, scope limits, and existing-system constraints.

### Secondary (MEDIUM confidence)

- Existing implementation/test paths cited throughout the research: `ConsensusManager`, `TransactionManager`, `MintTransactionV2`, `BridgeRelayer`, bridge watcher, GlobalDB/CRDT callback machinery, RocksDB batching, and local multi-node/restart fixtures.

---
*Research completed: 2026-08-20*
*Ready for roadmap: yes — after the listed protocol confirmations are resolved or explicitly assigned to Phase 1.*
