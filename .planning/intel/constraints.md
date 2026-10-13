# Synthesized Constraints (Intel)

<!-- Types: api-contract | schema | nfr | protocol. Source: .planning/notes/ELM-bridging-gaps.md
     (DOC precedence, non-locked) unless noted. -->

## C-ELM-01 — GCS must not schedule (protocol boundary)

- **Type:** protocol
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Correct architecture", "Exact correction to issue #369")
- **Content:** GCS must not: select a worker, ask nodes for bids, collect price quotes, run an intent window, grant its own execution lease, maintain node→model lists, or prefer nodes with warm caches. SuperGenius's existing grid already provides queue ownership, node participation, task acquisition, result publication, a result channel, queue-ownership requests, an escrow path, processing-channel discovery, and node-creation competition.

## C-ELM-02 — No main-protobuf change for the ELM job definition

- **Type:** api-contract
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Revised protobuf conclusion"); corroborated by `src/processing/proto/SGProcessing.proto` and `.planning/codebase/ARCHITECTURE.md`
- **Content:** The existing `Task { string ipfs_block_id = 1; bytes json_data = 2; float random_seed = 3; string results_channel = 4; string escrow_path = 5; }` carries the full ELM array in `json_data`. Forbidden new messages: ELM capability advertising, model inventory, cache inventory, node bidding, processing intent, price quotes, GCS-side claims, GCS-side leases, requester-selected workers. Any optional additions go in a separate `SGElmProcessing.proto` and must not alter task ownership.

## C-ELM-03 — ELM job JSON shape

- **Type:** schema
- **Source:** .planning/notes/ELM-bridging-gaps.md ("The job describes the ELM work")
- **Content:** `job_type: "elm_processing"`, `version`, `elms[]` (each `work_item_id`, `elm_type` [e.g. `causal_lm`], `model_manifest_uri`, `model_manifest_hash`, `input_uri`, `generation { max_output_tokens, temperature, top_p, seed }`), `funding { maximum_processing_hours, escrow_path }`, `results_channel`. Illustrative sketch — exact field names finalize at planning.

## C-ELM-04 — Model manifest schema

- **Type:** schema
- **Source:** .planning/notes/ELM-bridging-gaps.md ("What the model manifest contains")
- **Content:** `schema_version`, `elm_type`, `model_format` (e.g. MNN), `quantization` (e.g. SGFP4), `artifacts[]` (`name` model/tokenizer/chat_template, `uri`, `sha256`, optional `size_bytes`), `runtime { minimum_version, required_memory_bytes }`. The job carries only pointer + hash; the manifest holds the details. Node verifies manifest and artifacts before running. (MNN-based `SGProcessingManager` corroborated by `.planning/codebase/STRUCTURE.md`.)

## C-ELM-05 — Cache-state neutrality (eligibility)

- **Type:** nfr
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Model caching is entirely local")
- **Content:** Model cache state is not advertised and does not affect task assignment policy. Every processor gets a chance to participate; a new node with an empty cache is not less eligible. Warm cache affects execution time only.

## C-ELM-06 — Fixed pricing, no negotiation

- **Type:** nfr
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Funding model"); rate units flagged in INGEST-CONFLICTS.md
- **Content:** Funding = funded processing hours × built-in fixed hourly rate (source writes "$.0003 cents/hour" and "$0.0003 per hour" — 1000x ambiguity, see WARNING). No quote/bid/negotiation protocol. Download time is billable job work unless existing accounting already treats model transfer differently. Funding may be one total pool or per-work-item maxima.

## C-ELM-07 — Worker-node API surface restriction

- **Type:** api-contract
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Correct GCS responsibility")
- **Content:** Worker nodes do not expose `/v1`, do not register named OpenAI models, do not negotiate prices. Only installed GCS exposes the OpenAI-compatible `/v1` API (JSON or SSE) and translates to funded SuperGenius jobs.

## C-ELM-08 — Milestone placement (owner directive)

- **Type:** nfr (planning)
- **Source:** owner directive via ingest orchestration context
- **Content:** Feature is a v1.0 product requirement (product not yet shipped) and must land in the v1.0 milestone track, not backlog. Internal milestone "v1.0" already shipped 2026-07-03 — placement variant resolution required (see INGEST-CONFLICTS.md).
