# Requirements

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

