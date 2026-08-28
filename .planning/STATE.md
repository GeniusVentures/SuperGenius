---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: Blocked — Plan 12-08 post-review readiness evidence is mixed
last_updated: "2026-08-28T18:04:50.456Z"
last_activity: 2026-08-27 -- publisher readiness observer output mismatch retained for separate attribution
progress:
  total_phases: 5
  completed_phases: 5
  total_plans: 20
  completed_plans: 23
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 12 — multi-node-finality-fault-proof

## Current Position

Phase: 12 (multi-node-finality-fault-proof) — EXECUTING
Plan: 8 of 8
Status: Blocked — Plan 12-08 post-review readiness evidence is mixed
Last activity: 2026-08-27 -- publisher readiness observer output mismatch retained for separate attribution

Progress: [██████████] 100%

## Performance Metrics

**Velocity:**

- Total plans completed: 25 (5 prior milestone + 3 v3.0)
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
| 11 | 5 | - | - |

**Recent Trend:** Not yet measured for v3.0.
| Phase 08-canonical-slot-certificate-binding P01 | 48m | 2 tasks | 4 files |
| Phase 10 P01 | 10m | 2 tasks | 9 files |
| Phase 11 P01 | 6 min | 2 tasks | 2 files |
| Phase 11 P02 | 10 min | 2 tasks | 3 files |
| Phase 11 P03 | 8 min | 2 tasks | 5 files |
| Phase 11 P04 | 17m | 2 tasks | 2 files |
| Phase 12 P01 | 64m | 2 tasks | 9 files |
| Phase 12 P02 | 17m | 2 tasks | 1 files |
| Phase 12 P03 | 68m | 2 tasks | 5 files |
| Phase 12 P08 | 11 min | 2 tasks | 8 files |

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
- [Phase 11]: Reuse the existing lower serialized SHA-256 certificate ordering in SubmitCertificate and FilterCertificate; different verified Mint hashes only emit a consensus-fault diagnostic.
- [Phase 12]: Use StopPeer/recreate plus AddPeers as the Phase 12 real-transport recovery path.
- [Phase 12]: Use the existing test-chain IInputValidator and a fresh Mint nonce 0 for normal production-route audit ingress.
- [Phase 12]: Re-advertise unchanged signed proposals through public SubmitProposal after AddPeers because offline GossipPubSub broadcasts are not replayed. — Preserves isolated initial submission and real production ingress.
- [Phase 12]: Use durable active-vote identity rather than retry publication totals to prove late contender rejection. — Vote counters include normal retry broadcasts and cannot establish unique ownership.
- [Phase 12]: Use stop-aware post-durability test barriers so shutdown leaves incomplete work to existing recovery.
- [Phase 12]: CRDT immutable state is authoritative after publisher loss; PubSub is best-effort cleanup with no successor retry.
- [Phase 12]: Isolate the pre-broadcast vote owner until its same-root active-vote recovery is asserted. — Connected peers may correctly finalize the slot and release that direct local lock before the intended restart boundary.
- [Phase 12]: Publisher readiness repair requires two matching fresh failures with direct fixture lifecycle proof. — Three isolated publisher-loss runs passed readiness, so Plan 12-08 authorizes no repair.
- [Phase 12]: Complete passive readiness records include all directed host links, all peer lifecycle fields, and named per-peer mesh counts. — A mixed post-review refresh must be attributed before the evidence gate can be considered complete.

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 8 planning must lock implementation-level validation and exact winner evidence without changing the required `GetSlotID()` verified-fact semantics.
- Production-path multi-node failure and restart coverage is mandatory for milestone completion.
- Phase 12 blocked: Plan 12-06 found a fresh-process Mint-boundary recovery failure after valid topology, plus late/publisher diagnostic outcomes. See 12-07-HANDOFF.md; do not apply fixture or production repair without a separately scoped plan.
- Phase 12 Plan 07: three fresh real-socket Mint-boundary restart runs passed; D-14 reproduction gate closed (repair_authorization=). Phase remains blocked pending separately scoped evidence for the existing finality gaps.
- Phase 12 remains blocked: Plan 12-08 observed three successful publisher readiness gates, so no fixture lifecycle repair is authorized and separate diagnosis is required for the remaining proof gaps.
- Phase 12 Plan 08 post-review refresh: two canonical publisher-loss logs contain the complete passive record, while one ends during teardown with the prior aggregate record. Preserve the mismatch and do not retry, relax the schema, or alter topology/finality from it.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v1.0 | NodeType downstream propagation and Archive/Full behavior split | Deferred | v1.0 close |

## Session Continuity

Last session: 2026-08-28T18:04:50.447Z
Stopped at: Phase 12 observer attribution context gathered
Resume file: .planning/phases/12-multi-node-finality-fault-proof/12-CONTEXT.md
