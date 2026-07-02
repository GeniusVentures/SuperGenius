---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: planning
last_updated: "2026-07-02T20:33:33.539Z"
progress:
  total_phases: 3
  completed_phases: 1
  total_plans: 1
  completed_plans: 1
  percent: 33
---

# State: SuperGenius — GeniusNode Construction Refactor

**Last updated:** 2026-07-02
**Milestone:** v1.0 — GeniusNode Construction Refactor

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-02)

**Core value:** Constructing a `GeniusNode` must be a single, self-documenting call driven by config files.
**Current focus:** Phase 2 — variant factory + constructor reorder

## Current Position

Phase: 01 (config-driven-settings-foundation) — EXECUTING
Plan: Not started
**Phase:** 2 of 3 (variant factory + constructor reorder)
**Status:** Ready to plan
**Context:** ✓ gathered (01-CONTEXT.md)
**Plans:** 0/0 (ready for planning)
**Progress:** 0%
**Stopped at:** Phase 2 context gathered
**Resume file:** .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 1 | Config-Driven Settings Foundation | ○ not started | CFG-01, CFG-02, CFG-04 |
| 2 | Variant Factory + Constructor Reorder | ○ blocked by 1 | INTF-01..04, CFG-03 |
| 3 | Call-Site Migration + Verification | ○ blocked by 2 | MIG-01..04 |

## Key Decisions

- `node_type` → `sgns_config.json`; `autodht`+`base_port` → `network_config.json` (deployment-time, not per-call)
- `is_full_node_` stays a derived bool at the GeniusNode boundary; NodeType enum NOT propagated downstream this milestone
- Single `New(dev_config, AccountSource)` with `std::variant`; no compat shim; all 18 call sites migrated
- Account creation moves INTO the constructor (after `LoadSgnsConfig`) to resolve init-order chicken-and-egg

## Notes

- Research produced inline (subagent runtime returned schema error `no such column: replacement_seq` on all spawns); 5 docs in `.planning/research/`.
- Brownfield codebase map exists at `.planning/codebase/` (7 docs).
- Strictly sequential phases (1 → 2 → 3); later phases won't compile until earlier ones land.
