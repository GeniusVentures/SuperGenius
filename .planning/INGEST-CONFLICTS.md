## Conflict Detection Report

Mode: merge (1 doc ingested: .planning/notes/ELM-bridging-gaps.md — DOC, medium confidence, manifest override)
Precedence applied: ADR > SPEC > PRD > DOC (no per-doc override). No ADRs/SPECs/PRDs in ingest set; no LOCKED decisions in ingest or in existing context for this scope.
Cycle detection: acyclic (single-node ref graph; cross_refs "issue #369" and "SGElmProcessing.proto" point outside the ingest set — no intra-set edges, depth 1).

### BLOCKERS (0)

None. Verified no LOCKED-vs-LOCKED contradiction and no ingest-vs-existing-locked-decision collision: grep across .planning/PROJECT.md, .planning/REQUIREMENTS.md, .planning/ROADMAP.md, .planning/MILESTONES.md, .planning/milestones/, .planning/phases/ finds no locked decision mentioning ELM, #369, advertising, bidding, or NodeElmCapabilities.

### WARNINGS (2)

[WARNING] Competing placement variants for the v1.0 milestone track (REQ-ELM-V1-SHIP / D-ELM-07)
  Found: Owner directive (ingest orchestration context) requires the ELM feature to "land in the v1.0 milestone track, not backlog" — meaning the product v1.0 pre-ship release (product not yet shipped).
  Expected: .planning/ROADMAP.md:5 and .planning/MILESTONES.md record internal milestone "v1.0 GeniusNode Construction Refactor" as SHIPPED 2026-07-03; .planning/PROJECT.md and .planning/STATE.md show current milestone v1.1 (Multi-Signature Secure CRDT Storage). The name "v1.0" is already bound to a closed internal milestone.
  Impact: Synthesis cannot create/choose the target milestone entity without losing intent — inserting into the shipped internal v1.0 is impossible, and defaulting to backlog would violate the owner directive.
  → User/roadmapper must define (or confirm) a product-v1.0 pre-ship track (e.g., "v1.0 Product Release") and route REQ-ELM-* there. Do not resolve by silently renumbering existing milestones.
  source: ingest orchestration context; .planning/ROADMAP.md; .planning/MILESTONES.md; .planning/STATE.md; .planning/PROJECT.md

[WARNING] Competing variants for the fixed processing rate units (C-ELM-06, REQ-ELM-FUNDING)
  Found: .planning/notes/ELM-bridging-gaps.md states the built-in rate as "$.0003 cents/hour" (three occurrences) in the same document where it writes "$0.0003 per hour" and the replacement #369 text says "$.0003 cents/hour"; ingest orchestration context states "Fixed $0.0003/hour funding".
  Expected: One canonical rate. Read literally, "0.0003 cents/hour" = $0.000003/hour — a 1000x difference from "$0.0003/hour".
  Impact: Funding math (funded hours × rate) and any escrow/accounting tests depend on the exact value; synthesis cannot pick without guessing the author's intent.
  → Confirm the canonical rate (presumed variant per orchestration context: $0.0003/hour) before REQUIREMENTS/ROADMAP are finalized.
  source: .planning/notes/ELM-bridging-gaps.md; ingest orchestration context

### INFO (4)

[INFO] Ingest doc's own retraction honored; superseded design excluded
  Note: .planning/notes/ELM-bridging-gaps.md explicitly retracts its earlier scheduler/bidding layer ("I added a scheduler and bidding layer on top of SuperGenius even though SuperGenius already is the scheduler. That was the wrong split."). The corrected architecture (D-ELM-01..06) is the only variant synthesized; the retracted NodeElmCapabilities/advertising/intent/bidding/quotes/claims/leases design appears nowhere in intel except the "Superseded design (do not synthesize)" context topic. No existing planning doc encodes the retracted design, so nothing was overridden.
  source: .planning/notes/ELM-bridging-gaps.md

[INFO] Pre-existing planning drift observed (not caused by ingest; relevant to the placement WARNING)
  Note: .planning/MILESTONES.md says v1.1 shipped 2026-07-29 with phases 08-09 only and references a "v2.0" roadmap as then-current in PROJECT.md/STATE.md; .planning/ROADMAP.md shows v1.1 phases 8-12 all complete (2026-07-23..27); .planning/STATE.md says Phase 11 executing at 60% (2026-07-24); .planning/PROJECT.md shows all v1.1 requirements Active/Pending. These four files disagree on current milestone state. Recorded so the roadmapper reconciles them when defining the product-v1.0 track.
  source: .planning/MILESTONES.md; .planning/ROADMAP.md; .planning/STATE.md; .planning/PROJECT.md

[INFO] DOC-classified source carrying directive content handled per manifest override
  Note: .planning/intel/classifications/ELM-bridging-gaps-2c574375.json (DOC, medium confidence, manifest_override: true) directs that scope-removal and redirection directives be preserved as authoritative context despite DOC-level precedence. Handled by extracting them into intel/decisions.md, intel/requirements.md, intel/constraints.md — every entry marked non-locked at DOC precedence with source attribution. Nothing was auto-resolved against a higher-precedence source (none exists in this ingest), and no existing decision was overwritten.
  source: .planning/intel/classifications/ELM-bridging-gaps-2c574375.json

[INFO] SGProcessingManager work-split captured, not merged
  Note: Orchestrator directive: SGProcessingManager execution work (D-ELM-05 / REQ-ELM-RUNTIME) may become a separate issue/phase with a different assignee (issue #369 is assigned itsafuu + henriqueaklein; SGProcessingManager is a separate submodule per .planning/SUBREPOS.md). Preserved as D-ELM-08 and a routing note on REQ-ELM-RUNTIME — two routable work packages within the same v1.0 track, not one merged scope.
  source: ingest orchestration context; .planning/SUBREPOS.md; .planning/codebase/STRUCTURE.md

---

## Resolutions (owner, 2026-08-26)

- **Rate-units variant**: resolved → **$0.0003/hour** (doc's inline heading form). Requirements use ELM-08 with this value.
- **v1.0-track placement variant**: resolved → **extend the active v1.1 milestone** with Phases 13–15; ROADMAP notes the milestone now gates product v1.0 (pre-ship). Internal shipped "v1.0 GeniusNode" milestone is untouched (historical).
