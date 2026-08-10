# Phase 7: Deferred Validation and Pending Proposal Lifecycle - Context

**Gathered:** 2026-06-16
**Status:** Ready for planning

## Phase Boundary

This phase adds a bounded local pending lifecycle for consensus proposals whose validation cannot be
completed yet. It covers structured pending outcomes, dependency-indexed retries, scheduled retries
for transient failures, TTL expiry, resource limits, and cleanup. Pending remains local and does not
change consensus quorum rules.

## Implementation Decisions

### Pending Result Contract

- **D-01:** Subject validation should return a structured result rather than only `ConsensusManager::Check`.
  The result must preserve `Approve`, terminal `Reject`, local `Pending`, and local infrastructure
  `Stalled` semantics.
- **D-02:** `Pending` must carry typed dependency keys plus optional retry metadata. Dependency keys
  are local bookkeeping only; they are not broadcast and do not affect quorum.
- **D-03:** Use typed dependency keys from the beginning to avoid collisions. The first required type
  is `Certificate(tx_hash)`, used when a proposal is waiting for a predecessor transaction
  certificate. Future types may include registry snapshots, CRDT/datastore keys, or RPC receipts.
- **D-04:** For multiple missing dependencies, retry on any dependency arrival. Revalidation is
  incremental: if dependencies remain missing, the handler returns `Pending` again with the remaining
  keys.

### Retry Policy

- **D-05:** Transient failures without explicit dependency events use conservative scheduled backoff:
  retry after roughly 1s, 2s, 5s, then every 10s until the pending TTL expires.
- **D-06:** Dependency-triggered retries wake immediately, but each proposal has a small minimum retry
  interval to prevent retry storms when many dependency events arrive quickly.
- **D-07:** Retrying must be idempotent. A proposal can emit at most one local Approval vote per
  proposal/slot and must not double-count votes, corrupt transaction state, or duplicate cleanup.

### Capacity Policy

- **D-08:** When the pending pool reaches count or byte limits, fail closed for new pending proposals.
  Existing pending entries keep their TTL; do not evict older valid pending work just to admit newer
  work.
- **D-09:** Enforce both global and per-proposer limits. This prevents one proposer from filling the
  node's pending pool while still bounding total memory.
- **D-10:** Start with small conservative defaults: global 1,024 pending proposals, per-proposer 64,
  and 64 MB total retained pending proposal bytes. Values can be adjusted after production data.
- **D-11:** Admission failure due to capacity is not a network-level rejection vote. It is a local
  resource decision and should be logged/observable.

### Expiry Behavior

- **D-12:** Default pending TTL is three minutes. Tests should inject a shorter TTL, normally ten
  seconds.
- **D-13:** When a local outgoing transaction's proposal reaches TTL without a conclusive result, mark
  it `UNCONFIRMED`. `FAILED` is reserved for locally proven invalid transactions.
- **D-14:** Do not automatically resubmit `UNCONFIRMED` outgoing transactions in this phase. Surface
  the state; caller or queue policy decides whether and when to resubmit.
- **D-15:** For remote embedded transactions temporarily tracked as `VERIFYING`, expiry removes the
  temporary transaction record rather than keeping `UNCONFIRMED` state.
- **D-16:** Expiry must remove proposal state, typed dependency indexes, queued votes, retry metadata,
  capacity accounting, and temporary transaction tracking.

### the agent's Discretion

- Exact C++ type names and storage layout for the structured pending result and dependency key.
- Exact minimum retry interval for dependency-triggered throttling.
- Whether small defaults are compile-time constants, constructor config, or both, provided tests can
  inject lower TTL/limits deterministically.
- Exact log message wording and metrics counter names, following existing `ConsensusManagerLogger()`
  and `TransactionManagerLogger()` patterns.

### Reviewed Todos

- `bridge-startup-wiring-mock-rpc.md` — matched by keyword but not folded. It belongs to the EVM
  bridge startup/mock RPC track and is outside this consensus pending lifecycle phase.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Planning Context

