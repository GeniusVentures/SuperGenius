# Phase 3: Network Hardening and Operational Readiness - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-28
**Phase:** 03-network-hardening-and-operational-readiness
**Areas discussed:** PubSub message size enforcement, Timestamp clock skew tolerance, Tracking entry cleanup strategy, Operational metrics and observability

---

## PubSub Message Size Enforcement

| Option | Description | Selected |
|--------|-------------|----------|
| **Pre-publish at SendTransactionItem** | Check size after SerializeByteVector(), before consensus pipeline | ✓ |
| Post-receive only (existing) | Already handled by Phase 1 handler (64KB cap) | Complementary |

**User's choice:** Pre-publish enforcement prevents oversized messages from ever reaching PubSub. Same 64KB threshold as the existing handler check for defense-in-depth.

**Notes:** Reuse MAX_EMBEDDED_TX_BYTES constant. Error behavior: outcome::failure with descriptive message.

---

## Timestamp Clock Skew Tolerance

| Option | Description | Selected |
|--------|-------------|----------|
| **Configurable window** | Replace hardcoded ±5min with configurable value | ✓ |
| Keep hardcoded | No change | |

**User's choice:** Configurable via existing config/env system. Default ±5 minutes preserves current behavior for existing deployments.

**Notes:** Config key design at agent's discretion — follow existing codebase patterns for timestamp/tolerance configuration.

---

## Tracking Entry Cleanup Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| **Callback registration** | Register handler on ConsensusManager; fires from ClearProposalSlot timeout callers | ✓ |
| Periodic sweep | Timer-based scan of tx_processed_m for stale VERIFYING entries | |
| Direct notification | ConsensusManager directly notifies TransactionManager | |

**User's choice:** Callback registration — follows existing RegisterSubjectHandler/RegisterCertificateHandler patterns. Timeout callers (lines 1392, 1476) trigger the callback; certificate caller (line 1912) does not (entries already CONFIRMED via Phase 2).

**Notes:** Handler calls ChangeTransactionState(tx, FAILED) for matching VERIFYING entries. Use GetTransactionByHash to find the entry.

---

## Operational Metrics

| Option | Description | Selected |
|--------|-------------|----------|
| **Existing TransactionManagerLogger** | Use spdlog-based structured logging with atomic counters | ✓ |
| External metrics system | Prometheus, StatsD, etc. | |

**User's choice:** Use existing logging infrastructure. Log lifecycle events (certificate fallback, validation results, tracking entry transitions) at info level. Simple atomic counters for vote counts and validation breakdown.

**Notes:** Counters logged periodically or on shutdown. No external metrics system integration.

---

## the agent's Discretion

- SPECIFIC config key name and location for timestamp tolerance
- Callback registration API design
- Metrics counter implementation details
- Exact log message format

## Deferred Ideas

None.
