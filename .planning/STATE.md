---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: planning
last_updated: "2026-08-21T20:39:30.250Z"
last_activity: 2026-08-21
progress:
  total_phases: 5
  completed_phases: 3
  total_plans: 8
  completed_plans: 10
  percent: 60
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 11 — convergent certificate consumption & mint recovery

## Current Position

Phase: 11
Plan: Not started
Status: Ready to plan
Last activity: 2026-08-21

Progress: [█████░░░░░] 50%

## Performance Metrics

**Velocity:**

- Total plans completed: 15 (5 prior milestone + 3 v3.0)
- v3.0 plans completed: 3
- Average duration: Not yet measured for v3.0

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 8. Canonical Slot & Certificate Binding | 1 | 48m | 48m |
| 9. Durable One-Vote Finality | 2 | - | - |
| 10. Authoritative Slot Certificate Publication | 0 | - | - |
| 11. Convergent Certificate Consumption & Mint Recovery | 0 | - | - |
| 12. Multi-Node Finality Fault Proof | 0 | - | - |
| 10 | 7 | - | - |

**Recent Trend:** Not yet measured for v3.0.
| Phase 08-canonical-slot-certificate-binding P01 | 48m | 2 tasks | 4 files |
| Phase 10 P01 | 10m | 2 tasks | 9 files |

## Accumulated Context

### Decisions

- `MintTransactionV2::GetSlotID()` remains the canonical slot and must retain verified chain, token, source transaction, amount, and destination; proposer and nonce cannot alter it.
- Certificates are authoritative only at `/cert/<canonical-slot-id>` and remain bound to the exact winning proposal; no bridge-specific finality record or legacy certificate authority is permitted.
- A direct RocksDB active-vote record is written before broadcast, permits only exact-vote recovery, and clears only on matching durable certificate finality without incompatible overlap.
- Certificate publisher authority and failover are deterministic and protocol-visible; persist and verify before PubSub; recipients consume/resolve only and never write the CRDT certificate key.
- [Phase 08]: Keep the legacy subject-hash CRDT key non-authoritative while validating it at key-aware ingress.
- [Phase 08]: Reject invalid certificate bindings before submit, cleanup, callbacks, registry finalization, or handler dispatch.
- [Phase 08]: Use in-memory secure storage in the pending-lifecycle fixture before creating the signing account.
- [Phase 10]: Immutable authority records converge by the lowest SHA-256 lowercase-hex serialized bytes. — A reserved replicated CRDT priority avoids local first-seen and read-then-write semantics.

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 8 planning must lock implementation-level validation and exact winner evidence without changing the required `GetSlotID()` verified-fact semantics.
- Production-path multi-node failure and restart coverage is mandatory for milestone completion.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v1.0 | NodeType downstream propagation and Archive/Full behavior split | Deferred | v1.0 close |

## Session Continuity

Last session: 2026-08-21T20:39:30.241Z
Stopped at: Phase 11 context gathered
Resume file: .planning/phases/11-convergent-certificate-consumption-mint-recovery/11-CONTEXT.md
