# Phase 20: Reclamation Proof & E2E Hardening - Context

**Gathered:** 2026-10-24
**Status:** Ready for planning

<domain>
## Phase Boundary

The milestone's headline claim proven in measured bytes: scheduled light-node-only compaction after real prune cycles; the measured-bytes CI gate (dedicated target, ≥fraction bound); the RECL-03 bytes-reclaimed field completing (deliberately absent until now — Doxygen hook in TxPruner.hpp:116); non-transaction data byte-identical across prune AND compaction; full/archive 100% retention in the same mesh; and the window-edge race fault test (spend during an active cycle never erased, picked up next cycle).

Covers GATE-02, RECL-01, RECL-02. This is v1.2's final phase — after its verification, /gsd-complete-milestone closes the milestone.

</domain>

<decisions>
## Implementation Decisions

### Locked carry-forwards (do not re-litigate)
- **D-C1:** Compaction recipe fully specified (18-STACK research): whole-DB `CompactRange` with `bottommost_level_compaction = kForce`, `allow_write_stall = false`, scheduled off the DAG workers/hot path; ONE shared DB (the two facades are the same handle)
- **D-C2:** `bytes_reclaimed` absent from PruneCycleStats by design (Doxygen points here); RECL-03's log currently states not-measured; RECL-04's status query exists — this phase completes both with real numbers
- **D-C3:** Substrate + survival behavior (16-19) complete: erase flow, compose lock, markers, restart matrix, tri-state, mesh E2E — this phase proves the DISK claim, not new node behavior
- **D-C4:** The full/archive retention and non-tx immutability tests extend 18's byte-identity snapshot patterns (tx_pruner_test / globaldb_erase_test fixtures)
- **D-C5:** Anti-vacuous rule: compaction rewrites files — identity assertions must be taken AFTER compaction, not just after erase

### Compaction scheduling policy (discussed — owner decided)
- **D-01:** Post-cycle + byte threshold. Compact after every real prune cycle that erased ≥1 tx AND whose cumulative erased-bytes watermark crosses a threshold (default ~16MB — no compacting after tiny cycles); cancellable at shutdown (checked before starting; aborted cleanly if already running); never during the settle window. No new timer thread — the compaction runs on the pruner's own post-cycle path (off the DAG workers per D-C1). The cumulative watermark is in-memory only (rebuilds from zero at restart — worst case one extra early compaction; NOT persisted; no /prune/ cursor).

### Measurement & reporting shape (discussed — owner decided)
- **D-02:** `GetApproximateSizes` over the whole DB, taken at cycle start vs after compaction; the delta stamps `PruneStatus.bytes_reclaimed` AT COMPACTION TIME (erase-time stamping would be a lie — deletes are tombstones until compaction). The per-cycle INFO keeps its honest split: at erase it reports the erase counts; the measured reclaimed number is reported (and stored) when compaction completes. The RECL-03 log wording updates accordingly (drop the "not measured until Phase 20" note).

### Measured-bytes CI gate design (discussed — owner decided)
- **D-03:** Dedicated ctest target (NOT in the quick matrix): seed a fixture with many known-size transactions (e.g. 200 × ~10KB), prune them, run ONE compaction, assert the byte drop ≥ a conservative fraction of the erased bytes (≥60%) — a bounded-range assertion immune to RocksDB writer/versioning variance. Generous timeout; runs in the standard full suite; skipped on very slow runners via the existing timeout mechanism. Exact-delta rejected (brittle against metadata overhead).

### Race + immutability harness (discussed — owner decided)
- **D-04:** Seam-held race + post-compaction identity. SC-5 uses the 18-landed cycle interrupt seam to hold the cycle between evaluation and the per-tx erase; the spend (a genuine consumed record) lands on the held tx's outpoint; release; assert (a) the under-lock spent-ness re-verify skips the erase THIS cycle, (b) the NEXT cycle picks it up — deterministic interleaving, debug+release timing legs per SC-5. The immutability ctest (GATE-02) extends 18's byte-identity snapshots to AFTER compaction (certificates, SecureCRDT policy, processing results, heads, seal markers) and the retention ctest (SC-4) runs full/archive nodes in the same mesh across the light's prune+compact cycles.

### Claude's Discretion
- The threshold's exact knob name (`pruning.compact_threshold_mb`?) and default value (~16MB) — config surface per the PruningConfig convention
- Where the compaction call lives (TxPruner post-cycle vs a small GlobalDB surface — the recipe wraps `getDB()`; owner-vended per the one-owner rule)
- The gate target's name and the fixture's exact seeding sizes

### Folded Todos
None — the two ≥0.5 todo matches (bridge-race, nodeconsensustest flake) are pre-existing CI flakes, untagged and unrelated.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Compaction recipe (verified in 18's research)
- `.planning/research/STACK.md` — the CompactRange/DeleteRange/getApproximateSizes mechanics: options, caveats (no automatic compaction on idle DBs; DeleteFilesInRange rejected), telemetry API

### Prior phases
- `.planning/phases/18-txpruner-service-first-real-prune/18-CONTEXT.md` (D-C5: the compaction deferral) + `18-04-SUMMARY.md` (the cycle orchestration the compaction hooks into; the not-measured Doxygen hook)
- `.planning/phases/18-txpruner-service-first-real-prune/18-RESEARCH.md` (composition; the cycle seam interrupt points)
- `.planning/phases/19-pruned-node-survival-recovery-hardening/19-06-SUMMARY.md` (the restart matrix — the retention ctest's mesh shapes; harness lessons)

### Requirements & roadmap
- `.planning/milestones/v1.2-REQUIREMENTS-wip.md` — GATE-02, RECL-01, RECL-02 (RECL-03/04 complete)
- `.planning/ROADMAP.md` — Phase 20 entry: goal + 5 success criteria
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `TxPruner::RunCycle` + `SetCycleInterruptHookForTest` (the D-04 seam — AfterEvaluation/BeforeErase interrupt points already named)
- The GlobalDB `getDB()` accessors both wrappers expose — the CompactRange call site; `GetApproximateSizes` via the same handle
- 18's byte-identity snapshot helpers (tx_pruner_test) — extend to post-compaction for GATE-02
- The full-node fixture role (19-07's mesh) — the retention ctest's baseline holder
- `PruneStatus`/`GetPruneStatus` — gains `bytes_reclaimed` (stamped at compaction per D-02)

### Established Patterns
- PruningConfig::FromJSON — the threshold knob's home (degenerate-zero rejection convention)
- Fresh-DB fixtures, double-run, mutation checks; the anti-vacuous D-C5 rule
- ForTest seams for timing control

### Integration Points
- TxPruner's post-cycle path (between status update and reschedule — the D-01 hook point)
- The RECL-03 INFO line (wording update) and PruneStatus (new field)
- test/src/account/CMakeLists.txt (the dedicated gate target per the copy-block pattern)

</code_context>

<specifics>
## Specific Ideas

- The owner's framing from the milestone definition: "we would want to erase them physically from the disk" — this phase is the proof of that sentence
</specifics>

<deferred>
## Deferred Ideas

- Persistent compaction watermark (in-memory only per D-01 — worst case one early compaction per restart)
- Exact-delta byte assertions (rejected per D-03)
- True concurrent-writer racing (rejected per D-04 — the seam-held interleaving is deterministic)
- Disk-budget auto-GC / watermark-driven GC (v2 HIST-03 territory)
</deferred>

---

*Phase: 20-Reclamation Proof & E2E Hardening*
*Context gathered: 2026-10-24*
