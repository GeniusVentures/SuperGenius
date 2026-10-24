# Roadmap: SuperGenius

## Milestones

- ✅ **v1.0 GeniusNode Construction Refactor** — Phases 1-3 (shipped 2026-07-03)
- ✅ **v3.0 Canonical Burn Finality Rebuild** — Phases 8-12 (shipped 2026-09-03)
- ✅ **v1.1 trusted-peer genesis, quorum-policy, and production integration gaps** — Phases 08-15 (shipped 2026-10-05, known gaps recorded)
- 🚧 **v1.2 Light-node transaction pruning** — Phases 16-20 (**ACTIVE on this branch** — started 2026-10-15; tracked live below)
- 🚧 **v1.3 Security Remediation & Network Readiness** — Phases 21-29 (planned on `develop` in parallel — tracked below; snapshot of the v1.2 roadmap as created: `.planning/milestones/v1.2-ROADMAP.md`)

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Phase numbering continues across milestones: v1.2 owns 16-20 on its execution branch; this branch's v1.3 owns 21-29. Do not reset or renumber.

<details>
<summary>✅ v1.0 GeniusNode Construction Refactor (Phases 1-3) — SHIPPED 2026-07-03</summary>

- [x] **Phase 1: Config-Driven Settings Foundation** (1/1 plans) — completed 2026-07-03
- [x] **Phase 2: Variant Factory + Constructor Reorder** (1/1 plans) — completed 2026-07-03
- [x] **Phase 3: Call-Site Migration + Verification** (3/3 plans) — completed 2026-07-03

Full phase details: `.planning/milestones/v1.0-ROADMAP.md`

</details>

<details>
<summary>✅ v3.0 Canonical Burn Finality Rebuild (Phases 8-12) — SHIPPED 2026-09-03</summary>

- [x] **Phase 8: Canonical Slot & Certificate Binding** (1/1 plans) — completed 2026-08-20
- [x] **Phase 9: Durable One-Vote Finality** (2/2 plans) — completed 2026-08-20
- [x] **Phase 10: Authoritative Slot Certificate Publication** (7/7 plans) — completed 2026-08-21
- [x] **Phase 11: Convergent Certificate Consumption & Mint Recovery** (5/5 plans) — completed 2026-08-24
- [x] **Phase 12: Multi-Node Finality Fault Proof** (22/22 plans, six gap-closure rounds) — completed 2026-09-03

Full phase details: `.planning/milestones/v3.0-ROADMAP.md`

</details>

<details>
<summary>✅ v1.1 trusted-peer genesis, quorum-policy, and production integration gaps (Phases 08-15) — SHIPPED 2026-10-05</summary>

**Milestone Goal:** Quorum-signed CRDT trust state as the production authority: decoupled multi-signature and SecureCRDT layers back an authenticated TrustedPeerRegistry and live BURN_BASIS_POINTS policy, closed through reviewed genesis ceremony, versioned quorum policy, restart/tamper authority, and production integration gates — then private-network identity (`privateNetworkId`) bound through every layer with fail-closed membership enforcement.

- [x] **Phase 08: MultiSig Primitive** (1/1 plans) — MSIG-01..03
- [x] **Phase 09: SecureCRDT Layer** (2/2 plans) — SCRDT-01..04
- [x] **Phase 10: TrustedPeerRegistry** (2/2 plans) — TPR-01..03
- [x] **Phase 11: BurnConfig Quorum Wiring** (2/2 plans) — BURN-01..03
- [x] **Phase 12: ValidatorRegistry Migration** (1/1 plans) — MIG-05/06
- [x] **Phase 13: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps** (29/29 plans, four gap-closure waves) — BOOT-01..04, POLICY-01, VALID-01, TEST-01
- [x] **Phase 15: Private networks consume privateNetworkId identity and bind** (17/18 plans; 15-04 descoped into 15-13) — D-01..D-11 (CONTEXT-derived)
- ✗ **Phase 14: Account generation publication and retired manager lifecycle** — 0/15 executed, discarded by owner (2026-08); scope unshipped

**Known gaps at ship:** ELM-01..08 dropped (cross-repo SGProcessingManager scope); MIG-05 wiring behavior-neutral revert; phase-14 SelectAccount lifecycle residual. Full record: `.planning/milestones/v1.1-MILESTONE-AUDIT.md` and `.planning/MILESTONES.md`.

Full phase details: `.planning/milestones/v1.1-ROADMAP.md`

</details>

### 🚧 v1.2 Light-node transaction pruning (ACTIVE on this branch — started 2026-10-15)

**Milestone Goal:** Light-node disk stays bounded by physically erasing spent transaction history — both the CRDT value and its raw ipfs-lite CID block — beyond a configurable depth/finality window, while the node keeps its unspent UTXO set and remains fully operational (restart, tip sync, spend/receive). Full and archive nodes keep complete history and remain the sync/audit source for fresh nodes.

**Execution order:** 16 → 17 → 18 → 19 → 20 (strict sequence). Two hard ordering constraints from research hold: the block-parse eligibility gate (Phase 17) lands before any block deletion (Phase 18), and the own-frontier survival fix (Phase 17) lands before the first real prune (Phase 18). Reclamation proof is deliberately last.

- [x] **Phase 16: Erasure Primitives & Bookkeeping Hardening** - Two-level local erasure primitives behind owner-module APIs; zero production behavior change (PRUNE-02, GATE-03) (completed 2026-10-15)
- [ ] **Phase 17: Eligibility, Policy & Gating (Dry-Run)** - Evaluator + multi-element block-parse gate + own-frontier fix + Light-only fail-closed gate + config knobs; evaluates, never erases in production (PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06)
- [x] **Phase 18: TxPruner Service & First Real Prune** - First real erasure: idempotent crash-consistent cycle, re-delivery guard + sweep re-prune, GeniusNode wiring, per-cycle observability (PRUNE-04, PRUNE-05, RECL-03, RECL-04) (completed 2026-10-23)
- [x] **Phase 19: Pruned-Node Survival & Recovery Hardening** - Restart matrix, boot/checkpoint-rebuild safety, pruned-vs-not-found answers, fresh-node-through-pruned-mesh E2E (GATE-05, SURV-01, SURV-02, SURV-03) (completed 2026-10-24)
- [ ] **Phase 20: Reclamation Proof & E2E Hardening** - Scheduled CompactRange + measured-bytes CI gate, non-tx immutability + full/archive retention ctests, window-edge race faults (GATE-02, RECL-01, RECL-02)

*Parallel milestones:* v1.2 executes HERE; v1.3 (Phases 21-29) is planned on `develop` in parallel — both tracked in this planning root. The v1.3-side read-only snapshot of this roadmap: `.planning/milestones/v1.2-ROADMAP.md`.

### 🚧 v1.3 Security Remediation & Network Readiness (ACTIVE — started 2026-10-19)

**Milestone Goal:** Close all 25 findings from the October 2026 external security assessment (`.planning/notes/security-assessment-2026-10-response.md`) so the network is safe to expose publicly — every fix proven by an adversarial regression suite (VER-01) that names the attacker authority required, the expected rejection, and the actual behavior, paired with a compatibility twin proving the legitimate pre-upgrade path survives.

**Execution order:** strictly 21 → 22 → 23 → 24 → 25 → 26 → 27 → 28 → 29 (numeric order = execution order; no planned overlap). Hard ordering constraints encoded: REL-02 (artifact verification) is satisfied in-phase at Phase 22 — its digest-verification CI step lands as an early plan, with or before that phase's first fork-riding change, because fork changes ride the 4-stage release pipeline (fork commit → thirdparty superproject bump → SuperGenius CI download → integration tests) that today consumes unverified thirdparty/zkLLVM artifacts; the per-phase adversarial+compat test-pair discipline applies to every security-fix phase even though the first-class apparatus (VER-01) and release gating (REL-01) arrive at Phase 25 — Phases 21-24's fixes carry their own adversarial tests in-phase per their criteria; Phase 26 is the dependency sink — GRID-03 consumes GRID-01/02/04 and MINT-01 in-phase plus authenticated transport (23) and mint identities (21); Phase 27 carries the complete-relay-vs-topology-contract decision gate; Phase 29 (return leg) is strictly last — RET-01 requires TRUST-01 (Phase 21) and BRIDGE-01/02 (Phase 24) closed and proven first.

- [ ] **Phase 21: Consensus Trust & Mint Authorization** - Registry-CID authorization chain with a grandfather rule that keeps history re-validating; admission/equivocation audit; fail-closed chain-ids; legacy-v1 admission cutover; receipt-to-mint output binding (TRUST-01, TRUST-02, MINT-02, MINT-03, MINT-04)
- [ ] **Phase 22: Wire & Ingestion Hardening (Fork)** - Length and nesting caps before allocation across the three fork readers; error-result hash parsing with fuzz coverage; thirdparty/zkLLVM digest verification + SHA-pinned actions landing with or before the first fork-riding change; first FORK-DELTA manifest + mixed-version interop-matrix run (WIRE-01, WIRE-02, REL-02)
- [ ] **Phase 23: Gossip & DHT Authentication (Fork)** - Verify-before-forward on public topics with signing-key↔from binding and per-topic policy as data; DHT record validation with admission limits and request-bound nonces; before/after throughput benchmark (GOSSIP-01, GOSSIP-02)
- [ ] **Phase 24: Bridge Ingestion Safety** - Per-chain confirmation depth/finality as a restart-durable state machine proven under anvil reorg; content-corroborated multi-operator RPC quorum (BRIDGE-01, BRIDGE-02)
- [ ] **Phase 25: Verification Spine & Release Gate** - Adversarial-suite apparatus promoted to first-class ctest/CI targets, release-before-tests reorder, fork-delta discipline as a standing gate; the dependency-free EVM on-chain audit and GeniusSDK memory-safety tracks ride here (VER-01, REL-01, REL-03, EVM-01, SDK-01)
- [ ] **Phase 26: Processing-Grid Integrity** - Authenticated queue/result/payout claims; escrow entitlement (certified task result) and independent sufficiency validation; staged removal of the proof-verification bypass; independent result verification before payout on the deterministic/redundant tier (GRID-01, GRID-02, GRID-03, GRID-04, MINT-01)
- [ ] **Phase 27: Network Topology** - Relay/hole-punch completed limits-first OR the relay-through-full-nodes topology published as an explicit supported contract, decided at a mid-phase gate on fork-divergence cost (TOPO-01)
- [ ] **Phase 28: Worker Profile, Isolation & Mobile Readiness** - Minimal worker profile with measured duty-cycle controls; honestly-defined job privacy boundary with scoped controls; adaptive resource budgets and demonstrated lifecycle recovery (SDK-02, PRIV-01, MOB-01)
- [ ] **Phase 29: Return-Leg Activation (SGNS→EVM)** - New bridge exporter module feeding GNUSBridgeAttestor; replay protection; activation checklist with deployed-contract verification — strictly last (RET-01)

