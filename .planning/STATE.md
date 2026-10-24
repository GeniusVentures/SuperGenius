---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Light-node transaction pruning
current_phase: 20
status: executing
stopped_at: Phase 20 context gathered
last_updated: "2026-10-24T18:40:01.288Z"
last_activity: 2026-10-24
progress:
  total_phases: 25
  completed_phases: 4
  total_plans: 29
  completed_plans: 28
  percent: 16
---

# State: SuperGenius — Light-node Transaction Pruning

**Last updated:** 2026-10-15
**Milestone:** v1.2 — Light-node transaction pruning
**Current Phase:** 20

## Project Reference

See: .planning/PROJECT.md (updated 2026-10-15)
**Core value:** Light-node disk stays bounded by physically erasing spent transaction history — both the CRDT value and its raw ipfs-lite CID block — beyond a depth/finality window, while the unspent UTXO set is kept and the node stays fully operational.
**Current focus:** Phase 20 — reclamation-proof-e2e-hardening

## Current Position

Phase: 20 (reclamation-proof-e2e-hardening) — EXECUTING
Plan: 5 of 5
Status: Ready to execute
Last activity: 2026-10-24

Progress: [██████████] 97%

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 16 | Erasure Primitives & Bookkeeping Hardening | not started | PRUNE-02, GATE-03 |
| 17 | Eligibility, Policy & Gating (Dry-Run) | not started | PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06 |
| 18 | TxPruner Service & First Real Prune | not started | PRUNE-04, PRUNE-05, RECL-03, RECL-04 |
| 19 | Pruned-Node Survival & Recovery Hardening | not started | GATE-05, SURV-01, SURV-02, SURV-03 |
| 20 | Reclamation Proof & E2E Hardening | not started | GATE-02, RECL-01, RECL-02 |

## Key Decisions

Decisions are logged in PROJECT.md Key Decisions table. Recent decisions affecting current work (v1.2):

- Only light nodes prune; full/archive never erase history — light nodes are disk-constrained consumers, full/archive remain the complete sync/audit source
- Erase both levels — CRDT value + raw ipfs-lite block — via the existing tx→CID map (already exists at `TransactionManager.cpp:4322/4433`; extended with a deletion lifecycle, not replaced)
- Erase requires all outputs genuinely spent (`IsOutPointGenuinelyConsumed`) AND depth/finality window elapsed (priority vs `maxHeight`); `spent_epoch`/`spent_by_txid` are dead schema — never populated, do not build on them
- Pruning is a local RocksDB operation, never the CRDT tombstone path — `DeleteKey`/`DeleteTransaction` publish network-wide tombstones that would erase history on full/archive nodes too
- Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator that prevents re-fetch storms on the single DAG worker
- A delta block erases only when every element it carries is prune-eligible (parse the block at prune time; no persistent reverse index — the block is the source of truth)
- Ordering invariants: own-frontier survival fix (cert-slot previous_hash resolution) lands before the first real prune; block-parse eligibility gate lands before any block deletion
- One shared RocksDB, two facades (`globaldb.cpp:322`) — a single WriteBatch erases atomically and a single whole-DB CompactRange reclaims
- Reclamation is proven in measured bytes (SST/`du` before-vs-after) via explicit `CompactRange` with `bottommost_level_compaction = kForce` — deletes alone reclaim nothing
- Never trigger prune work from CRDT element callbacks (they fire pre-batch-commit — proven invariant, cert-receipt memory)

## Notes

- **Parallel milestone:** v1.3 (Security Remediation & Network Readiness, Phases 21-29) is planned on `develop` while this branch executes v1.2 — both milestones run in parallel. This STATE.md tracks the v1.2 branch position; v1.3's primary STATE/PROJECT/ROADMAP live on develop (snapshot: `.planning/milestones/v1.2-ROADMAP.md` covers this milestone's numbering). REQUIREMENTS.md on this branch carries both: the v1.3 doc plus a parallel v1.2 section. Do not execute v1.3 phases here.
- Phase numbering continues across milestones: v1.1's highest phase was 15; v1.2 phases are 16-20. Do not reset or renumber. (Phases 01-07 predate formal GSD milestone tracking — bridge-relayer/consensus-voting work, unrelated.)
- Research synthesis (`.planning/research/SUMMARY.md`, HIGH confidence) converged the 5-phase structure; the two hard ordering constraints (block-parse gate before block deletion; own-frontier fix before first real prune) are encoded in the roadmap's execution order.
- Research flags deeper research during planning for Phases 17 (own-frontier mechanism choice, window definition, lock/re-verify protocol), 18 (re-delivery guard placement and state source), and 19 (checkpoint-rebuild guard mechanism); Phases 16 and 20 follow standard patterns (skip research-phase).
- `TxPruner` is a new owning module in `src/transaction/` beside `TransactionManager` — not a `GeniusNode` lambda (god-class facade already flagged in CONCERNS.md).
- Every pruning test uses a fresh temp DB with three-part assertions (present-before, absent-after in both stores, seal-marker-still-present) plus a negative control.
- Brownfield codebase map exists at `.planning/codebase/`; v1.2 research at `.planning/research/` (SUMMARY, STACK, ARCHITECTURE, PITFALLS, FEATURES).

