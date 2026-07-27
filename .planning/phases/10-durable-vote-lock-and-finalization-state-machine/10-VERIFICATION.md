---
phase: 10-durable-vote-lock-and-finalization-state-machine
verified: "2026-07-27"
status: passed
score: "33/33 must-have truths verified"
requirements: "10/10 phase requirements satisfied"
artifacts: "21/21 declared artifact entries present and substantive"
key_links: "17/17 declared key links wired"
roadmap_success_criteria: "4/4 verified"
verified_head: e53c4682231b6cbd097461c85d7d4fbf4b3188f5
gaps: []
human_verification: []
advisory_follow_up:
  - WR-10-001
  - WR-10-002
  - WR-10-003
  - WR-10-004
deferred:
  - truth: "Bridge burn reservation and consumption are owned by the canonical slot."
    addressed_in: "Phase 11"
  - truth: "The complete 11-node single-burn race passes end to end."
    addressed_in: "Phase 12"
---

# Phase 10 Verification: Durable Vote Lock and Finalization State Machine

## Verdict

Phase 10 passes at committed `HEAD`
`e53c4682231b6cbd097461c85d7d4fbf4b3188f5`.

The committed implementation gives each validator a strict node-local vote
journal, restores it before live consensus side effects, persists exact signed
vote and envelope bytes before publication, and prevents a different local
vote while the lock remains usable. Candidate selection has one fixed bounded
window and deterministic ordering. Certificate ingress converges on one
authoritative slot finalizer that establishes finality before application and
cleanup, applies the exact winner idempotently, and records valid competing
certificates as durable slot-local safety violations.

The advisory review's four warnings are real follow-up defects, but none
falsifies the Phase 10 safety goal, a normative Phase 10 requirement, or a
declared plan must-have. They concern stale-conflict classification, fail-safe
liveness after pre-publication failure, callback-initiated shutdown, and
same-process CRDT scheduling recovery after a journal-write failure. They do
not permit a second published local vote, overwrite or apply a second winner,
clear state before durable finality, or make restart forget an active vote.

## Roadmap Success Criteria

| # | Criterion | Status | Direct evidence |
|---|---|---|---|
| 1 | Persist a slot vote before publication, restore it after restart, and never sign a competitor while it remains certificate-valid | VERIFIED | `ProcessCandidateDeadlines` reserves `SigningPublishing`, serializes one signed vote/envelope, calls `PutActiveVote`, then `PublishSerialized`. `RestoreLocalConsensusState` scans before startup side effects, and `ReplayDurableVote` publishes stored envelope bytes only. Horizon equality remains locked and retirement is durable before a later generation. |
| 2 | Candidates may replace the best only during a bounded pre-vote window | VERIFIED | The first admitted proposal establishes one steady-clock deadline; `IsBetterProposal` updates only `Selecting`; deadline processing freezes a generation and all later candidates become diagnostic. Fixed-window/comparator and finalization-race tests pass. |
| 3 | Certificate finality precedes proposal/vote cleanup even when the winner differs from the local vote | VERIFIED | `FinalizeSlot` persists or confirms `/cert/v2/slot` authority, installs a durable Pending marker, runs the exact-winner handler, persists Complete, and only then calls `ClearProposalSlot`. Finalizing reservations suppress competing publication. |
| 4 | Duplicate paths are idempotent; conflicts are not applied and emit actionable diagnostics | VERIFIED | Local, PubSub, CRDT recovery, and startup recovery reach `FinalizeSlot`; processing leases plus exact process identity prevent duplicate handler effects. `RecordCertificateConflict` atomically persists certificate-free evidence and `SafetyViolation`, preserves authority, blocks the affected slot, and emits a critical diagnostic/unique-pair metric. |

## Plan Must-Haves

Every declared truth, artifact, and key link was checked against committed
source and focused tests rather than accepted from summary claims.

| Plan | Truths | Artifacts | Key links | Evidence and result |
|---|---:|---:|---:|---|
| 10-01 | 3/3 | 3/3 | 1/1 | Both active harness targets use real same-path RocksDB/CRDT dependencies, explicit clocks, predicate barriers, RAII release/join ownership, and friend-only access. |
| 10-02 | 4/4 | 3/3 | 2/2 | `/consensus/local/v2` direct-RocksDB records preserve exact bytes, fail closed on malformed/query state, retire durably, and batch conflict evidence with safety state without CRDT replication. |
| 10-03 | 4/4 | 3/3 | 2/2 | Strict scans and cross-checks precede subscriptions, filters, timers, and replay. Configuration is resolved before construction; replay uses stored envelopes; absent handlers remain Pending. |
| 10-04 | 7/7 | 3/3 | 3/3 | Selecting, `SigningPublishing`, Voted, replay, and Retired transitions enforce a fixed deadline, canonical comparator, store-before-publish ordering, exact-byte retry, and strict horizon retirement. |
| 10-05 | 6/6 | 3/3 | 4/4 | All ingress adapters converge on one finalizer. Authority precedes Pending, handler, Complete, and cleanup; exact occupied authority is idempotent and a valid different local winner overrides local preference. |
| 10-06 | 5/5 | 3/3 | 3/3 | Fully validated conflicts from every ingress create one canonical evidence pair and durable per-slot stop, never overwrite/rebroadcast/apply a second winner, and preserve original-winner retry. |
| 10-07 | 4/4 | 3/3 | 2/2 | Owned shutdown wakes and drains normal timer/callback/handler activity, CRDT ingress avoids synchronous persistence reentrancy, focused tests contain no timing sleeps/detached threads, and all Phase 9 compatibility targets pass. |
| **Total** | **33/33** | **21/21** | **17/17** | **VERIFIED** |