## Phase Details (v1.2 — ACTIVE on this branch)

### Phase 16: Erasure Primitives & Bookkeeping Hardening

**Goal**: The physical two-level erasure primitives exist behind owner-module APIs with the safety invariants baked into their shape — seal markers unaddressable, in-use CIDs guarded, one atomic WriteBatch — and the fresh-DB test-fixture rules established, with zero production behavior change.
**Depends on**: Nothing within v1.2 (first phase of the milestone; numbering continues after v1.1's Phase 15)
**Requirements**: PRUNE-02, GATE-03
**Success Criteria** (what must be TRUE):

  1. A primitive erase of one transaction on a fresh temp DB passes the three-part assertions — CRDT set entries (`/crdt/s|t/<key>/<cid>`, `/crdt/k/<key>/v|p`) absent, raw CID block absent from the block store, `<cid>sealed` marker still present — plus the negative control (an untouched sibling key remains byte-identical).
  2. CRDT entries, raw block bytes, and the tx→CID map entry erase in one atomic RocksDB WriteBatch: a crash-injected interrupted batch leaves no partial state (never value-without-block, never block-without-map-entry).
  3. The block-erase exposure refuses in-use CIDs — head CID, root-CID pending/active, and LRU-cached CIDs are all rejected by guards — and no code path can address a seal marker (`isResolved` asserted before and after erase).
  4. No tombstone path exists in the new surface: erasure writes directly to the shared DB through the `CrdtSet`-owned local primitive, `DeleteKey`/`DeleteTransaction` appear nowhere in the pruner code (grep gate), and the tx→CID map gains a deletion lifecycle with tracked-map cleanup.
  5. The default build and the full existing ctest suite pass green with the pruner unwired — `removeNode`'s first consumer is the test suite, so production behavior is unchanged.

**Plans**: 4 plans
Plans:
**Wave 1**

- [x] 16-01-PLAN.md — CrdtSet batch-erase staging primitive + CrdtDatastore guard exposure (PRUNE-02)
- [x] 16-02-PLAN.md — Guarded DAGSyncer block staging + revert-free tracked-map erase + real-store erase fixture (PRUNE-02, GATE-03)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 16-03-PLAN.md — GlobalDB one-batch erase composition (guards, interruption seam, m_dagSyncer) + tx map deletion lifecycle (PRUNE-02, GATE-03)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 16-04-PLAN.md — Fresh-DB erase suites (atomicity, seal retention, guards, integration) + ctest source-scan gates + full-suite green (PRUNE-02, GATE-03)

### Phase 17: Eligibility, Policy & Gating (Dry-Run)

**Goal**: The pruning policy evaluator decides erasability correctly on live data in dry-run — never erasing in production — and the two correctness dependencies of the first real prune (multi-element block gate, own-frontier survival fix) are implemented and proven before any production erasure exists.
**Depends on**: Phase 16
**Requirements**: PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06
**Success Criteria** (what must be TRUE):

  1. The dry-run evaluator on a live light node reports exactly the truly-eligible set: every output genuinely consumed (`IsOutPointGenuinelyConsumed` — synthesized zero-amount tombstones excluded) AND depth beyond the configured finality window (priority vs `maxHeight`); unspent or in-window transactions are never reported eligible.
  2. The block-parse gate reports a delta block erasable only when every element it carries is prune-eligible (parsed at evaluation time from the block itself, not from "what this node applied") — a multi-element block with any non-eligible sibling (e.g. another account's live element) is never eligible.
  3. With the node's own frontier transaction deep inside the eligible window, outgoing `previous_hash` resolution still completes from cert-slot data (certificates are never pruned) and the cold-start nonce chain is correct — proven by test before any production erasure exists.
  4. Gating fails closed: the evaluator runs only on `NodeType::Light` (full/archive refuse), erasure candidates are confined to the `/bc-<netid>/tx/` prefix allowlist (the verified `TRANSACTION_BASE_FORMAT` constant), and out-of-prefix keys are rejected and ctest-asserted.
  5. Pruning knobs sit beside `node_type` in `sgns_config.json`, default off, with resolved values INFO-logged; a config without the new keys behaves exactly as today.

**Plans**: 6 plans
Plans:

**Wave 1** *(parallel — no file overlap)*

- [ ] 17-01-PLAN.md — Candidate B spend-recency carrier: `spent_by_txid` populated at ConsumeUTXOs + 4 call sites + read accessor + REQUIREMENTS amendment (PRUNE-01)
- [ ] 17-02-PLAN.md — CRDT read exposures: GetPriority passthrough chain + GetLocalDeltaKeys sibling (elements AND tombstones) (PRUNE-01, PRUNE-03)

**Wave 2** *(blocked on Wave 1)*

- [ ] 17-03-PLAN.md — Cert-slot previous_hash generalization (one-line rung relaxation) + SC-3 pruned-frontier proof family (GATE-04)
- [ ] 17-04-PLAN.md — PruneEligibility evaluator: structured result, Light+allowlist gates, spend-recency window, PruningConfig, SC-1/SC-4 suites (PRUNE-01, GATE-01)

**Wave 3** *(blocked on Wave 2)*

- [ ] 17-05-PLAN.md — Multi-element block-parse gate (dry-run) + `/prune/` marker design contract + SC-2/marker suites (PRUNE-03, GATE-01)
- [ ] 17-06-PLAN.md — `pruning.*` config knobs + config-gated dry-run timer + per-cycle INFO summary + SC-5 full-suite gate (GATE-06)

### Phase 18: TxPruner Service & First Real Prune

**Goal**: The `TxPruner` service performs the first real erasure on a light node — an idempotent, crash-consistent cycle that survives re-delivery and resurrection — wired into `GeniusNode` behind default-off config, with per-cycle observability and queryable status.
**Depends on**: Phase 17
**Requirements**: PRUNE-04, PRUNE-05, RECL-03, RECL-04
**Success Criteria** (what must be TRUE):

  1. A light node with spent history beyond the window physically erases eligible transactions through the ordered four-step flow (CRDT value → CID lookup → block-eligibility + delete → map entry, in that order) — both storage levels gone, unspent UTXO set untouched.
  2. Crash consistency to a fixed point: killing and restarting the node at any step of the erase flow converges to the same fixed point — no partially-erased transaction survives (no CRDT value without block, no orphan map entry), and re-running the cycle on a clean node is a no-op.
  3. Re-delivery is terminal: re-broadcasting a pruned root and restarting head exchange produce zero pruned-CID fetch attempts and zero re-created elements; the periodic sweep re-prunes resurrected entries (add-wins OR-Set re-merge from lagging peers) back to the fixed point — no re-fetch storm.
  4. The tick loop activates only under Light ∧ `State::READY` ∧ empty `missing_tx_hashes_` ∧ settle-window elapsed; `GeniusNode` constructs/starts/stops the pruner per config, and default-off config erases nothing.
  5. Each cycle emits the observability log (txs examined/erased, watermark advance — the bytes-reclaimed field completes with Phase 20 compaction) and prune status (watermark + last-cycle stats) is queryable.

**Plans**: TBD

### Phase 19: Pruned-Node Survival & Recovery Hardening

**Goal**: A pruned light node is fully operational — every recovery path written under the "history never disappears" assumption is audited and hardened, and history queries answer "pruned" distinctly and cleanly to both local callers and peers.
**Depends on**: Phase 18
**Requirements**: GATE-05, SURV-01, SURV-02, SURV-03
**Success Criteria** (what must be TRUE):

  1. The restart matrix on a pruned node is green: prune → restart → READY with zero pruned-CID fetch attempts → new-tip sync → spend/receive → next outgoing tx `previous_hash`/nonce chain correct.
  2. Boot-time UTXO assembly and the checkpoint-mismatch clear-and-rebuild path treat a missing pruned transaction as not relevant — boot completes without failure, and the rebuild never resurrects spent outputs from remaining history (no double-spend window).
  3. Local history queries on pruned transactions return a distinct "history pruned" answer (never conflated with not-found); peers requesting pruned data get an immediate `RS_NOT_FOUND`/`CID_NOT_FOUND` refusal with prompt requester failover — both tested against a real pruned node.
  4. A fresh light node bootstraps through a mesh whose light peers have pruned the history it needs, served by the full node — E2E green.
  5. The recovery-path audit (root jobs, `RetryDueFailedRoots`, `RetryStalledDeltas`, work-journal recovery, `missing_tx_hashes_`) treats pruned/resolved CIDs as terminal, with targeted tests per consumer; `AccountMessenger` remains correct once map entries are deleted.

**Plans**: 7 plans
Plans:

**Wave 1** *(parallel — no file overlap)*

- [x] 19-01-PLAN.md — UTXOManager consumed-preserving clear (D-01 substrate) + KeepConsumed unit suites (GATE-05)
- [x] 19-02-PLAN.md — D-02 send-wedge fix: typed pre-broadcast error + READY-preserving TickOnce branch + bounded SYNCING exit + lifecycle suites + folded todo closed (SURV-01)
- [x] 19-03-PLAN.md — SC-5 recovery-audit guards: RetryDueFailedRoots isResolved guard + stalled-delta/cert/AccountMessenger verify tests (GATE-05, SURV-01)

**Wave 2** *(blocked on Wave 1)*

- [x] 19-04-PLAN.md — D-01 checkpoint-rebuild guard: preserve-clear swap + PRUNED marker consult + own-replay recognition + gap-fill crosscheck + RebuildGuard suites (GATE-05)
- [x] 19-07-PLAN.md — SURV-02 peer refusal/failover + SURV-03/D-04 three-node pruned-mesh E2E (SURV-02, SURV-03)

**Wave 3** *(blocked on Wave 2)*

- [x] 19-05-PLAN.md — D-03 tri-state lookup (found/not-found/pruned) + GeniusNode passthrough + `tx <hash>` CLI + TriState suite (SURV-02)
- [x] 19-06-PLAN.md — SC-1 restart matrix: prune → real StartCore boot → READY, zero pruned-CID fetches → tip sync → spend/receive → nonce/previous_hash chain (SURV-01)

### Phase 20: Reclamation Proof & E2E Hardening

**Goal**: The milestone's headline claim is proven in measured bytes — deleted data physically leaves the disk via scheduled compaction, non-transaction data is provably untouched, and the window-edge races are fault-tested.
**Depends on**: Phase 19
**Requirements**: GATE-02, RECL-01, RECL-02
**Success Criteria** (what must be TRUE):

  1. A scheduled light-node-only `CompactRange` over the shared DB (`bottommost_level_compaction = kForce`, off the hot path) runs after prune cycles, and measured SST/`du` bytes drop before-vs-after on a node with known spent history.
  2. The measured-bytes CI gate proves real reclamation — a prune + compaction cycle on a fixture with known block sizes must reclaim the expected byte delta (measured bytes, not key counts) or CI fails.
  3. Non-transaction data is byte-identical across a prune run: certificates, SecureCRDT policy values, processing results, DAG heads, and seal markers (ctest).
  4. Full and archive nodes in the same mesh retain 100% of their transaction history across the light node's prune cycles (ctest).
  5. Window-edge races are fault-tested: a spend arriving during an active prune cycle is never erased (spent-ness re-verified under lock immediately before each erase) and is picked up by the next cycle — verified under debug and release timing.

**Plans**: 5 plans

Plans:

**Wave 1**

- [x] 20-01-PLAN.md — compaction substrate: GlobalDB CompactStorage + GetApproximateDiskUsageBytes (the one recipe home) + pruning.compact_threshold_mb knob + resolved-values INFO (RECL-01)

**Wave 2** *(blocked on Wave 1)*

- [x] 20-02-PLAN.md — TxPruner post-cycle compaction gate (D-01) + evaluation-carried erased-bytes estimate + PruneStatus.bytes_reclaimed stamped at compaction (D-02) + RECL-03 wording retirement + shutdown cancel wiring + canary (RECL-01)

**Wave 3** *(parallel — no file overlap)*

- [ ] 20-03-PLAN.md — dedicated measured-bytes gate target: 200×~10KB → prune → ONE compaction → ≥60% bounded assertion + anti-vacuous pin + du belt (RECL-02)
- [ ] 20-04-PLAN.md — compaction-trigger conjunct matrix + bytes_reclaimed honesty + GATE-02 post-compaction byte-identity (D-C5) + SC-5 seam-held race both legs (RECL-01, GATE-02)

**Wave 4** *(blocked on Wave 3)*

- [ ] 20-05-PLAN.md — SC-4 mesh retention: full/archive GlobalDB peers byte-identical across the light's prune + compaction (GATE-02)

## Phase Details (v1.3 — ACTIVE)

### Phase 21: Consensus Trust & Mint Authorization
**Goal**: The certificate trust anchor holds on a public network — a registry loaded by CID is authorization-verified with a grandfather rule that keeps historical finality re-validating — and every mint/escrow authorization gap from the assessment's highest-credibility findings (#1, #3, #4, #5, #14) closes fail-closed without orphaning legitimate state.
**Depends on**: Nothing within v1.3 (first phase; numbering continues after v1.2's Phase 20)
**Requirements**: TRUST-01, TRUST-02, MINT-02, MINT-03, MINT-04
**Success Criteria** (what must be TRUE):
  1. A certificate path loading a validator registry by CID rejects a forged/self-certified registry (adversarial test: attacker authority = a peer supplying an unauthorized registry CID; expected rejection; actual behavior recorded), while pre-upgrade certificates still re-validate after restart and catch-up sync completes unchanged (compat twin) (TRUST-01)
  2. The validator admission growth-bounds and cross-certificate equivocation audit is documented with findings and dispositions against the hardened registry path (TRUST-02)
  3. Public-chain evidence validation fails closed on empty/local chain identifiers and empty source references — adversarial test shows rejection where today's path accepts — and native-mint vs bridge-mint follow explicitly separated per-type consensus rules (MINT-02)
  4. A newly submitted self-signed legacy-v1 mint is rejected (adversarial test), while pre-cutover outputs remain spendable, a fresh node syncs to READY, and a restart recovers — admission gating only, never deserialization gating (compat twin) (MINT-03)
  5. A mint whose token ID, amount, or destination mismatches the burn receipt is rejected even when every RPC endpoint honestly reports the receipt (MINT-04)
**Plans**: TBD

*Planning note:* research flagged for this phase — the MINT-01 minimal-path question (does a certified task-result record already exist on the consensus-certificate path?), grandfather-rule design, and verdict-model (Reject/Defer/Accept) primitive placement. MINT-01's full entitlement binding is mapped to Phase 26 with the grid cluster.

### Phase 22: Wire & Ingestion Hardening (Fork)
**Goal**: The remote-crash class is closed at the fork parse boundaries — every network-provided length prefix is capped before allocation, nesting depth is bounded, and transaction ingestion returns errors instead of crashing on malformed hashes — shipped as the first fork delta under the divergence discipline with proven old/new-peer interop, riding a release pipeline whose artifacts are verified: the REL-02 digest-verification step lands as an early plan in this phase, with or before the first fork-riding change.
**Depends on**: Phase 21 (execution order; no hard technical dependency beyond sequencing)
**Requirements**: WIRE-01, WIRE-02, REL-02
**Success Criteria** (what must be TRUE):
  1. Oversized length prefixes at all three fork readers (uvarint, big-endian, Kademlia) are rejected before allocation with oversized-vs-malformed distinguished as named errors, and nesting depth is capped — adversarial tests exercise each boundary (WIRE-01)
  2. Transaction deserialization on malformed hashes returns an error result rather than a `.value()` crash on network-derived data, and fuzz coverage of the ingestion path runs in a clang CI leg (WIRE-02)
  3. CI fails when a thirdparty/zkLLVM artifact digest mismatches the recorded expectation (GitHub digest API or committed checksum manifest), and workflow actions execute only when pinned by commit SHA — in place with or before this phase's first fork-riding change, so no fork delta rides the pipeline on unverified artifacts (REL-02)
  4. Pre-change and post-change peers interoperate: the mixed-version interop matrix shows large-payload exchange between old and new peers, with the FORK-DELTA manifest updated and the thirdparty superproject bump released through the now-digest-verified pipeline
**Plans**: TBD

*In-phase ordering constraint:* the REL-02 digest-verification CI step (GitHub digest API or committed checksum manifest, plus SHA-pinned actions) is an early plan in this phase, landing with or before the first fork-riding change — fork changes ride the 4-stage release pipeline (fork commit → thirdparty superproject bump → SuperGenius CI download → integration tests) that today consumes unverified thirdparty/zkLLVM artifacts.

### Phase 23: Gossip & DHT Authentication (Fork)
**Goal**: The transport authenticates what it forwards — public-network gossip messages are signature-verified before mesh forwarding and the DHT admits only validated records — at a measured, bounded hot-path cost, so the grid's authenticated claims (Phase 26) ride an authentic transport.
**Depends on**: Phase 22 (same fork release train; divergence discipline and the REL-02-verified artifact pipeline established on the simpler wire delta)
**Requirements**: GOSSIP-01, GOSSIP-02
**Success Criteria** (what must be TRUE):
  1. An unsigned or forged-identity gossip message on a public topic is dropped before mesh forwarding (adversarial test: attacker authority = any mesh peer; signing-key↔`from` identity binding and per-topic policy enforced), and a before/after throughput benchmark quantifies the verification cost (GOSSIP-01)
  2. The DHT stores and serves only validator-passed records under admission/resource limits, and a response nonce cannot answer a request it was not issued for (nonce-poisoning adversarial test) (GOSSIP-02)
  3. A validly-signed message from a key not entitled to a restricted topic is rejected — authentication-without-authorization is its own adversarial test — and pre-upgrade peers remain functional under the decided mixed-version skew policy (compat twin) (GOSSIP-01)
**Plans**: TBD

*Planning note:* research flagged for this phase — the fork gossip API extension shape (signed envelope to the validator hook), per-topic policy table schema, and benchmark methodology.

### Phase 24: Bridge Ingestion Safety
**Goal**: Irreversible native issuance happens only on source-chain finality proven by content — per-chain confirmation depth and operator-distinct corroboration — so reorgs, endpoint compromise, and de-facto single providers cannot create or force a mint.
**Depends on**: Phase 21 (MINT-04's canonical receipt decode is shared); scope is contained to the evidence validator + config
**Requirements**: BRIDGE-01, BRIDGE-02
**Success Criteria** (what must be TRUE):
  1. A burn below the per-chain confirmation-depth/finality policy produces no irreversible issuance; an anvil simulated-reorg test proves a reorged-out burn never mints, a finalized one does, and the depth-tracking state survives restart (BRIDGE-01)
  2. RPC corroboration requires agreeing block/event content across distinct operators — an endpoint config that collapses to a single operator fails validation at startup, and below-quorum corroboration defers (bounded), never mints (BRIDGE-02)
  3. Adversarial: endpoints whose URL-derived success counts agree but whose block/event content disagrees cannot authorize issuance (the fabricated-quorum test) (BRIDGE-02)
**Plans**: TBD

### Phase 25: Verification Spine & Release Gate
**Goal**: The security-fix discipline that Phases 21-24 carried in-phase becomes first-class apparatus, and the release pipeline becomes a gate — the adversarial regression suite exists as documented ctest/CI targets that releases gate on, releases publish only after required tests pass, and fork-delta discipline becomes a standing gate; the two dependency-free findings — the on-chain EVM privileged-role audit and GeniusSDK memory safety — close here as parallel tracks that depend on no other phase and may be pulled forward ad hoc if desired.
**Depends on**: Phases 21-24 (execution order only — nothing in the spine hard-requires the earlier fixes; it formalizes the in-phase test discipline those phases already carried and completes the release-pipeline hardening whose artifact verification began at Phase 22)
**Requirements**: VER-01, REL-01, REL-03, EVM-01, SDK-01
**Success Criteria** (what must be TRUE):
  1. A release tag whose required CI checks have not passed is not published (publish requires the full build+test matrix); deleting and re-pushing a tag cannot replace a published release; the documented hotfix path passes the same required checks (REL-01)
  2. The adversarial regression suite exists as a first-class ctest target set that CI runs and releases gate on, with the convention documented — each security-fix test names the attacker authority required, the expected rejection, and the actual behavior, paired with a compat test proving the pre-upgrade legitimate path survives — consolidating the in-phase pairs Phases 21-24 already produced into the standing suite (VER-01)
  3. A fork change (libp2p/ipfs-pubsub) cannot ride the release pipeline without a FORK-DELTA manifest, and the mixed-version interop-matrix harness exists and runs — the per-delta discipline Phases 22-23 carried becomes a standing pipeline gate (REL-03)
  4. The deployed EVM privileged roles (owner/minter/upgrader) and bridge selectors are audited directly on-chain with a documented findings and remediation list (EVM-01); GeniusSDK's three referenced-but-missing test targets exist and run in CI, and its fixed-buffer copies are length-checked with documented ownership conventions (SDK-01)
**Plans**: TBD

### Phase 26: Processing-Grid Integrity
**Goal**: The grid pays only proven work — queue, result, and payout claims are authenticated; escrow release is entitled by certified results and independently validated as sufficient for the workload; proofs actually verify on the ingress path; and worker results are independently verified before payout on the deterministic/redundant tier (ZK/TEE explicitly deferred to v2).
**Depends on**: Phases 21, 23 (mint/authorization identities from 21; authenticated gossip transport for sealed queue/result messages from 23) plus in-phase siblings — GRID-03 consumes GRID-01/02/04 and MINT-01 within this phase (the dependency sink)
**Requirements**: GRID-01, GRID-02, GRID-03, GRID-04, MINT-01
**Success Criteria** (what must be TRUE):
  1. Queue updates, task-result ownership, payout addresses, and developer splits are authenticated — a self-asserted payout address or a result submitted by a non-owner is rejected (adversarial test) (GRID-01)
  2. Escrow release requires the entitled authority — a certified task result; a signature from the spending key alone at the lock-address exception is rejected (adversarial test) (MINT-01)
  3. Proof verification is enforced on the active ingress path (the `valid_proof = true` bypass is removed) after the staged audit → quarantine → enforce rollout — pre-change stored valid proofs are not tombstoned (compat test), invalid proofs are rejected, and the proof-format-drift inventory is documented (GRID-02)
  4. Worker acceptance independently validates escrow sufficiency against workload dimensions — an overcommittable job is refused before work starts (adversarial test) (GRID-04)
  5. Payout follows independent verification on the deterministic/redundant tier — a divergent or forged worker result earns no payout (redundant-worker disagreement test) (GRID-03)
**Plans**: TBD

*Planning note:* research flagged for this phase — GRID-03 determinism preconditions and redundancy cost model; GRID-02's stored-proof/zkLLVM format inventory and per-element verify budget; payout-record schema fixed at agreement time.

### Phase 27: Network Topology
**Goal**: Mobile/client nodes can actually reach the public network — circuit-relay-v2 is completed limits-first or the relay-through-full-nodes topology is published as an explicit supported contract — decided at a mid-phase gate whose review criterion is the fork-divergence cost of relay completion.
**Depends on**: Phases 22, 23 (parse caps and gossip authentication before opening a relay surface)
**Requirements**: TOPO-01
**Success Criteria** (what must be TRUE):
  1. A decision record documents the mid-phase gate outcome — complete relay limits-first vs. explicit topology contract — with the fork-divergence cost review as the deciding criterion
  2. The chosen topology is proven by test: client/mobile nodes join and transact through the documented connectivity path under CGNAT/symmetric-NAT emulation, with DCUtR best-effort punch behavior measured and documented
  3. Relay exposure is bounded: if relay is completed, reservation ACL and per-peer/IP/duration/data limits are enforced and adversarially tested (a reservation flood is refused); if the contract path is chosen, the published document states relays remain disabled on the stub and why that is safe
**Plans**: TBD

*Planning note:* research flagged for this phase — the complete-vs-topology decision inputs (circuit-relay-v2 spec-conformance cost vs. divergence cost) and the CGNAT emulation harness.

### Phase 28: Worker Profile, Isolation & Mobile Readiness
**Goal**: A minimal worker exists with measured duty-cycle control, the job privacy boundary is honestly defined and enforced at its named scope (no confidentiality claims without TEE), and mobile nodes survive real OS lifecycles within adaptive resource budgets.
**Depends on**: Phase 27 (documented connectivity path); Phase 25 (SDK-01's memory-safe SDK base)
**Requirements**: SDK-02, PRIV-01, MOB-01
**Success Criteria** (what must be TRUE):
  1. A minimal worker profile builds and links without full-node components, and its CPU/GPU duty-cycle controls are measured and documented (SDK-02)
  2. The job privacy boundary is documented — including the explicit no-confidentiality-without-TEE statement — and the scoped controls for job metadata and input access are enforced and tested at that boundary (PRIV-01)
  3. A mobile node adapts connection/data budgets to platform state, honors metered-data rules, and demonstrates OS-termination → relaunch → rejoin recovery (MOB-01)
**Plans**: TBD

### Phase 29: Return-Leg Activation (SGNS→EVM)
**Goal**: The bridge's return leg goes live on the certificate-based attestor path — a certified SGNS-side event produces exactly one EVM-side effect through the new exporter module — activated only on top of closed and proven trust boundaries, because activating it over an unhardened registry would convert a native trust bug into an EVM mint bug.
**Depends on**: Phases 21, 24, 25 (TRUST-01 certificate trust anchor; BRIDGE-01/02 burn-fact quality; EVM-01 audit findings) — strictly last
**Requirements**: RET-01
**Success Criteria** (what must be TRUE):
  1. A certified SGNS-side event produces exactly one EVM effect end-to-end through the new exporter module (`src/bridge_exporter/`) feeding GNUSBridgeAttestor via `evmrelay` (RET-01)
  2. Replay protection holds: re-delivering the same certificate, or reissuing after restart, produces no second EVM effect — nonce/certificate-slot binding proven by adversarial test
  3. The activation checklist is green and recorded: attestor-set rotation drill, rate/size limits, monitoring, and verification of actually-deployed selectors/roles on the claimed mainnet addresses, folding in Phase 25's EVM-01 audit findings
**Plans**: TBD

*Planning note:* research flagged for this phase — exporter module design and the deployed-contract state verification method.

## Progress

**Execution Order (v1.3):** Phases 21 → 29 in strict numeric order (numeric order = execution order; no planned overlap).

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Config-Driven Settings Foundation | v1.0 | 1/1 | Complete | 2026-07-03 |
| 2. Variant Factory + Constructor Reorder | v1.0 | 1/1 | Complete | 2026-07-03 |
| 3. Call-Site Migration + Verification | v1.0 | 3/3 | Complete | 2026-07-03 |
| 8. MultiSig Primitive | v1.1 | 1/1 | Complete | 2026-07-29 |
| 9. SecureCRDT Layer | v1.1 | 2/2 | Complete | 2026-07-29 |
| 10. TrustedPeerRegistry | v1.1 | 2/2 | Complete | 2026-07-24 |
| 11. BurnConfig Quorum Wiring | v1.1 | 2/2 | Complete | 2026-07-27 |
| 12. ValidatorRegistry Migration | v1.1 | 1/1 | Complete | 2026-07-27 |
| 13. Close v1.1 gaps | v1.1 | 29/29 | Complete | 2026-09-01 |
| 14. Account Generation (discarded) | v1.1 | 0/15 | Discarded | — |
| 15. Private Networks | v1.1 | 17/18 | Complete | 2026-10-05 |
| 16. Erasure Primitives & Bookkeeping Hardening | v1.2 (this branch) | 4/4 | Complete | 2026-10-15 |
| 17. Eligibility, Policy & Gating (Dry-Run) | v1.2 (this branch) | 0/6 | Planned (planning complete) | — |
| 18. TxPruner Service & First Real Prune | v1.2 (this branch) | 7/7 | Complete    | 2026-10-23 |
| 19. Pruned-Node Survival & Recovery Hardening | v1.2 (this branch) | 7/7 | Complete    | 2026-10-24 |
| 20. Reclamation Proof & E2E Hardening | v1.2 (this branch) | 2/5 | In Progress|  |
| 21. Consensus Trust & Mint Authorization | v1.3 | 0/? | Not started | — |
| 22. Wire & Ingestion Hardening (Fork) | v1.3 | 0/? | Not started | — |
| 23. Gossip & DHT Authentication (Fork) | v1.3 | 0/? | Not started | — |
| 24. Bridge Ingestion Safety | v1.3 | 0/? | Not started | — |
| 25. Verification Spine & Release Gate | v1.3 | 0/? | Not started | — |
| 26. Processing-Grid Integrity | v1.3 | 0/? | Not started | — |
| 27. Network Topology | v1.3 | 0/? | Not started | — |
| 28. Worker Profile, Isolation & Mobile Readiness | v1.3 | 0/? | Not started | — |
| 29. Return-Leg Activation (SGNS→EVM) | v1.3 | 0/? | Not started | — |

## Backlog

(v1.0 deferrals: PROP-01 NodeType propagation *(partially delivered by develop role gating)*, PROP-02 Archive/Full split *(delivered: participates_in_consensus_)*, HARD-01 pubsub_port numeric, HARD-02 config schema versioning.)
(v2 beyond v1.3, tracked in REQUIREMENTS.md: GRID-V2-01 ZK result verification, GRID-V2-02 TEE attestation, PRIV-V2-01 confidential execution, EVM-V2-01/02 contracts-repo hardening.)

---
*Roadmap last updated: 2026-10-19 — v1.3 roadmap revised after review (execution order now strictly numeric: consensus trust first, REL-02 moved into Phase 22 with or before its first fork-riding change, verification spine at Phase 25; 27 requirements mapped); v1.2 snapshot archived to `.planning/milestones/v1.2-ROADMAP.md` (executes on separate branch); shipped-milestone history and legacy tracks preserved below*
## Track A: EVM Bridge Integration

**Branch:** `evmrelay_integration`

### Completed

| Item | Status | Notes |
|------|--------|-------|
| evmrelay v1 (ChainList, RPC infra, tests) | Done | Submodule at `59d1ed2`, 55+ tests |
| Consensus opaque-payload refactor | Done | `subject_type_hash` dispatch |
| Bridge consensus adapter (now deleted) | Removed | No separate bridge consensus round |
| `MintTransactionV2` RPC cleanup | Done | No RPC members — clean value class |
| `PublicChainInputValidator` RPC verification | Done | `SetRpcEndpoints(chain_id, urls)`, `VerifyPublicChainSmartContract` calls 3+ RPC endpoints |
| `BridgeRpcWatcher` in src/watcher | Done | RPC-based event detection via evmrelay |
| SuperGenius build | Passes | 405 targets, consensus + watcher tests pass |

### Phase 1: Wire RPC Endpoints from evmrelay ChainList

**Goal:** Load chain RPC endpoints from evmrelay's ChainList provider (driven by `chains_config.json`) and configure `PublicChainInputValidator` at startup.

**Status:** Implementation complete — in-review ([#293](https://github.com/GeniusVentures/SuperGenius/issues/293))
**Plan:** [01-PLAN.md](phases/01-rpc-endpoint-wiring/01-PLAN.md)
**Summary:** [01-SUMMARY.md](phases/01-rpc-endpoint-wiring/01-SUMMARY.md)

**Tasks:**

- [x] Load `chains_config.json` to determine supported chains
- [x] For each supported chain, ingest RPC URLs via `eth::rpc::load_chainlist_from_json_text()`
- [x] Filter/deduplicate by chain ID
- [x] Call `PublicChainInputValidator::SetRpcEndpoints(chain_id, urls)` for each chain
- [x] Wire this into the application startup path (GeniusNode or equivalent)

**Success criteria:**

- Each configured chain has at least 3 RPC endpoints available
- Endpoints are loaded from data, not hardcoded
- Build passes

---

### Phase 2: Relayer — Burn Detection → MintFunds

**Goal:** Wire evmrelay burn event detection to `TransactionManager::MintFunds()` via `BridgeRelayer`.

**Architecture:** `BridgeRelayer` takes a shared `EthWatchService` (DI), registers a `BridgeSourceBurned` watch, and calls `MintFunds` when burns are detected.

**Tasks:**

- [x] Create `BridgeRelayer` with DI `EthWatchService`
- [x] Register `BridgeSourceBurned` watch via `eth::cli::event_registry()`
- [x] Extract burn details from decoded ABI values (sender, token_id, amount, srcChainID, tx_hash)
- [x] Call `MintFunds(amount, tx_hash, chain_id, token_id, destination)`
- [x] Unit test for BridgeRelayer
- [ ] Validators independently verify the burn via `PublicChainInputValidator::VerifyPublicChainSmartContract`

**Success criteria:**

- Burn event on EVM chain triggers `MintFunds` via evmrelay signal
- `MintTransactionV2` created with correct chain_id, amount, token_id, burn_tx_hash
- Transaction enters nonce consensus

---

### Phase 3: Burn Deduplication Cache

**Goal:** Prevent double-minting by tracking which burn transaction hashes have already been processed.

**Status:** Complete (2026-05-31, verified 2026-06-09)
**Plans:** 1 plan

Plans:

- [x] 03-01-PLAN.md — Gap closure: slot key collision, fail-closed endpoints, UTXO witness fix, receipt log verification

**Tasks:**

- [x] Define canonical message_id for EVM bridge source events
- [x] Map bridge mints to deterministic consensus slot keys
- [x] Add processing reservation state
- [x] Persist executed bridge message state
- [x] Fix 1: Add burn tx hash to slot key (MintTransactionV2::GetSlotID() line 229-233)
- [x] Fix 2: Fail-closed on missing RPC endpoints (PublicChainInputValidator.cpp:166)
- [x] Fix 3: Disable UTXO witness requirement for bridge mints (RequiresConsensusUTXOData → false)
- [x] Fix 4: Add receipt log verification for bridge contract + topic0 (PublicChainInputValidator.cpp:229-252)

**Success criteria:** All met. Fixes verified present in bridge_phase5 refactored code.

---

### Phase 4: End-to-End Integration Test

**Goal:** Demonstrate the full pipeline: EVM burn → detection → MintTransactionV2 → UTXO consensus → RPC verification → minted tokens.

**Status:** Complete (impl verified 2026-06-09)
**Plans:** 3/3 plans complete

Plans:

- [x] 04-01-PLAN.md — Test infrastructure + positive E2E test (fixture, burn-to-mint pipeline)
- [x] 04-02-PLAN.md — Negative tests (replay rejection, missing endpoints, invalid logs)
- [x] 04-03-PLAN.md — Slot key collision resistance verification

**Tasks:**

- [x] Create test/src/bridge_e2e/ directory with CMakeLists.txt and BridgeE2ETest fixture
- [x] Wire into test/src/CMakeLists.txt via add_subdirectory
- [x] Positive E2E: burn on Sepolia via cast send -> detection -> MintTransactionV2 -> UTXO consensus -> minted tokens
- [x] Negative: replay rejection (same burn tx hash twice -> deduplicated)
- [x] Negative: missing RPC endpoints -> fail-closed (Phase 3 D-03)
- [x] Negative: invalid receipt logs -> rejected (Phase 3 D-05/D-06)
- [x] Slot key: two distinct burns with identical chain/token/amount/dest -> different slot keys

**Success criteria:** All met. E2E test at `test/src/bridge_e2e/bridge_e2e_test.cpp`.

---

### Phase 04.1: Anvil Local Bridge E2E Test (INSERTED)

**Goal:** Self-contained local-Anvil E2E test — forks Sepolia, funds Anvil account #0 via `anvil_impersonateAccount`, sends bridge burn with `cast send`, verifies MintTokens pipeline, tests replay rejection. Also exercises Phase 5's startup catch-up scan (nodes scan RPC for historical burns and auto-mint).

**Depends on:** Phase 4, Phase 5
**Status:** In progress
**Plans:** 1/2 plans complete

Plans:

- [x] 04.1-01-PLAN.md — AnvilProcess helper + BridgeAnvilE2ETest (positive burn-to-mint + replay rejection) + Sepolia-direct fallback
- [ ] 04.1-02-PLAN.md — Startup catch-up scan E2E: nodes start, scan RPC for historical burns via BridgeRelayer watch, auto-mint, verify

---

### Phase 04.2: P2P RLPx Burn Event Gossip (INSERTED)

**Goal:** Move RLPx burn event gossip from deferred to active. Nodes receive real bridge events via devp2p RLPx protocol, watch them stream for 10-20 seconds (1-2s intervals), verify each mints on SuperGenius. Requires real Sepolia PRIVATE_KEY.

**Depends on:** Phase 04.1, Phase 5
**Status:** Planned
**Plans:** 1/1 plans complete

Plans:

- [x] 04.2-01-PLAN.md — Live-Sepolia RLPx E2E: 3 nodes each run EthWatchService in production RLPx mode (kDiscoverFirst), BridgeRelayer auto-mints streamed burns (10 burns at 1-2s cadence), all-3-nodes mint verification

---

### Phase 5: Startup Wiring + Mock RPC Transport

**Goal:** Wire `BridgeRelayer::Start()` and `InitializeRpcEndpoints()` into the node startup path so the burn→MintFunds pipeline actually runs in normal nodes. Add in-process mock RPC transport for Tier 1 verification testing. On startup (especially full/archive nodes), after syncing CRDT data, check for unprocessed bridged transactions that need to be minted.

**Source:** PR #298 Codex review (June 2, 2026) — 3 deferred P1 findings

**Depends on:** Phase 4
**Status:** Complete — PR #309 (bridge_phase5 → develop) in review
**Plans:** 6 plans (5 implementation + 1 test generation)

**Requirements:** REQ-WIRE-01, REQ-WIRE-02, REQ-WIRE-03, REQ-MOCK-01, REQ-MOCK-02, REQ-MOCK-03, REQ-MOCK-04, REQ-UTXO-01, REQ-UTXO-02, REQ-UTXO-03, REQ-CATCH-01, REQ-CATCH-02

**Wave 1** (parallel — independent subsystems):

Plans:

- [x] 05-01-PLAN.md — BridgeRelayer multi-chain Start(): ChainContractPair struct, per-chain watch_id tracking, best-effort registration (D-01, D-21); REQ-WIRE-01, REQ-CATCH-02
- [x] 05-02-PLAN.md — Mock RPC Transport: MockRpcTransport implements JsonRpcTransport, 6 failure modes (D-13), stateful sequences (D-10), config parser (D-08/D-09), behavioral tests; REQ-MOCK-01, REQ-MOCK-02, REQ-MOCK-03
- [x] 05-03-PLAN.md — Transport Factory DI: TransportFactory + SetTransportFactory() in PublicChainInputValidator, replaces hard RpcHttpTransport construction, SGNS_E2E_REAL_RPC=1 env var (D-15); REQ-MOCK-01, REQ-MOCK-03, REQ-MOCK-04
- [x] 05-04-PLAN.md — UTXO Changes: remove 8 foreign-address guards (D-17), add UTXO_RESERVED state (D-18), add UTXOType::UTXO_BRIDGE marker (D-19); REQ-UTXO-01, REQ-UTXO-02, REQ-UTXO-03

**Wave 2** (blocked on Wave 1 completion — depends on 05-01, 05-03, 05-04):

Plans:

- [x] 05-05-PLAN.md — GeniusNode Startup Wiring: InitializeAndStartBridge() async init from INITIALIZING_TRANSACTIONS (D-04), rewritten InitializeRpcEndpoints() from chains_config.json (D-02/D-05), startup catch-up scan (D-20), CWD path fix; REQ-WIRE-02, REQ-WIRE-03, REQ-CATCH-01
- [x] 05-06-SUMMARY.md — Unit Test Generation: 28 GTest tests across 3 modules (UTXOManager, GeniusNode startup, ChainRpcEndpointProvider)

**Architecture note:** See `.planning/notes/rpc-verification-tiers.md`

---

### Phase 05.2: Bridge V2 — X-only compressed encoding: smart contract updated with `bridgeOut(uint256,uint256,uint256,bytes32)` accepting 32-byte X-only compressed SG public key (not an Ethereum address). Event renamed from `BridgeSourceBurned` to `BridgeOutInitiated`. C++ side must decode 32-byte X-only key → decompress to full X+Y → match `GetAddress()`. Versioned catch-up scan handles old topic0 for v1 burns.

Contract:

```solidity
function bridgeOut(uint256 amount, uint256 id, uint256 destChainID, bytes32 sgnsDestination) external {
    address sender = _msgSender();
    require(GNUSNFTFactoryStorage.layout().NFTs[id].nftCreated, "Token not created.");
    require(balanceOf(sender, id) >= amount, "Insufficient tokens.");
    require(destChainID != GNUSControlStorage.layout().chainID, "Cannot bridge to same chain");
    _burn(sender, id, amount);
    emit BridgeOutInitiated(sender, id, amount, GNUSControlStorage.layout().chainID, destChainID, sgnsDestination);
}
``` (INSERTED)

	**Goal:** Decode 32-byte X-only compressed SG public key from BridgeOutInitiated events → decompress to full X+Y key → construct destination matching GetAddress(). Register new event in EventRegistry (kBytes32), retain v1 BridgeSourceBurned (kBytes). BridgeRelayer registers dual watches and dispatches v1/v2 events. Catch-up scan queries both v1 and v2 topic0 hashes.
	**Requirements**: REQ-V2-01, REQ-V2-02, REQ-V2-03, REQ-V2-04, REQ-V2-05, REQ-V2-06, REQ-V2-07, REQ-V2-08, REQ-V2-09
	**Depends on:** Phase 05.1 (Observer pattern, ChainContractPair, IBridgeInitObserver)
	**Plans:** 4/4 plans complete
	
	**Wave 1** (evmrelay foundation — independent):
	Plans:

	- [x] 05.2-01-PLAN.md — EventRegistry v2 registration (D-04, D-05) + DecompressXOnlyPubkey free function (D-07, D-08, D-09, D-10) + 5 secp256k1 decompression tests (REQ-V2-01, REQ-V2-02, REQ-V2-05, REQ-V2-06, REQ-V2-09)
	
	**Wave 2** (BridgeRelayer + GeniusNode — parallel, depends on Wave 1):
	Plans:

	- [x] 05.2-02-PLAN.md — BridgeRelayer::Start() dual-watch v1+v2 (D-14, D-15) + OnWatchEvent() variant dispatch ByteBuffer/Hash256 with v2 decompression (D-06) (REQ-V2-03, REQ-V2-04)
	- [x] 05.2-03-PLAN.md — GeniusNode::PerformStartupCatchupScan() dual topic0 query v1+v2 (D-11, D-12) + v2 X-only decompression before MintFunds (D-13) (REQ-V2-07, REQ-V2-08)
	
	**Wave 3** (test coverage — depends on Waves 1+2):
	Plans:

	- [x] 05.2-04-PLAN.md — EventRegistry v2 tests + BridgeRelayer v2 dispatch/dual-watch tests + Startup catch-up scan v2 topic0 tests (REQ-V2-03, REQ-V2-04, REQ-V2-07, REQ-V2-09)

### Phase 05.1: Refactor: Move RPC endpoint initialization from GeniusNode to ChainRpcEndpointProvider (INSERTED)

**Goal:** Move `GeniusNode::InitializeRpcEndpoints()` (~175 lines of path resolution, JSON parsing, provider construction, validator registration) into `ChainRpcEndpointProvider::Initialize()`. GeniusNode becomes a thin orchestrator: resolve the config path, construct the provider, register observers (BridgeRelayer + self for catch-up scan), and post `Initialize()` to the io_context. Chain IDs come from a new `chain_id` field in `bridge_chains_config.json`, eliminating the hardcoded `kChainNameToId` map.
**Requirements**: None — internal refactor, no formal requirement IDs mapped.
**Depends on:** Phase 5
**Plans:** 3 plans

Plans:

- [ ] 05.1-01-PLAN.md — Define IBridgeInitObserver + re-signature ChainRpcEndpointProvider::Initialize(path, validator, logger); move JSON read, chain_id parse, IInputValidator::Register, and observer notification into the provider (D-01, D-02, D-03, D-04)
- [ ] 05.1-02-PLAN.md — Make BridgeRelayer implement IBridgeInitObserver (OnRpcEndpointsReady delegates to Start); add numeric chain_id to bridge_chains_config.json (D-03, D-04)
- [ ] 05.1-03-PLAN.md — Rewrite GeniusNode as thin orchestrator (resolve path, construct provider, AddObserver relayer+self, post Initialize); remove InitializeRpcEndpoints body, kChainNameToId, bridge_chains_; catch-up scan reads catchup_chains_ via observer callback (D-01, D-02, D-04)

### Phase 6: Network Voting Weight Classes (Tier 2)

**Goal:** Implement slot-based RPC-hash voting for bridge mints (CONTEXT.md D-01..D-10). ConsensusVote carries 3 slot hashes (DIRECT_API + 2 PUBLIC). Cumulative quorum: slot 0 contributes voter reputation x 0.50; slots 1-2 each contribute reputation x 0.25 only for hash groups with >=2 distinct validators. Certificate produced iff the cumulative qualified_sum > 75% of total voter reputation. Reputation = existing ValidatorEntry.weight; Role::FULL promotion via ApplyVoteEffects retained. Tier 1 per-node RPC verification (Phase 5) unchanged.

**Depends on:** Phase 5
**Requirements:** REQ-SLOT-01, REQ-SLOT-02, REQ-SLOT-03, REQ-SLOT-04, REQ-SLOT-05, REQ-SLOT-06, REQ-REPUT-01, REQ-DETERM-01
**Plans:** 3/4 plans executed

**Wave 1** (proto foundation + node self-classification):

Plans:

- [x] 06-01-PLAN.md — Extend ConsensusVote proto with slot_0_hash/slot_1_hash/slot_2_hash (D-01, D-04, D-09) + PublicChainInputValidator hashing accessors + GeniusNode::PopulateVoteSlotHashes wiring (D-10 Tier 1 unchanged) (REQ-SLOT-01)

**Wave 2** (slot tally + dual tally-site routing — depends on Wave 1; touches shared consensus hot paths):

Plans:

- [x] 06-02-PLAN.md — ValidatorRegistry::EvaluateSlotQuorum (D-02 slot 0 50%, D-03 slots 1-2 dedup 25%, D-06 cumulative >75%) + ConsensusManager::EvaluateQuorum dispatcher routing BOTH TallyVotes + HandleVote through one helper (D-05 abstain, D-07 rep=weight, REQ-DETERM-01) (REQ-SLOT-02, REQ-SLOT-03, REQ-SLOT-04, REQ-SLOT-05, REQ-SLOT-06)

**Wave 3** (Role::FULL promotion — depends on Waves 1+2; serialized due to ValidatorRegistry file overlap):

Plans:

- [x] 06-03-PLAN.md — REGULAR->FULL promotion in ApplyVoteEffects via full_promotion_weight_ + penalty gate (D-07 reuse weight, D-08 independence from tally) (REQ-REPUT-01)

**Wave 4** (test coverage + full regression gate — depends on Waves 1+2+3):

Plans:

- [ ] 06-04-PLAN.md — consensus_slot_quorum_test.cpp (golden D-06 example, dedup, both-sites-agree, determinism) + validator_registry_promotion_test.cpp + blocking full ctest -j8 checkpoint per CLAUDE.md shared-library mandate (all REQs)

---

## Track B: Consensus Voting Decentralization

### Overview

A three-phase protocol change to make SuperGenius consensus truly decentralized. Phase 1 embeds full serialized transaction bytes in `NonceSubject` messages so any validator can validate and vote from the proposal alone — no CRDT dependency. Phase 2 hardens security for standalone validators by closing double-spend and nonce-replay detection gaps using the certificate chain. Phase 3 hardens network resilience with message size enforcement, clock-skew tolerance, memory cleanup, and operational metrics.

### Phases

- [x] **Phase 1: Core Embedded-Transaction Validation Path** — Validators validate and vote from embedded tx bytes, no CRDT needed (completed 2026-05-27)
- [ ] **Phase 2: Conflict and Replay Detection Hardening** — Standalone validators detect double-spends and nonce replays via certificate chain
- [ ] **Phase 3: Network Hardening and Operational Readiness** — Robust at scale: size enforcement, clock tolerance, cleanup, metrics

### Phase 1: Core Embedded-Transaction Validation Path

**Goal**: Any validator receiving a `NonceSubject` proposal with embedded transaction data can deserialize the transaction, run all existing validation checks against it, and cast an Approve or Reject vote — without needing the transaction in their local CRDT store.
**Mode**: mvp
**Depends on**: Nothing (first phase)
**Requirements**: PROTO-01, SER-01, DESER-01, BIND-01, SANTZ-01, TRACK-01, VALID-ALL
**Success Criteria** (what must be TRUE):

  1. **Embedding works:** A proposal originator serializes and embeds the full transaction bytes into `NonceSubject` (fields 5 and 6); other validators parse these fields from received protobuf messages without errors.
  2. **Non-CRDT validation works:** A validator that lacks the transaction in its local CRDT store successfully deserializes the embedded data and progresses beyond `Check::Pending` — reaching `Check::Approve` for valid proposals and `Check::Reject` for invalid ones.
  3. **Integrity and binding verified:** The validator rejects proposals where the deserialized transaction's hash does not match `NonceSubject.tx_hash`, or where the reconstructed UTXO commitment from the embedded tx params does not match `subject.utxo_commitment` — preventing commitment-tx binding bypass.
  4. **DoS resistant:** Maliciously oversized or malformed `transaction_data` payloads are safely rejected without crashing, hanging, or leaking resources (sanitization sandwich: size cap → hash verify → bounded parse).
  5. **All checks pass from embedded data:** All 12 existing validation checks (well-formed, authorization, timestamp, replay protection, type rules, witness, input conflict, nonce/address consistency, transaction status, migration eligibility) produce correct results when the transaction is sourced from embedded bytes. Re-proposals (vote bundles) find the temporarily tracked transaction and reach `Check::Approve`, enabling the validator to cast a vote.

**Plans**: 2 plans

**Wave 1**

- [x] 01-01-PLAN.md — Walking Skeleton: proto schema + serialization threading + handler deserialization + temp tracking + all 12 validation checks (PROTO-01, SER-01, DESER-01, TRACK-01, VALID-ALL)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 01-02-PLAN.md — Validation Hardening: sanitization sandwich + commitment-tx binding + witness hardening + tracking lifecycle cleanup (SANTZ-01, BIND-01, TRACK-01)

### Phase 2: Conflict and Replay Detection Hardening

**Goal**: Standalone validators (without CRDT nonce/UTXO state) can reliably detect double-spends against previously certified transactions and reject nonce replays, using certificate chain data that all validators have access to — no CRDT state dependency for security-critical rejection.
**Mode**: mvp
**Depends on**: Phase 1
**Status**: Complete
**Requirements**: CONFLICT-01, NONCE-01
**Success Criteria** (what must be TRUE):

  1. **Double-spend detection without CRDT state:** A standalone validator that receives a proposal spending a UTXO already consumed in a prior certified transaction correctly returns `Check::Reject`, using certificate store cross-referencing instead of local `tx_processed_m` history.
  2. **Nonce replay detection without CRDT state:** A standalone validator receiving a transaction whose nonce was already used in a prior certified transaction correctly rejects it, using either an embedded `confirmed_nonce` field or a lazy certificate-store fallback.
  3. **No regression for full nodes:** Validators with full CRDT state (Genesis, full nodes) continue to detect double-spends and nonce replays with identical accuracy as before — the Phase 2 changes are additive, not substitutive.
  4. **Deterministic across validators:** For the same proposal, a CRDT-full node and a CRDT-less standalone validator produce the same Accept/Reject decision — no divergence due to state availability differences.

**Plans**: 1 plan

**Wave 1**

- [x] 02-01-PLAN.md — Certificate Fallback Deserialization: signature change + certificate fallback path in OnConsensusCertificate + edge case hardening (CONFLICT-01, NONCE-01)

### Phase 3: Network Hardening and Operational Readiness

**Goal**: The protocol is robust at scale — oversized messages are caught before PubSub publish, timestamp validation tolerates distributed clock skew, temporary tracking data is cleaned up, and operational metrics are available for monitoring and debugging.
**Mode**: mvp
**Depends on**: Phase 2
**Status**: Complete
**Requirements**: SIZE-01, TS-01, CLEAN-01, METRICS-01
**Success Criteria** (what must be TRUE):

  1. **Message size enforcement:** Proposals exceeding the configured PubSub message size threshold are rejected at proposal creation time (pre-publish) with a clear error, preventing silent PubSub message drops.
  2. **Clock skew tolerance:** Proposals from validators whose clocks are within a configurable tolerance window (default wider than the current ±5 minutes) are accepted rather than rejected on timestamp grounds alone, enabling geographically distributed validators to participate.
  3. **Tracking cleanup:** Temporary `tx_processed_m` entries from Phase 1's TRACK-01 are removed after the voting lifecycle completes (certificate produced, proposal rejected, or proposal timed out), with no memory leak observed over sustained multi-hour operation.
  4. **Observability:** Operational logs/metrics show standalone validator voting rate, proposal validation success/failure breakdown (by check type), and tracking entry lifecycle events — enabling troubleshooting in production.

**Plans**: 2 plans

**Wave 1**

- [x] 03-01-PLAN.md — Size Enforcement + Timestamp Tolerance + Metrics: pre-publish 64KB gate at SendTransactionItem, configurable DevConfig timestamp tolerance, atomic metrics counters + lifecycle logging + destructor flush (SIZE-01, TS-01, METRICS-01)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — Tracking Entry Cleanup via ProposalCleanupHandler: callback registration on ConsensusManager/Blockchain, FireProposalCleanupCallbacks at timeout callers (NOT certificate path), TransactionManager OnProposalTimeoutCleanup handler transitioning VERIFYING → FAILED (CLEAN-01)

## Design Notes

### Consensus Trust Model for Bridge Mints

Public-chain mint validation differs from internal transfers: a compromised validator can skip RPC verification and vote `Approve` without detection. Reputation-based voting alone is insufficient — a quorum of colluding nodes could mint fake tokens.

**Proposed safeguard:** Require at least **N validators** to independently and verifiably confirm the burn receipt via RPC before the certificate is produced. Each validator's vote should include a commitment to the RPC result (e.g., `sha256(rpc_response)`) so other nodes can detect fabricated votes. This moves trust from "reputation" to "independent verification count."

**Current state:** `VerifyPublicChainSmartContract` queries 3+ RPC endpoints per validator. This protects against RPC endpoint compromise but does not protect against validator compromise. The consensus protocol layer needs an additional quorum rule for bridge mints.

---

## Out of Scope (v1)

| Concern | Disposition |
|---------|-------------|
| Multi-provider quorum (`RpcQuorumClient`, `ProviderVote<T>`) | SuperGenius builds on evmrelay's endpoint pool |
| `SecurityDecision` structured evidence | Deferred — validator uses simple majority |
| P2P RLPx burn event gossip | Deferred — WebSocket/RPC polling for v1 |
| `EthWatchService` integration into watcher | Deferred — uses dedicated BridgeRpcWatcher |
| Burn contract deployment/management | External concern |

### Phase 7: Deferred Validation and Pending Proposal Lifecycle

**Goal**: Proposals that are temporarily unverifiable remain eligible for validation and voting when
their dependencies arrive or transient failures recover, without retaining unbounded state or
misclassifying inconclusive consensus as transaction failure.
**Mode**: mvp
**Depends on**: Phase 3
**Status**: Not started
**Requirements**: PEND-01, PEND-02, PEND-03, PEND-04, PEND-05, PEND-06, PEND-07, TXSTATE-01
**Design note**: [Deferred Consensus Validation](notes/deferred-consensus-validation.md)
**Success Criteria** (what must be TRUE):

  1. **Out-of-order certificate recovery:** When Peer B receives `tx2` before `tx1`'s certificate,
     Peer B keeps `tx2` pending and automatically revalidates it when the certificate arrives. A
     valid `tx2` then receives Peer B's Approval vote without requiring re-proposal.

  2. **Generic deferred outcomes:** Subject handlers can identify explicit dependency keys or request
     bounded scheduled retry for transient failures. Pending remains local and never contributes to
     quorum.

  3. **Bounded lifecycle:** Pending state uses a compile-time three-minute default TTL, supports an
     injected ten-second test TTL, and enforces count and retained-byte limits.

  4. **Complete cleanup:** Certification, terminal rejection, and expiry remove proposal,
     dependency, vote, retry, and temporary transaction state without duplicate callbacks or leaks.

  5. **Correct terminal state:** Locally proven invalid transactions become `FAILED`; proposals that
     reach TTL without a conclusive outcome become `EXPIRED` or `UNCONFIRMED`.

  6. **Retry safety:** Revalidation is idempotent, emits at most one local Approval vote per proposal
     slot, and never double-counts validator weight.

**Plans**: 5 plans
Plans:

**Wave 0**

- [x] 07-01-PLAN.md — Wave 0 focused pending lifecycle test targets

**Wave 1** *(blocked on Wave 0 completion)*

- [x] 07-02-PLAN.md — Structured validation result and local-only Pending contract

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 07-03-PLAN.md — Bounded pending pool, dependency indexes, and cleanup accounting

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 07-04-PLAN.md — Dependency wakeups, scheduled retry, TTL expiry, and retry safety

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 07-05-PLAN.md — TransactionManager pending dependency and UNCONFIRMED expiry semantics

---

## Progress Summary

| Track | Phase | Status | Completed |
|-------|-------|--------|-----------|
| A. EVM Bridge | 1. Wire RPC Endpoints | Complete | 2026-05-27 |
| A. EVM Bridge | 2. Relayer — Burn Detection | Complete | 2026-05-31 |
| A. EVM Bridge | 3. Burn Dedup Cache | Complete | 2026-05-31 |
| A. EVM Bridge | 4. E2E Integration Test | Complete | impl verified 2026-06-09 |
| A. EVM Bridge | 5. Startup Wiring + Mock RPC | In review (PR #309) | 2026-06-04 |
| A. EVM Bridge | 6. Network Voting (Tier 2) | Not started | - |
| B. Consensus | 1. Embedded-Transaction Validation | Complete | 2026-05-27 |
| B. Consensus | 2. Conflict and Replay Hardening | Complete | impl verified 2026-06-09 |
| B. Consensus | 3. Network Hardening | Complete | impl verified 2026-06-09 |
| B. Consensus | 7. Deferred Validation Lifecycle | Not started | - |

---

## Milestone v1.1: Multi-Signature Secure CRDT Storage

**Goal:** Add a decoupled multi-signature primitive and a secure CRDT storage layer so specific CRDT-backed values require quorum signatures to create/update — applied first to a new `TrustedPeerRegistry` and to `BURN_BASIS_POINTS`; `ValidatorRegistry` migrated onto the same interface.

### Phases

- [x] **Phase 8: MultiSig Primitive** - Standalone N-of-M signature/quorum component reusing `ConsensusAuth` primitives (completed 2026-07-23)
- [x] **Phase 9: SecureCRDT Layer** - `ISignedCRDTData` interface + static policy registry + CRDT-transported propose/sign/quorum flow (completed 2026-07-23)
- [x] **Phase 10: TrustedPeerRegistry** - Genesis-seeded, quorum-updatable trusted-peer set built on SecureCRDT (completed 2026-07-24)
- [x] **Phase 11: BurnConfig Quorum Wiring** - `BURN_BASIS_POINTS` becomes a TrustedPeerRegistry-quorum-signed CRDT value, cached in `TransactionManager` (completed 2026-07-27)
- [x] **Phase 12: ValidatorRegistry Migration** - `ValidatorRegistry`'s genesis-path signature verification migrated onto `multisig::VerifyPayloadSignature`, existing behavior/tests preserved (completed 2026-07-27)

**Extended scope (added 2026-08-26) — ELM Bridging, a product-v1.0 pre-ship requirement.** Extends v1.1 beyond its original multisig/CRDT goal per owner directive; the milestone now gates product v1.0.

- [ ] **Phase 13: ELM Job Bridging (SuperGenius)** — GCS creates/funds one normal processing job whose `Task.json_data` carries multiple ELM work items; grid distributes subtasks; fixed $0.0003/hour funding; work-item results via existing mechanism. Corrects issue #369 (scheduler/bidding layer removed). Issue: GeniusVentures/SuperGenius#369
- [ ] **Phase 14: ELM Runtime in SGProcessingManager** — manifest resolution + verification, content-addressed model cache, real LM runtime (tokenizer/prefill/generation/KV-cache/sampling/stop/detokenize/cancel), work-item results. Executes in the GeniusVentures/SGProcessingManager repo. Issue: assigned to itsafuu only

> Phase 15 (ELM Events & Streaming proto) **dropped** 2026-08-26 per owner: streaming, if ever needed, rides the existing gossip-pubsub results channel (seq-numbered JSON events on `Task.results_channel`) — no new protobuf. `SGElmProcessing.proto` is not planned.

## Phase Details

### Phase 8: MultiSig Primitive

**Goal**: A standalone, node-independent component can compute canonical signing-bytes for an arbitrary payload, verify signatures against it, and evaluate N-of-M quorum for a given signer set and threshold.
**Depends on**: Nothing (first phase of this milestone)
**Requirements**: MSIG-01, MSIG-02, MSIG-03
**Success Criteria** (what must be TRUE):

  1. Given a payload, the component produces canonical signing-bytes and verifies a valid signature against them using `ConsensusAuth`'s SHA-256/`VerifySignature` primitives, rejecting invalid/tampered signatures.
  2. Given a signer set and a required threshold (N-of-M, no hardcoded N), the component correctly reports quorum-met/quorum-not-met for varying valid-signature counts, including boundary cases (exactly N, N-1, all M).
  3. The component can be constructed, exercised, and unit-tested with no running node, no CRDT store, and no network dependency.

**Plans**: 1 plan

Plans:

- [x] 08-01-PLAN.md — MultiSig library (VerifyPayloadSignature + EvaluateQuorum) + CMake wiring + tests (MSIG-01, MSIG-02, MSIG-03)

### Phase 9: SecureCRDT Layer

**Goal**: Registered CRDT keys can only be created/updated when accompanied by quorum-verified signatures, using CRDT's own put/filter-callback mechanism as the sole transport — no unsigned or under-signed write is ever applied.
**Depends on**: Phase 8
**Requirements**: SCRDT-01, SCRDT-02, SCRDT-03, SCRDT-04
**Success Criteria** (what must be TRUE):

  1. An `ISignedCRDTData` interface exists; a concrete implementer can supply a payload codec plus `Verify()`/`Apply()`, and the interface compiles/links independent of any specific data type.
  2. A static, code-declared registry maps topic/key patterns to {signer-set source, quorum rule, `ISignedCRDTData` type}, resolvable at startup for a given key.
  3. A propose+sign+quorum sequence for a registered key — driven entirely by CRDT puts and filter callbacks (pending-value entry + signature entries) — results in the value being applied only after quorum is reached, with no new networking/RPC code path introduced.
  4. An unsigned or under-signed write attempt to a registered key is rejected locally (never applied) before quorum is reached, verified by an automated test.

**Plans**: 2 plans

Plans:

- [x] 09-01-PLAN.md — ISignedCRDTData interface + SecureCrdtRegistry static registry + Wave-0 tests (SCRDT-01, SCRDT-02)
- [x] 09-02-PLAN.md — SecureCrdt wrapper (local-write gate + filter registration + read-path quorum re-derivation) + quorum-gate/propose-sign-quorum tests (SCRDT-03, SCRDT-04)

### Phase 10: TrustedPeerRegistry

**Goal**: A new `TrustedPeerRegistry` component maintains a genesis-seeded, quorum-updatable set of trusted peers, implemented entirely via the SecureCRDT abstraction rather than bespoke signature-checking logic.
**Depends on**: Phase 9
**Requirements**: TPR-01, TPR-02, TPR-03
**Success Criteria** (what must be TRUE):

  1. A freshly-initialized genesis node's `TrustedPeerRegistry` contains exactly the initial trusted-peer set hardcoded in genesis config, with no manual bootstrapping step required.
  2. Adding, removing, or replacing a trusted-peer member succeeds only when a configurable N-of-M quorum of signatures from the CURRENT trusted-peer set is presented; a sub-quorum attempt is rejected and the membership set is unchanged.
  3. `TrustedPeerRegistry`'s implementation is a consumer of `ISignedCRDTData`/SecureCRDT (registered via the Phase 9 policy registry) — code inspection confirms no parallel/duplicate signature-verification logic exists outside SecureCRDT.

**Plans**: 2 plans

Plans:

- [x] 10-01-PLAN.md — TrustedPeerRegistry core: TrustedPeerListPayload (ISignedCRDTData) + cache/signer-set-source/Propose/Sign/TryConfirm/SeedGenesis + CMake wiring (TPR-01, TPR-02, TPR-03)
- [x] 10-02-PLAN.md — Genesis ceremony test helper + genesis/quorum tests + sgns_config.json trusted_peers/bootstrapper_node parsing (TPR-01, TPR-02, TPR-03)

### Phase 11: BurnConfig Quorum Wiring

**Goal**: `BURN_BASIS_POINTS` is a live, quorum-signed CRDT value gated by `TrustedPeerRegistry` membership instead of a compile-time constant, and `TransactionManager` reads a cached value refreshed via callback rather than performing a CRDT read on every `PayEscrow` call.
**Depends on**: Phase 10
**Requirements**: BURN-01, BURN-02, BURN-03
**Success Criteria** (what must be TRUE):

  1. `BURN_BASIS_POINTS` is stored and updated as a `TrustedPeerRegistry`-quorum-signed CRDT value (via the Phase 9/10 SecureCRDT machinery), not a hardcoded constant in `TransactionManager.hpp`.
  2. `TransactionManager::PayEscrow` uses a cached in-memory value for the burn rate; no CRDT read occurs on the `PayEscrow` call path, and the cache updates automatically via a CRDT-change callback when a quorum-signed update lands.
  3. A freshly-seeded genesis node burns exactly 1% (`BURN_BASIS_POINTS=100`) on `PayEscrow` by default, matching pre-milestone behavior, until a quorum-signed update changes the value.

**Plans**: 2 plans

Plans:

- [x] 11-01-PLAN.md — Majority-floor quorum validation (D-07) + TrustedPeerRegistry::New breaking-change retrofit + BurnConfigPayload/BurnConfig core (genesis auto-seed, signer-set-source, cache-refresh) + tests (BURN-01)
- [x] 11-02-PLAN.md — TransactionManager cached burn-rate + GeniusNode INITIALIZING_TRANSACTIONS wiring (SecureCrdt/TrustedPeerRegistry/BurnConfig construction) + config fields + CMake linkage (BURN-02, BURN-03)

### Phase 12: ValidatorRegistry Migration

**Goal**: `ValidatorRegistry`'s genesis-path signature verification is migrated from `GeniusAccount::VerifySignature` onto the shared `multisig::VerifyPayloadSignature` primitive (Phase 8), narrowed per 12-CONTEXT.md D-01/D-03 to signature-verification-only reuse — `ValidatorRegistry` does not adopt `ISignedCRDTData`/`SecureCrdt` (its weighted-quorum/certificate machinery is structurally incompatible), with zero regression in existing behavior or tests.
**Depends on**: Phase 8
**Requirements**: MIG-05, MIG-06
**Success Criteria** (what must be TRUE):

  1. `VerifyUpdate`'s genesis-path signature check calls `multisig::VerifyPayloadSignature` instead of `GeniusAccount::VerifySignature`; `blockchain_genesis` links `multisig` directly.
  2. All pre-migration `ValidatorRegistry` unit/integration tests pass unchanged, with no behavioral regression in genesis-signature verification, and the D-05 `multi_account_test` exit gate (5-10 consecutive clean runs) is satisfied.

**Plans:** 1/1 plans complete
Plans:

- [x] 12-01-PLAN.md — Migrate genesis-path signature verification onto multisig::VerifyPayloadSignature, wire CMake link, run D-05 exit gate

### Progress Table (v1.1)

| Phase | Plans Complete | Status | Completed |
|-------|-----------------|--------|-----------|
| 8. MultiSig Primitive | 1/1 | Complete   | 2026-07-23 |
| 9. SecureCRDT Layer | 2/2 | Complete   | 2026-07-23 |
| 10. TrustedPeerRegistry | 2/2 | Complete   | 2026-07-24 |
| 11. BurnConfig Quorum Wiring | 2/2 | Complete   | 2026-07-27 |
| 12. ValidatorRegistry Migration | 1/1 | Complete   | 2026-07-27 |
| 13. ELM Job Bridging (SuperGenius) | 0/? | Not started | — |
| 14. ELM Runtime in SGProcessingManager (cross-repo) | 0/? | Not started | — |

### Phase 13: ELM Job Bridging (SuperGenius)

**Goal**: A GCS instance creates and funds one normal SuperGenius processing job containing one or more ELM work items; the existing grid distributes them as subtasks and results flow back identifying their work item — with no scheduler/bidding/leasing layer added on top.
**Depends on**: Nothing within v1.1 (can proceed in parallel with Phase 14; end-to-end proof needs Phase 14's runtime)
**Requirements**: ELM-01, ELM-02, ELM-03, ELM-08
**Success Criteria** (what must be TRUE):

  1. A job with multiple ELM work items in `Task.json_data` is published and distributed through the existing processing grid with no new task-ownership protocol messages.
  2. Funding is deterministic from the job JSON at the fixed $0.0003/hour rate; no quote/bid/negotiation protocol exists anywhere in the feature.
  3. Every result identifies its ELM work item; GCS aggregates by work-item ID with no SuperGenius-side aggregation.

**Tracking**: GeniusVentures/SuperGenius#369 (corrected scope — see INGEST-CONFLICTS.md auto-resolutions)
**Depends on (cross-repo)**: GeniusVentures/AsyncIOManager#11 — `ipfspubsub://` URL prefix (gossip pubsub channels as FileManager source/destination), assigned itsafuu; results delivery addresses the results channel as `ipfspubsub://results/<task_id>` with the durable artifact via the `ipfs://` saver (CID)
**Plans**: none yet

### Phase 14: ELM Runtime in SGProcessingManager

**Goal**: SGProcessingManager parses multiple ELM work items, resolves and verifies immutable model manifests, maintains a content-addressed model-bundle cache, and executes a real ELM processor end to end.
**Depends on**: Phase 13's job/subtask JSON shape (contract only — development can start in parallel from the manifest spec)
**Requirements**: ELM-04, ELM-05, ELM-06, ELM-07
**Success Criteria** (what must be TRUE):

  1. A node never executes a model whose manifest or artifact verification failed; cached files are verified before reuse.
  2. A node with an empty cache completes an assigned ELM subtask by downloading the model as part of the job; no protocol message carries node model/cache inventory.
  3. A causal-LM work item produces generated text honoring `max_output_tokens`/`temperature`/`top_p`/`seed` and stop conditions, with accurate token counts; cancellation aborts in-flight generation.

**Tracking**: Executes in the GeniusVentures/SGProcessingManager repo — separate issue, assigned to itsafuu only
**Plans**: none yet

---
*Roadmap last updated: 2026-09-07 — v3.0 rebased onto develop (Track A bridge phases + ELM from develop preserved)*
