---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: executing
last_updated: "2026-09-02T21:28:32.918Z"
last_activity: 2026-09-02 -- Phase 12 planning complete
progress:
  total_phases: 5
  completed_phases: 4
  total_plans: 29
  completed_plans: 29
  percent: 80
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-20)

**Core value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart.
**Current focus:** Phase 12 — multi-node-finality-fault-proof

## Current Position

Phase: 12 (multi-node-finality-fault-proof) — EXECUTING
Plan: 3 of 14
Status: Ready to execute
Last activity: 2026-09-02 -- Phase 12 planning complete

Progress: [██████████] 100%

## Performance Metrics

**Velocity:**

- Total plans completed: 37 (5 prior milestone + 3 v3.0)
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
| 12 | 12 | - | - |

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
| Phase 12 P12 | 62min | 2 tasks | 3 files |
| Phase 12 P13 | 9m | 3 tasks | 2 files |
| Phase 12 P14 | 90m | 2 tasks | 3 files |

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
- [Phase 12]: Convergent-immutable certificate slot keys are written in fixtures only through PutConvergentImmutable, exactly once per db — a per-direction fresh node db removes any dependence on same-priority CRDT overwrite ordering.
- [Phase 12]: CRDT equal-priority overwrite guard and self-created-write head advancement are deferred follow-ups, not 12-12 changes — production authority is already protected by PutConvergentImmutable's reserved priority and lowest-hash convergence, and CRDT merge changes ripple across every CRDT-dependent suite.
- [Phase 12]: [Phase 12]: CRDTFixture db/keypair paths are run-unique (pid + fixture counter) and reaped at construction before KeyPairFileStorage/GlobalDB::New — leftover databases from killed or crashed runs can no longer poison later runs.
- [Phase 12]: [Phase 12]: ConsensusManager::ValidateCertificate's no-quorum/tally-error reject now emits one warn line (slot key, registry cid, vote count, tally error) — the previously sole silent validation stage; per-vote non-member drop stays debug.
- [Phase ?]: Peer::Stop releases every co-owner of the pubsub libp2p host (db, account) after the io_thread join and BEFORE pubsub->Stop() - the fixture-level asio io_context-outlives-I/O-objects fix for the kqueue teardown SIGSEGV (12-14; 8 crash-free reordered full runs vs 2 crashes in 3 pre-fix control runs).
- [Phase ?]: A Peer::Stop variant force-closing host connections before db release was rejected - it removed one recovery failure signature but introduced a new 5s topology-readiness flake with no net gain; the shipped diff stays exactly the plan-prescribed reorder (12-14).

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 8 planning must lock implementation-level validation and exact winner evidence without changing the required `GetSlotID()` verified-fact semantics.
- Production-path multi-node failure and restart coverage is mandatory for milestone completion.
- Phase 12 blocked: Plan 12-06 found a fresh-process Mint-boundary recovery failure after valid topology, plus late/publisher diagnostic outcomes. See 12-07-HANDOFF.md; do not apply fixture or production repair without a separately scoped plan.
- Phase 12 Plan 07: three fresh real-socket Mint-boundary restart runs passed; D-14 reproduction gate closed (repair_authorization=). Phase remains blocked pending separately scoped evidence for the existing finality gaps.
- Phase 12 remains blocked: Plan 12-08 observed three successful publisher readiness gates, so no fixture lifecycle repair is authorized and separate diagnosis is required for the remaining proof gaps.
- Phase 12 Plan 08 post-review refresh: two canonical publisher-loss logs contain the complete passive record, while one ends during teardown with the prior aggregate record. Preserve the mismatch and do not retry, relax the schema, or alter topology/finality from it.
- Phase 12 Plan 12-12 resolved: multi_node_finality_fault_test three-consecutive-serial evidence was temporarily blocked by leftover artificial CPU load from the 2026-09-01 same-burn diagnosis run (about six orphaned busy-loop shells, load 15-20 on 8 cores); the orphans terminated on their own at about 15:50 local and the gate then closed with three consecutive serial full passes (logs at /tmp/p12_mn_triple_{a,b,c}.log). Residual note: PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch failed once at low load with the usual child-readiness signature, so its pre-existing intermittence stands under the Plan 12-08 discipline; no repair was authorized or applied by 12-12.
- Phase 12 Plan 12-14 evidence gap: multi_node_finality_fault_test currently flakes ~40-60% per full serial run from order-independent signatures (SameBurnContention bridge-marker timing, RestartAtVote block-3 certificate reconvergence, PublisherLoss child-readiness), blocking the three-consecutive-pass 12-12 evidence standard. Crash fix itself confirmed (8 crash-free reordered runs vs 2 pre-fix control crashes). Diagnosis scope recorded in phases/12-multi-node-finality-fault-proof/deferred-items.md; prime suspect WR-07 mint-v2 retry (caf34458).

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v1.0 | NodeType downstream propagation and Archive/Full behavior split | Deferred | v1.0 close |
| crdt-hardening | Require strictly-greater priority (or apply the convergent-hash tiebreak generally) before an equal-priority different-value overwrite in CrdtSet::SetValue (src/crdt/impl/crdt_set.cpp:630-649; the ConvergentImmutableValueHash guard currently applies only at the reserved UINT64_MAX priority) | Deferred | Phase 12 UAT gap closure 12-12 |
| crdt-hardening | Advance DAG heads for no-topic self-created writes so CreateDAGNode priorities are monotonic and replays are harmless (src/crdt/impl/crdt_datastore.cpp UpdateCRDTHeads 'untracked head' early-return keeps heads static; src/blockchain/Consensus.cpp:2115 is unaffected) | Deferred | Phase 12 UAT gap closure 12-12 |
| thirdparty-hardening | GossipPubSub::StopImpl hardening in the thirdparty checkout (/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub, gossip_pubsub.cpp:695-848): defer m_context destruction when m_host.use_count() > 1 (e.g. to the GossipPubSub destructor) and force-close ALL connections including unresolved-remotePeer() ones; optionally revisit the 1000ms shutdown deadline. Rationale: protects non-test owners against this same lifetime inversion; kept out of in-phase repair by the established thirdparty change-control constraint. Control-build confirmation 2026-09-02: two fresh SIGSEGV reports (multi_node_finality_fault_test-2026-09-02-125309/125541.ips, kqueue_reactor::deregister_descriptor via ~TcpConnection) reproduced with the pre-fix Stop order while the reordered build ran crash-free | Deferred | Phase 12 UAT round-2 gap closure 12-14 |
| teardown-uaf | MintRecoveryDiagnostics destructor → UTXOManager::GetUTXOs destroyed-mutex use-after-free (crash report multi_node_finality_fault_test-2026-08-26-173919.ips, distinct signature from the SIGSEGV closed by 12-14): destroyed-manager use during diagnostics destruction in RestartAtVote teardown; needs its own scoped diagnosis | Deferred | Phase 12 UAT round-2 gap closure 12-14 |

## Session Continuity

Last session: 2026-09-02T16:48:51.741Z
Stopped at: Completed 12-14-PLAN.md; teardown SIGSEGV fixed, evidence gate open on ambient flakiness
Resume file: .planning/phases/12-multi-node-finality-fault-proof/12-UAT.md
