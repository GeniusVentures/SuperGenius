# Analyze ELM bridging gaps

**User:** Anonymous  
**Created:** 8/26/2026 8:04:37  
**Updated:** 8/26/2026 10:24:29  
**Exported:** 8/26/2026 10:42:13  
**Link:** [https://chatgpt.com/c/6a8f0084-24bc-83e8-b1d2-a892fd7d2742](https://chatgpt.com/c/6a8f0084-24bc-83e8-b1d2-a892fd7d2742)  

## Response:
8/26/2026, 10:21:24 AM

You’re right. GCS should only define and fund the job; SuperGenius already handles processor participation, scheduling, and execution.

> **Updating the architecture**
>
> I’m revising my understanding: Super Genius handles participant selection, while the job owner supplies job data and funding. A single job may fund multiple ELMs, with built-in hourly pricing of $0.0003 cents.
>
> **Refining the workflow**
>
> I’m correcting the design: Super Genius selects participants automatically. The job owner only submits job data and adds funds. One job can fund multiple ELMs, using the built-in rate of $0.0003 per hour.
>
> **Updated the architecture and refined the workflow**
>
> 
>
> **Aligning the design**
>
> I’m treating the existing job system as the source of truth: the owner submits job data and funds it at the built-in rate, while Super Genius assigns participants and coordinates multiple ELMs automatically.
>
> **Aligned the design**
>
> 
>
> **Simplifying the model**
>
> I’m reducing this to the existing flow: the owner submits job data and funds one job at the built-in rate. That job may contain multiple ELMs, while Super Genius automatically selects participants without bidding or manual routing.
>
> **Simplified the model**
>
> 
>
> **Investigated repository cost handling, processing estimates, and task queue creation**
>
> 
>
> Worked for 3m 3s

Yep - you’re right. I added a scheduler and bidding layer **on top of SuperGenius even though SuperGenius already is the scheduler**. That was the wrong split.

## Correct architecture

```text
OpenAI-compatible client
        |
        v
Installed GCS /v1 API
        |
        | creates one funded SuperGenius job
        v
SuperGenius processing grid
        |
        | existing queue, ownership and participation logic
        v
One or more SuperGenius processor nodes
        |
        | fetch model if missing
        | keep model cached
        | execute through SGProcessingManager
        v
Results back to GCS
        |
        v
OpenAI-compatible response
```

The GCS instance does only three main things:

1. Build the job data.
2. Add enough GNUS funding at the existing fixed `$.0003 cents/hour` rate.
3. Publish the job and wait for the result.

It does **not**:

- Select a worker.
- Ask nodes for bids.
- Collect price quotes.
- Run an intent window.
- Grant its own execution lease.
- Maintain a list of which node has which model.
- Prefer a node merely because its model is warm.

SuperGenius already handles queue ownership, node participation, task acquisition and result publication through its processing grid. The current processing protocol already includes task JSON, a result channel, queue ownership requests and an escrow path.  The processing service also already handles processing-channel discovery and node-creation competition. 

## The job describes the ELM work

The job should contain one or more ELM work items.

Conceptually:

```json
{
  "job_type": "elm_processing",
  "version": 1,

  "elms": [
    {
      "work_item_id": "planner",
      "elm_type": "causal_lm",
      "model_manifest_uri": "ipfs://bafy...",
      "model_manifest_hash": "sha256:...",
      "input_uri": "ipfs://bafy...",
      "generation": {
        "max_output_tokens": 512,
        "temperature": 0.2,
        "top_p": 0.95,
        "seed": 12345
      }
    },
    {
      "work_item_id": "critic",
      "elm_type": "causal_lm",
      "model_manifest_uri": "ipfs://bafy...",
      "model_manifest_hash": "sha256:...",
      "input_uri": "ipfs://bafy...",
      "generation": {
        "max_output_tokens": 256,
        "temperature": 0.0,
        "seed": 12346
      }
    }
  ],

  "funding": {
    "maximum_processing_hours": 0.02,
    "escrow_path": "..."
  },

  "results_channel": "..."
}
```

That is one funded job containing multiple ELM operations. SuperGenius can split those into one or more subtasks and distribute them through its normal queue.

The job owner does not care which node runs `planner` or `critic`. It funds the work, publishes it, and consumes the result.

## Model caching is entirely local

A processor node does not need to announce:

- Installed model names
- Cached model IDs
- Warm model IDs
- ELM aliases
- Per-model prices
- Download estimates

When a node receives a compatible ELM subtask:

```text
Check cache by manifest hash
    |
    +-- found -> use cached bundle
    |
    +-- missing -> fetch and verify bundle
                       |
                       v
                  execute model
                       |
                       v
                 retain in cache
```

The cache should use the model manifest hash or CID as its key.

```text
cache/<model-manifest-hash>/
```

This gives every processor a chance to participate. A new node with an empty cache is not treated as less eligible. It simply downloads the model when SuperGenius assigns it work.

A warm cache improves the node’s actual execution time, but it does not become part of task assignment policy.

## What the model manifest contains

The job only needs a pointer and hash. The manifest holds the details:

```json
{
  "schema_version": 1,
  "elm_type": "causal_lm",
  "model_format": "MNN",
  "quantization": "SGFP4",

  "artifacts": [
    {
      "name": "model",
      "uri": "ipfs://bafy...",
      "sha256": "...",
      "size_bytes": 312345678
    },
    {
      "name": "tokenizer",
      "uri": "ipfs://bafy...",
      "sha256": "..."
    },
    {
      "name": "chat_template",
      "uri": "ipfs://bafy...",
      "sha256": "..."
    }
  ],

  "runtime": {
    "minimum_version": "1.0",
    "required_memory_bytes": 700000000
  }
}
```

The node verifies the manifest and artifacts before running the model.

## Multiple ELMs fit the existing task model

The most natural mapping is:

```text
SuperGenius Task
  |
  +-- SubTask: planner ELM
  |
  +-- SubTask: critic ELM
  |
  +-- SubTask: verifier ELM
```

Each subtask carries or references:

- `work_item_id`
- `elm_type`
- Model manifest pointer
- Input pointer
- Generation settings

The current `SubTask.subtaskid` already identifies each unit of work, and the current result includes the corresponding subtask ID. 

GCS can collect the results by work-item ID and perform any higher-level aggregation.

SuperGenius remains responsible for distributing the subtasks. GCS does not select the processors.

## Revised protobuf conclusion

This simplifies the protobuf changes a lot.

### No protobuf changes are required for the main ELM job definition

The current protocol already has:

```protobuf
message Task {
    string ipfs_block_id = 1;
    bytes json_data = 2;
    float random_seed = 3;
    string results_channel = 4;
    string escrow_path = 5;
}
```

The full ELM array can live in `Task.json_data`.

`ProcessingCoreImpl` already creates `SGProcessingManager` from `task.json_data()` and uses the subtask JSON to resolve the model input. 

So we do not need protobuf messages for:

- ELM capability advertising
- Model inventory
- Cache inventory
- Node bidding
- Processing intent
- Price quotes
- GCS-side claims
- GCS-side leases
- Requester-selected workers

Those should be removed from the feature spec.

## Minimal protobuf additions that may still help

A small separate file such as `SGElmProcessing.proto` may still help with output streaming and control, but it should not alter task ownership.

### Execution events

```protobuf
message ElmExecutionEvent {
  string task_id = 1;
  string subtask_id = 2;
  string work_item_id = 3;
  string node_id = 4;

  uint64 sequence = 5;
  uint64 timestamp_ms = 6;

  oneof event {
    ElmProgress progress = 10;
    ElmTokenDelta token_delta = 11;
    ElmExecutionResult completed = 12;
    ElmExecutionFailure failed = 13;
  }
}
```

### Token output

```protobuf
message ElmTokenDelta {
  uint32 token_id = 1;
  bytes text_utf8 = 2;
}
```

### Progress

```protobuf
message ElmProgress {
  string phase = 1;
  float percent = 2;
  uint64 prompt_tokens = 3;
  uint64 completion_tokens = 4;
}
```

### Final result

```protobuf
message ElmExecutionResult {
  string work_item_id = 1;
  string elm_type = 2;

  bytes text_utf8 = 3;
  bytes structured_output = 4;

  string finish_reason = 5;

  uint64 prompt_tokens = 6;
  uint64 completion_tokens = 7;

  bytes model_manifest_hash = 8;
  bytes input_hash = 9;
  bytes output_hash = 10;

  uint64 processing_time_ms = 11;
  string output_artifact_cid = 12;
}
```

### Cancellation

```protobuf
message ElmCancellationRequest {
  string task_id = 1;
  string subtask_id = 2;
  string reason = 3;
}
```

Even these are not required for the first non-streaming proof. The first version could return a JSON result artifact through the existing result mechanism.

## What SGProcessingManager actually needs

This is where most of the real work belongs.

### 1. Parse multiple ELM work items

`SGProcessingManager` must understand an ELM job definition and select the work item represented by the current subtask.

### 2. Resolve the model manifest

It must:

- Load the manifest
- Verify its hash
- Check the local cache
- Fetch missing artifacts
- Verify each artifact
- Pin the cache entry while processing

### 3. Add a real ELM processor

The processor needs:

- Tokenizer loading
- Chat-template support
- Prompt tokenization
- Model prefill
- Autoregressive generation
- KV cache
- Sampling
- Stop tokens and strings
- Detokenization
- Token counts
- Cancellation
- Final output creation

### 4. Cache model bundles

The cache should:

- Store by manifest hash
- Avoid duplicate downloads
- Pin active models
- Keep recently used models
- Evict under disk pressure
- Recover from partial downloads
- Verify cached files before reuse

### 5. Return work-item-specific results

Every result should identify the ELM work item so GCS can reconstruct the full job result.

## Funding model

There should be no quote or bidding protocol.

The job creator calculates or caps the funded processing time using the existing fixed rate:

```text
funding =
    funded processing hours
    × built-in $.0003 cents/hour rate
```

For multiple ELM work items, the job can fund either:

- One total processing-hour pool, or
- A maximum processing allocation for each ELM work item

SuperGenius consumes the funded work according to its existing accounting and escrow mechanism.

A model download should simply be part of the processor’s work needed to complete the job unless the existing accounting code already treats model transfer differently. It does not require a separate price negotiation.

## Correct GCS responsibility

An installed GCS with an OpenAI-compatible API should do this:

```text
POST /v1/chat/completions
        |
        v
Resolve requested model or GCS orchestration plan
        |
        v
Create one SuperGenius job containing one or more ELM work items
        |
        v
Add GNUS funds
        |
        v
Publish job
        |
        v
Listen for results or token events
        |
        v
Return OpenAI-compatible JSON or SSE
```

That lets one installed GCS use any SuperGenius processing capacity available through the network.

The worker nodes do not expose `/v1`. They do not register named OpenAI models. They do not negotiate prices. They process funded SuperGenius jobs.

## Exact correction to issue #369

The issue should remove:

- `NodeElmCapabilities`
- ELM inventory advertising
- Cache-state advertising
- `ElmProcessingIntent`
- Requester-side participant selection
- Per-node cost quotes
- GCS-managed child-job claims
- GCS-managed execution leases
- Price-policy negotiation
- Resource bidding

It should replace those sections with:

> A GCS instance creates and funds a normal SuperGenius processing job containing one or more ELM work items. Each work item specifies an ELM type and an immutable model-manifest pointer. SuperGenius’s existing processing grid handles worker participation, task ownership and execution. A processor retrieves and verifies the referenced model bundle, uses a local content-addressed cache, executes the work through SGProcessingManager, and publishes the result. Model cache state is not advertised and does not affect eligibility. The existing fixed `$.0003 cents/hour` processing rate applies.

That is the much simpler and more accurate feature.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)