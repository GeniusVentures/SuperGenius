---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: GeniusNode Construction Refactor
status: "Phase 04.1 shipped — PR #337"
last_updated: "2026-07-14T03:58:44.340Z"
last_activity: 2026-07-13
progress:
  total_phases: 19
  completed_phases: 15
  total_plans: 40
  completed_plans: 41
  percent: 79
---

# State: SuperGenius — GeniusNode Construction Refactor

**Last updated:** 2026-07-02
**Milestone:** v1.0 — GeniusNode Construction Refactor

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-02)

**Core value:** Constructing a `GeniusNode` must be a single, self-documenting call driven by config files.
**Current focus:** Phase 04.1 — anvil-local-bridge-e2e-test-use-tokencontracts-gnus-ai-env-t

## Current Position

Phase: 04.1 (anvil-local-bridge-e2e-test-use-tokencontracts-gnus-ai-env-t) — EXECUTING
Plan: 2 of 3
Status: Phase 04.1 shipped — PR #337
Last activity: 2026-07-13

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
- Phase 04.1 Plan 03: D-19/D-20 swapped scan_depth (10000) -> start_block (0=genesis) in BridgeCatchupWatcher::Config; D-23 added exponential-backoff retry to RpcHttpTransport::call(); D-03 corrected Sepolia fallback URL to sepolia.drpc.org

## Notes

- Research produced inline (subagent runtime returned schema error `no such column: replacement_seq` on all spawns); 5 docs in `.planning/research/`.
- Brownfield codebase map exists at `.planning/codebase/` (7 docs).
- Strictly sequential phases (1 → 2 → 3); later phases won't compile until earlier ones land.

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone
