# Retrospective

## Milestone: v1.0 — GeniusNode Construction Refactor

**Shipped:** 2026-07-03
**Phases:** 3 | **Plans:** 5 | **Stats:** 39 files, +2956/−341 LOC

### What Was Built

- Config-driven `auto_dht`/`port_seed` in `InitNetwork` (config wins, safe defaults; `base_port`→`port_seed`)
- `NodeType` enum + case-insensitive `NodeTypeFromString` + `node_type` read; `is_full_node_` derived in a reordered ctor (`std::visit` after `LoadSgnsConfig` — init-order hinge fixed)
- Canonical `New(dev_config, AccountSource)` variant factory; `nullptr`-on-failure preserved
- All ~25 call sites migrated; old factories + old private ctor deleted
- Shared `WriteNetworkConfig`/`WriteSgnsConfig` helpers; full build + CTest green

### What Worked

- **Phased build-green sequencing.** Each phase (and the D-01 "retain old factories until Phase 3" decision) kept the build compilable end-to-end — no broken-build windows across the milestone.
- **The user's build-gate loop.** With the GSD subagent runtime broken (inline execution), the operator running the real CMake/CTest build was the verification. It caught every meaningful defect (see below) that inline self-checks missed.
- **Discuss-phase locking decisions before planning.** The 4 gray areas per phase (variant shapes, node_type parsing, failure semantics, helper design, deletion ordering) resolved upfront kept plans concrete and avoided mid-execution rework on intent.
- **`std::visit` + `if constexpr` visitor** for the `AccountSource` variant — clean, type-safe dispatch over 4 alternatives in the reordered ctor.

### What Was Inefficient

- **Config-helper design churn (truncate → merge → truncate).** I initially built truncate helpers, then over-engineered a merge (read-modify-write via RapidJSON) to "preserve existing keys," then reverted to truncate after the user correctly pushed back that merge was unnecessary (callers should pass correct values; NodeExample should edit the shipped config). One design decision took three rounds — the truncate-vs-merge tradeoff should have been reasoned once, upfront, against the actual callers' needs.
- **Several inline-execution defects the build caught:** wrong `is_processor` values hardcoded at 3 sites; leftover dead `ofstream` config writes after migration; a premature `*/` inside a Doxygen block (`(const char*/std::string_view)`); a missing `INVALID_NODE_TYPE` case in the `-Wswitch` error-category switch; a `std::ofstream` silently failing because per-node directories didn't exist yet. Each was a real bug; collectively they cost ~5 extra build/fix round-trips.
- **Subagent runtime broken the entire milestone.** Every discuss/plan/execute/verify ran inline (orchestrator-as-planner/executor/verifier). This lost the parallel gsd-executor/gsd-verifier/gsd-plan-checker separation that normally catches defects before the operator's build, and concentrated all reasoning in one context (higher mistake risk).

### Patterns Established

- **Migrate-then-delete:** when collapsing an API, migrate all call sites first (build stays green), delete the old API as the final atomic commit.
- **Variant factory + `std::visit` visitor** as the idiom for "one entry point, N source shapes" (replaces overloaded boolean-flag factories).
- **Config-write helpers as `static` methods** on the class that owns the config format (`WriteNetworkConfig`/`WriteSgnsConfig` on `GeniusNode`), truncate-and-rewrite, create-parent-dir, validate inputs.
- **Discuss-phase "defer to a later phase" (D-01 pattern):** when a deletion would break a build window, explicitly defer it and record the deferral as a locked decision so the planner sequences correctly.

### Key Lessons

- **Decide truncate-vs-merge for config writers once, against the real callers.** If no caller needs to preserve existing keys, truncate (simpler). If even one does (e.g. a shipped operator config), either merge or have that caller edit the shipped file directly — don't add merge machinery speculatively.
- **Verify every file:line reference against the actual source before planning,** and re-verify linkage after deletion (the old-ctor deletion temporarily left a mangled duplicate ctor signature caught only by a post-edit read).
- **The build/test gate is non-optional** when executing inline: no amount of static grep-checking substitutes for the real compiler + CTest run.
- **Restore the GSD subagent runtime before the next milestone** (`npx get-shit-done-cc@latest --claude --local`) — the `replacement_seq` DB error forced inline mode the whole time.

### Cost Observations

- **Execution mode:** 100% inline (subagent runtime broken) — no model-mix / parallel-agent data recorded.
- **Rework driver:** ~5 build round-trips in Phase 3 (the largest phase) due to inline-execution defects; Phases 1-2 each took 1-2 round-trips.
- **Notable:** the milestone still completed in a single session despite the broken runtime, but at higher defect/round-trip cost than the multi-agent flow would incur.

---

## Milestone: v3.0 — Canonical Burn Finality Rebuild

**Shipped:** 2026-09-03
**Phases:** 5 (8-12) | **Plans:** 37 | **Stats:** 186 files, +72,648/−1,029 LOC, 290 commits, 14 days

### What Was Built

