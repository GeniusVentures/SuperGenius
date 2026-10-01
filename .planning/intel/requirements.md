# Synthesized Requirements (Intel)

<!-- Extracted from a DOC-level source per manifest override (owner: v1.0 pre-ship requirement).
     IDs are provisional REQ-ELM-* slugs for downstream routing; gsd-roadmapper assigns final IDs.
     Every entry traces to .planning/notes/ELM-bridging-gaps.md unless noted. -->

## REQ-ELM-JOB-MODEL — One funded SuperGenius job containing multiple ELM work items

- **Source:** .planning/notes/ELM-bridging-gaps.md ("The job describes the ELM work")
- **Description:** A GCS instance creates one normal SuperGenius processing job (`job_type: "elm_processing"`, `version: 1`) whose `elms[]` array holds one or more ELM work items (each: `work_item_id`, `elm_type`, `model_manifest_uri`, `model_manifest_hash`, `input_uri`, `generation` settings). The ELM array lives entirely in the existing `Task.json_data`; no main-protobuf change.
- **Acceptance criteria:**
  - A job with multiple ELM work items is published and distributed through the existing processing grid with no new task-ownership protocol messages.
  - `ProcessingCoreImpl` constructs `SGProcessingManager` from `task.json_data()` and the subtask JSON resolves the model input (existing behavior extended, not replaced).

## REQ-ELM-SCOPE-REMOVAL — Remove scheduler/bidding layer from issue #369

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Exact correction to issue #369", "Revised protobuf conclusion")
- **Description:** The #369 feature spec drops: `NodeElmCapabilities`, ELM inventory advertising, cache-state advertising, `ElmProcessingIntent`, requester-side participant selection, per-node cost quotes, GCS-managed child-job claims, GCS-managed execution leases, price-policy negotiation, resource bidding — and the corresponding protobuf messages (capability advertising, model/cache inventory, bidding, intent, quotes, claims, leases, requester-selected workers).
- **Acceptance criteria:**
  - Revised #369 text replaces those sections with the corrected statement (GCS creates/funds a normal job; grid handles participation/ownership/execution; manifest-driven retrieval + local content-addressed cache; cache state not advertised and not eligibility-relevant; fixed rate applies).
  - None of the removed mechanisms appear in the implemented feature or its protobuf.

## REQ-ELM-SCHEDULER — Existing grid owns participant selection and distribution

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Correct architecture")
- **Description:** SuperGenius's existing queue ownership, node participation, task acquisition, subtask distribution, and result publication handle ELM subtasks exactly like other work. GCS performs no worker selection.
- **Acceptance criteria:**
  - GCS code path contains no worker selection, bid solicitation, or lease management for ELM jobs.
  - Job owner is agnostic to which node runs each work item.

## REQ-ELM-MANIFEST — Immutable model-manifest resolution and verification

- **Source:** .planning/notes/ELM-bridging-gaps.md ("What the model manifest contains", "What SGProcessingManager actually needs" §2)
- **Description:** Each work item references an immutable manifest (URI + hash). The node loads the manifest, verifies its hash, checks local cache, fetches missing artifacts, verifies each artifact (sha256), and pins the cache entry while processing. Manifest carries `schema_version`, `elm_type`, `model_format`, `quantization`, `artifacts[]` (model/tokenizer/chat_template with uri+sha256[+size]), and `runtime` (minimum_version, required_memory_bytes).
- **Acceptance criteria:**
  - A node never executes a model whose manifest or artifact verification failed.
  - Cache entries are verified before reuse.

## REQ-ELM-CACHE — Content-addressed local model bundle cache

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Model caching is entirely local", §4)
- **Description:** Cache keyed by manifest hash/CID at `cache/<model-manifest-hash>/`. Behavior: store by manifest hash; avoid duplicate downloads; pin active models; keep recently used; evict under disk pressure; recover from partial downloads; verify cached files before reuse. Cache state is not advertised and does not affect task assignment.
- **Acceptance criteria:**
  - A node with an empty cache successfully completes an assigned ELM subtask by downloading the model as part of the job.
  - No protocol message carries node model/cache inventory.