## Operator Next Steps

- Plan the first phase: /gsd:plan-phase 16

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260827-hbf | Add GetGraphsyncNetwork() accessor to GeniusNode (SDK needs it — no public path existed) | 2026-08-27 | 8c9e1b4f | [260827-hbf-check-if-graphsyncnetwork-can-be-obtaine](./quick/260827-hbf-check-if-graphsyncnetwork-can-be-obtaine/) |
| 260901-erh | Investigate Migration3_6_0To3_7_0 readiness for first real use post-3.5.20 (verdict: ADJUST — claim-timeout retry fix applied; allow-list propagation gap documented for launch) | 2026-09-01 | 4084545d | [260901-erh-investigate-migration-transaction-step](./quick/260901-erh-investigate-migration-transaction-step/) |

## Session

**Last session:** 2026-10-24T18:40:01.283Z
**Stopped At:** Phase 20 context gathered
**Resume File:** None

## Accumulated Context

### Pending Todos

- 3 pending — see `.planning/todos/pending/`

### Roadmap Evolution

- Phase 13 added: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps
- Phase 14 added: Account-generation publication and retired-manager lifecycle safety
- Milestone v1.2 roadmap created: Phases 16-20 (erasure primitives → dry-run policy/gating → first real prune → survival hardening → reclamation proof)
- Milestone v1.3 roadmap created on develop (Phases 21-29, security remediation) in parallel with v1.2 execution on this branch — both milestones accommodated in one planning root

### v1.0 History

v1.0 (GeniusNode Construction Refactor) shipped 2026-07-03 — see `.planning/MILESTONES.md` and `.planning/milestones/v1.0-*` for full history. Between v1.0 and v1.1, a substantial body of bridge-relayer/consensus-voting work (`.planning/phases/01` through `07`) was executed outside formal GSD milestone tracking; it is unrelated to this milestone's scope.

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| Phase 13 P01 | 10min | 1 tasks | 7 files |
| Phase 13 P03 | 27min | 3 tasks | 10 files |
| Phase 13 P12 | 12 min | 1 tasks | 8 files |
| Phase 13 P02 | 11min | 2 tasks | 5 files |
| Phase 13 P04 | 22m | 3 tasks | 9 files |
| Phase 13 P05 | 23min | 3 tasks | 11 files |
| Phase 13 P06 | 8min | 1 tasks | 4 files |
| Phase 13 P09 | 22min | 2 tasks | 9 files |
| Phase 13 P07 | 12min | 3 tasks | 15 files |
| Phase 13 P10 | 7min | 1 tasks | 2 files |
| Phase 13 P11 | 4min | 1 tasks | 4 files |
| Phase 13 P13 | 10min | 1 tasks | 5 files |
| Phase 13 P16 | 6min | 1 tasks | 2 files |
| Phase 13 P17 | 13min | 2 tasks | 8 files |
| Phase 13 P14 | 13min | 1 tasks | 8 files |
| Phase 13 P15 | 4min | 1 tasks | 2 files |
| Phase 13 P18 | 7min | 1 tasks | 1 files |
| Phase 13 P19 | 5 min | 1 tasks | 3 files |
| Phase 13 P20 | 22 min | 2 tasks | 3 files |
| Phase 13 P21 | 25 min | 1 tasks | 2 files |
| Phase 13 P22 | 9 min | 1 tasks | 1 files |
| Phase 13 P23 | 29 min | 1 tasks | 3 files |
| Phase 13 P25 | 1h 31m | 1 tasks | 3 files |
| Phase 13 P24 | 1h 18m | 1 tasks | 3 files |
| Phase 13 P26 | 11min | 2 tasks | 1 files |
| Phase 13 P27 | 36min | 2 tasks | 4 files |
| Phase 13 P28 | 34min | 2 tasks | 4 files |
| Phase 13 P29 | 31min | 2 tasks | 3 files |
| Phase 15 P18 | 27min | 2 tasks | 2 files |
| Phase 16 P01 | 12min | 3 tasks | 6 files |
| Phase 16 P02 | 18min | 3 tasks | 7 files |
| Phase 16 P03 | 40min | 3 tasks | 5 files |
| Phase 16 P04 | 71min | 3 tasks | 6 files |
| Phase 19 P01 | 17min | 2 tasks | 3 files |
| Phase 19 P02 | 19min | 3 tasks | 4 files |
| Phase 19 P03 | 34min | 3 tasks | 6 files |
| Phase 19 P04 | 53min | 3 tasks | 5 files |
| Phase 19 P07 | 82min | 2 tasks | 1 files |
| Phase 19 P05 | 105min | 3 tasks tasks | 7 files files |
| Phase 19 P06 | 34min | 2 tasks | 1 files |
| Phase 20 P01 | 24min | 2 tasks | 7 files |
| Phase 20 P02 | 23min | 3 tasks | 6 files |
| Phase 20 P03 | 16min | 2 tasks | 2 files |
| Phase 20 P04 | 20min | 3 tasks | 1 files |

