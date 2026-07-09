# Phase 7: Deferred Validation and Pending Proposal Lifecycle - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-06-16
**Phase:** 7-Deferred Validation and Pending Proposal Lifecycle
**Areas discussed:** Pending result contract, Retry policy, Capacity policy, Expiry behavior

---

## Pending Result Contract

| Option | Description | Selected |
|--------|-------------|----------|
| Structured result | Return `Pending` with dependency keys and optional retry metadata. | yes |
| Dependency keys only | Empty keys imply scheduled retry using defaults. | |
| Separate outcomes | Distinguish `PendingDependency` from `PendingTransient`. | |

**User's choice:** Structured result.
**Notes:** Pending remains local. The user asked what dependency keys mean; clarified that they are
local wake-up indexes such as "waiting for `tx1` certificate", not network messages.

| Option | Description | Selected |
|--------|-------------|----------|
| On any dependency arrival | Revalidate incrementally and return remaining dependencies if still pending. | yes |
| After all dependencies arrive | Reduce validation attempts but requires tracking full dependency completion. | |
| Hybrid | Retry immediately for critical dependencies, wait for all others. | |

**User's choice:** Retry on any dependency arrival.
**Notes:** This keeps the pending mechanism simple and lets validation remain the authority on which
dependencies are still missing.

| Option | Description | Selected |
|--------|-------------|----------|
| Typed keys | Use `{type, id}` such as `Certificate(tx_hash)`. | yes |
| Namespaced strings | Use strings such as `cert:<hash>`. | |
| Raw strings | Each handler owns key interpretation. | |

**User's choice:** Typed keys.
**Notes:** The first required dependency type is `Certificate(tx_hash)`.

---

## Retry Policy

| Option | Description | Selected |
|--------|-------------|----------|
| Conservative backoff | Retry after 1s, 2s, 5s, then every 10s until TTL. | yes |
| Round-based | Retry every consensus round or every N rounds. | |
| Sparse | Retry only a few times, then wait for proposer re-submission. | |

**User's choice:** Conservative backoff.
**Notes:** Applies to transient failures without an explicit dependency event.

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, per proposal | Dependency arrivals wake immediately, but each proposal has a small minimum retry interval. | yes |
| No | Dependency arrivals always retry immediately. | |
| Only under load | Retry immediately unless pending queue is above a threshold. | |

**User's choice:** Per-proposal rate limit.
**Notes:** Prevents retry storms while preserving fast recovery when a missing certificate arrives.

---

## Capacity Policy

| Option | Description | Selected |
|--------|-------------|----------|
| Fail-closed admission | Do not accept new pending proposals; existing pending entries keep their TTL. | yes |
| Evict oldest | Drop the oldest pending entries to admit newer proposals. | |
| Evict largest | Drop large entries first to preserve more proposals. | |

**User's choice:** Fail-closed admission.
**Notes:** Admission failure is a local resource decision, not a network rejection vote.

| Option | Description | Selected |
|--------|-------------|----------|
| Global + per proposer | Enforce total limits and a smaller per-proposer cap. | yes |
| Global only | Simpler, but one proposer can fill the queue. | |
| Per proposer only | Protects fairness but not total memory. | |

**User's choice:** Global plus per-proposer limits.
**Notes:** Protects both total memory and proposer fairness.

| Option | Description | Selected |
|--------|-------------|----------|
| Small defaults | Global 1,024 pending proposals, per proposer 64, total retained bytes 64 MB. | yes |
| Larger defaults | Global 10,000, per proposer 512, total retained bytes 256 MB. | |
| Agent discretion | Planner chooses values based on code constraints. | |

**User's choice:** Small conservative defaults.
**Notes:** Tune later with production data.

---

## Expiry Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| UNCONFIRMED | No final consensus result; allows clean re-submission semantics. | yes |
| EXPIRED | More explicit that local pending TTL elapsed. | |
| FAILED | Current behavior, but conflates inconclusive with invalid. | |

**User's choice:** `UNCONFIRMED`.
**Notes:** `FAILED` is reserved for locally proven invalid transactions.

| Option | Description | Selected |
|--------|-------------|----------|
| No automatic re-submit | Surface state; caller or queue policy decides when to resubmit. | yes |
| Auto re-submit once | Useful for transient partitions, but may create repeated proposal churn. | |
| Auto re-submit with backoff | More robust, but expands this phase's scope. | |

**User's choice:** No automatic resubmission.
**Notes:** Automatic resubmission is deferred.

| Option | Description | Selected |
|--------|-------------|----------|
| Remove temp record | Remote inconclusive proposals leave no transaction state after cleanup. | yes |
| Mark UNCONFIRMED | Keep a visible record for observability. | |
| Mark FAILED | Current-ish behavior, but conflates inconclusive with invalid. | |

**User's choice:** Remove temporary remote embedded transaction records.
**Notes:** `UNCONFIRMED` applies to local outgoing transactions; remote temporary entries are cleaned.

---

## the agent's Discretion

- Exact C++ type names and storage layout for structured pending results and typed dependency keys.
- Exact minimum retry interval for dependency-triggered retries.
- Exact compile-time/config injection shape for TTL and limits, provided tests can inject short values.
- Exact log and metrics names.

## Deferred Ideas

- Signed Reject votes, rejection certificates, negative quorum, and validator reputation adjudication.
- Automatic resubmission policy for `UNCONFIRMED` outgoing transactions.

