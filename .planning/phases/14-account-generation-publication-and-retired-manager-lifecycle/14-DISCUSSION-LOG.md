# Phase 14: Account-generation publication and retired-manager lifecycle safety - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-17
**Phase:** 14-account-generation-publication-and-retired-manager-lifecycle-safety
**Areas discussed:** Switch completion contract, In-flight operations, Replacement failure, Retired-manager behavior

---

## Switch completion contract

### Meaning of `SelectAccount()` success

| Option | Description | Selected |
|--------|-------------|----------|
| New generation is fully ready | Success guarantees coherent account and manager publication. | |
| Switch was accepted | Return after accepting the asynchronous transition. | ✓ |
| Bounded wait | Wait for readiness up to a timeout, then report still switching. | |

### Completion observation

| Option | Description | Selected |
|--------|-------------|----------|
| Existing node-state callback/event | Emit generation-bound ready or failed state. | ✓ |
| Polling API | Query switch status until terminal. | |
| Both | Event delivery plus polling snapshot. | |

### Calls during switching

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit `SWITCH_IN_PROGRESS` | Distinguish temporary switching from other unavailable states. | ✓ |
| Existing `NOT_READY` | Reuse a generic readiness error. | |
| Queue until ready | Carry caller intent into the replacement generation. | |

### Overlapping switch requests

| Option | Description | Selected |
|--------|-------------|----------|
| Reject with `SWITCH_IN_PROGRESS` | Complete or fail one transition before another starts. | ✓ |
| Latest request wins | Cancel the active transition. | |
| Queue one follow-up | Start the latest request after the current transition. | |

---

## In-flight operations

### Operations accepted before switching

| Option | Description | Selected |
|--------|-------------|----------|
| Finish accepted operations; reject new ones | Preserve terminal results for work that crossed admission. | ✓ |
| Cancel unfinished operations | Roll back every outstanding operation. | |
| Reject switch until idle | Do not start switching while operations exist. | |

### Admission-close timing

| Option | Description | Selected |
|--------|-------------|----------|
| Before acceptance returns | No old-generation admission remains after caller observes acceptance. | ✓ |
| During asynchronous teardown | Leave a small post-acceptance race. | |
| Grace period | Permit a final burst of old-account work. | |

### Replacement initialization timing

| Option | Description | Selected |
|--------|-------------|----------|
| Initialize in parallel; delay publication | Reduce latency while old work drains. | |
| Drain before initialization | Prevent old and replacement mutable work from overlapping. | ✓ |
| Publish while old drains | Maximize availability with concurrent generations. | |

### Drain timeout

| Option | Description | Selected |
|--------|-------------|----------|
| Fail switch and remain unavailable | Do not fabricate cancellation while old durable work may complete. | ✓ |
| Force cancellation and continue | Requires universal proven rollback. | |
| Wait indefinitely | Preserve completion at the cost of a hung switch. | |

---

## Replacement failure

### Failed replacement state

| Option | Description | Selected |
|--------|-------------|----------|
| Remain unavailable and emit failure | Never republish retired or partial generations. | ✓ |
| Restore previous account | Rebuild the retired generation automatically. | |
| Retry automatically | Continue attempting the requested target. | |

### Recovery trigger

| Option | Description | Selected |
|--------|-------------|----------|
| New explicit `SelectAccount()` | Operator chooses same or different target. | ✓ |
| Dedicated retry API | Retry only the failed target. | |
| Automatic bounded retries | Retry transient failures before manual recovery. | |

### Address visibility after failure

| Option | Description | Selected |
|--------|-------------|----------|
| No active account | Return explicit unavailable error. | ✓ |
| Failed target address | Preserve requested identity with failed status. | |
| Prior address | Display the retired identity. | |

### Failure-event timing

| Option | Description | Selected |
|--------|-------------|----------|
| After complete cleanup | Failure means the generation is quiescent and another switch is safe. | ✓ |
| Before background cleanup | Notify immediately while ownership is still unwinding. | |
| Retain partial resources | Reuse them for same-target retry. | |

---

## Retired-manager behavior

### Mutation result

| Option | Description | Selected |
|--------|-------------|----------|
| `MANAGER_RETIRED` | Permanent lifecycle-specific error. | ✓ |
| `NOT_READY` | Reuse temporary readiness semantics. | |
| Generic failure | Hide lifecycle details. | |

### Read-only diagnostics

| Option | Description | Selected |
|--------|-------------|----------|
| Immutable final diagnostics only | Generation, retirement, and accepted-operation terminal results. | ✓ |
| Reject every method | No diagnostics after retirement. | |
| Keep all reads | Preserve current read surfaces despite teardown. | |

### Accepted-operation completion events

| Option | Description | Selected |
|--------|-------------|----------|
| Deliver with retired generation | Preserve observable completion without cross-generation attribution. | ✓ |
| Suppress after switch | Hide stale generation events. | |
| Forward through replacement | Attribute old work to the new generation. | |

### Node processing status

| Option | Description | Selected |
|--------|-------------|----------|
| Lifecycle-specific status | Switching, unavailable, then replacement-only status. | ✓ |
| Last old status | Display retired status until replacement readiness. | |
| Generic unknown | Hide lifecycle distinctions. | |

---

## the agent's Discretion

- Internal admission/lease and lock mechanics.
- Finite timeout value and configuration location.
- Concrete typed error/event representation.
- Deterministic concurrency-test hook implementation.

## Deferred Ideas

- Bridge-owner lifecycle safety.
- Trusted-peer refresh coalescing and timer lifetime.
- Repository-wide CRDT/GlobalDB capability hardening.
