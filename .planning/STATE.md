---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: verifying
last_updated: "2026-08-24T13:31:45.044Z"
last_activity: 2026-08-24
progress:
  total_phases: 5
  completed_phases: 4
  total_plans: 11
  completed_plans: 13
  percent: 80
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 11 — convergent-certificate-consumption-mint-recovery

## Current Position

Phase: 11 (convergent-certificate-consumption-mint-recovery) — EXECUTING
Plan: 3 of 3
Status: Phase complete — ready for verification
Last activity: 2026-08-24

Progress: [██████████] 100%

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
| Phase 11 P01 | 6 min | 2 tasks | 2 files |
| Phase 11 P02 | 10 min | 2 tasks | 3 files |
| Phase 11 P03 | 8 min | 2 tasks | 5 files |

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
- [Phase 11]: Certificate handlers trigger committed recovery after releasing certificate_handlers_mutex_. — Preserves the CRDTWorkJournal as the sole retry boundary and avoids handler-map lock re-entry.
- [Phase 11]: No-handler durable certificates retain stalled work and their active vote lock. — A registered handler must process committed readback successfully before certificate work completes.
- [Phase 11]: Certificate-first processing selects tracked state, exact CRDT evidence, then only a validated embedded fallback.
- [Phase 11]: Every certificate-first candidate must satisfy Phase 10 exact account, nonce, hash, embedded-hash, and slot binding before confirmation.
- [Phase 11]: Use the existing certificate work journal as the sole retry mechanism; no Mint or finality journal was added. — Certificate recovery already provides the durable retry boundary.
- [Phase 11]: For Mint V2, keep transaction tracking VERIFYING until idempotent effects and the existing bridge marker both persist. — Terminal confirmation must not outrun durable local effects.
- [Phase 11]: Expose only friend-scoped test access to the fixture-owned ConsensusManager; production APIs remain unchanged. — The regression needs real ingress access without a production getter.

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

Last session: 2026-08-24T13:31:45.040Z
Stopped at: Completed 11-03-PLAN.md
Resume file: None
