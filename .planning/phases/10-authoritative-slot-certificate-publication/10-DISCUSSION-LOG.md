# Phase 10: Authoritative Slot Certificate Publication - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-21
**Phase:** 10-authoritative-slot-certificate-publication
**Areas discussed:** Publisher selection, Persistence and advertisement, Failover condition, Subject-hash consumers

---

## Publisher selection

| Option | Description | Selected |
|--------|-------------|----------|
| Existing rotation | Preserve proposal-derived round rotation for the certified proposal. | ✓ |
| Slot-derived rotation | Seed rotation from the canonical slot. | |
| Static ordering | Use a fixed validator ordering. | |

**User's choice:** Keep current round rotation; it does not need to change.
**Notes:** Only the selected aggregator may persist. Non-selected validators already wait; PubSub receivers must not become writers. The first valid slot record is final and differing contents are conflicts.

---

## Persistence and advertisement

| Option | Description | Selected |
|--------|-------------|----------|
| Persist and read back | Verify a durable readback before notification. | |
| Persist only | Treat successful durable persistence as sufficient before notification. | ✓ |
| Publication journal | Add an unpublished-record journal. | |

**User's choice:** Persist first, then send the full certificate on best-effort PubSub.
**Notes:** A PubSub failure is logged but not retried. CRDT replication is the finality and cleanup fallback. The publisher has no special local completion path.

---

## Failover condition

| Option | Description | Selected |
|--------|-------------|----------|
| Existing next round | The next existing proposal-derived consensus round selects the successor. | ✓ |
| Local quorum delay | Take over after a locally observed timer. | |
| Embedded timeout | Use a new deadline in protocol data. | |

**User's choice:** Preserve existing rotation as the failover rule.
**Notes:** A successor needs the same validated quorum evidence, fails closed on indeterminate occupancy, and later normal rounds continue recovery without a special cap.

---

## Subject-hash consumers

| Option | Description | Selected |
|--------|-------------|----------|
| Subject/proposal-derived slot | Require proposal data for slot derivation. | |
| Hash-to-slot locator | Add a non-authoritative hash index. | |
| Legacy hash authority | Keep subject-hash certificate authority. | |
| Transaction-derived slot | Derive `GetSlotID()` from the transaction itself. | ✓ |

**User's choice:** Transaction-backed lookups derive the slot directly from the transaction, including one retrieved from CRDT by hash.
**Notes:** Missing transactions leave finality unavailable for normal retry. Registry-update slot semantics are explicitly out of scope; it retains existing generic `GetSlotKey` integration.

---

## the agent's Discretion

- Use the smallest safe implementation that preserves the locked publication, failover, and lookup decisions.

## Deferred Ideas

- Registry-batch slot-identity redesign.
- `bridge-startup-wiring-mock-rpc.md`, which weakly matched only on the word “bridge” and is unrelated to this phase.
