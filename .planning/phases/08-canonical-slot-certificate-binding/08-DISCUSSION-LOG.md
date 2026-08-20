# Phase 8: Canonical Slot & Certificate Binding - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-20
**Phase:** 8-Canonical Slot & Certificate Binding
**Areas discussed:** Canonical mint slot, certificate-to-proposal binding, phase boundaries

---

## Canonical mint slot

| Option | Description | Selected |
|--------|-------------|----------|
| Keep the existing verified-fact slot | Retain chain, token, source transaction, amount, and destination in `MintTransactionV2::GetSlotID()`. | ✓ |
| Redefine the slot identity | Exclude amount or destination and introduce a different bridge identity. | |

**User's choice:** Keep the existing Mint slot calculation.
**Notes:** Amount and destination are immutable, independently verified burn facts. Proposer account and proposal nonce must not affect the slot.

---

## Certificate-to-proposal binding

| Option | Description | Selected |
|--------|-------------|----------|
| Fail closed on any binding mismatch | Reject slot/key/payload/proposal disagreement without finality effects or state unlock. | ✓ |
| Treat a matching slot as interchangeable | Permit a different proposal certificate when it shares a slot. | |

**User's choice:** Fail closed on mismatch.
**Notes:** A canonical slot identifies the finality domain but never replaces the certificate's exact winning-proposal binding.

---

## Phase boundaries

| Option | Description | Selected |
|--------|-------------|----------|
| Sequence lifecycle work into later phases | Keep vote timing/locking in Phase 9, publication in Phase 10, and convergence/mint recovery in Phase 11. | ✓ |
| Solve all lifecycle behavior in Phase 8 | Expand identity and binding into later protocol work. | |

**User's choice:** Keep the planned phase boundaries.
**Notes:** The startup-wiring/mock-RPC todo was reviewed and remains outside this phase.

---

## the agent's Discretion

- Select the smallest existing consensus validation seams and tests that implement the locked Phase 8 invariants.

## Deferred Ideas

- Bridge startup wiring and mock RPC endpoints remain in `.planning/todos/pending/bridge-startup-wiring-mock-rpc.md`.
