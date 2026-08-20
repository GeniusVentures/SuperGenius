# Feature Landscape: Canonical Burn Finality Rebuild

**Domain:** Cross-chain bridge burn-to-mint finality in a C++17, CRDT-backed validator network
**Researched:** 2026-08-20
**Confidence:** HIGH for scope and acceptance criteria (derived from the milestone contract and current codebase); MEDIUM for the exact finality-record schema and failover schedule (design decisions still required).

## Product Boundary

This is a safety-and-liveness rebuild, not a new bridge product. A verified external burn must occupy one protocol-defined finality domain, yield no more than one authoritative certificate, and cause exactly one mint effect. The contract must hold when multiple validators propose concurrently, data arrives in different orders, a receiver restarts, or the initially selected publisher disappears.

The present path does not provide that contract: `BridgeRelayer::OnWatchEvent()` directly invokes `TransactionManager::MintFunds()`, and `ConsensusManager::SubmitCertificate()` persists under `/cert/<subject-hash>`. Since a proposal subject can vary with proposer-owned fields, subject-hash storage is not a burn-finality namespace. Per-node UTXO reservation and the executed-burn check remain useful local defenses, but are not authority or distributed-finality rules.

## Table-Stakes Features

Every item below is an atomic, observable requirement for this milestone.

| ID | Feature / required behavior | Acceptance signal | Complexity | Dependencies |
|---|---|---|---|---|
| FIN-01 | **Canonical external-burn identity.** Define a canonical identity for one externally verified burn event from immutable source-event facts. It must include sufficient source context to distinguish two burn events in one transaction (for example source chain, bridge contract, transaction hash, and log/event position), with canonical byte/normalization rules. | Equivalent burn observations and competing mint proposals calculate identical identity/slot values; different burn events never collide. | Med | Existing external receipt/event verification and canonical encoders. |
| FIN-02 | **Identity is independent of proposal-local fields.** The burn identity/slot must not depend on proposer address, account nonce, mint transaction hash, destination, amount, or callback arrival order. Those values remain certificate-bound proposal content and must still be validated. | Two valid proposals for one burn with different proposer/nonce or otherwise competing proposal fields share a slot; distinct burns that merely share amount/destination do not. | Med | FIN-01; bridge subject decoding. |
| FIN-03 | **Deterministic slot arbitration.** All validators select the same eligible winning proposal in a burn slot using a total ordering derived only from signed, durable protocol facts. Votes/certification for a losing proposal must not make it authoritative. | Reordering proposal delivery at peers produces the same winner; only the winner can progress to burn finality. | High | FIN-01, FIN-02; existing proposal/vote validation. |
| FIN-04 | **Certificate-to-proposal binding.** A finality record accepts a certificate only when its embedded proposal, proposal ID, signatures, registry snapshot/quorum, canonical burn identity, and winning-slot status all verify. | Tampered, mismatched, wrong-slot, losing, or insufficient-quorum certificates are rejected and never mint. | High | FIN-03; existing `ValidateCertificate()` cryptographic checks. |
| FIN-05 | **One canonical durable finality record per burn.** Store/retrieve finality by the canonical burn identity, not by an arbitrary proposal/subject hash. The record must identify the selected proposal and certificate and have deterministic conflict handling. | Two competing certificates/proposals cannot create two authoritative CRDT finality values for one burn; a lookup after restart finds the same authoritative record. | High | FIN-01, FIN-04; GlobalDB/CRDT persistence. |
| FIN-06 | **Protocol-verifiable publication ownership.** The permitted publisher is deterministically derived from certificate/proposal/registry/finality facts that every peer can verify. Receipt from PubSub, CRDT, replay, or local code is not evidence of authorship. | A node can accept or reject a publication attempt from its durable protocol contents alone; no `DeliverySource`-like local ingress flag participates in authorization. | High | FIN-04, FIN-05; validator ordering/registry snapshot. |
| FIN-07 | **Persistence before advertisement.** The authorized publisher durably commits the canonical finality record before advertising it on the network. Retrying the operation observes the prior durable state rather than creating a second record. | A forced interruption after local commit but before outbound publication recovers and advertises/serves the same record; no second certificate is made. | High | FIN-05, FIN-06; crash-safe persistence semantics. |
| FIN-08 | **Safe deterministic failover.** If the initial owner fails before publication, a deterministic successor can publish the same already-verified finality record after the specified eligibility/liveness condition. Failover must not permit concurrent contradictory records. | Stop the selected publisher before advertisement: a successor completes publication; restart of the first publisher does not create a second authority or mint. | High | FIN-05 through FIN-07; explicit ownership/failover rule. |
| FIN-09 | **Unified certificate ingress and recovery.** PubSub receipt, CRDT callback/sync, direct local recovery, and restart replay use the same validation and durable-finality transition. Receivers consume a valid record; they do not re-write it merely because they received it. | Delayed or duplicate transport delivery causes one durable record and one handler/application transition at every node. | High | FIN-04 through FIN-08; work journal/recovery path. |
| FIN-10 | **Exactly-once mint application.** Mint application is gated by the canonical durable finality record and records an idempotent completion state sufficient to recover after process restart. A duplicate certificate, callback, replay, or failover retry cannot create another MintV2 effect. | For one burn, observed balance/UTXO/mint effects equal one mint across all test schedules, including restart during finality handling. | High | FIN-05, FIN-09; TransactionManager/UTXO durability. |
| FIN-11 | **Preserve the existing bridge verification boundary.** Only a validated external burn is eligible to enter the canonical-finality path. Malformed bridge event data and missing/unverifiable source-chain evidence fail closed; the redesign does not weaken contract/log verification. | Existing malformed-event and missing-endpoint negative tests still pass, and invalid events never obtain a slot or mint. | Med | Existing BridgeRelayer/watch/receipt validation. |
| FIN-12 | **Non-bridge consensus compatibility.** The finality rules are scoped to bridge-burn subjects and do not change semantics of nonce, task-result, registry-batch, processing-grid, or ordinary transaction consensus. | Existing consensus and non-bridge test targets retain their current behavior. | Med | Subject-type dispatch boundaries. |