## Decisions

- [Phase 13]: Genesis identity uses SGNS_TRUST_GENESIS_V1 with fixed-width big-endian fields and sorted lowercase 64-byte keys. — This makes the reviewed trust root deterministic and independently verifiable.
- [Phase 13]: Initial genesis policy is version 1 with burn value 100 and majority/two-thirds safety floors. — Unsafe bootstrap thresholds or ambiguous economic readiness must fail before fingerprinting.
- [Phase 13]: SecureCrdt owns an isolated registry by default; duplicate same-node patterns fail closed instead of replacing the current owner. — This prevents co-located nodes and duplicate policy owners from replacing signer sources or unregistering one another.
- [Phase 13]: Quorum policy identity binds distinct predecessor and authorizer hashes in canonical bytes. — Both links must equal the confirmed current policy hash for a successor.
- [Phase 13]: Membership uses M/2+1 and BurnConfig uses M-M/3 with bounds checked first. — Exact integer formulas avoid percentage drift and overflow.
- [Phase 13]: Production TPR and BurnConfig select explicit policy-specific validators. — The burn path cannot silently inherit the weaker membership floor.
- [Phase 13]: Genesis derives version-1 policy and burn records from the canonical signed manifest. — This makes the bootstrap proof independently revalidatable after restart.
- [Phase 13]: Trust-store loads verify complete policy and burn predecessor chains. — Canonical hashes and quorum proofs, not JSON or arrival order, determine authority.
- [Phase 13]: Competing same-version trust candidates return STALE_HEAD. — Exact replay/decrease remains distinct while concurrent losers have a stable race outcome.
- [Phase 13]: Candidate approvals repeat exact canonical core bytes and bind version, content hash, and signer in the storage key. — Self-contained signed bytes and key binding prevent ambiguous reconstruction or cross-candidate approval reuse.
- [Phase 13]: One CandidateAuthorizationSource snapshot drives local submission and remote filtering. — Both ingress paths must enforce the same live network, predecessor, policy, signer, and resource constraints.
- [Phase 13]: Current-only candidate listing separates activation eligibility from bounded stale audit visibility. — Predecessor changes must immediately deactivate stale candidates without erasing authenticated audit records.
- [Phase 13]: Production TPR publishes peers only from durable TrustStateStore snapshots. — Genesis and successor candidates remain separate, and receive callbacks never sign.
- [Phase 13]: Trust records bind proofs to exact candidate-core authorization bytes with strict legacy decoding. — Restart verification must check the same context operators approved without rejecting prior Phase 13 records.
- [Phase 13]: Burn v1 uses a domain-separated genesis anchor and current burn quorum. — Economic readiness cannot derive from the bootstrap proof or unconfirmed peers.
- [Phase 13]: A node-scoped confirmed burn provider publishes only after durable activation. — TransactionManager replacements need one authoritative readiness and value source.
- [Phase 13]: GlobalDbNetworkComposition validates configuration in Create but defers network and datastore side effects to Start.
- [Phase 13]: The GlobalDB transport identity stays internal beneath the database path; callers provide no account or private key.
- [Phase 13]: GlobalDB listen and broadcast topics remain mandatory caller inputs so local trust tools reuse the production CRDT channel.
- [Phase 13]: sgns-trust consumes canonical GenesisManifest bytes and a caller-supplied existing production CRDT topic. — The reviewed, fingerprinted, signed, submitted, and verified bytes remain identical while the tool reuses the deployed topic.
- [Phase 13]: Private key material enters only through an owner-controlled 0600 no-symlink file or echo-disabled terminal input and never through argv or environment variables. — This limits secret exposure and permits deterministic cleanse plus success-only deletion.
- [Phase 13]: Local propose and approve operations attempt durable activation after the explicit signature while receive and list paths remain signer-free. — Explicit operator actions can complete quorum without adding an automatic signing surface.
- [Phase 13]: TrustStartupController advances only from verified durable records while networking remains live in restricted waiting states.
- [Phase 13]: Canonical peer ordering is diagnostic-equivalent on restart; actual trust-field conflicts alert while network mismatch remains fatal.
- [Phase 13]: First-boot verification connects independent production GlobalDB compositions through the reviewed GenesisCeremony path before protected-key deletion.
- [Phase 13]: Operator manifest construction mirrors the release codec exactly because sgns-trust accepts canonical binary GenesisManifest bytes, not JSON. — This keeps independently reviewed, fingerprinted, submitted, and durably verified bytes identical.
- [Phase 13]: Durable verified trust state is restart authority; software-only protection excludes restoration of the whole disk and all local anchors. — TPM or OS-keystore monotonic state, or authenticated off-host checkpoints, is required for that threat.
- [Phase 13]: Canonical v1.1 completion is derived from Plan 13-08's recorded exact 25/25 gate and five additional policy-lifetime passes.
- [Phase 13]: MIG-05 remains the approved signature-verification-only migration; full ValidatorRegistry ISignedCRDTData adoption remains retired.
- [Phase 13]: BOOT-04 covers software-visible rollback while whole-disk/all-anchor restoration remains an accepted unsolved boundary without external anchoring.
- [Phase 13]: Burn readiness is classified from the verified authorization path stored with the current burn head, never from proof count or the current policy threshold.
- [Phase 13]: While burn v1 is BootstrapOnly, both successor APIs reject before normal validation and writes; only an identical burn-v1/value-100 canonical candidate-core with current-policy burn quorum may replace its proof.
- [Phase 13]: PayEscrow uses validated quotient/remainder basis-point arithmetic so full-domain uint64_t financial outputs are exact without non-standard wide integers. — Validated bounds make each intermediate and the final sum fit uint64_t while preserving floor semantics.
- [Phase 13]: One validated signer-set snapshot governs legacy membership, retained-child pruning, the storage bound, and quorum evaluation for each operation.
- [Phase 13]: Remote legacy signatures must bind to the exact base/sig/canonical-address key before cryptographic verification or persistence.
- [Phase 13]: Policy successor authorization is available only in ConfirmedReady, and local policy signing preflights BurnConfig economic readiness. — Rejected policy operations must not write or sign before peer-confirmed burn readiness.
- [Phase 13]: Activation APIs return false only for authenticated below-quorum candidates; actionable validation and durable failures remain errors. — Callers can preserve ordinary pending operations without masking corruption, wrong-head, or commit failures.
- [Phase 13]: Startup activation failures emit TRUST_ACTIVATION_FAILED with candidate identity and typed error context. — Operators need structured evidence when asynchronous durable activation fails.
- [Phase 13]: BurnConfig restart publication consumes TrustStateStore's verified PeerQuorum classification and never reconstructs authority from proof cardinality or the current policy threshold.
- [Phase 13]: Phase 13 gap closure requires focused counterexamples, exact 25-test enumeration/JUnit accounting, and five post-gate lifetime repetitions in one fail-fast chain.
- [Phase 13]: Public LoadAndVerify holds transition_mutex_ across complete trust verification; lock-owning commit paths use LoadAndVerifyUnlocked. — This serializes coherent durable views without recursively locking commit precondition and post-commit verification.
- [Phase 13]: Automatic signing is limited to a verified current member and the exact deterministic BootstrapOnly burn-v1/value-100 candidate. — This closes initial-burn liveness without broadening manual approval or successor-signing authority.
- [Phase 13]: SecureCrdt callbacks queue serialized worker Refresh operations. — Activation writes must not reenter the GlobalDB callback thread.
- [Phase 13]: Failed candidate IDs are suppressed only for the current controller while authoritative CRDT records remain restart-discoverable. — This prevents tight in-process retries while preserving fault recovery after reconstruction.
- [Phase 13]: Only remotely authored trusted-peer callback records enter passive activation; explicit local admin paths retain their own activation result. — Prevents synchronous callback activation from racing the explicit administrative decision.
- [Phase 13]: Policy candidates use the serialized callback worker; teardown joins queued work before owner-safe callback removal. — Keeps GlobalDB writes off callback delivery and queued work within controller lifetime.
- [Phase 13]: Burn-ready refresh lists and deterministically processes retained successors before ConfirmedReady; only foreign burn approvals enqueue passive activation. — This closes passive divergence while preserving explicit LocalTrustAdmin activation and preventing local callback races.
- [Phase 13]: GeniusNode serializes complete transitions with a recursive mutex so existing same-thread nested transitions remain valid while concurrent duplicate work is excluded.
- [Phase 13]: Posted trust transitions carry their source state and lifecycle epoch; ConfirmedReady may initialize transactions only from the two trust waiting states.
- [Phase 13]: TransactionManager replacement stops and releases prior bridge, state, slot-hash, GlobalDB, and account callback ownership before TransactionManager::New installs a generation-owned replacement.
- [Phase 13]: Policy replay merges authoritative retained candidates with callback hints deterministically. — Callback delivery is not restart authority; controller-local suppression resets on reconstruction.
- [Phase 13]: Policy candidates use pending and failed containers distinct from burn candidates. — Their authoritative discovery APIs, retry state, and typed error contexts must not cross domains.
- [Phase 13]: Actionable retained-policy failure suppression is scoped to one controller lifetime. — This bounds in-process retries while reconstruction can rediscover and retry the retained authenticated record.
- [Phase 13]: Final Phase 13 closure requires exact source/XML/CTest/JUnit name equality and target-scoped sanitizer proof — Absent two-target instrumentation is NOT_RUN, never PASS.
- [Phase 13]: Account and TransactionManager owners are copied only as one lifecycle-mutex-protected generation snapshot; a switching generation is unavailable. — This prevents mixed account/manager ownership during live selection.
- [Phase 13]: SelectAccount invalidates publication before draining watcher/services outside the lifecycle lock, then publishes one complete replacement pair. — Blocking stop and join work must not hold the lifecycle mutex.
- [Phase 13]: Trusted-peer approval identity is one immutable node-scoped address/signing authority captured together for the controller lifetime. — Selected transaction accounts cannot relabel or change trusted-peer signatures.
- [Phase 13]: Refresh dispatch state owns serialization, retry timing, cancellation, and diagnostics independently; it locks the weak controller only across one Refresh attempt. — This permits final-owner release from inside dispatched callbacks without self-join, detach, termination, or use-after-free.
- [Phase 13]: Only policy and burn discovery failures are transient; activation failures remain actionable and controller-scoped candidate suppression is preserved. — Semantic stage classification prevents retries from masking durable, genesis, publication, or candidate-specific activation faults.
- [Phase 13]: One refresh cycle is exactly attempts 1 through 7 with 100/200/400/800/1600/3200ms delays; duplicate requests coalesce and exhaustion returns the dispatcher to idle. — The fixed cap and exact observability close retry storms while preserving autonomous recovery from transient discovery failures.
- [Phase 13]: Plan 13-29 structural guards trace named expected values and delegated helpers through semantic assertions without weakening exact behaviors.
- [Phase 15]: Wrong-PSK GlobalDB test nodes must release db (ShutdownNow+reset) and graphsync Network BEFORE pubsub Stop - StopImpl destroys its io_context right after its own host ref, so a host surviving Stop via a retained Network member destroys parked never-negotiated pnet handshake connections against a dead kqueue reactor (15-18 ordered teardown)
- [Phase 15]: UAT-1 closed by GlobalDB-level composition (no Blockchain/genesis): NetworkMembershipFilterFlowTest case 11 joins real-data-path scoped job replication (TaskQueueImpl+SubTaskResultStorageImpl on the scoped topic), data-level public-node isolation (live public GlobalDB, both-direction dials, multi-key negative window), and deny-all teardown on a still-live GlobalDB - mutation-verified non-vacuous
- [Phase 16]: Erase-prefix scans carry a trailing '/' so sibling keys sharing a name prefix (key vs key2) are never swept; StageEraseElement stages all four CRDT key families unconditionally into the caller's batch and never fires the delete hook (RevertTransaction hazard).
- [Phase 16]: StageBlockRemoval stages exactly the CID::toBytes() block-bytes key into the caller's batch under dagMutex_; the seal marker is never constructed and the removeNode chain stays dormant (cannot join an external WriteBatch).
- [Phase 16]: StageBlockRemoval is the one SuperGNUS-side home of the vendored block-key format; RemoveProcessedTransactionEntry is a revert-free plain tracked-map erase (missing key = success no-op).
- [Phase 16]: EraseEntry composes all four guards (all-topic head scan, pending/active root job, LRU cache, isResolved) fail-closed before staging into ONE BufferBatch with a single sync commit and a post-commit seal re-assert; key shapes are built by the caller — GlobalDB stays generic (D-01).
- [Phase 16]: EraseTransactionEntry completes the tx->CID map deletion lifecycle (resolve CID, atomic GlobalDB erase, revert-free tracked-map cleanup); the test-only interrupt hook aborts before commit so the dying batch proves all-or-nothing (D-03/D-C4).
- [Phase 16]: The erase pending/active guard protects the EXTERNAL root path only (pendingRootQueue_/activeRootCID_); self-created Put jobs live in selfCreatedJobList_ — the 16-04 guard proof stalls a mirrored receiver's getNode, the fetch entry the DAG worker reaches with the root job active
- [Phase 16]: The source-scan gate roots at PROJECT_ROOT, never CMAKE_SOURCE_DIR — this repo drives CMake from build/OSX/ (out-of-root driver), so CMAKE_SOURCE_DIR/src does not exist and would make the gate vacuously green
- [Phase 16]: GlobalDB end-to-end erase seeds must Put on a real topic: an empty-topics Put creates no destinations, so UpdateCRDTHeads records no head and writes no seal — the integration-file topic recipe is load-bearing for guard and seal assertions
- [Phase 19]: Preserve-clear (19-01): ClearUnspentKeepConsumed is unconditional and owner-vended in UTXOManager; SetUTXOs wipe semantics stay byte-identical for existing callers
- [Phase 19]: SetUTXOs wipes only index-resident entries — ConsumeUTXOs leaves CONSUMED entries orphaned from address_outpoints_; the real-world wipe path is post-restart (LoadUTXOs indexes CONSUMED before the mismatch clear), so D-01 tests must seed the reload shape
- [Phase 19]: D-02 half 1 — preflight failures (witness/size) return typed Error::PRE_BROADCAST_VALIDATION; TickOnce rolls back and STAYS READY (error-code comparison across the Commit boundary cannot classify — same errc codes fire both sides)
- [Phase 19]: D-02 half 2 — bounded SYNCING exit (60s knob, SetSyncingExitTimeoutForTest seam) anchors to GetLocalConfirmedNonce and never touches pending_nonces_; held reservations stay held so the next reserved nonce is strictly greater
- [Phase 19]: SC-5 retry guard (19-03): RetryDueFailedRoots consults isResolved BEFORE the counter site; pruned/sealed roots are terminal (entry cleared), the fetch-attempt counter never moves for them, and the isResolved error branch stays fail-safe (re-armed, never enqueued)
- [Phase 19]: Post-18-01, no natural path can place a sealed root on the retry ladder (the broadcast intake guard blocks sealed CIDs) — 19-03 added ForTest ladder-seeding seams + ladder-count observables on CrdtDatastore; the pass under test stays the real DAG worker loop
- [Phase 19]: AccountMessenger's HandleBlockLikeRequest is private and InterfaceMethods has no response-send callback (19-03 plan assumptions) — the pruned-serving row is proven over real loopback pubsub with the responder's sign_ callback as the public capture point for the empty signed BlockResponse
- [Phase 19]: (19-07): Broadcast-order sequencing is unreproducible on this gossip layer (graft relays all cached publishes at once) -- peer-refusal proofs split into a hermetic ordered-AddRoute + direct-getNode walk beside an ordering-free mesh E2E
- [Phase 19]: (19-07): PrunedMesh determinism needs a full-node-owned serving topic (dual-topic put links the mesh history under one serving head); single-shared-topic wiring makes the Pitfall-6 ladder ride a coin flip
- [Phase 19]: (19-07): A directly-fetched block silences the broadcast intake for that CID (HasBlock skip, delta never merges) -- discard the LRU copy (DeleteCIDBlock, the RetryDueFailedRoots idiom) between proof phases
- [Phase 19]: (19-05): Tri-state lookup result carries the invariant violation on the verdict (VALUE_PRESENT_INVARIANT_VIOLATION + found tx) so the loud case is observable on the result, not only in logs; a present value always outranks a marker
- [Phase 19]: (19-05): TransactionLookupResult uses an opaque PrunedLookup forward declaration (not a PruneEligibility.hpp include) -- the include leaks rapidjson into every TM.hpp consumer and broke the migration build; the single construction site always sets verdict
- [Phase 20]: GlobalDB is the one home of the whole-DB compaction recipe (CompactStorage: bottommost force flag, no write stall, canceled passthrough) and the whole-DB size measurement (GetApproximateDiskUsageBytes: 0xFF sentinel range, files+memtables) — callers never touch getDB(); post-shutdown fail-closes GLOBALDB_NOT_STARTED, pre-set cancel returns operation_canceled (clean skip)
- [Phase 20]: pruning.compact_threshold_mb defaults 16MB, rejects degenerate 0 at read time (write-amp DoS on self, V5/WR-03); MB is the single config representation — threshold bytes derive at the TxPruner comparison site
- [Phase 20]: TxPruner post-cycle compaction gate (D-01): erased>=1 AND !canceled AND cumulative erased-bytes watermark >= compact_threshold_mb; the watermark is the evaluation-carried estimate, kept on cancel/failure as the natural retry, reset only on success — never fails the completed cycle
- [Phase 20]: PruneStatus.bytes_reclaimed is the MEASURED baseline-at-last-compaction delta, stamped only at compaction success under the status mutex (nullopt until the first compaction since restart); the erase-time RECL-03 INFO stays counts-only and the compaction emits its own before/after/delta INFO (D-02 honest split)
- [Phase 20]: The compaction cancel atomic is a TxPruner MEMBER (the engine's compaction iterator reads it asynchronously; a stack local dangles) and GeniusNode's stop path sets it before tx_pruner_.reset() — the io-pool drain is the join, the atomic only shortens any not-yet-started compaction's window
- [Phase 20]: The reclamation gate's sanity ceiling is calibrated to the MEASURED storage envelope (erased*20+2MB), not the payload multiplier — the CRDT stack stores each value under four key families + delta block + UTXO records, so an erase legitimately reclaims ~15x the payload (measured 1492%, stable across runs); payload-only denominators stay valid for floors only

## Deferred Items

Items acknowledged and deferred at milestone close on 2026-10-05 (v1.1):

| Category | Item | Status |
|----------|------|--------|
| todo | 2026-08-10-secure-trusted-peer-genesis-configuration | Superseded — Phase 13 delivered the BOOT-01..04 scope (durable confirmed trust-state, ceremony, policy authority); kept for reference |
| todo | bridge-race-not-all-11-mint-within-window (P1) | Open — Track A bridge scope (evmrelay_integration track), not v1.1 |
| todo | bridge-startup-wiring-mock-rpc (P1) | Open — Track A Phase 5 scope (PR #298 findings), not v1.1 |
| quick_task | 11 June-2026 TokenId/InputValidator records (260602-*) | Stale — artifacts no longer on disk |
| uat | 14-UAT.md (4 open scenarios) | Discarded — phase 14 execution discarded by owner 2026-08; scope unshipped |
| uat | 15-HUMAN-UAT.md | Resolved 2026-10-02 (status label predates resolve state) |
| verification | 11-VERIFICATION.md (conditional) | Documented — multi_account instability later green; see v1.1-MILESTONE-AUDIT.md |
| verification | 13-VERIFICATION.md (29/32) | Documented — residuals map to discarded phase-14 scope; see v1.1-MILESTONE-AUDIT.md |
| gap | ELM-01..08 | Dropped from v1.1 — cross-repo SGProcessingManager scope; see v1.1-MILESTONE-AUDIT.md |
| gap | MIG-05 wiring | Partial — behavior-neutral revert d1f2a14ca; re-apply or amend records |
