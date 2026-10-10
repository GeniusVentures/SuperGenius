# Synthesized Context (Intel)

<!-- Running notes from DOC-classified sources, keyed by topic, with source attribution.
     Verbatim or near-verbatim preservation; no dedup against other docs (single-doc ingest). -->

## Topic: Corrected ELM architecture (issue #369)

- **Source:** .planning/notes/ELM-bridging-gaps.md — exported ChatGPT analysis, 2026-08-26, created/updated same day. Manifest override resolved DOC type; carries directive scope corrections for issue #369 (open, assigned itsafuu + henriqueaklein).
- Pipeline: OpenAI-compatible client → installed GCS /v1 API → (creates one funded SuperGenius job) → SuperGenius processing grid → (existing queue, ownership, participation logic) → one or more processor nodes → (fetch model if missing, keep cached, execute through SGProcessingManager) → results back to GCS → OpenAI-compatible response.
- The analysis explicitly concedes its own earlier design was wrong: "I added a scheduler and bidding layer on top of SuperGenius even though SuperGenius already is the scheduler. That was the wrong split."
- Existing capabilities the design relies on (per doc, corroborated by `.planning/codebase/ARCHITECTURE.md`): task JSON, result channel, queue ownership requests, escrow path, processing-channel discovery, node-creation competition; `ProcessingCoreImpl` already creates `SGProcessingManager` from `task.json_data()`; `SubTask.subtaskid` already identifies each work unit and results carry the subtask ID.

## Topic: Superseded design (do not synthesize)

- **Source:** .planning/notes/ELM-bridging-gaps.md (retractions)
- Explicitly removed from the #369 feature spec: `NodeElmCapabilities`, ELM inventory advertising, cache-state advertising, `ElmProcessingIntent`, requester-side participant selection, per-node cost quotes, GCS-managed child-job claims, GCS-managed execution leases, price-policy negotiation, resource bidding.
- Replacement statement (doc's words): "A GCS instance creates and funds a normal SuperGenius processing job containing one or more ELM work items. Each work item specifies an ELM type and an immutable model-manifest pointer. SuperGenius's existing processing grid handles worker participation, task ownership and execution. A processor retrieves and verifies the referenced model bundle, uses a local content-addressed cache, executes the work through SGProcessingManager, and publishes the result. Model cache state is not advertised and does not affect eligibility. The existing fixed $.0003 cents/hour processing rate applies."

## Topic: Task/SubTask mapping for multiple ELMs

- **Source:** .planning/notes/ELM-bridging-gaps.md
- SuperGenius Task → SubTask per ELM (planner / critic / verifier example). Each subtask carries or references work_item_id, elm_type, manifest pointer, input pointer, generation settings. SuperGenius distributes subtasks; GCS collects results by work-item ID and performs higher-level aggregation.

## Topic: Model fetch/cache flow

- **Source:** .planning/notes/ELM-bridging-gaps.md
- Check cache by manifest hash → found: use cached bundle; missing: fetch and verify bundle → execute model → retain in cache. Cache path pattern `cache/<model-manifest-hash>/`.

## Topic: Scope-relevant repository facts (from existing planning)

- **Source:** .planning/SUBREPOS.md; .planning/codebase/STRUCTURE.md; .planning/codebase/ARCHITECTURE.md
- `SGProcessingManager/` is a git submodule (GeniusVentures/SGProcessingManager), described as the MNN-based ML inference engine; `ProcessingCore` interface implemented by `SGProcessingManager` in `src/processing/impl/processing_core_impl.cpp`. This grounds D-ELM-05 and the work-split directive (D-ELM-08): the execution work lives in a separate repo/submodule with a different assignee.

## Topic: Milestone placement facts (merge mode)

- **Source:** .planning/ROADMAP.md; .planning/MILESTONES.md; .planning/STATE.md; .planning/PROJECT.md
- Internal GSD milestone "v1.0 GeniusNode Construction Refactor" shipped 2026-07-03 (ROADMAP.md, MILESTONES.md). Current milestone per PROJECT.md/STATE.md is v1.1 Multi-Signature Secure CRDT Storage; ROADMAP.md shows v1.1 phases 8-12 complete (2026-07-23..27); MILESTONES.md records v1.1 "Shipped 2026-07-29" with only phases 08-09 and references a "v2.0" roadmap as then-current. Product itself has not shipped. No existing locked decision anywhere addresses ELM/#369/advertising/bidding (verified by grep across EXISTING_CONTEXT).
