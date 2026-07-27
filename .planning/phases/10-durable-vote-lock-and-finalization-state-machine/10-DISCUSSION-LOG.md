# Phase 10: Durable Vote Lock and Finalization State Machine - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-27
**Phase:** 10-Durable Vote Lock and Finalization State Machine
**Areas discussed:** Vote-journal lifecycle, Candidate-selection window, Finalization and replay, Conflicting certificates

---

## Vote-journal lifecycle

| Decision | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Startup handling | Fail initialization; observer-only; quarantine individual slots | Fail initialization before consensus activity |
| Durable contents | Exact signed vote; vote intent; signature-only record | Exact signed vote plus slot, proposal, validator, and validity metadata |
| Uncertified-lock retirement | Deterministic acceptance horizon; never without certificate; operator action | Deterministic acceptance horizon with durable retirement |
| Crash before publication | Exact automatic replay; passive lock; operator recovery | Replay exact bytes automatically while valid |

**User's choice:** Selected the recommended fail-closed, exact-replay lifecycle throughout.
**Notes:** The journal is both the non-equivocation lock and the exact publication source; reconstruction or re-signing is prohibited.

---

## Candidate-selection window

| Decision | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Window start | First valid local observation; canonical time boundary; local participation | First valid proposal observed for an empty slot |
| Duration behavior | Fixed; quiet-period extension; existing round | Fixed network-configured duration, never extended |
| Late better proposal | Selection already closed; replace until persistence; reject entirely | Track, but do not alter the frozen local vote |
| Comparator tie | Canonical proposal ID; first arrival; local preference | Lexicographically smallest deterministic proposal ID |

**User's choice:** Selected deterministic bounded selection with no deadline extension or post-deadline vote replacement.
**Notes:** The existing proposal-ordering rule remains primary.

---

## Finalization and replay

| Decision | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Winner authority | Slot certificate; separate finalized record; transitioned vote record | Canonical slot certificate alone |
| Delivery convergence | Shared finalization operation; CRDT-only trigger; separate paths | One normalized `FinalizeSlot(certificate)` operation |
| Application failure | Preserve finality and retry; roll back finality; manual recovery | Preserve finality and retry exact winner |
| Cleanup point | After application; immediately after certificate write; retain forever | After successful application and completed marker |

**User's choice:** Confirmed all recommended finalization and recovery semantics.
**Notes:** The durable processing marker provides idempotency but is not an alternative source of finality.

---

## Conflicting certificates

| Decision | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Safety response | Fail closed for slot; log and continue; stop whole node | Preserve winner, reject conflict, halt affected-slot participation |
| Evidence | Structured digests; logs only; duplicate full bytes | Slot, proposals, digests, source, timestamps |
| Repeated delivery | Deduplicate pair; create every time; ignore repeats | Update one canonical conflict-pair record |
| Propagation | Local evidence; conflict gossip; normal rebroadcast | No normal gossip; critical local log, metric, and record |

**User's choice:** Confirmed all recommended fail-closed conflict semantics.
**Notes:** A conflict can never overwrite or apply alongside the original authoritative winner.

## the agent's Discretion

- Concrete record encodings, persistence keys, atomic batching, synchronization, timer implementation, and state representation remain implementation choices constrained by CONTEXT.md.

## Deferred Ideas

- The `bridge-race-not-all-11-mint-within-window.md` todo remains deferred to Phase 12 race verification.
