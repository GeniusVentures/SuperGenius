# Requirements: SuperGenius — v1.3 Security Remediation & Network Readiness

**Defined:** 2026-10-19
**Core Value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart — and nothing an unauthenticated peer sends can change that.

**Evidence base:** `.planning/notes/security-assessment-2026-10-response.md` (25 findings, developer credibility triage). Each requirement carries its finding number(s).
**Driver:** pre-public-network gate. VER-01 is the verification spine for every other requirement.

## v1.3 Requirements

### Verification & Release Engineering

- [ ] **VER-01**: An adversarial regression suite exists as a first-class ctest target set — every security fix ships a test naming the attacker authority required, the expected rejection, and the actual behavior, and the suite gates releases
- [ ] **REL-01**: CI publishes releases only after required tests pass; release tags are immutable; a documented hotfix path exists (#20)
- [ ] **REL-02**: CI verifies thirdparty/zkLLVM artifact digests (GitHub API digest or committed checksum manifest) and pins workflow actions by SHA (#19)
- [ ] **REL-03**: Fork changes (libp2p, ipfs-pubsub) carry a FORK-DELTA manifest and a mixed-version interop matrix before riding the release pipeline

### Consensus Trust

- [ ] **TRUST-01**: Certificate paths loading a validator registry by CID verify the registry's authorization — forged-registry self-certification is rejected — without breaking catch-up sync or historical certificate re-validation (#1)
- [ ] **TRUST-02**: Validator admission growth bounds and cross-certificate equivocation detection are audited, with findings documented (#14)

### Mint & Escrow Authorization

- [ ] **MINT-01**: Escrow release requires entitled authority (certified task result); a signature from the spending key alone at the lock-address exception is rejected (#2)
- [ ] **MINT-02**: Public-chain evidence validation fails closed on empty/local chain identifiers and empty source references; native-mint and bridge-mint follow explicitly separated per-type consensus rules (#3)
- [ ] **MINT-03**: Legacy-v1 mint reception is disabled or mint authority enforced via admission gating — never deserialization gating, so boot/sync/history spends keep working (#4)
- [ ] **MINT-04**: A burn receipt's token ID, burned amount, and destination are bound to the mint's outputs — even fully honest RPC endpoints cannot authorize a mismatched mint (#5)

### Bridge Ingestion Safety

- [ ] **BRIDGE-01**: Irreversible native issuance requires source-chain confirmation depth/finality, proven reorg-safe under an anvil reorg test (#10)
- [ ] **BRIDGE-02**: Multi-endpoint RPC corroboration requires agreeing block/event content across distinct operators — not endpoint-URL-hash success counts (#11)

### Wire & Ingestion Hardening

- [ ] **WIRE-01**: All fork parse boundaries (uvarint, big-endian, Kademlia readers) cap length and nesting depth before allocation (#8)
- [ ] **WIRE-02**: Transaction deserialization returns errors on malformed hashes (no `.value()` on network-derived data), with fuzz coverage of the ingestion path (#9)

### Gossip & DHT Authentication

- [ ] **GOSSIP-01**: Public-network gossip messages are signature-verified before mesh forwarding, with signing-key↔`from` identity binding and per-topic policy; before/after throughput is benchmarked (#7)
- [ ] **GOSSIP-02**: DHT stores and retrieves only validator-passed records with admission/resource limits; nonce responses are bound to outstanding requests (#15)

### Processing-Grid Integrity

- [ ] **GRID-01**: Queue updates, task-result ownership, payout addresses, and developer splits are authenticated — not accepted by timestamp or self-assertion (#13)
- [ ] **GRID-02**: Proof verification is enforced on the active ingress path (the `valid_proof = true` bypass is removed), with a proof-format-drift inventory and staged rollout so stored-proof drift does not tombstone valid transactions (#21)
- [ ] **GRID-03**: Worker results are independently verified before payout via the deterministic/redundant execution tier (redundant-worker agreement); ZK/TEE tiers are deferred to v2 (#6)
- [ ] **GRID-04**: Escrow sufficiency is independently validated against the workload dimensions before worker acceptance (#22)

### Topology · Isolation · Mobile

- [ ] **TOPO-01**: Relay/hole-punch is completed limits-first (circuit-relay-v2 reservation caps, per-peer/IP limits) **or** the relay-through-full-nodes topology is published as an explicit supported contract (#17)
- [ ] **PRIV-01**: The job privacy boundary is explicitly defined (a documented no-confidentiality-without-TEE statement) with scoped controls for job metadata and input access (#23)
- [ ] **MOB-01**: Mobile nodes have adaptive connection/data budgets, metered-data rules, and demonstrated OS-termination recovery (#25)

### SDK · On-Chain Audit · Return Leg

- [ ] **SDK-01**: GeniusSDK fixed-buffer copies are length-checked and memory-safe; ownership conventions are documented; the three referenced-but-missing test targets are implemented and run (#24)
- [ ] **SDK-02**: A minimal worker profile exists with measured CPU/GPU duty-cycle controls (#18)
- [ ] **EVM-01**: Deployed EVM privileged roles (owner/minter/upgrader) and bridge selectors are audited directly on-chain, with results and a remediation list documented (#12)
- [ ] **RET-01**: The SGNS→EVM return leg is activated end-to-end on the certificate-based attestor path (new exporter module feeding GNUSBridgeAttestor) (#16)

## v2 Requirements

Deferred beyond v1.3. Tracked, not in the current roadmap.

### Result Verification (deep tiers)

- **GRID-V2-01**: ZK-proof-based result verification for nondeterministic workloads
- **GRID-V2-02**: TEE-based executor attestation

### Confidentiality

- **PRIV-V2-01**: Confidential job execution against the executor (requires TEE or equivalent)

### Smart-Contract Hardening (contracts repo)

- **EVM-V2-01**: Two-step ownership transfer; admin/upgrader role separation
- **EVM-V2-02**: Holder-allowance requirement for burning held tokens; owner limiter-exception removal

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Smart-contract code changes in the GNUS contracts repo | Separate change stream and release cadence; #12 is covered as an on-chain operational audit only. Hardening tracked as EVM-V2-01/02 |
| Rewriting the consensus architecture | The assessment's defects are boundary/authorization gaps in existing modules, not architecture flaws; the "wrong architecture / 12–18 months" verdict is not accepted as established |
| v1.2 light-node pruning work | Executes on its own branch; this branch's v1.3 planning assumes its existence, not its absence |
| Public-network exposure before the gate | The milestone's own driver: no exposure until VER-01 gates the fixed paths |
| New node roles, consensus redesign, or tokenomics changes | Not required by any of the 25 findings |
| Light nodes serving historical sync to other peers | Carried over as a standing v1.2-era boundary |

## Traceability

Which phases cover which requirements. Filled by roadmap creation (2026-10-19). Phase numbering continues from v1.2 (Phases 16-20, separate branch).

| Requirement | Phase | Status |
|-------------|-------|--------|
| TRUST-01 | Phase 21 | Pending |
| TRUST-02 | Phase 21 | Pending |
| MINT-02 | Phase 21 | Pending |
| MINT-03 | Phase 21 | Pending |
| MINT-04 | Phase 21 | Pending |
| WIRE-01 | Phase 22 | Pending |
| WIRE-02 | Phase 22 | Pending |
| REL-02 | Phase 22 | Pending |
| GOSSIP-01 | Phase 23 | Pending |
| GOSSIP-02 | Phase 23 | Pending |
| BRIDGE-01 | Phase 24 | Pending |
| BRIDGE-02 | Phase 24 | Pending |
| VER-01 | Phase 25 | Pending |
| REL-01 | Phase 25 | Pending |
| REL-03 | Phase 25 | Pending |
| EVM-01 | Phase 25 | Pending |
| SDK-01 | Phase 25 | Pending |
| GRID-01 | Phase 26 | Pending |
| GRID-02 | Phase 26 | Pending |
| GRID-03 | Phase 26 | Pending |
| GRID-04 | Phase 26 | Pending |
| MINT-01 | Phase 26 | Pending |
| TOPO-01 | Phase 27 | Pending |
| SDK-02 | Phase 28 | Pending |
| PRIV-01 | Phase 28 | Pending |
| MOB-01 | Phase 28 | Pending |
| RET-01 | Phase 29 | Pending |

**Coverage:**
- v1.3 requirements: 27 total *(corrected 2026-10-19 during roadmap creation — the previously stated count of 26 undercounted by one; the REQ-ID list above contains 27)*
- Mapped to phases: 27/27 ✓
- Unmapped: 0
- Phase mapping notes: REL-02 rides Phase 22 so its digest-verification CI step lands with or before that phase's first fork-riding change (fork changes ride the 4-stage release pipeline that today consumes unverified thirdparty/zkLLVM artifacts); EVM-01 and SDK-01 ride Phase 25 as dependency-free tracks (no hard dependency on other phases — may be pulled forward ad hoc if desired); MINT-01 is mapped to Phase 26 with the grid cluster because GRID-03 (independent result verification before payout) consumes the certified-result entitlement — Phase 21 planning research may validate an earlier minimal binding, but the requirement's completion gate stays in Phase 26.

---
*Requirements defined: 2026-10-19 after v1.3 milestone research synthesis*
*Traceability filled: 2026-10-19 during v1.3 roadmap creation (Phases 21-29); re-mapped same day after roadmap review (strict numeric execution order, REL-02 → Phase 22)*

---

# Parallel Milestone: v1.2 — Light-node transaction pruning (Phases 16-20)

**Defined:** 2026-10-15
**Core Value:** Light-node disk stays bounded by physically erasing spent transaction history — both the CRDT value and its raw ipfs-lite CID block — beyond a depth/finality window, while the node keeps its unspent UTXO set and remains fully operational.

v1.2 runs on its own branch (`gsd/phase-16-erasure-primitives-bookkeeping-hardening` and successors), in parallel with v1.3 (see v1.3 Out of Scope: "v1.2 light-node pruning work — executes on its own branch"). Its requirements and traceability are tracked here so one planning root shows both live milestones.

## v1.2 Requirements

### Pruning Core

- [ ] **PRUNE-01**: A transaction is erasable only when every output is genuinely consumed (`IsOutPointGenuinelyConsumed`, not synthesized zero-amount tombstones) AND its depth beyond the current DAG head exceeds the configured finality window
- [x] **PRUNE-02**: Erasure physically deletes, in one atomic RocksDB `WriteBatch`, the CRDT set entries (`/crdt/s|t/<key>/<cid>`, `/crdt/k/<key>/v|p`), the raw CID block (existing `removeNode` chain), and the tx→CID map entry — never via the CRDT tombstone path (`DeleteKey`/`DeleteTransaction` are forbidden for pruning)
- [ ] **PRUNE-03**: A delta block is erased only when every element it carries is prune-eligible (parse the block at prune time; deltas are multi-element)
- [x] **PRUNE-04**: The prune cycle is idempotent and crash-consistent — ordered erase flow (value → CID lookup → block-eligibility+delete → map entry) reaches a fixed point across crashes and restarts
- [x] **PRUNE-05**: Re-delivered/resurrected entries (add-wins OR-Set re-merge from lagging peers or restart head exchange) are re-pruned by a periodic sweep, and root processing treats pruned/resolved CIDs as terminal — no re-fetch storm

### Gating & Safety

- [ ] **GATE-01**: Pruning runs only on `NodeType::Light` (fail-closed gate on the enum) and only within the `/bc-<netid>/tx/` prefix allowlist
- [ ] **GATE-02**: Non-transaction data is provably untouched — certificates, SecureCRDT policy values, processing results, DAG heads, and seal markers are byte-identical across a prune run (ctest)
- [x] **GATE-03**: Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator that prevents re-fetch of pruned chains
- [ ] **GATE-04**: Outgoing `previous_hash` resolution survives pruning of the node's own frontier transaction (cert-slot based resolution; certificates are never pruned) — must land before the first real prune
- [x] **GATE-05**: Boot-time UTXO assembly/checkpoint rebuild treats a missing pruned transaction as not relevant — skips it and completes boot without failure, and never resurrects spent outputs
- [ ] **GATE-06**: Pruning config knobs live beside `node_type` in `sgns_config.json`, default off, resolved values INFO-logged

### Pruned-Node Survival

- [ ] **SURV-01**: Restart matrix on a pruned node passes — READY with zero pruned-CID fetch attempts, new-tip sync, spend/receive, and correct outgoing nonce chain
- [ ] **SURV-02**: Local history queries answer "history pruned" (distinct from not-found); peer requests get clean `RS_NOT_FOUND`/`CID_NOT_FOUND` refusal with prompt requester failover — both tested against a pruned node
- [x] **SURV-03**: A fresh light node bootstraps through a mesh whose peers have pruned the history it needs (full node serves) — E2E

### Reclamation & Observability

- [ ] **RECL-01**: Deleted bytes actually leave the disk — periodic light-node-only `CompactRange` (`bottommost_level_compaction = kForce`) scheduled off the hot path
- [ ] **RECL-02**: CI gate proves real reclamation — measured SST/`du` bytes before-vs-after a prune+compaction cycle (not key counts)
- [x] **RECL-03**: Per-cycle observability log — txs examined/erased, bytes reclaimed, watermark advance
- [x] **RECL-04**: Prune status is queryable — watermark and last-cycle stats

## v1.2 Deferred (future milestones)

- **HIST-01**: On-demand historical fetch from full peers (getblockfrompeer analog)
- **HIST-02**: UTXO-snapshot bootstrap for fresh/pruned nodes (assumeutxo analog)
- **HIST-03**: Disk-budget-driven automatic GC mode (IPFS-style watermark)
- **RTN-01**: Retention policy for non-transaction data classes (proof records, registration records — each belongs to its owning module's future decision)
- **RTN-02**: Seal-marker lifecycle/cleanup (markers grow unboundedly; v1.2 keeps them all — monitor and revisit)
- **RTN-03**: Receive-time watermark drop (skip storing pre-watermark deltas at ingress) — needs its own correctness research vs head reachability

## v1.2 Out of Scope

| Feature | Reason |
|---------|--------|
| Pruning on full/archive nodes | Full/archive are the complete-history sync/audit source by design (PROJECT.md key decision) |
| Light nodes serving historical sync to peers | Pruned nodes must not masquerade as history servers (Bitcoin BIP-159 precedent); full nodes serve |
| Pruning non-transaction CID data (processing results, SecureCRDT values, certificates, proofs) | Transactions only this milestone; each other data class belongs to its owning module's future decision |
| Spent-only erasure without the depth/finality window | Spent-ness is mutable CRDT state; DAG conflict resolution may still need recent transactions (stricter than Bitcoin deliberately) |
| Value-only erasure (CRDT entries without raw block deletion) | Reclaims nothing meaningful — raw blocks are the bulk of the footprint |
| CRDT tombstone deletion as the pruning mechanism | `DeleteKey`/`DeleteTransaction` publish network-wide tombstones that would erase history on full/archive nodes too — pruning is a local operation |
| Vendored ipfs-lite-cpp changes (e.g. `removeSeal`) | Zero vendored changes required; seal markers must be retained anyway |
| Populating `spent_epoch`/`spent_by_txid` schema fields | Dead schema — never populated at runtime; window comes from height/priority or certificates |
| New storage dependencies, column-family rework, compaction filters | RocksDB 9.4 + vendored ipfs-lite-cpp already provide the full API surface (STACK.md) |

## v1.2 Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| PRUNE-01 | Phase 17 | Pending |
| PRUNE-02 | Phase 16 | Complete |
| PRUNE-03 | Phase 17 | Pending |
| PRUNE-04 | Phase 18 | Complete |
| PRUNE-05 | Phase 18 | Complete |
| GATE-01 | Phase 17 | Pending |
| GATE-02 | Phase 20 | Pending |
| GATE-03 | Phase 16 | Complete |
| GATE-04 | Phase 17 | Pending |
| GATE-05 | Phase 19 | Complete |
| GATE-06 | Phase 17 | Pending |
| SURV-01 | Phase 19 | Pending |
| SURV-02 | Phase 19 | Pending |
| SURV-03 | Phase 19 | Complete |
| RECL-01 | Phase 20 | Pending |
| RECL-02 | Phase 20 | Pending |
| RECL-03 | Phase 18 | Complete |
| RECL-04 | Phase 18 | Complete |

v2 (future) requirements — no phase mapping by design:

| Requirement | Phase | Status |
|-------------|-------|--------|
| HIST-01 | — (v2) | Deferred |
| HIST-02 | — (v2) | Deferred |
| HIST-03 | — (v2) | Deferred |
| RTN-01 | — (v2) | Deferred |
| RTN-02 | — (v2) | Deferred |
| RTN-03 | — (v2) | Deferred |

**v1.2 coverage:** 18 requirements, mapped 18/18 (Phase 16: 2 · Phase 17: 5 · Phase 18: 4 · Phase 19: 4 · Phase 20: 3). Unmapped: 0.

---
*v1.2 requirements defined: 2026-10-15; traceability filled by v1.2 roadmap creation (phases 16-20). v1.2 section appended 2026-10-20 while rebasing the v1.2 branch onto the v1.3 planning root — both milestones run in parallel.*
*Last updated: 2026-10-16 — Phase 16 complete (PRUNE-02, GATE-03); v2 IDs added to traceability as Deferred*
