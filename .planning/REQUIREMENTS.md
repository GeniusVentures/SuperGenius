# Requirements

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

