# Synthesized Decisions (Intel)

<!-- Extracted from DOC-level source per manifest override. All entries are NON-LOCKED and carry DOC
     precedence (lowest). They are preserved as authoritative context by owner directive (v1.0 pre-ship
     requirement) but have NOT overridden any existing decision — no existing locked decision touches
     this scope (verified against PROJECT.md, REQUIREMENTS.md, ROADMAP.md, MILESTONES.md, milestones/,
     phases/01-12). Downstream (gsd-roadmapper) formalizes these into PROJECT.md Key Decisions. -->

## D-ELM-01 — SuperGenius IS the scheduler; GCS only creates/funds/publishes

- **Status:** proposed (non-locked)
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Correct architecture", "Exact correction to issue #369")
- **Scope:** ELM processing architecture, issue #369
- **Decision:** An installed GCS instance does exactly three things: (1) build the job data, (2) add GNUS funding at the existing fixed built-in rate, (3) publish the job and wait for results. It does not select workers, request bids, collect quotes, run an intent window, grant execution leases, maintain node→model inventories, or prefer nodes with warm caches. SuperGenius's existing processing grid (queue ownership, node participation, task acquisition, result publication, processing-channel discovery, node-creation competition) handles all scheduling.
- **Note:** This explicitly retracts the earlier scheduler/bidding layer that had been layered on top of SuperGenius. The retracted design is NOT synthesized anywhere in this intel.

## D-ELM-02 — One funded job, multiple ELM work items; ELM array lives in Task.json_data

- **Status:** proposed (non-locked)
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("The job describes the ELM work", "Revised protobuf conclusion")
- **Scope:** protobuf task protocol, job schema
- **Decision:** A job (`job_type: "elm_processing"`) contains an `elms[]` array of work items; the full array lives in the existing `Task.json_data` bytes. No protobuf changes to the main ELM job definition. Verified against `src/processing/proto/SGProcessing.proto` (`Task.ipfs_block_id/json_data/random_seed/results_channel/escrow_path`) and `.planning/codebase/ARCHITECTURE.md` (`ProcessingCoreImpl` constructs `SGProcessingManager` from `task.json_data()`).

## D-ELM-03 — Local content-addressed model cache; no advertising; cache state never affects eligibility

- **Status:** proposed (non-locked)
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Model caching is entirely local")
- **Scope:** SGProcessingManager, processor nodes
- **Decision:** Cache key is the model manifest hash or CID (`cache/<model-manifest-hash>/`). Nodes do not announce installed/cached/warm models, ELM aliases, per-model prices, or download estimates. A warm cache improves execution time only; a new node with an empty cache is not less eligible — it downloads the model when assigned work.

## D-ELM-04 — Model download is part of the job's work at the fixed rate

- **Status:** proposed (non-locked)
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Funding model")
- **Scope:** GNUS funding, escrow
- **Decision:** No quote or bidding protocol. Funding = funded processing hours × built-in fixed rate, consumed through existing accounting/escrow. Model download is simply processor work needed to complete the job unless existing accounting already treats model transfer differently. No separate price negotiation.
- **Caveat:** Rate units are internally inconsistent in the source ("$.0003 cents/hour" vs "$0.0003 per hour") — see INGEST-CONFLICTS.md WARNING on rate units.

## D-ELM-05 — The real work belongs in SGProcessingManager

- **Status:** proposed (non-locked)
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("What SGProcessingManager actually needs")
- **Scope:** SGProcessingManager submodule
- **Decision:** SGProcessingManager must: (1) parse multiple ELM work items and select the one for the current subtask; (2) resolve the model manifest (load, verify hash, check cache, fetch missing artifacts, verify each artifact, pin while processing); (3) add a real ELM processor (tokenizer, chat template, prompt tokenization, prefill, autoregressive generation, KV cache, sampling, stop tokens/strings, detokenization, token counts, cancellation, final output); (4) cache model bundles (store by manifest hash, dedup downloads, pin active, retain recent, evict under pressure, recover partial downloads, verify before reuse); (5) return work-item-specific results.

## D-ELM-06 — Optional SGElmProcessing.proto for events/streaming/control only

- **Status:** proposed (non-locked), explicitly optional
- **Precedence:** DOC
- **Source:** .planning/notes/ELM-bridging-gaps.md ("Minimal protobuf additions that may still help")
- **Scope:** protobuf task protocol
- **Decision:** A small separate `SGElmProcessing.proto` (ElmExecutionEvent, ElmTokenDelta, ElmProgress, ElmExecutionResult, ElmCancellationRequest) may help with output streaming and control but must not alter task ownership. Not required for the first non-streaming proof — v1 can return a JSON result artifact through the existing result mechanism.

## D-ELM-07 — v1.0 pre-ship requirement (owner directive)

- **Status:** proposed (non-locked), owner-directed
- **Precedence:** DOC (directive)
- **Source:** owner directive recorded in ingest orchestration context; scope corrections from .planning/notes/ELM-bridging-gaps.md
- **Scope:** milestone placement
- **Decision:** This feature is a v1.0 requirement for SuperGenius (product not yet shipped) and must land in the v1.0 milestone track, not the backlog.
- **Conflict:** Existing planning's internal milestone "v1.0" (GeniusNode Construction Refactor) already shipped 2026-07-03 — see INGEST-CONFLICTS.md WARNING on milestone-track placement. The directive's "v1.0" means the product release track; the target milestone entity must be defined by the roadmapper.

## D-ELM-08 — SGProcessingManager work-split (orchestrator directive)

- **Status:** proposed (non-locked), owner-directed
- **Precedence:** DOC (directive)
- **Source:** ingest orchestration context (issue #369 assignees itsafuu + henriqueaklein; SGProcessingManager is a separate submodule)
- **Scope:** work routing
- **Decision:** The SGProcessingManager execution work (D-ELM-05) may become a separate issue/phase with a different assignee from the issue #369 architecture/spec-correction work. Synthesis preserves the split so the roadmapper can route them as separate phases within the same v1.0 track.