## Requirement Accounting

| Requirement | Status | Evidence |
|---|---|---|
| CERT-05 | SATISFIED | Slot/index authority and durable Pending state are established before handler execution; Complete is durable before cleanup. |
| CERT-06 | SATISFIED | Local, PubSub, CRDT-deferred recovery, and startup recovery share `FinalizeSlot`; exact process identity and a per-slot lease make application idempotent. |
| CERT-07 | SATISFIED | A different valid certificate preserves the authoritative winner, records both proposal/digest identities and source metadata, applies no second winner, and activates a durable slot-local safety stop. |
| VOTE-01 | SATISFIED | Exact signed vote and outbound envelope are committed by `PutActiveVote` before the first raw publication attempt. |
| VOTE-02 | SATISFIED | Active durable slot/generation identity suppresses competitor signing; replay reuses the same stored envelope without invoking the signer. |
| VOTE-03 | SATISFIED | Vote/process/conflict/safety scans and authoritative-certificate reconciliation finish before transport participation and replay. |
| VOTE-04 | SATISFIED | First valid admission starts one bounded immutable window; the existing comparator and deterministic proposal-ID tie-break freeze one winner. |
| VOTE-05 | SATISFIED | After signing/publication reservation, later candidates are diagnostic and cannot replace or retract the durable vote. |
| VOTE-06 | SATISFIED | A valid authoritative certificate finalizes and applies its exact winner regardless of the validator's local candidate or vote, without applying another winner. |
| VOTE-07 | SATISFIED | Acceptance horizon is derived from the signed proposal/vote timestamps; equality stays locked, and `RetireVote` must commit before a later generation signs. |

## Advisory Review Classification

| Finding | Phase 10 classification |
|---|---|
| WR-10-001: stale different live certificate can create a safety stop | Valid warning, not a must-have gap. It may over-classify a stale structurally valid conflict, but preserves the first authority, produces no second signature/application, and is fail-safe. Source-specific live-vs-historical conflict semantics should be tightened. |
| WR-10-002: signer/serialization/store failure enters a non-durable `Voted` state | Valid liveness warning, not a safety gap. The current behavior deliberately suppresses every later signature and publishes nothing; therefore it cannot violate at-most-one usable signature, though its lifecycle label and retry behavior should be improved. |
| WR-10-003: `Close()` can self-deadlock from a leased callback | Valid lifecycle warning outside the verified external-owner shutdown contract. Normal close with open windows, blocked handlers, pending recovery, and timer activity is owned and tested; callback-initiated close needs a two-phase contract. |
| WR-10-004: CRDT journal-write failure can postpone application until restart | Valid same-process recovery warning, not an idempotency/finality gap. The authoritative certificate remains durable, startup rescan recovers it, and no duplicate or conflicting winner can be applied. |

## Test and Regression Evidence

- The exact eight-target Phase 10 CTest gate passed **8/8**:
  `consensus_vote_journal`, `consensus_finalization`,
  `consensus_pending_lifecycle`, `consensus_certificate_store`,
  `certificate_compatibility`, `network_config_precedence`,
  `transaction_manager_pending_lifecycle`, and `utxo_manager`.
- The Phase 9 regression gate passed **11/11**.
- Schema drift check reported **false**.
- Deterministic focused coverage includes store-before-publish, exact restart
  replay, competitor suppression, fixed deadline/comparator behavior,
  finalization/publication ordering in both directions, horizon retirement,
  all-ingress exact-winner application, missing-handler recovery, durable
  conflict evidence, safety restart, and owned shutdown.
- The three focused consensus lifecycle sources contain no `sleep_for` or
  detached-thread timing assertions.

## Deferred Boundary

Phase 10 intentionally does not redesign bridge burn reservation/consumption
and does not execute the 11-node single-burn race. Phase 11 owns canonical
slot-scoped burn reservations; Phase 12 owns the end-to-end race proof.

## Human Verification

None required.
