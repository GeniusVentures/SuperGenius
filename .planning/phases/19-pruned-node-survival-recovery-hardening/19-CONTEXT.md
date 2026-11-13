# Phase 19: Pruned-Node Survival & Recovery Hardening - Context

**Gathered:** 2026-10-23
**Status:** Ready for planning

<domain>
## Phase Boundary

A pruned light node is fully operational: the restart matrix (prune → restart → READY with zero pruned-CID fetches → tip sync → spend/receive → nonce chain correct), the checkpoint-rebuild guard (GATE-05's remaining half — rebuild skips missing refs AND never resurrects spent outputs), the TM-SYNCING wedge fix (folded live-incident todo), the tri-state "history pruned" answer at the tx-lookup surface, and the fresh-node-through-pruned-mesh E2E (two pruned lights + one full server). Plus the per-consumer recovery audit (root jobs, RetryDueFailedRoots, RetryStalledDeltas, work-journal, missing_tx_hashes_, AccountMessenger-after-map-deletion).

Covers GATE-05 (residual), SURV-01, SURV-02, SURV-03. Compaction/measured-bytes is Phase 20.

</domain>

<decisions>
## Implementation Decisions

### Locked carry-forwards (do not re-litigate)
- **D-C1:** GATE-05's BOOT half landed in Phase 18 (bounded recovery, attempt-capped drop, `SetMissingTxMaxAttemptsForTest` seam; regression `MissingCreatingTxDoesNotWedgeBoot`) — this phase owns the CHECKPOINT-REBUILD half only
- **D-C2:** Harnesses exist: the 18-01 two-node real-pubsub Redelivery fixture, the `tx_pruner_test` CRDTFixture family, `TransactionManagerPendingLifecycleTestAccess`, `GlobalDBEraseTest::EraseNodeCollection`
- **D-C3:** The isResolved broadcast guard (18-01) already makes re-broadcast + restart-head-exchange terminal for pruned roots — SC-5's audit checks the OTHER consumers against the same invariant, not re-proving broadcast
- **D-C4:** SC-2's strengthened wording (missing OR unresolvable creating tx; no unbounded re-request loops) is the requirement of record — live-incident-backed
- **D-C5:** Substrate invariants: compose lock, marker batch-join, sweep-as-cycle, seal retention, dev-mint policy, tick gate

### Checkpoint-rebuild guard (discussed — owner decided)
- **D-01:** Rebuild + consumed-crosscheck. On checkpoint mismatch with pruned-below evidence: (a) the clear-and-rebuild replay SKIPS missing/pruned transaction refs (treating them as not-relevant per GATE-05 semantics), and (b) every gap-filled READY UTXO is cross-checked against the durable consumed-outpoint records (`IsOutPointGenuinelyConsumed` semantics) — an outpoint whose consuming record exists durably is NOT re-created. The durable UTXO set is authoritative; rebuild is gap-filling only; the double-spend window closes by construction. Rejected: refuse-and-resync-from-full (needs a peer protocol that doesn't exist and a reachable full node).

### TM-SYNCING wedge fix (discussed — owner decided, folded todo)
- **D-02:** Rollback + bounded exit. (1) Pre-broadcast failure (witness-build/size-check — the error classes that fire BEFORE any CRDT Put or proposal side effect) rolls the expected-next nonce baseline back to the last durable value: nothing was published, so the baseline advance was a promise never kept — the next send reuses the nonce cleanly. (2) Bounded SYNCING exit: if no confirmation advances the nonce within a bounded window (knob, sane default), fall back to the last DURABLE confirmed nonce, log loudly, return to READY — the GATE-05 durable-state-authoritative principle applied to the send side. Fix wiring point: the `ChangeState(SYNCING)` error path at TransactionManager.cpp:~581 + `SyncNonce` (:3487).

### PRUNED answer surface (discussed — owner decided)
- **D-03:** Tri-state at the tx-lookup path: found / not-found / pruned (backed by `LookupPrunedEntry` from 17-05). A distinguishable outcome in the owning module's error set; the example `ds` command (and local query paths) surface "history pruned" distinctly. NO RPC/transport mapping this phase (deferred).

### Pruned-mesh E2E harness (discussed — owner decided)
- **D-04:** Extend the 18-01 two-node Redelivery fixture to THREE real-pubsub GlobalDB nodes: two light peers that prune + one full node that serves. Reuses the proven wiring (per-test temp dirs, real topics, deterministic teardown). A fresh light bootstraps history the lights no longer have; the full node serves it; E2E green = SC-4.

### Claude's Discretion
- The recovery-audit deliverable shape per consumer (targeted test per consumer is the bar; exact harness per consumer follows existing patterns)
- The bounded-SYNCING window knob name/default and where the fallback logs
- Baseline-rollback placement (inline in the error path vs a small helper)

### Folded Todos
- **failed-send-wedges-tm-in-syncing** (P2, `resolves_phase: 19`): fixed by D-02. Close on landing.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Prior phases
- `.planning/phases/18-txpruner-service-first-real-prune/18-CONTEXT.md` + `18-RESEARCH.md` (substrate contracts; GATE-05 boot fix at `InitializeUTXOs`/`InitTransactions`)
- `.planning/phases/18-txpruner-service-first-real-prune/18-01-SUMMARY.md` (the two-node Redelivery fixture D-04 extends)
- `.planning/phases/17-eligibility-policy-gating-dry-run/17-CONTEXT.md` + `17-RESEARCH.md` (marker/lookup contract — D-03's backing)
- `.planning/phases/16-erasure-primitives-bookkeeping-hardening/16-SECURITY.md` + 17-SECURITY.md + 18-REVIEW-FIX.md (audit trail of every residual landing here)

### Incident records
- `.planning/todos/completed/initializeutxos-boot-wedge-on-unresolvable-creating-tx.md` (three live occurrences; boot half fixed in 18 — the checkpoint-rebuild residual is D-01's motivation)
- `.planning/todos/pending/failed-send-wedges-tm-in-syncing.md` (the live incident D-02 fixes)

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — GATE-05 (strengthened), SURV-01..03 (in milestones/v1.2-REQUIREMENTS-wip.md — live-doc note: develop's REQUIREMENTS.md tracks v1.3; read the v1.2 WIP snapshot)
- `.planning/ROADMAP.md` — Phase 19 entry: goal + 5 success criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `InitializeUTXOs`/`InitTransactions` post-18 (bounded recovery — the pattern D-01's rebuild path mirrors)
- `LookupPrunedEntry` tri-state + `/prune/tx/<hash>` markers (17-05) — D-03's backing store, already production-written by 18's erase batch
- `IsOutPointGenuinelyConsumed` + durable consumed records — D-01's crosscheck source
- The Redelivery fixture (18-01, `globaldb_erase_test.cpp`) — D-04 extends to 3 nodes
- `EraseTransactionEntryPruned` (18-03) — the sanctioned driver for pruned-state setup in every 19 test
- `TransactionManagerPendingLifecycleTestAccess` — drives READY/SYNCING states for D-02 tests

### Established Patterns
- GATE-05 fixed-point thinking: durable state authoritative; network waits bounded; unresolvable ⇒ not-relevant with a loud log
- Fresh-DB fixtures, three-part assertions, double-run, mutation checks
- ForTest seams (SetMissingTxMaxAttemptsForTest precedent) for knob injection

### Integration Points
- `InitializeUTXOs` checkpoint-mismatch branch (~TransactionManager.cpp:3240-3260 — the clear-and-rebuild D-01 modifies)
- `SendTransactionItem` error path → `ChangeState(SYNCING)` + `RollbackTransactions` (:575-590) and `SyncNonce` (:3487) — D-02's two halves
- The tx-lookup path(s) backing `ds`/local queries — D-03's surface

</code_context>

<specifics>
## Specific Ideas

- Owner's GATE-05 framing (from the incident): "if it can't find a pruned tx, it has to infer it's not relevant. that is not a failure." — D-01 applies the same sentence to the rebuild path
- D-02's principle: a pre-broadcast failure is LOCAL — the baseline advance was a promise never kept

</specifics>

<deferred>
## Deferred Ideas

- Refuse-and-resync-from-full rebuild — rejected (D-01)
- RPC/API-layer PRUNED mapping — deferred (D-03)
- PRUNED answers for peers' *query* protocols beyond graphsync refusal — 18-01 already covers the graphsync half; richer peer-query semantics later

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window` (P1), `bridge-startup-wiring-mock-rpc` (P1) — bridge scope, untagged

</deferred>

---

*Phase: 19-Pruned-Node Survival & Recovery Hardening*
*Context gathered: 2026-10-23*
