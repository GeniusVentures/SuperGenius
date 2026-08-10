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

## Cross-Milestone Trends

*(First milestone — trends will accumulate as v1.1+ ship.)*

| Milestone | Phases | Plans | Build round-trips | Inline? | Notes |
|-----------|--------|-------|-------------------|---------|-------|
| v1.0 | 3 | 5 | ~7 | yes (subagents broken) | Config-helper design churn; build-gate caught all defects |