- `.planning/ROADMAP.md` — Phase 7 goal, requirements, and success criteria.
- `.planning/REQUIREMENTS.md` — `PEND-01` through `PEND-07` and `TXSTATE-01`.
- `.planning/notes/deferred-consensus-validation.md` — design decisions from exploration; local-only
  Pending/Reject, typed dependencies, TTL, and deferred reject-vote scope.
- `.planning/phases/03-network-hardening-and-operational-readiness/03-CONTEXT.md` — prior cleanup
  callback and tracking lifecycle decisions that this phase extends.

### Core Implementation Files

- `src/blockchain/Consensus.hpp` — current `Check` enum, `SubjectHandler`, proposal state maps,
  pending proposal maps, and cleanup handler APIs.
- `src/blockchain/Consensus.cpp` — `HandleProposal()`, `ResumeProposalHandling()`,
  `AddPendingProposal()`, `TakePendingProposals()`, `HandleVote()`, `ProcessCertificates()`,
  `CertificateReceived()`, and `ClearProposalSlot()`.
- `src/blockchain/impl/Blockchain.cpp` — `TryResumeProposal()` facade and consensus handler
  registration wiring.
- `src/account/TransactionManager.cpp` — `HandleNonceConsensusSubject()`,
  `CheckTransactionReplayProtection()`, `OnConsensusCertificate()`, `OnProposalTimeoutCleanup()`,
  and `ChangeTransactionState()`.
- `src/account/TransactionManager.hpp` — transaction status enum, consensus subject handler API, and
  witness validation result type.
- `src/blockchain/impl/proto/Consensus.proto` — proposal, vote, certificate, and subject schemas.

## Existing Code Insights

### Reusable Assets

- `ConsensusManager::Check` already distinguishes `Approve`, `Reject`, `Pending`, and `Stalled`,
  but lacks a structured payload for dependency keys and retry metadata.
- `pending_proposals_` and `pending_by_subject_hash_` already retain proposals and support
  `ResumeProposalHandling()`. They need to be generalized from "subject hash readiness" to typed
  dependency keys.
- `RegisterProposalCleanupHandler()` and `FireProposalCleanupCallbacks()` already provide a cleanup
  extension point; this phase should extend cleanup semantics rather than introduce a disconnected
  cleanup path.
- `ChangeTransactionState()` is the established transaction lifecycle transition point.

### Established Patterns

- Consensus APIs use `outcome::result<T>` and explicit enum outcomes rather than exceptions.
- Handler registration follows `RegisterSubjectHandler()`, `RegisterCertificateHandler()`, and
  `RegisterProposalCleanupHandler()` patterns.
- Logging uses `ConsensusManagerLogger()` and `TransactionManagerLogger()` with short proposal/tx
  hashes and existing `[address - full]` transaction-manager context.
- Tests use GTest fixtures and deterministic injected timing/configuration rather than real wall-clock
  sleeps where possible.

### Integration Points

- Missing predecessor certificate currently fails in `CheckTransactionReplayProtection()` when
  `blockchain_->GetCertificateBySubjectHash(previous_hash)` fails. That path should produce
  `Pending` with `Certificate(previous_hash)` instead of terminal transaction failure.
- Certificate arrival in `ConsensusManager::CertificateReceived()` should notify or resume proposals
  waiting on `Certificate(subject_hash)`.
- `ResumeProposalHandling()` should re-run validation and either approve, re-pend with updated
  dependencies, or terminally reject.
- TTL expiry should replace the current VERIFYING-to-FAILED cleanup behavior for inconclusive local
  outgoing proposals with `UNCONFIRMED`, and remove temporary remote embedded records.

## Specific Ideas

No user-specified class or function names. The behavior decisions above are locked; implementation
shape should follow existing consensus and transaction-manager patterns.

## Deferred Ideas

- Signed Reject votes, rejection certificates, negative quorum, and validator reputation adjudication
  are explicitly out of scope for this phase.
- Automatic resubmission of `UNCONFIRMED` outgoing transactions is out of scope. This phase only
  exposes the state cleanly.

---

*Phase: 7-Deferred Validation and Pending Proposal Lifecycle*
*Context gathered: 2026-06-16*

