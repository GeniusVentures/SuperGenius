# Phase 11: Convergent Certificate Consumption & Mint Recovery - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-21
**Phase:** 11-convergent-certificate-consumption-mint-recovery
**Areas discussed:** Certificate acceptance boundary, Mint work state machine, Certificate-before-transaction recovery, Application failure policy

---

## Certificate acceptance boundary

| Option | Description | Selected |
|--------|-------------|----------|
| Durable acceptance journal | A separate per-slot local acceptance record | |
| Existing transaction/UTXO state | CRDT certificate remains authority; existing durable transaction and UTXO state is recovery truth | ✓ |
| Separate publisher and receiver bookkeeping | Distinct local paths | |

**User's choice:** No redundant certificate-acceptance record.
**Notes:** The user clarified that certificate arrival already drives the existing transaction lifecycle; adding a second journal would not add safety.

---

## Mint work state machine

| Option | Description | Selected |
|--------|-------------|----------|
| Retry completion, keep UTXOs | Preserve idempotent effects and recover a missing completion marker | ✓ |
| Log and treat complete | Accept missing durable completion evidence | |
| Roll back UTXOs | Undo effects after marker failure | |

**User's choice:** Apply idempotently first, persist the marker afterward, and retry marker completion on failure.
**Notes:** The executed marker is a completion barrier, never a pre-application permission.

---

## Certificate-before-transaction recovery

| Option | Description | Selected |
|--------|-------------|----------|
| CRDT first, verified embedded fallback | Prefer replicated transaction data; use only exact certificate-embedded transaction if replication lags | ✓ |
| CRDT only | Wait for ordinary replication | |
| Embedded transaction first | Prefer certificate payload before CRDT | |

**User's choice:** CRDT first with exact verified embedded fallback.
**Notes:** Both certificate-first and transaction-first routes must converge on the same transaction lifecycle.

---

## Application failure policy

| Option | Description | Selected |
|--------|-------------|----------|
| Keep retryable automatically | Recover transient local application failures | ✓ |
| Require operator action | Stop automatic recovery | |
| Fixed retry limit | Stop after bounded attempts | |

**User's choice:** Keep valid certified Mint work retryable automatically.
**Notes:** A local transient failure must not terminally fail a valid certified Mint.

---

## the agent's Discretion

- Select the smallest recovery trigger and test seams consistent with the existing transaction lifecycle and idempotent UTXO behavior.

## Deferred Ideas

- Bridge startup wiring and mock RPC endpoints — unrelated to Phase 11 certificate consumption and Mint recovery.
