---
gsd_state_version: 1.0
milestone: v3.0
milestone_name: Canonical Burn Finality Rebuild
status: planning
last_updated: "2026-08-20T12:20:48.817Z"
last_activity: 2026-08-20
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# State: SuperGenius — GeniusNode Construction Refactor

**Last updated:** 2026-07-02
**Milestone:** v1.0 — GeniusNode Construction Refactor

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-02)

**Core value:** Constructing a `GeniusNode` must be a single, self-documenting call driven by config files.
**Current focus:** Milestone complete

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-08-20 — Milestone v3.0 started

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

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone
