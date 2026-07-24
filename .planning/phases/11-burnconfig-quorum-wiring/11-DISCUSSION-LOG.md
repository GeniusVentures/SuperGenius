# Phase 11: BurnConfig Quorum Wiring - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-24
**Phase:** 11-BurnConfig Quorum Wiring
**Areas discussed:** BurnConfig genesis seeding trigger, Node auto-signing vs out-of-band signing, TransactionManager wiring point, Quorum threshold for BurnConfig changes

---

## BurnConfig genesis seeding trigger

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-seed at startup if absent, by any trusted-peer node | Each trusted-peer node proposes+signs BURN_BASIS_POINTS=100 on its own startup if absent, converging via CRDT once enough sign | ✓ |
| Manual one-time operator action | A human explicitly triggers genesis seeding once via a script/tool | |

**User's choice:** Auto-seed at startup if absent, by any trusted-peer node (Recommended)

---

## Node auto-signing vs out-of-band signing

| Option | Description | Selected |
|--------|-------------|----------|
| Genesis only auto-signs; later changes need explicit operator action | Auto-signing only makes sense for the known, hardcoded genesis default; any change is a policy decision requiring deliberate approval | ✓ |
| Trusted-peer nodes auto-sign ALL BurnConfig proposals matching some rule | More automated but requires defining and trusting a rule now | |

**User's choice:** Genesis only auto-signs; later changes need explicit operator action (Recommended)

---

## TransactionManager wiring point

| Option | Description | Selected |
|--------|-------------|----------|
| At INITIALIZING_TRANSACTIONS | Matches where CRDT dependencies are already available and where TransactionManager itself is constructed | ✓ |
| Something different | | |

**User's choice:** Yes, at INITIALIZING_TRANSACTIONS (Recommended)

---

## Quorum threshold for BurnConfig changes

| Option | Description | Selected |
|--------|-------------|----------|
| Separately configurable threshold | BurnConfig registers its own threshold, independent of TrustedPeerRegistry's | ✓ |
| Same threshold as TrustedPeerRegistry | Reuse TrustedPeerRegistry's configured N-of-M value directly | |

**User's choice:** Separately configurable threshold (Recommended)

---

## Claude's Discretion

- Exact BurnConfigPayload class shape and serialization format (mirrors TrustedPeerListPayload).
- Exact BurnConfig wrapper class name and source-tree location.
- Exact CRDT key name/HierarchicalKey for the BurnConfig value.

## Deferred Ideas

- A CLI/tooling for an operator to propose and sign a BurnConfig change post-genesis — explicitly out of this phase's scope, future phase/milestone item.
