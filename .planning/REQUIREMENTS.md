# Requirements

## Milestone v1.1: Multi-Signature Secure CRDT Storage

### Active

**MultiSig — decoupled multi-signature primitive**
- [ ] **MSIG-01:** A component computes canonical signing-bytes for an arbitrary payload and verifies signatures against it, reusing `ConsensusAuth`'s SHA-256/`VerifySignature` primitives
- [ ] **MSIG-02:** The component supports N-of-M quorum evaluation given a signer set and a required threshold (no hardcoded N)
- [ ] **MSIG-03:** The component is usable independently of CRDT (importable/testable without a running node)

**SecureCRDT — secure CRDT storage layer**
- [ ] **SCRDT-01:** An `ISignedCRDTData` interface exists: implementers provide payload codec, `Verify()`, `Apply()`
- [ ] **SCRDT-02:** A static registry maps a topic/key pattern to {signer-set source, quorum rule, `ISignedCRDTData` type}, declared in code at startup
- [ ] **SCRDT-03:** Writing/updating a registered CRDT key requires quorum-verified signatures; unsigned or under-signed writes are rejected locally before being applied
- [ ] **SCRDT-04:** Propose/sign/quorum flow works entirely via CRDT puts + filter callbacks (pending-value + signature entries) — no new networking/RPC

**TrustedPeerRegistry — new component**
- [ ] **TPR-01:** Genesis node seeds an initial trusted-peer set from a hardcoded genesis config entry
- [ ] **TPR-02:** Adding/removing/replacing a member requires a configurable N-of-M quorum of signatures from the CURRENT trusted-peer set
- [ ] **TPR-03:** `TrustedPeerRegistry` is implemented via `ISignedCRDTData`/SecureCRDT (SCRDT-01..04), not bespoke logic

**BurnConfig — applying it to BURN_BASIS_POINTS**
- [ ] **BURN-01:** `BURN_BASIS_POINTS` becomes a `TrustedPeerRegistry`-quorum-signed CRDT value instead of a compile-time constant
- [ ] **BURN-02:** `TransactionManager` caches the current value and refreshes it via a CRDT-change callback (no CRDT read per `PayEscrow` call)
- [ ] **BURN-03:** Existing behavior is preserved by default — genesis seeds `BURN_BASIS_POINTS=100` (1%) so `PayEscrow` burns the same amount until a quorum-signed update changes it

**Migration**
- [ ] **MIG-05 (approved adjusted scope):** `ValidatorRegistry` genesis-path signature verification reuses `multisig::VerifyPayloadSignature`; the broader `ISignedCRDTData` storage/quorum migration is retired by the locked Phase 12 scope decision
- [ ] **MIG-06:** Existing `ValidatorRegistry` behavior/tests remain green after migration

**Phase 13 closure — trusted-peer genesis, policy authority, and production integration**
- [ ] **BOOT-01:** Document the manual trusted-channel peer collection, validation, canonicalization, fingerprint review, ephemeral-key handling, and non-production status of example identities
- [ ] **BOOT-02:** Define a canonical authenticated genesis manifest binding network ID, bootstrapper public key, ordered peers, policy version, both quorum thresholds, initial burn value, and fingerprint
- [ ] **BOOT-03:** Confirm TPR genesis through production SecureCrdt and persist the confirmed identity before enabling policy or economic behavior
- [ ] **BOOT-04:** On restart, use verified persisted state as authority and reject rollback, fork, corruption, or network mismatch without erasing last-known-good state
- [ ] **POLICY-01:** Store membership and BurnConfig thresholds in versioned quorum-signed policy state whose successor is authorized exclusively by the current confirmed policy
- [ ] **VALID-01:** Enforce bounded non-empty unique valid peers, complete threshold bounds, strict-majority membership floor, and two-thirds BurnConfig floor
- [ ] **TEST-01:** Prove first boot, restart, altered JSON/bootstrapper/thresholds, manifest tamper, rollback/fork, candidate race, explicit approval, live `PayEscrow`, and account-switch lifetime behavior

### Out of Scope

- `ConsensusManager` changes / pluggable voter sources — CRDT itself carries propose/sign/quorum messages
- Any new pubsub/RPC transport — reuse existing CRDT put/filter-callback machinery
- Unrelated consensus refactors

### Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MSIG-01 | Phase 8 | Pending |
| MSIG-02 | Phase 8 | Pending |
| MSIG-03 | Phase 8 | Pending |
| SCRDT-01 | Phase 9 | Pending |
| SCRDT-02 | Phase 9 | Pending |
| SCRDT-03 | Phase 9 | Pending |
| SCRDT-04 | Phase 9 | Complete |
| TPR-01 | Phase 10 | Complete |
| TPR-02 | Phase 10 | Complete |
| TPR-03 | Phase 10 | Pending |
| BURN-01 | Phase 11 | Complete |
| BURN-02 | Phase 11 | Complete |
| BURN-03 | Phase 11 | Complete |
| MIG-05 | Phase 12 | Pending |
| MIG-06 | Phase 12 | Pending |
| BOOT-01 | Phase 13 | Pending |
| BOOT-02 | Phase 13 | Complete |
| BOOT-03 | Phase 13 | Complete |
| BOOT-04 | Phase 13 | Complete |
| POLICY-01 | Phase 13 | Complete |
| VALID-01 | Phase 13 | Complete |
| TEST-01 | Phase 13 | Complete |

Coverage: 22/22 v1.1 requirements mapped.

## Slot-Based Network Voting (Phase 6)

### Active

- [ ] **REQ-SLOT-01 — Proto extension:** `ConsensusVote` extended with 3 `bytes32` fields (`slot_0_hash`, `slot_1_hash`, `slot_2_hash`) using unused field tags 6/7/8 per D-01.
- [ ] **REQ-SLOT-02 — Slot 0 (DIRECT_API):** Only validators with a paid/API-key RPC endpoint fill slot 0. 1 valid hash contributes `voter.weight × 0.50` to qualified_sum per D-02.
- [ ] **REQ-SLOT-03 — Slots 1-2 (PUBLIC) hash deduplication:** Hash groups with ≥2 distinct validators contribute `voter.weight × 0.25`. Solo hashes (1 validator) contribute zero per D-03.
- [ ] **REQ-SLOT-04 — Cumulative >75% quorum:** Certificate produced iff `qualified_sum > total_voting_reputation × 0.75` where qualified_sum is the cumulative slot-weighted sum across all 3 slots per D-06.
- [ ] **REQ-SLOT-05 — Both tally sites agree:** Shared `EvaluateQuorum` helper dispatches both `TallyVotes` (certificate) and `HandleVote` (incremental) for bridge-mint subjects; non-bridge subjects use unchanged single-pool `IsQuorum` per D-06, RESEARCH Pitfall 1.
- [ ] **REQ-SLOT-06 — Abstention:** A validator that cannot produce any valid RPC hash marks the transaction seen/invalid and does not vote; all three slot hashes empty contributes full weight to total_voting_reputation but zero to qualified_sum (raising the threshold without helping meet it) per D-05.
- [ ] **REQ-REPUT-01 — Role::FULL promotion:** REGULAR validator with weight ≥ `full_promotion_weight_` and penalty_score < `penalty_threshold_` is promoted to Role::FULL inside `ApplyVoteEffects` per D-07, D-08.
- [ ] **REQ-DETERM-01 — Deterministic:** Given the same vote set and registry snapshot, every peer computes identical qualified_sum and threshold; slot tally is a pure function of votes + registry state per D-06, D-07.

## Pending Proposal Lifecycle

### Active

- [ ] **PEND-01 — Structured deferred validation:** Subject validation can return `Pending` with zero
  or more dependency keys while preserving distinct `Approve`, terminal `Reject`, and local
  infrastructure `Stalled` outcomes.
- [ ] **PEND-02 — Local-only Pending:** Pending outcomes are retained locally and never broadcast as
  votes or counted toward quorum. Approval remains the only broadcast voting outcome in this phase.
- [ ] **PEND-03 — Dependency-triggered retry:** Consensus indexes pending proposals by their missing
  dependency keys and retries validation immediately when a dependency becomes available. A
  predecessor certificate arrival must resume every proposal waiting on that certificate hash.
- [ ] **PEND-04 — Scheduled transient retry:** Pending outcomes without an explicit dependency event
  are retried using bounded scheduling and backoff so transient RPC, datastore, or similar local
  failures can recover.
- [ ] **PEND-05 — Bounded lifetime:** Pending proposals expire after a compile-time default TTL of
  three minutes. `ConsensusManager` permits TTL injection/configuration for deterministic tests,
  which normally use ten seconds.
- [ ] **PEND-06 — Resource bounds and cleanup:** Consensus enforces pending proposal count and retained
  byte limits. Certification, terminal rejection, or expiry removes proposal state, dependency
  indexes, queued votes, retry metadata, and temporary transaction tracking.
- [ ] **PEND-07 — Retry-safe validation:** Retrying the same proposal is idempotent and cannot cast
  duplicate votes, double-count validator weight, or corrupt transaction state.
- [ ] **TXSTATE-01 — Inconclusive transaction state:** Consensus timeout/TTL expiry uses a distinct
  `EXPIRED` or `UNCONFIRMED` transaction state. `FAILED` is reserved for transactions proven invalid
  by local validation.

### Out of Scope

- Broadcasting Pending decisions.
- Signed Reject votes, rejection certificates, or negative-quorum rules.
- Validator reputation rewards or penalties based on negative votes.
- Penalizing validators when a proposal merely expires without a conclusive outcome.