## Required Regression Coverage

Correctness is not demonstrated by direct helper calls or a single-node replay check. The milestone must add deterministic, local multi-node tests using real `GossipPubSub`/`GlobalDB` and consensus ingress paths—the existing CRDT integration fixture is the closest precedent. Live Sepolia tests may remain smoke tests, but must not be the proof of protocol correctness.

| ID | Scenario | Assertions | Complexity | Depends on |
|---|---|---|---|---|
| TEST-01 | Competing proposals for one burn at three or more validator nodes. | All nodes select one proposal/certificate/finality record; exactly one mint effect results. | High | FIN-01–FIN-10 |
| TEST-02 | Same burn proposals delivered in different orders. | Convergence on the same winner and record, not first-seen behavior. | High | FIN-03–FIN-05 |
| TEST-03 | Certificates, proposals, and CRDT updates delayed/reordered/duplicated. | All ingress paths converge; no receiver-side duplicate CRDT write or second mint. | High | FIN-05, FIN-09, FIN-10 |
| TEST-04 | Initial publisher crashes or is disconnected before publication. | Verifiable successor publishes one finality record and one mint completes. | High | FIN-06–FIN-10 |
| TEST-05 | Receiver and/or original publisher restarts mid-finality. | Recovery reuses the durable record/completion state and does not re-certify or re-mint. | High | FIN-07, FIN-09, FIN-10 |
| TEST-06 | Negative certificate tests. | Wrong identity, losing proposal, altered embedded proposal, invalid signatures/quorum, and unauthorized publisher are rejected without state change. | Med | FIN-04, FIN-06 |
| TEST-07 | Distinct-burn control case. | Two different external burn identities with the same amount/destination each finalize and mint once. | Med | FIN-01, FIN-10 |

## Constraints and Design Invariants

- **Safety precedes liveness:** lack of an eligible publisher may delay minting, but must never authorize a second certificate or mint. Failover is required precisely so the safe design remains live after initial publisher loss.
- **Authority must be portable:** authorization can depend only on data independently available and verifiable by participants (canonical burn identity, certificate, signed proposal, registry snapshot, and defined ownership/failover state). It cannot depend on which local callback ran first.
- **Certificate binding is retained:** canonical slots group contenders; they do not let a certificate authenticate a different proposal or mutable mint payload.
- **Durability is part of the protocol outcome:** in-memory `slot_states_`, UTXO reservation, and a callback work lease are insufficient by themselves for cross-node/restart safety. The record and application-completion boundary must have an explicitly defined crash/retry story.
- **Scope changes are minimal:** use C++17, CMake, existing RocksDB/GlobalDB/CRDT/libp2p facilities, and existing consensus cryptography. A new third-party dependency needs a specific demonstrated gap.
- **Test the deployed shape:** production PubSub, CRDT filtering/callback, certificate handling, durable storage, and mint application must be exercised together. A unit test of `GetSlotKey()` is supporting evidence only.

## Differentiators

These are not additional product surfaces; they make the core safety contract operable and diagnosable.

