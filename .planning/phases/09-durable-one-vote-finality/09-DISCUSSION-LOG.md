# Phase 9: Durable One-Vote Finality - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-20
**Phase:** 9-Durable One-Vote Finality
**Areas discussed:** Contention window, Winner rule, Lock lifetime & recovery, Durable record contract

---

## Contention window

| Decision | Selected | Notes |
|----------|----------|-------|
| Window anchor | First locally validated proposal | Opens promptly without external-chain timing dependency. |
| Duration | Fixed two seconds | A bounded shared policy; not a local configuration value. |
| Eligibility | Fully validated by deadline | Stalled work cannot prolong or alter the attempt. |
| Empty window | No vote lock | A proposal can start fresh only once fully valid; invalid proposals are discarded. |

**Clarification:** A single valid proposal wins at the deadline. The empty-window case means every candidate was invalid or still unvalidated at the cutoff.

---

## Winner rule

| Decision | Selected | Notes |
|----------|----------|-------|
| Ordering | Lowest transaction hash, then proposal ID | Existing generic deterministic ordering remains authoritative. |
| Late higher-ranked candidate | Never replaces the winner | Deadline freezes the eligible set. |
| Votes for losers | Ignore for finality | They cannot accumulate quorum or lead to a later certificate. |
| Scope | Generic canonical-slot arbitration | No bridge-Mint special case. |

---

## Lock lifetime & recovery

| Decision | Selected | Notes |
|----------|----------|-------|
| Restart before deadline | Automatically re-announce exact stored vote | No re-signing or substitute vote. |
| Send failure | Bounded-backoff exact-vote retries | Preserves safety while restoring liveness. |
| Expiry without certificate | Retain lock; stop re-announcement | Expiry never enables a new vote. |
| Certificate that clears lock | Any durably accepted same-slot certificate | It may contain a different winning proposal. |

---

## Durable record contract

| Decision | Selected | Notes |
|----------|----------|-------|
| Backend and scope | Generic RocksDB consensus-vote record | Per canonical slot; no bridge-specific namespace. |
| Stored material | Slot, full proposal, exact signed vote, absolute deadline | Enables exact restart recovery. |
| Write failure | No broadcast | No usable in-memory vote state remains. |
| Existing same-slot entry | Exact vote is idempotent; different vote is rejected | Never overwrite a prior vote. |
| Deletion ordering | After durable same-slot certificate acceptance | Receipt/parse alone is insufficient. |

## the agent's Discretion

- Exact RocksDB prefix, encoding, retry parameters, and C++ timer/test seams within the locked contract.

## Deferred Ideas

None.