## REQ-ELM-RUNTIME — Real ELM processor in SGProcessingManager

- **Source:** .planning/notes/ELM-bridging-gaps.md (§3 "Add a real ELM processor")
- **Description:** The processor implements: tokenizer loading, chat-template support, prompt tokenization, model prefill, autoregressive generation, KV cache, sampling, stop tokens and strings, detokenization, token counts, cancellation, final output creation.
- **Acceptance criteria:**
  - A causal-LM work item produces generated text honoring generation settings (`max_output_tokens`, `temperature`, `top_p`, `seed`) and stop conditions, with accurate prompt/completion token counts.
  - Cancellation aborts in-flight generation.
  - **Routing note (D-ELM-08):** candidate for a separate issue/phase and different assignee.

## REQ-ELM-RESULTS — Work-item-specific results via existing result mechanism

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Multiple ELMs fit the existing task model", §5)
- **Description:** Each subtask maps to one ELM work item (existing `SubTask.subtaskid`); results identify the originating work item so GCS can reconstruct the full job result. Task→SubTask mapping: planner/critic/verifier ELMs become subtasks carrying or referencing `work_item_id`, `elm_type`, manifest pointer, input pointer, generation settings.
- **Acceptance criteria:**
  - Every result identifies its ELM work item; GCS aggregates by work-item ID with no SuperGenius-side aggregation logic added.

## REQ-ELM-FUNDING — Fixed-rate funding, no negotiation

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Funding model")
- **Description:** Job creator calculates or caps funded processing time at the existing fixed built-in hourly rate (see INGEST-CONFLICTS.md rate-units WARNING). Multiple work items may be funded by one total processing-hour pool or per-work-item maxima. SuperGenius consumes funded work via existing accounting/escrow. Model download is part of the job's work.
- **Acceptance criteria:**
  - No quote/bid/negotiation protocol exists in the feature.
  - Funding math is deterministic from the job JSON (`funding.maximum_processing_hours`, `escrow_path`).

## REQ-ELM-PROTO-OPTIONAL — Optional SGElmProcessing.proto (events/streaming/control)

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Minimal protobuf additions that may still help")
- **Description:** Optional separate `SGElmProcessing.proto`: `ElmExecutionEvent` (task/subtask/work-item/node IDs, sequence, timestamp, oneof progress|token_delta|completed|failed), `ElmTokenDelta` (token_id, text_utf8), `ElmProgress` (phase, percent, prompt/completion tokens), `ElmExecutionResult` (work_item_id, elm_type, text, structured_output, finish_reason, token counts, manifest/input/output hashes, processing_time_ms, output_artifact_cid), `ElmCancellationRequest`. Must not alter task ownership.
- **Acceptance criteria:**
  - First non-streaming proof ships without these messages (JSON result artifact through existing result mechanism).
  - If added, the file is separate from the main task-ownership proto.

## REQ-ELM-GCS-API — GCS OpenAI-compatible /v1 endpoint

- **Source:** .planning/notes/ELM-bridging-gaps.md ("Correct GCS responsibility")
- **Description:** Installed GCS handles `POST /v1/chat/completions`: resolve requested model or GCS orchestration plan → create one SuperGenius job with one or more ELM work items → add GNUS funds → publish → listen for results/token events → return OpenAI-compatible JSON or SSE. Worker nodes expose no `/v1`, register no named OpenAI models, and negotiate no prices.
- **Acceptance criteria:**
  - One installed GCS can use any SuperGenius processing capacity available through the network via this flow.

## REQ-ELM-V1-SHIP — Lands in the v1.0 (pre-ship) milestone track

- **Source:** owner directive via ingest orchestration context
- **Description:** ELM processing for GCS is a v1.0 requirement for SuperGenius (product not yet shipped); it must land in the v1.0 milestone track, not the backlog.
- **Acceptance criteria:**
  - Roadmap routes REQ-ELM-* into the product-v1.0 pre-ship track with the SGProcessingManager split (D-ELM-08) preserved.
  - Placement conflicts with the shipped internal "v1.0" milestone resolved per INGEST-CONFLICTS.md WARNING.