| Capability | Value | Complexity | Notes |
|---|---|---|---|
| Inspectable finality status by canonical burn identity | Operators and tests can distinguish pending, published, failed-over, and mint-applied states without inferring from a balance. | Med | Prefer internal query/test access before exposing a public API. |
| Structured ownership/failover diagnostics | Makes a stalled burn attributable to missing evidence, invalid certificate, current owner, or elapsed failover condition. | Low | Logging/metrics should include canonical identity and proposal ID, with safe truncation. |
| Deterministic fault-injection hooks in tests | Converts timing races into reproducible schedules for publication loss, delayed replication, and restart. | Med | Test-only seams; do not add a production bypass. |

## Explicit Anti-Features / Exclusions

| Anti-feature | Why excluded | Do instead |
|---|---|---|
| Copy, port, rebase, or repair Phase 9–12 production code | The rejected implementation established the exact multi-writer and local-provenance failure this milestone replaces. | Use it only as forensic evidence; implement the protocol from `develop` with this contract. |
| Treat `DeliverySource::Local`, PubSub origin, callback timing, or any local boolean as publication authority | It cannot be independently verified by peers and disappears across restart. | Verify deterministic ownership/failover from durable protocol data. |
| Let every receiver write/forward the canonical CRDT certificate key | Concurrent writers reintroduce the certificate-store race. | One authorized publisher writes; receivers validate, consume, and recover; failover is rule-governed. |
| Store finality solely under a proposal/subject hash | Competing proposals for the same burn can have different subject hashes and thus independent certificates. | Use canonical burn identity as the finality namespace while retaining proposal binding inside the record. |
| “First arrival wins” or a wall-clock/local-retry winner rule | Network ordering differs by peer and is not a deterministic consensus rule. | Use a total ordering over protocol-visible signed facts. |
| Relax certificate validation to accept a record because its slot matches | A slot proves competition domain, not that the certificate certified the exact payload to mint. | Enforce FIN-04 before any durable/mint transition. |
| Broad CRDT, TransactionManager, registry, or persistence rewrites | Large blast radius obscures the finality contract and violates the narrowly scoped milestone. | Make only the smallest adapter/state changes necessary for finality, publication, recovery, and idempotent application. |
| New bridge APIs, node roles, token standards, source chains, or EVM ABI changes | They do not solve canonical finality and expand testing/compatibility scope. | Preserve current bridge-event verification and `MintV2` semantics. |
| Depend on live Sepolia or an external RPC for race correctness tests | External timing/availability makes protocol regressions nondeterministic and hard to reproduce. | Use local multi-node fault-injection tests; retain opt-in live E2E only as a smoke layer. |

## Feature Dependencies

```text
FIN-01 canonical burn identity
  └─→ FIN-02 proposal-independent slot
        └─→ FIN-03 deterministic winner
              └─→ FIN-04 certificate/proposal/slot validation
                    └─→ FIN-05 canonical durable finality record
                          ├─→ FIN-06 verifiable ownership
                          │     └─→ FIN-07 persistence-before-advertisement
                          │           └─→ FIN-08 deterministic failover
                          └─→ FIN-09 unified ingress/recovery
                                └─→ FIN-10 exactly-once mint application

FIN-11 fail-closed external verification ──→ FIN-01
FIN-12 non-bridge compatibility ──────────→ every implementation phase
TEST-01…07 validate the chain above end-to-end
```

## MVP Recommendation

Prioritize in this order:

1. **Define and implement the canonical burn identity, slot arbitration, and certificate binding** (FIN-01–FIN-04). Do not begin publisher logic until the identity and winning-certificate rules are precise.
2. **Implement the single durable finality record plus verifiable ownership and failover** (FIN-05–FIN-08). This is the boundary that eliminates the known CRDT writer race.
3. **Route all ingress/recovery through that record and gate mint application idempotently** (FIN-09–FIN-10), while preserving the existing external verification boundary (FIN-11).
4. **Prove it with local multi-node fault-injection tests** (TEST-01–TEST-07) and retain non-bridge compatibility (FIN-12).

Defer public operational APIs, redesigning all consensus storage, and broad TransactionManager/CRDT cleanup. They may be useful later but do not establish this milestone's safety invariant.

## Sources

- Milestone contract and constraints: `.planning/PROJECT.md` (v3.0 start, 2026-08-20).
- Current bridge ingress/direct mint behavior: `src/account/BridgeRelayer.cpp` (`OnWatchEvent`) and `src/account/TransactionManager.cpp` (`MintFunds`).
- Current proposal/certificate persistence and validation behavior: `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` (`GetSlotKey`, `SubmitCertificate`, `CertificateReceived`, recovery).
- Existing scope of regression coverage and fixture conventions: `test/src/blockchain/consensus_slot_key_test.cpp`, `test/src/crdt/globaldb_integration.cpp`, and `test/src/bridge_e2e/bridge_e2e_test.cpp`.
