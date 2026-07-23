# Phase 9 Plan Outline

**Phase:** Canonical Slot and Certificate Storage  
**Plans:** 4 plans in 3 waves

| Plan ID | Objective | Wave | Depends On | Requirements |
|---------|-----------|------|------------|--------------|
| 09-01 | Establish strict, hashed canonical slot derivation for normal and bridge-mint subjects | 1 | none | SLOT-01, SLOT-02, SLOT-03, SLOT-04 |
| 09-02 | Carry immutable receipt-local burn identity through every mint path and validate the exact event | 2 | 09-01 | SLOT-03, SLOT-04 |
| 09-03 | Persist and replicate a write-once certificate/index pair and reject legacy certificate state | 2 | 09-01 | CERT-01, CERT-02, CERT-03, COMP-02 |
| 09-04 | Expose verified slot/hash lookup semantics and prove existing hash consumers remain compatible | 3 | 09-03 | SLOT-01, CERT-04, COMP-01 |

## Dependency Rationale

- `09-01` defines the slot ID consumed by all later persistence and lookup code.
- `09-02` and `09-03` can execute in parallel after that contract exists: one owns EVM/account identity propagation, the other owns CRDT/certificate persistence.
- `09-04` depends on the v2 store and index introduced by `09-03`.

## Phase Boundary

These plans establish canonical identity, source-event binding, v2 storage, and lookup. They do not add durable validator vote locks, finalized-slot transition ordering, slot-owned bridge reservation lifecycle, or the complete 11-node/restart matrix; those remain Phases 10–12.
