# Phase 11: Slot-Owned Bridge Burn Reservations - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-28
**Phase:** 11-slot-owned-bridge-burn-reservations
**Areas discussed:** Reservation durability, Competing proposal ownership, Finality and failed consumption, Slot abandonment

---

## Scope Triage

| Option | Description | Selected |
|--------|-------------|----------|
| Neither pending todo | Keep the race proof in Phase 12 and mock-RPC work deferred | ✓ |
| Race-window todo | Fold the multi-node mint-window failure into Phase 11 | |
| Both todos | Also include bridge mock-RPC/startup work | |

**User's choice:** Phase 11 is slot-owned bridge burn reservations; no mock-RPC.
**Notes:** The full 11-node race remains Phase 12. Both matched todos were reviewed and not folded.

---

## Reservation Durability

| Decision | Alternatives considered | Selected behavior |
|----------|-------------------------|-------------------|
| Restart lifetime | Direct persistence; reconstruct from consensus; runtime-only | Persist and restore directly |
| Distribution | Node-local; CRDT replicated; hybrid | Durable node-local only |
| Reconciliation | Automatic; indefinite quarantine; release whenever no active vote | Automatic from certificate/vote state; missing candidates are normal |
| Persistence failure | Reject/retry admission; memory-only admission; normal voting | Reject or retry without admission/voting |

**User's choice:** Direct durable local reservation with automatic startup reconciliation and fail-closed malformed-state handling.
**Notes:** The user pointed out that votes/candidates may be ephemeral. Clarification established that missing candidate state is expected, while Phase 10 retains durable votes only through their usable horizon.

---

## Competing Proposal Ownership

| Decision | Alternatives considered | Selected behavior |
|----------|-------------------------|-------------------|
| Later valid contender | Join slot; transfer proposal ownership; reject | Join the existing slot reservation |
| Losing cleanup | Never release; release last claim; release/reacquire | Never modify reservation directly |
| Bound identity | Slot plus burn outpoint; slot only; slot plus best proposal | Slot plus exact burn outpoint |
| Local scope | Node-wide; per account; per manager | Node-wide across all accounts |

**User's choice:** The canonical slot alone owns one node-wide reservation shared by every valid contender.
**Notes:** Proposer, account, nonce, transaction hash, and current-best status do not own the burn.

---

## Finality and Failed Consumption

| Decision | Alternatives considered | Selected behavior |
|----------|-------------------------|-------------------|
| Finality transition | FinalizedPendingApplication; immediate separate consume; stay reserved | Durable FinalizedPendingApplication |
| Transient application failure | Retry exact winner; operator-only; choose another candidate | Retry exact winner across restart |
| Durable contradiction | Safety error; retry forever; automatic repair | Durable safety-error state |
| Reservation-write failure | Wait; apply then reconstruct; trust memory | Wait before application or cleanup |

**User's choice:** Certificate finality irrevocably protects the burn; physical consumption stays atomic with exact winning mint effects.
**Notes:** A certified burn never returns to ready. Irreconcilable local state preserves consensus authority but blocks reminting and emits critical diagnostics.

---

## Slot Abandonment

| Decision | Alternatives considered | Selected behavior |
|----------|-------------------------|-------------------|
| Release trigger | Deterministic safe horizon; generic TTL; operator-only | Deterministic safe-release horizon |
| Later proposal | Fresh reservation; permanent rejection; operator approval | Fresh reservation generation allowed |
| Release race | Atomic recheck; release then reacquire; cleanup priority | Atomic conditional release |
| Released history | Compact history; delete; full history | Delete the record completely |

**User's choice:** Automatically release only after every protection horizon expires and no certificate exists; delete the safe-abandoned record.
**Notes:** The user raised the risk of a local reservation being held forever. The resolution uses bounded candidate/vote horizons and automatic reconciliation. Finalized or corrupt states remain locked by design. A later reservation gets a fresh unique identity so stale cleanup cannot affect it.

---

## the agent's Discretion

- Durable schema, key prefix, encoding, and internal component boundaries.
- Atomic conditional-transition mechanism and unique generation-token representation.
- Recovery scheduling, backoff, typed errors, logs, and metrics.

## Deferred Ideas

- Complete 11-node single-burn race proof — Phase 12.
- Bridge startup wiring and mock-RPC infrastructure — outside Phase 11.
