---
gsd_state_version: '1.0'
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: planning
last_updated: '2026-08-20'
progress:
  total_phases: 5
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 4 — Canonical Slot & Certificate Binding

## Current Position

Phase: 4 of 8 overall (1 of 5 in v3.0) — Canonical Slot & Certificate Binding
Plan: Not yet planned
Status: Ready to plan
Last activity: 2026-08-20 — Created v3.0 canonical-finality roadmap and requirement traceability.

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**
- Total plans completed: 5 (prior milestone)
- v3.0 plans completed: 0
- Average duration: Not yet measured for v3.0

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 4. Canonical Slot & Certificate Binding | 0 | - | - |
| 5. Durable One-Vote Finality | 0 | - | - |
| 6. Authoritative Slot Certificate Publication | 0 | - | - |
| 7. Convergent Certificate Consumption & Mint Recovery | 0 | - | - |
| 8. Multi-Node Finality Fault Proof | 0 | - | - |

**Recent Trend:** Not yet measured for v3.0.

## Accumulated Context

### Decisions

- `MintTransactionV2::GetSlotID()` remains the canonical slot and must retain verified chain, token, source transaction, amount, and destination; proposer and nonce cannot alter it.
- Certificates are authoritative only at `/cert/<canonical-slot-id>` and remain bound to the exact winning proposal; no bridge-specific finality record or legacy certificate authority is permitted.
- A direct RocksDB active-vote record is written before broadcast, permits only exact-vote recovery, and clears only on matching durable certificate finality without incompatible overlap.
- Certificate publisher authority and failover are deterministic and protocol-visible; persist and verify before PubSub; recipients consume/resolve only and never write the CRDT certificate key.

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 4 planning must lock implementation-level validation and exact winner evidence without changing the required `GetSlotID()` verified-fact semantics.
- Production-path multi-node failure and restart coverage is mandatory for milestone completion.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v1.0 | NodeType downstream propagation and Archive/Full behavior split | Deferred | v1.0 close |

## Session Continuity

Last session: 2026-08-20
Stopped at: v3.0 roadmap created; Phase 4 is ready for detailed planning after approval.
Resume file: None
