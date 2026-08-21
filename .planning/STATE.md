---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: executing
last_updated: "2026-08-21T16:50:34.806Z"
last_activity: 2026-08-21 -- Phase 10 planning complete
progress:
  total_phases: 5
  completed_phases: 2
  total_plans: 8
  completed_plans: 3
  percent: 38
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 10 — authoritative slot certificate publication

## Current Position

Phase: 10
Plan: Not started
Status: Ready to execute
Last activity: 2026-08-21 -- Phase 10 planning complete

Progress: [████░░░░░░] 40%

## Performance Metrics

**Velocity:**

- Total plans completed: 8 (5 prior milestone + 3 v3.0)
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

**Recent Trend:** Not yet measured for v3.0.
| Phase 08-canonical-slot-certificate-binding P01 | 48m | 2 tasks | 4 files |

## Accumulated Context

### Decisions

- `MintTransactionV2::GetSlotID()` remains the canonical slot and must retain verified chain, token, source transaction, amount, and destination; proposer and nonce cannot alter it.
- Certificates are authoritative only at `/cert/<canonical-slot-id>` and remain bound to the exact winning proposal; no bridge-specific finality record or legacy certificate authority is permitted.
- A direct RocksDB active-vote record is written before broadcast, permits only exact-vote recovery, and clears only on matching durable certificate finality without incompatible overlap.
- Certificate publisher authority and failover are deterministic and protocol-visible; persist and verify before PubSub; recipients consume/resolve only and never write the CRDT certificate key.
- [Phase 08]: Keep the legacy subject-hash CRDT key non-authoritative while validating it at key-aware ingress.
- [Phase 08]: Reject invalid certificate bindings before submit, cleanup, callbacks, registry finalization, or handler dispatch.
- [Phase 08]: Use in-memory secure storage in the pending-lifecycle fixture before creating the signing account.

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

Last session: 2026-08-21T14:32:34.047Z
Stopped at: Phase 10 context gathered
Resume file: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md
