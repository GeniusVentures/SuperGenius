# Phase 18: TxPruner Service & First Real Prune - Context

**Gathered:** 2026-10-21
**Status:** Ready for planning

<domain>
## Phase Boundary

The first phase that ERASES: the `TxPruner` service composing the Phase-16 substrate and Phase-17 evaluator into an idempotent, crash-consistent erase cycle (the ordered four-step flow + the `/prune/` marker write joining the batch); the re-delivery guard making pruned roots terminal; the net-scoped dev-mint policy enabling burn-less mints through FULL consensus on designated nets (staging + local dev) so real prunable history exists; `GeniusNode` wiring behind default-off config; per-cycle observability + queryable status. Also closes the two deferral debts (WR-02 critical section, concurrency placement) and the folded MintFunds burn-UTXO leak.

Covers requirements PRUNE-04, PRUNE-05, RECL-03, RECL-04. Survival testing, checkpoint guard, PRUNED answer surfacing are Phase 19; compaction + measured-bytes proof are Phase 20.

</domain>

<decisions>
## Implementation Decisions

### Locked carry-forwards from Phases 16/17 (do not re-litigate)
- **D-C1:** Substrate as landed: `GlobalDB::EraseEntry` four-step ordered flow (value → CID lookup → block-eligibility+delete → map entry), `PruneEligibility` spend-recency evaluator, block gate, marker contract (`/prune/tx/<hash>`, tri-state lookup, `WritePrunedEntryForTest`) — the marker WRITE joins this phase's erase batch (D-04 of 17-CONTEXT)
- **D-C2:** Tick gate: Light ∧ `State::READY` ∧ empty `missing_tx_hashes_` ∧ settle window; config default-off; evaluator runs on committed state only, never from CRDT callbacks
- **D-C3:** Zero-tombstone / seal-retention / one-batch discipline — gate-enforced (EraseSurfaceGateTest); `dry_run: true` keeps the Phase-17 observe-only behavior, `dry_run: false` arms the real erase
- **D-C4:** Generation discipline: per-tick service snapshot (SnapshotAccountServices), generation tag on summaries, no cached evaluator across account switches

### Erase-compose locking (discussed — owner decided)
- **D-01:** Mutex-serialized compose. The erase path holds the existing `CrdtSet` recursive `mutex_` across guard-check → staging → commit (the same discipline `PutElems` already uses across build+commit), plus a spent-ness re-verification under the UTXO lock immediately before staging (guards against a spend revert between evaluation and erase). The evaluator stays lock-free read-only; per-tick bounded work (D-04) keeps the hold short. This CLOSES the WR-02 check-then-act window by construction. Rejected alternatives: optimistic epoch checks (subtle abort-retry correctness), CRDT-worker-affine scheduling (pins the single DAG worker — the archive-replication-flake hazard).

### Re-delivery guard (discussed — owner challenged, verified in source, locked)
- **D-02:** Seal-based dedup at the broadcast entry. Verified in source: today's re-delivery dedup is `HasBlock` (block existence — `crdt_datastore.cpp:694-701`); pruning deletes the block so the dedup evaporates and `EnqueueRootCID` would re-fetch the deleted root. The fix consults the PERSISTENT seal state (owner's insight: `markResolved` outcome survives deletion): after a `HasBlock` miss, `isResolved(cid) == true` ⇒ processed-and-pruned ⇒ skip. **No new durable state** — the never-erased seal markers are the dedup of record. The SC-3 test (re-broadcast pruned root → zero fetch attempts) proves it.
- **D-02b:** The prune cycle IS the sweep. Each tick re-evaluates everything that exists; add-wins-resurrected entries are still spent + still old → re-erased. Idempotence is the sweep; no separate sweep service.

### Net-scoped dev-mint policy (discussed — owner corrected the framing; locked)
- **D-03:** Burn-less mints go through FULL consensus (proposal → quorum → certificate) — the ctest model, replicated for the live node. Permitted ONLY when the node's net is designated (staging 333 + local dev nets); every validator REFUSES to certify unbacked mints on all other nets — test and main nets can never certify them regardless of proposer. This simultaneously: (a) creates real prunable history for the live grow-then-prune demo; (b) RESOLVES the witness-bypass-hardening todo by making the currently-global ctest allowance explicitly net-scoped instead of bleeding into every net and the released SDK; (c) is NOT a bypass-consensus test-seam (owner's correction: ctest uses real consensus — only the burn requirement is waived). Research MUST trace the exact ctest mint path — how fixtures pass the `BuildUTXOWitness` producer-transaction gate and validator witness checks — and replicate those semantics under the net-scoped policy; the researcher also decides the designated-net representation (config list vs constant) and the enforcement points (witness bypass condition, SendTransactionItem witness requirement, validator type-rules).

### First-prune backfill (discussed — owner decided)
- **D-04:** Bounded K per tick (config knob, e.g. `pruning.max_erases_per_cycle`, sane default ~100). NO cursor: the evaluator enumerates EXISTING transactions and erased ones vanish from enumeration — the backlog self-marks; each tick takes the first K eligible until drained; restart-safe by construction. A persistent `/prune/` cursor is built ONLY if the prefix scan measurably outcosts the erases (measure first).