- Canonical burn-slot identity: every competing Mint proposal for one verified burn resolves to one slot derived from verified facts (chain, token, source tx, amount, destination) — proposer/nonce cannot move it; certificates bind to the exact winning proposal (Phase 8)
- Durable one-vote-per-slot: direct RocksDB active-vote record written before broadcast, exact-vote-only recovery, cleared only on matching durable finality (Phase 9)
- Authoritative publication at `/cert/<canonical-slot-id>` only: deterministic protocol-visible publisher authority, persistence-before-advertisement, lowest-SHA-256 immutable-record convergence for contested slots, recipients consume-only (Phase 10)
- Convergent consumption: CRDT work journal as the sole retry boundary, certificate-first Mint recovery with validated embedded fallback, Mint V2 held VERIFYING until effects + marker persist (Phase 11)
- Real-socket four-peer fault proof: one slot, one certificate, one mint through contention, disorder, publisher loss, restart — exact-once in every run ever recorded (Phase 12, six gap-closure rounds)

### What Worked

- **Evidence discipline prevented mis-repairs.** Twice-reproduced-before-repair and honest no-reroll preservation stopped at least two wrong fixes: 12-15 correctly withheld the WR-02 production change (attribution excluded the hypothesis 4/4), and the blacklist backoff hypothesis was disproven for the cost of a seam instead of a blind "fix". The A/B control-build method cleanly separated crash-freedom from pass-rate flakiness.
- **Fresh verification each gap round.** Re-verifying from source and raw logs (not SUMMARY trust) repeatedly caught what execution self-checks missed — including a plan whose gate assertion could never pass on a green run (ctest `--output-on-failure` suppresses passing output) and an "effective" fix that missed a GlobalDB co-owner (`ValidatorRegistry::db_`).
- **Durable evidence paths.** Moving evidence from `/tmp` into the repo (`round4-traces/`+) after a reboot wiped every round-2/3 log made subsequent rounds possible at all.
- **Developer escalation gates at evidence spend.** Routing go/no-go decisions before burning one-shot no-reroll budgets (12-17, 12-19, 12-22) kept every round's evidence honest and the decisions explicit.

### What Was Inefficient

- **The publisher-observer apparatus accreted into the milestone's biggest time sink.** Built to fix one real contamination incident (mixed-process evidence in 12-06→12-09), it grew to 9 of 13 tests in the fault suite, all mapping to no requirement and no production code — and its flaky child-readiness meta-test became the sole blocker across four gap rounds. Removed by developer directive at close (801 lines). The lesson: process safeguards should be scaffolding, not shipped infrastructure; when a test certifies the test-harness rather than the protocol, it does not belong in the deliverable's gate.
- **Six gap-closure rounds on Phase 12.** Rounds 3-5 each ended honestly (STOP branches, no rerolls) — the discipline was right, but the flake (RestartAtVote route loss) needed the developer's Option A insight (surviving-replica retention + mesh-readiness gating) that the diagnose-repair loop hadn't converged on earlier. Escalating the mechanism analysis to the developer one round earlier would have saved two rounds.
- **Suites depending on undeclared load conditions.** Several flakes were only attributable by re-deriving load/timing context from logs across rounds; recording pre-run load in every log header from the start (the 12-15+ practice) would have shortened attribution.

### Patterns Established

- **Teardown invariant:** in fixtures owning a pubsub host, release every GlobalDB/registry/account co-owner BEFORE `pubsub->Stop()` (asio io_context outlives all I/O objects) — now commented at every site.
- **Run-unique fixture state:** CRDTFixture db paths embed pid + counter and are reaped at construction; stale-state immunity is proven, not assumed.
- **Durable in-repo evidence directories** (`roundN-traces/`) with per-run xunit XML copies and crash-baseline markers for any gated evidence series.
- **Readiness gating for re-publication:** wait for topic-mesh connectivity (`getPeerCount >= 2` on every peer) before re-advertising after restart; create surviving replicas before killing a publisher.

### Key Lessons

- **Scaffolding must be removable.** Any test-only machinery should be deletable in one commit without touching `src/` — the final cleanup (801 lines, zero production diff, suite immediately green) is what the apparatus should have been all along.
- **Attribute before repairing, but time-box attribution.** The twice-reproduced rule is right; the corollary is an explicit escalation point when two rounds of attribution haven't converged — the developer's cross-mechanism insight (mesh race vs DAG route) resolved what the loop couldn't.
- **A gate's pass condition must only contain tests that measure the deliverable.** Meta-tests protecting diagnosis integrity belong before or beside the gate, never inside it.

### Cost Observations

- **Execution mode:** multi-agent GSD runtime restored (executors/verifiers/checkers/code-reviewers as subagents) — contrast with v1.0's forced-inline milestone; planner/checker/verifier separation caught plan-level defects (unsatisfiable assertions, wrong binary paths) before execution burned evidence budgets.
- **Phase 12 dominated cost:** 22 of 37 plans (6 original + 16 gap-closure), driven by intermittent-fault attribution under real-socket conditions.
- **Notable:** the final close (apparatus removal + fresh 3-pass gate + verification + archive) took under an hour once the scope decision was made — decisive scope calls are the cheapest lever available.

---

## Cross-Milestone Trends

| Milestone | Phases | Plans | Build round-trips | Inline? | Notes |
|-----------|--------|-------|-------------------|---------|-------|
| v1.0 | 3 | 5 | ~7 | yes (subagents broken) | Config-helper design churn; build-gate caught all defects |
| v3.0 | 5 | 37 | n/a (subagent executors) | no (multi-agent) | Evidence discipline strong; observer apparatus overgrowth cost 4 gap rounds — removed at close |