### Claude's Discretion
- TxPruner class placement/shape (service in `src/transaction/` per the milestone research's owning-module map)
- Sweep-vs-tick logging detail; the queryable status surface shape (RECL-04 — local API vs simple getter; no RPC surface this phase)
- Exact config key names beyond the locked ones (`pruning.*` namespace, `max_erases_per_cycle`)

### Folded Todos
- **mintfunds-leaks-burn-utxo-on-consensus-reject** (P1, `resolves_phase: 18`): wire reservation/UTXO cleanup to the FAILED terminal transition (not just the exception path), mirroring the D-18 rollback contract. Lands as a plan in this phase.
- **genius-chain-witness-bypass-hardening** (P1): RESOLVED BY DESIGN here — D-03's net-scoping replaces the global bypass. Update the todo to `resolves_phase: 18` and close it with the policy's landing.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Prior phases (the substrate + its contracts)
- `.planning/phases/16-erasure-primitives-bookkeeping-hardening/16-CONTEXT.md` + `16-SECURITY.md` (erase API shapes, guards, accepted residuals incl. WR-02 window)
- `.planning/phases/17-eligibility-policy-gating-dry-run/17-CONTEXT.md` + `17-SECURITY.md` (evaluator contracts, marker design D-04, config surface, deferrals landing here)
- `.planning/phases/17-eligibility-policy-gating-dry-run/17-RESEARCH.md` (evaluator inputs, cert-slot ladder, config conventions)

### Milestone research
- `.planning/research/SUMMARY.md` — Phase 18 implications (erase flow order, re-delivery guard, tick gating, backfill note); open questions on guard placement are now DECIDED (D-02) — do not re-open
- `.planning/research/PITFALLS.md` — fetch-resurrect loop (P1/3/6), collect-then-delete, batch caps, never-from-callbacks

### Live-incident record (2026-10-20 staging session)
- `.planning/notes/phase18-live-demo-seeding.md` — the owner's live-demo expectation (superseded in shape by D-03: consensus-path policy, not a test-seam)
- `.planning/todos/pending/genius-chain-witness-bypass-hardening.md` — severity analysis incl. the three mint gates (witness-build producer, consensus RPC evidence, remote type-rules); D-03 resolves it
- `.planning/todos/pending/mintfunds-leaks-burn-utxo-on-consensus-reject.md` — folded work

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — PRUNE-04, PRUNE-05, RECL-03, RECL-04
- `.planning/ROADMAP.md` — Phase 18 entry: goal + 5 success criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `GlobalDB::EraseEntry` + `SetEraseInterruptHookForTest` — the erase compose D-01 wraps with the mutex discipline; the seam extends for crash tests at the service level
- `PruneEligibility::Evaluate()` + `BlockGateResult` — the per-tick input; `PruningConfig` gains `max_erases_per_cycle` + the D-03 policy fields
- `RunPruneDryRunCycle` (GeniusNode.cpp:~4860-5020) — the timer/snapshot/generation-tag/guarded-summary pattern the real cycle extends; `dry_run=false` swaps evaluation-only for evaluate→erase
- `WritePrunedEntryForTest` + `BuildPruneMarkerKey`/`LookupPrunedEntry` — the marker write moves INTO the erase batch (production path now has exactly one sanctioned PutRaw — the EraseSurfaceGate containment gate must be updated accordingly)
- ctest fixtures: `CertificateFallbackTest` full-consensus mints (the D-03 template — trace how they pass `BuildUTXOWitness` and validator checks); `GlobalDBEraseTest::EraseNodeCollection` for the service tests

### Established Patterns
- `HandleCIDBroadcast` dedup chain (HasBlock → cache → pending/active → enqueue) — D-02 inserts the isResolved check after the HasBlock miss
- gc_timer_ weak-self asio pattern (already hosting the dry-run timer)
- Fresh-DB fixtures, three-part assertions, double-run discipline, mutation checks

### Integration Points
- `CrdtSet::mutex_` (recursive — held across build+commit by PutElems; D-01 extends to erase composes)
- `UTXOManager` locks (utxos_mutex_) for spent-ness re-verification
- `HandleCIDBroadcast` (`crdt_datastore.cpp:677-771`) — the D-02 guard site
- `PublicChainInputValidator::GatherVerificationEvidence` bypass (:326-334) — becomes net-scoped under D-03
- `sgns_config.json` pruning block (GeniusNode.cpp:473-495) — new knobs

</code_context>

<specifics>
## Specific Ideas

- Owner's exact framing of D-03: "The ctest does use consensus, that's the point of it. The only difference is that Mints don't require a Burn on ctest… I want to basically have that only on staging net probably and not being certified on test and main nets."
- Owner's re-delivery insight: "markResolved gets called for a head cid and I don't think deletion reverts that" — true; the missing piece is only that the broadcast dedup consulted block existence, not the seal

</specifics>

<deferred>
## Deferred Ideas

- Durable pruned-CID record set — rejected (D-02: seals provide the state)
- Optimistic epoch locking / CRDT-worker-affine scheduling — rejected (D-01)
- Persistent backfill cursor — deferred until scans measure hot (D-04)
- RPC-facing prune-status surface — local queryable status only this phase (RECL-04); operator tooling later
- Bytes-reclaimed reporting — completes in Phase 20 with compaction (by design)

### Reviewed Todos (not folded)
- `initializeutxos-boot-wedge` (P1), `failed-send-wedges-tm-in-syncing` (P2) — tagged Phase 19; `bridge-*` todos — untagged bridge scope

</deferred>

---

*Phase: 18-TxPruner Service & First Real Prune*
*Context gathered: 2026-10-21*
