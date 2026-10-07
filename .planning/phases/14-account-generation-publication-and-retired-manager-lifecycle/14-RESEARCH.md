# Phase 14: Account-generation publication and retired-manager lifecycle safety - Research

**Researched:** 2026-08-18
**Domain:** C++17 concurrent lifecycle state machines, asynchronous account-generation publication, and transaction-manager retirement
**Confidence:** HIGH for current-code findings and locked behavior; MEDIUM for the recommended internal decomposition

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

### Switch completion contract
- **D-01:** `SelectAccount()` success means the switch request was accepted. Replacement readiness remains asynchronous; the call does not wait for a usable `TransactionManager`.
- **D-02:** Switch readiness and failure are delivered through the existing node-state callback/event path and carry the switch generation. Callers use the generation to reject stale completion events.
- **D-03:** Account-bound operations return an explicit `SWITCH_IN_PROGRESS` error from acceptance until the replacement generation is ready or the switch fails.
- **D-04:** A second `SelectAccount()` during an active switch is rejected with `SWITCH_IN_PROGRESS`. There is no cancellation, latest-wins behavior, or queued follow-up switch.

### In-flight operations
- **D-05:** The retiring manager closes admission before `SelectAccount()` returns accepted. Once acceptance is observable, no new old-generation operation may cross the admission boundary.
- **D-06:** Operations accepted before admission closes are allowed to reach a terminal result. Operations that have not crossed that boundary are rejected.
- **D-07:** The old generation drains completely before replacement account initialization begins. Old and replacement account generations must not perform mutable work concurrently.
- **D-08:** Drain is bounded. If an accepted operation misses the switch timeout, the switch fails and the node remains account-unavailable. The system must not claim cancellation or continue replacement initialization while durable old-generation work may still complete.

### Replacement failure
- **D-09:** Replacement initialization failure leaves the node account-unavailable and emits a generation-tagged `ACCOUNT_SWITCH_FAILED`. The retired generation is never republished and a partial replacement is never exposed.
- **D-10:** Recovery requires a new explicit `SelectAccount()` call, which may target the same or a different account. There are no automatic retries and no dedicated retry-only API.
- **D-11:** After failure, `GetAddress()` and equivalent account APIs report no active account with an explicit unavailable error; neither the retired address nor failed target is presented as active.
- **D-12:** `ACCOUNT_SWITCH_FAILED` is emitted only after every partial replacement resource is stopped and released. Receipt of the failure event means another explicit switch may start safely.

### Retired-manager behavior
- **D-13:** Every mutation attempted through a retained retired-manager reference returns an explicit `MANAGER_RETIRED` error, distinct from temporary startup or readiness errors.
- **D-14:** A retired manager may expose immutable final diagnostics only: its generation, retired state, and terminal results of operations accepted before retirement. Mutable service ownership is inaccessible.
- **D-15:** Terminal events for operations accepted before retirement are still delivered and are tagged with the retired generation. They are never suppressed or attributed to the replacement generation.
- **D-16:** Node-level processing status is lifecycle-specific: `SWITCH_IN_PROGRESS` while switching, `ACCOUNT_UNAVAILABLE` after failure, and the replacement manager's status only after readiness. It never exposes the retired manager's last status as current.

### the agent's Discretion
- Exact internal admission/lease primitive, lock layout, and condition-variable or future mechanics, provided they implement D-05 through D-08 without holding the node lifecycle mutex during blocking drain work.
- Exact timeout duration and configuration location; it must be finite, deterministic in tests, and produce the D-08 failure behavior.
- Exact error-enum and event-payload representation, provided the public distinctions and generation binding above remain explicit.
- Exact deterministic test-hook/barrier design and migration strategy for existing callers.
- Enumeration of `TransactionManager` mutation entry points that must share the retirement/admission gate, bounded to account-manager behavior rather than a repository-wide CRDT authority refactor.

### Deferred Ideas (OUT OF SCOPE)

- Bridge provider/relayer/catch-up watcher ownership and publication races — separate follow-up phase.
- Trusted-peer refresh coalescing and retry-timer lifetime — separate follow-up phase.
- Repository-wide CRDT mutation capabilities and raw `AtomicTransaction`/GlobalDB authority — not required to close the account-manager lifecycle contract and must not be pulled into Phase 14 without its own phase.

### Reviewed Todos (not folded)
- **Secure trusted-peer genesis configuration** — belongs to the Phase 13 trust-root scope, not account-generation lifecycle.
- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix)** — bridge reliability work; outside Phase 14.
- **Bridge Startup Wiring + Mock RPC Endpoints** — bridge integration work; outside Phase 14.
</user_constraints>

## Summary

Phase 14 should be planned as two coupled state machines, not as another `shared_ptr` lifetime patch. The node state machine owns one monotonically numbered switch request and keeps its publicly visible ready bundle empty from acceptance through drain and replacement initialization. The manager state machine closes admission atomically, retains only operations that crossed that boundary, lets those operations reach terminal outcomes, then freezes an immutable retirement snapshot. This directly implements D-01 through D-16. [VERIFIED: `.planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-CONTEXT.md`]

The current code already has useful pieces—`lifecycle_mutex_`, `account_service_generation_`, `AccountServiceSnapshot`, generation-checked callbacks, `outcome::result`, and GoogleTest friend access—but its publication point is too early and its stop semantics are incompatible with the locked contract. `SelectAccount()` clears switching after assigning only `account_`; `INITIALIZING_TRANSACTIONS` later assigns `transaction_manager_`, clears switching before `Start()`, and only a later manager callback advances processing and the node to `READY`. `TransactionManager::Stop()` cancels pending completion waits, while its submission methods and final enqueue have no shared admission gate. [VERIFIED: codebase, `GeniusNode.cpp:2491-2570,997-1048,1127-1260`; `TransactionManager.cpp:343-357,571-1144`]

The safest plan is to introduce a pending generation bundle, a nonblocking `CloseAdmission()` boundary, generation-owned admitted-operation records, a drain-completion callback plus injectable timeout scheduler, and one centralized ready/failure publication function. Replacement blockchain/manager/processing construction must use pending owners and generation/identity guards; none of those owners may become the active snapshot until the entire required bundle is ready. [VERIFIED: D-02, D-05 through D-12, D-16 plus current callback patterns in `GeniusNode.cpp:701-747,1030-1102`]

**Primary recommendation:** Use a three-bundle lifecycle (`ready`, `retiring`, `pending`) and a manager-owned admission/terminal ledger; close admission synchronously at `SelectAccount()` acceptance, drain asynchronously, initialize only after drain, and atomically publish the pending bundle only when it is ready. [VERIFIED: D-01 through D-16]

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Accept/reject `SelectAccount()` and allocate generation | API / Backend (`GeniusNode`) | — | `GeniusNode` owns the public account API, lifecycle mutex, generation, and node state. [VERIFIED: codebase, `GeniusNode.hpp:301,793-832`] |
| Close old-operation admission | API / Backend (`TransactionManager`) | `GeniusNode` orchestration | Only the manager can serialize every mutation entry point with retirement; the node invokes the close before returning acceptance. [VERIFIED: D-05, D-13 and codebase mutation paths] |
| Track accepted operations to terminal | API / Backend (`TransactionManager`) | Consensus/CRDT callbacks already used by the manager | Terminal transitions currently converge through transaction-state changes and wait notification. [VERIFIED: codebase, `TransactionManager.cpp:1014-1118,4648-4915`] |
| Bounded drain and replacement sequencing | API / Backend (`GeniusNode`) | `TransactionManager` drain notification | The node decides whether and when replacement initialization may start; the manager knows when its admitted set is empty. [VERIFIED: D-07, D-08] |
| Build and publish replacement generation | API / Backend (`GeniusNode`) | Blockchain and processing services | The current node constructs blockchain, manager, and processing owners and owns their state transitions. [VERIFIED: codebase, `GeniusNode.cpp:692-1260`] |
| Expose immutable retired diagnostics | API / Backend (`TransactionManager`) | — | D-14 limits retained manager references to generation, retired state, and final accepted-operation results. [VERIFIED: D-14] |
| Expose lifecycle-specific processing status | API / Backend (`GeniusNode`) | Processing service | The node must choose switching/unavailable/ready before consulting the replacement processing owner. [VERIFIED: D-16 and codebase, `GeniusNode.hpp:535-540`] |
| Persist transactions and finish consensus | Existing database/network services | `TransactionManager` | Phase 14 does not redesign CRDT authority; it only controls which manager operations are admitted and which admitted operations may continue. [VERIFIED: scope fence and D-06 through D-08] |

## Decision Coverage

| Decision | Planning consequence |
|----------|----------------------|
| D-01 | Change `SelectAccount()` from “teardown and start inline” to “validate, allocate generation, close admission, schedule continuation, return acceptance.” [VERIFIED: 14-CONTEXT.md] |
| D-02 | Add generation to the node lifecycle event payload and emit ready/failure from one centralized generation-checked path. [VERIFIED: 14-CONTEXT.md] |
| D-03/D-04 | Represent switch state independently from generic transaction readiness and use it for both public account APIs and overlap rejection. [VERIFIED: 14-CONTEXT.md] |
| D-05/D-06 | Define one linearization point inside the manager: admission succeeds before close or returns `MANAGER_RETIRED` after close. [VERIFIED: 14-CONTEXT.md] |
| D-07 | Do not call replacement account database configuration, blockchain creation, manager creation, or processing initialization until the old admitted set is empty. [VERIFIED: 14-CONTEXT.md] |
| D-08 | Timeout ends the switch request, not the old durable operation; keep the old runtime alive until its admitted set really becomes terminal. [VERIFIED: 14-CONTEXT.md] |
| D-09/D-12 | Cleanup pending owners first, then publish unavailable state and emit failure; never swap the retired bundle back into `ready`. [VERIFIED: 14-CONTEXT.md] |
| D-10 | Replacement-path initialization failures bypass the current automatic blockchain retry helper and require a later explicit selection. [VERIFIED: D-10 plus current retry behavior at `GeniusNode.cpp:1938-1956`] |
| D-11 | Remove the current string sentinel behavior and return a typed unavailable result from the active-address API and equivalent account-bound getters. [VERIFIED: D-11 plus codebase, `GeniusNode.cpp:3410-3419`] |
| D-13/D-14 | Gate or make private every mutation-capable retained-manager entry point; preserve a separate immutable diagnostic snapshot after runtime teardown. [VERIFIED: 14-CONTEXT.md] |
| D-15 | `Stop()` cannot cancel accepted-operation observers during retirement; terminal event payloads retain the old generation. [VERIFIED: D-15 plus current cancellation at `TransactionManager.cpp:343-357,1088-1118`] |
| D-16 | Make `GetProcessingStatus()` a node lifecycle snapshot, not an inline dereference of `processing_service_`. [VERIFIED: D-16 plus codebase, `GeniusNode.hpp:535-540`] |

## Current API and Mutation Inventory

### Node publication and observation surfaces

| Surface | Current behavior | Required planning treatment |
|---------|------------------|-----------------------------|
| `SelectAccount(std::string_view) -> outcome::result<void>` | Returns success after synchronous teardown and starting blockchain initialization; it does not return the accepted generation. [VERIFIED: codebase, `GeniusNode.hpp:301`; `GeniusNode.cpp:2491-2570`] | Return an acceptance payload containing generation and target, or provide an equivalently explicit generation result. [VERIFIED: D-01/D-02] |
| `SnapshotAccountServices()` | Returns empty only while `account_service_switching_` is true. [VERIFIED: codebase, `GeniusNode.cpp:3387-3395`] | Keep the ready bundle empty for the whole switch, including drain, initialization, cleanup, and failure. [VERIFIED: D-03/D-09] |
| `GetAddress()` | Returns the misspelled sentinel `"UNVAILABLE"` when no account is in the snapshot. [VERIFIED: codebase, `GeniusNode.cpp:3410-3419`] | Return a typed `SWITCH_IN_PROGRESS` or `ACCOUNT_UNAVAILABLE` error; do not return either old or target address. [VERIFIED: D-03/D-11] |
| Pre-readiness address consumers | Startup/test wiring calls `GeniusNode::GetAddress()` before node readiness, including authorized-full-node setup and diagnostic waits. [VERIFIED: `multi_account_sync.cpp:270-296`; broader codebase grep] | Separate configured/bootstrap identity from the public active-generation address, and mechanically migrate internal boot wiring to the former. Do not weaken D-11 merely to preserve pre-ready callers. [VERIFIED: D-11 plus current caller evidence] |
| `GetBalance*`, transaction list/count, manager-state/status/wait getters | Several return `0`, empty containers, `CREATING`, or `INVALID` when no manager/account is published. [VERIFIED: codebase, `GeniusNode.cpp:3034-3112,3360-3486,3812-3820`] | Provide checked lifecycle-aware variants or migrate these surfaces to typed results so temporary switching and terminal unavailability remain distinguishable. [VERIFIED: D-03/D-11] |
| `GetProcessingStatus()` | Inline unsynchronized access to `processing_service_`; absence maps only to `DISABLED`. [VERIFIED: codebase, `GeniusNode.hpp:535-540`] | Return `{generation, lifecycle_status, optional replacement service status}` from a lifecycle snapshot. [VERIFIED: D-16] |
| Node-state event surface | `GeniusNode` has an atomic `state_` and `StateTransition()`, but the inspected public header has no node-state callback registration API or event payload; tests poll `GetState()`. [VERIFIED: exhaustive codebase grep in `GeniusNode.hpp/.cpp` and both scoped tests] | Extend the centralized node-state transition path with a callback/event API carrying `{state/event, generation, target, error}`; do not build a separate polling-only switch API. [VERIFIED: D-02] |

### Manager mutation entry points that need one admission contract

The admission boundary must be crossed before the first account/nonce/UTXO/CRDT mutation, and the same admitted-operation token must be carried through the final enqueue. Re-acquiring admission at `EnqueueTransaction()` would incorrectly reject work that was admitted before closure. [VERIFIED: D-05/D-06 plus code order below]

| Entry point | First mutation / durable-risk point | Current final boundary | Required action |
|-------------|-------------------------------------|------------------------|-----------------|
| `TransferFunds` | `FillDAGStruct()` reserves a nonce; `ReserveUTXOs()` reserves inputs. [VERIFIED: `TransactionManager.cpp:571-591,1147-1165`] | `EnqueueTransaction`. [VERIFIED: codebase] | Acquire an admitted-operation token before `FillDAGStruct`; pass it through reservation and enqueue; rollback and record terminal failure on any intermediate error. [VERIFIED: D-05/D-06/D-13] |
| `MintFunds` | `PutUTXO`, `ReserveUTXOs`, nonce creation, and bridge-source persistence checks affect account-bound state. [VERIFIED: `TransactionManager.cpp:596-726`] | `EnqueueTransaction`. [VERIFIED: codebase] | Acquire once before any mutation; preserve token until transaction terminal; reject retained-manager invocation with `MANAGER_RETIRED`. [VERIFIED: D-13/D-15] |
| `MigrationFunds` | `FillDAGStruct()` reserves a nonce. [VERIFIED: `TransactionManager.cpp:729-748`] | `EnqueueTransaction`. [VERIFIED: codebase] | Use the common admission token. [VERIFIED: D-13] |
| `HoldEscrow` | `FillDAGStruct()` and `ReserveUTXOs()` mutate account state. [VERIFIED: `TransactionManager.cpp:751-782`] | `EnqueueTransaction`. [VERIFIED: codebase] | Use the common admission token and rollback before terminal failure. [VERIFIED: D-06/D-13] |
| `PayEscrow` | Builds/signs a release and may add a CRDT topic to a caller-provided transaction. [VERIFIED: `TransactionManager.cpp:785-909`] | `EnqueueTransaction(TransactionItem)`. [VERIFIED: codebase] | Make direct and async forms use the same admission path; do not rely only on `payout_submission_mutex_`. [VERIFIED: current partial gate at `TransactionManager.cpp:912-948`] |
| `AsyncPayEscrow` | Currently checks `stopped_` only under `payout_submission_mutex_`. [VERIFIED: `TransactionManager.cpp:912-948`] | Calls `PayEscrow`, then registers a wait. [VERIFIED: codebase] | Convert rejection to explicit `MANAGER_RETIRED`; bind completion to the admitted operation and manager generation. [VERIFIED: D-13/D-15] |
| `EnqueueTransaction` overloads | Calls `ChangeTransactionState(CREATED)` and appends to `tx_queue_m` without checking retirement. [VERIFIED: `TransactionManager.cpp:1120-1144`] | Queue insertion under `mutex_m`. [VERIFIED: codebase] | Require a valid admitted token and return `outcome::result<void>`; never start a fresh admission here. [VERIFIED: D-05/D-06] |
| `GeniusNode::SendTransactionAndProof` | Directly calls protected enqueue through the current published manager. [VERIFIED: `GeniusNode.cpp:3489-3500`] | Same enqueue path. [VERIFIED: codebase] | Route through a manager method that acquires admission and returns a typed result; do not bypass the gate. [VERIFIED: D-13] |

### Continuations that must not be mistaken for new admission

`TickOnce`, `SendTransactionItem`, consensus-certificate handling, transaction-state transitions, and completion callbacks are continuations of already admitted work when correlated to an admitted transaction ID. They must remain able to move those operations to terminal state during drain. Unrelated new inbound/background work must not enlarge the drain set after admission closes. [VERIFIED: D-06/D-08/D-15 and current continuations in `TransactionManager.cpp:420-568,1290-1478,3387-3900,4648-4915`]

The plan should therefore add a draining ingress rule: after admission closes, internal callbacks may mutate only records already present in the admitted-operation ledger (or perform the minimum state transition needed to terminalize one); they must not create a new account-bound admitted record. This is narrower than a GlobalDB capability redesign. [VERIFIED: D-05 through D-08 and scope fence]

### Public manager methods that should become immutable or internal after retirement

`Start`, `RegisterTopicNames`, `StartListeningTopics`, `StartCore`, `QueryTransactions`, `FetchAndProcessTransaction`, `SetOutgoingStatusByNonce`, `OnConsensusCertificate`, `ChangeTransactionState`, and the protected enqueue/setter methods can mutate runtime state. The plan should either move node/internal-only methods behind private capability-bearing helpers or require an internal continuation token; a retained external manager reference must not be able to invoke them after retirement. [VERIFIED: codebase declarations in `TransactionManager.hpp:145-148,259-343,620-834`; D-14]

Read-only history methods are not automatically valid retired diagnostics: D-14 permits only frozen generation, retired state, and accepted-operation terminal results. The plan should add one `RetirementSnapshot` API and stop treating the live `GetInTransactions`, `GetOutTransactions`, `CountTransactions`, `GetState`, and arbitrary lookup surfaces as the retired contract. [VERIFIED: D-14 and current public reads at `TransactionManager.hpp:150-152,259-272`]

## Standard Stack

### Core

| Library / facility | Version | Purpose | Why standard here |
|--------------------|---------|---------|-------------------|
| C++ standard library concurrency | C++17 | Mutexes, condition variables, atomics, RAII admission handles, immutable snapshots | The configured workspace uses C++17, and existing lifecycle/test code already uses `std::mutex`, `std::condition_variable`, and atomics. [VERIFIED: `build/CommonCompilerOptions.cmake:6-7`; scoped source/tests] |
| Boost.Asio | Existing project-resolved version | Post drain continuations and schedule/cancel a finite timeout without blocking `lifecycle_mutex_` | `GeniusNode` already runs four `io_context` threads and posts lifecycle work. [VERIFIED: `GeniusNode.hpp:812-814,1160-1163`; `GeniusNode.cpp:342-345,731-747`] |
| Boost.Outcome | Existing project-resolved version | Typed `SWITCH_IN_PROGRESS`, `ACCOUNT_UNAVAILABLE`, and `MANAGER_RETIRED` errors | Public fallible APIs and both node/manager error categories already use `outcome::result`. [VERIFIED: `GeniusNode.hpp`; `TransactionManager.hpp:66-69`; category definitions in both `.cpp` files] |
| GoogleTest / GoogleMock | Existing bundled project version | Deterministic multi-thread barrier and lifecycle regression tests | Both target files are already GoogleTest binaries and `MultiAccountTestAccess` is a friend of node and manager. [VERIFIED: scoped tests and CMake files] |

### Supporting

| Facility | Version | Purpose | When to use |
|----------|---------|---------|-------------|
| Existing `AccountServiceSnapshot` | In-tree | Coherent public ready-generation read | Extend it to include lifecycle and processing owner, or replace it with a coherent ready-bundle snapshot. [VERIFIED: `GeniusNode.hpp:788-798`] |
| Existing generation counters and weak-owner callbacks | In-tree | Reject stale asynchronous work | Carry the accepted switch generation and expected owner identity through blockchain, manager, processing, and event completion paths. [VERIFIED: `GeniusNode.hpp:823-835`; `GeniusNode.cpp:1030-1102`] |
| Existing `MultiAccountTestAccess` | In-tree | Install deterministic hooks and inspect generation/manager diagnostics | Keep hooks test-only or default-empty; do not add sleeps to force interleavings. [VERIFIED: `multi_account_sync.cpp:55-170`] |

### Alternatives Considered

| Instead of | Could use | Tradeoff |
|------------|-----------|----------|
| Manager-owned admitted-operation ledger | Hold the node lifecycle mutex through every transaction submission | This would serialize mutations but violates the locked requirement not to hold the node lifecycle mutex during blocking drain and would couple transaction internals to node locking. [VERIFIED: agent's Discretion in 14-CONTEXT.md] |
| Asynchronous drain callback plus timer | Block `SelectAccount()` on a condition variable | This contradicts D-01 and risks blocking an I/O executor thread. [VERIFIED: D-01] |
| Pending bundle and one ready commit | Publish the account first and fill manager later | This is the current CR-01 failure. [VERIFIED: `13-REVIEW.md`; `GeniusNode.cpp:2557-2560`] |
| Preserve admitted work | Call current `Stop()` immediately | Current `Stop()` cancels pending waits and prevents the tick loop from continuing, contradicting D-06/D-15. [VERIFIED: `TransactionManager.cpp:343-357,420-568,1088-1118`] |

**Installation:** No new package or external dependency is required. [VERIFIED: recommended stack uses existing project facilities]

## Package Legitimacy Audit

No external package installation is recommended, so the package legitimacy gate does not apply. [VERIFIED: Standard Stack]

## Architecture Patterns

### System Architecture Diagram

```text
SelectAccount(target)
        |
        v
[lifecycle lock: validate overlap + allocate generation G]
        |
        +--> ready bundle becomes unavailable
        +--> old manager CloseAdmission(G_old)  <-- linearization boundary
        +--> publish SWITCH_IN_PROGRESS(G)
        |
        v
return AcceptedSwitch{G, target}
        |
        v (async, no lifecycle lock held while waiting)
[retiring manager admits no new work]
        |
        +--> already admitted op terminal event {G_old, op, result}
        |                         |
        |                         v
        +------------------- admitted set empty? ---- no ----+
        |                         |                           |
        |                        yes                       timeout
        v                         v                           v
[stop/release old runtime]   [begin pending G]       [fail G, unavailable]
                                  |                           |
                                  v                           +--> old runtime keeps
                         account -> blockchain                    finishing durable work
                                  -> manager -> processing
                                  |
                       generation + owner identity checks
                                  |
                         ready? --+-- failed
                           |             |
                          yes            v
                           |       cleanup pending bundle
                           v             |
[single lifecycle-locked commit of ready bundle G]            |
                           |                                   |
                           +--> ACCOUNT_SWITCH_READY(G)         +--> ACCOUNT_SWITCH_FAILED(G)
```

This flow keeps request acceptance synchronous, drain and initialization asynchronous, and all externally visible terminal events generation-bound. [VERIFIED: D-01 through D-12]

### Recommended Project Structure

```text
src/account/
├── GeniusNode.hpp                 # public switch/event/status contracts and generation bundles
├── GeniusNode.cpp                 # acceptance, async drain, pending init, ready/failure commit
├── TransactionManager.hpp         # admission token, lifecycle, retirement diagnostics
└── TransactionManager.cpp         # common gate, admitted ledger, terminalization, drain notification
test/src/
├── multiaccount/
│   └── multi_account_sync.cpp     # deterministic concurrent generation/drain/retirement regressions
└── account/
    └── account_management_test.cpp # public API acceptance/error/failure/recovery contract
```

No new subsystem or repository-wide authority layer is needed. [VERIFIED: scope fence]

### Pattern 1: Three explicit owner bundles

**What:** Represent lifecycle ownership as `ready_generation_`, `retiring_generation_`, and `pending_generation_`, each carrying its generation and the owners relevant to this phase. Only `ready_generation_` is visible through public account APIs. [VERIFIED: recommendation derived from D-07/D-09 and current scattered owners]

**When to use:** On selection acceptance, move the ready bundle to retiring and expose no account; after drain, construct pending; on complete readiness, swap pending into ready in one lifecycle-locked commit. [VERIFIED: D-03/D-07/D-09]

**Important boundary:** Bridge provider/relayer/watcher ownership remains outside this phase; do not turn this bundle into a repository-wide service graph. Existing teardown calls may remain mechanically invoked, but bridge publication redesign is deferred. [VERIFIED: scope fence]

### Pattern 2: Admission token plus asynchronous terminal record

**What:** `TryAdmit(kind)` takes a short manager mutex, rejects unless lifecycle is `ACTIVE`, creates an operation record tagged with the manager generation, and returns a move-only token. The token is transferred into tracked transaction state at enqueue and is removed only on terminal outcome. [VERIFIED: D-05/D-06/D-15]

**When to use:** Every client-originated manager mutation listed above. Internal callbacks use a separate continuation lookup by admitted operation/transaction ID rather than opening new admission. [VERIFIED: mutation inventory]

**Why two concepts are needed:** A short RAII reader lease alone is insufficient because submission returns before consensus terminalization; a terminal record alone is insufficient unless the initial record creation is serialized with admission closure. [VERIFIED: current asynchronous transaction lifecycle in `TransactionManager.cpp:1120-1478,4648-4915`]

### Pattern 3: One close/drain/retire sequence

**What:** `CloseAdmission()` is nonblocking and idempotent. It changes lifecycle from `ACTIVE` to `DRAINING` under the same mutex used by `TryAdmit`. `OnDrained(callback)` fires when the admitted set becomes empty. `FinalizeRetirement()` freezes diagnostics, releases the mutable runtime capsule, and changes lifecycle to `RETIRED`. [VERIFIED: D-05 through D-08, D-14]

**When to use:** `SelectAccount()` calls close under the node acceptance critical section; the asynchronous node continuation waits for drain without retaining the node mutex. [VERIFIED: D-05 and agent's Discretion]

**Timeout rule:** Configure a 60-second production default in `GeniusNodeConfig`, exceeding the current 30-second release operation wait defaults and 50-second debug defaults, while injecting a manual/short scheduler in tests. Timeout finishes the switch request but does not call `Stop()` on possibly durable admitted work. [ASSUMED]

### Pattern 4: Pending initialization with generation and identity guards

**What:** Every blockchain start, retry, post, manager callback, and processing completion captures generation `G` and expected pending owner identity. Before advancing or publishing, it rechecks both under `lifecycle_mutex_`. [VERIFIED: current weak/generation callback pattern at `GeniusNode.cpp:1030-1102`; missing blockchain binding at `701-747`]

**When to use:** All asynchronous steps from old drain completion through ready/failure event emission. Replacement failures do not invoke the automatic retry path. [VERIFIED: D-09/D-10]

### Pattern 5: Centralized lifecycle result mapping

**What:** Use one helper to snapshot `{generation, switch_state, ready owners}` and map public operations to:

```text
SWITCHING  -> SWITCH_IN_PROGRESS
FAILED/no ready generation -> ACCOUNT_UNAVAILABLE
READY but manager not service-ready -> lifecycle invariant failure (never publish this state)
READY -> delegate to the ready generation
```

**When to use:** `GetAddress`, manager acquisition, balance/history access, transaction submission, processing status, and account-management operations. [VERIFIED: D-03/D-11/D-16]

### Pattern 6: Immutable retirement snapshot

**What:** Before releasing mutable manager runtime, copy only `{generation, RETIRED, terminal_results}` into an immutable value guarded for concurrent reads. Each terminal result contains operation/transaction ID, terminal status/error, completion sequence/time as needed, and the retired generation. [VERIFIED: D-14/D-15]

**When to use:** Any caller retaining a strong manager reference after node unpublication. All mutation methods consult lifecycle first and return `MANAGER_RETIRED`. [VERIFIED: D-13/D-14]

### Component Responsibilities

| Component | Responsibility | Must not do |
|-----------|----------------|-------------|
| `GeniusNode` lifecycle lock | Serialize acceptance, visible state, bundle moves, generation allocation, and ready/failure commit. [VERIFIED: current owner model and D-01/D-02] | Wait for drain, join threads, or perform blocking stop while held. [VERIFIED: locked discretion] |
| `TransactionManager` admission mutex | Serialize `TryAdmit`, `CloseAdmission`, admitted-set changes, and retirement snapshot finalization. [VERIFIED: D-05/D-06/D-14] | Re-enter node lifecycle while held or treat background sync as fresh admitted client work. [VERIFIED: recommended lock-order rule] |
| Retiring runtime | Keep dependencies needed by already admitted operations alive until terminal. [VERIFIED: D-06/D-08] | Accept unrelated new account mutation or claim cancellation at timeout. [VERIFIED: D-05/D-08] |
| Pending generation | Own replacement account/blockchain/manager/processing during construction. [VERIFIED: D-07/D-09] | Leak any partial owner through public snapshot/status/address. [VERIFIED: D-09/D-11/D-16] |
| Event dispatcher | Deliver switch ready/failure and old-operation terminal events with generation. [VERIFIED: D-02/D-15] | Rewrite old generation to current or invoke callbacks while internal locks are held. [VERIFIED: D-15 plus recommended deadlock prevention] |

### Anti-Patterns to Avoid

- **Boolean-only switching state:** A bool cannot represent switching versus unavailable failure, cannot bind callbacks to a request, and currently clears at two premature points. Use an explicit lifecycle state plus generation. [VERIFIED: D-02/D-03/D-11 and `GeniusNode.cpp:2542,2560,2567,1028`]
- **Publishing at manager construction:** Current code clears switching before `TransactionManager::Start()` and before its later `READY` callback. Construction is not readiness. [VERIFIED: `GeniusNode.cpp:1000-1048,3823-3851`]
- **Using `stopped_` as admission:** `stopped_` is checked inconsistently and `Stop()` terminates the engine/cancels waits. Admission closure must precede stop and must still permit accepted continuations. [VERIFIED: `TransactionManager.cpp:343-357,368-425,571-1144`]
- **Rechecking retirement only at enqueue:** This allows nonce/UTXO mutations before rejection and can reject an operation that legitimately crossed admission before closure. Carry one token from the first mutation. [VERIFIED: mutation inventory and D-05/D-06]
- **Cancel-on-drain-timeout:** D-08 explicitly forbids claiming cancellation when durable old work may still finish. [VERIFIED: D-08]
- **Automatic replacement retry:** Current blockchain retry behavior is acceptable for initial startup but forbidden after a selected replacement fails. [VERIFIED: D-10; `GeniusNode.cpp:1938-1956`]
- **Callbacks under lifecycle/admission locks:** User callbacks can re-enter selection or diagnostics; invoke copied callbacks only after state has committed and locks are released. [ASSUMED]
- **Sleep-based concurrency tests:** Existing tests already have mutex/condition-variable barriers; add named hook barriers at exact linearization points. [VERIFIED: `multi_account_sync.cpp:618-634`; locked discretion]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Generation ownership | Independent account/manager booleans and pointer checks | One value bundle committed under `lifecycle_mutex_` | Independent fields recreate partial publication. [VERIFIED: CR-01 and D-09] |
| Completion waiting | Polling loops or arbitrary sleeps | Existing async I/O plus drain callback and injectable timer | Tests need deterministic barriers and production must not block acceptance. [VERIFIED: D-01 and locked discretion] |
| Error transport | Sentinel strings, zero values, empty vectors, generic error codes | Existing Boost.Outcome error categories with explicit lifecycle enums | D-03/D-11/D-13 require semantic distinctions. [VERIFIED: context and current Outcome usage] |
| Cancellation/rollback | Universal rollback for CRDT/consensus work | Allow admitted operations to terminal; fail switch on bound | Universal cancellation is explicitly outside scope and unsafe for durable work. [VERIFIED: D-06/D-08 and scope fence] |
| Test synchronization | Timing sleeps | `MultiAccountTestAccess` hooks plus mutex/condition-variable barriers | The exact races occur at admission, enqueue, callback, publication, and cleanup boundaries. [VERIFIED: Phase 13 review and existing fixture pattern] |
| Retired diagnostics | Live getters over retained service owners | Frozen `RetirementSnapshot` | D-14 permits only immutable final diagnostics. [VERIFIED: D-14] |

**Key insight:** Lifetime safety and admission safety are different. A strong `shared_ptr` prevents destruction, but only a serialized admission record proves whether an operation belongs to the old generation and therefore may continue after selection acceptance. [VERIFIED: `13-REVIEW.md` CR-02 and D-05/D-06]

## Common Pitfalls

### Pitfall 1: Defining “accepted” at the wrong point

**What goes wrong:** A call reads a ready manager before closure, but mutates nonce/UTXO state after closure without an admitted token. [VERIFIED: current `GeniusNode::TransferFunds` snapshot followed by manager call at `GeniusNode.cpp:2998-3016`]

**Why it happens:** Snapshot lifetime is mistaken for operation admission. [VERIFIED: `13-REVIEW.md` CR-02]

**How to avoid:** Linearize admission in `TransactionManager::TryAdmit()` before the first mutation; make `CloseAdmission()` use the same mutex. [VERIFIED: D-05/D-06]

**Warning signs:** Submission methods call `GetState()` or `stopped_.load()` without holding the admission mutex, or enqueue acquires a new permit. [VERIFIED: current anti-pattern]

### Pitfall 2: Counting only the synchronous method body as in-flight

**What goes wrong:** Drain reaches zero after enqueue even though consensus and terminal callbacks are outstanding, so replacement mutable work begins concurrently. [VERIFIED: current enqueue-to-terminal asynchronous flow]

**Why it happens:** A scoped reader count ends when `TransferFunds()` returns, not when the transaction becomes terminal. [VERIFIED: codebase behavior]

**How to avoid:** Transfer the admission token into a transaction-ID-indexed terminal ledger and decrement only on the first terminal transition. [VERIFIED: D-06/D-07/D-15]

**Warning signs:** Drain counter decrements in an admission guard destructor immediately after enqueue. [RECOMMENDED: derived from D-06]

### Pitfall 3: Stopping the manager before drain

**What goes wrong:** The tick loop stops and pending waits are cancelled with `operation_aborted`, suppressing the terminal result D-15 requires. [VERIFIED: `TransactionManager.cpp:343-357,420-568,1088-1118`]

**How to avoid:** Close admission first; call destructive `Stop()` only after real drain and terminal callback delivery. On timeout, keep the runtime alive and unavailable. [VERIFIED: D-06/D-08/D-15]

### Pitfall 4: Letting background ingress grow the drain set

**What goes wrong:** New inbound transaction/sync callbacks mutate the retiring account after admission closure, so drain is not bounded to pre-close work. [VERIFIED: callback queues and manager tick in `TransactionManager.cpp:420-459,3178-3234`]

**How to avoid:** During `DRAINING`, accept only continuations correlated with existing admitted IDs; prevent creation of unrelated account-bound records. [VERIFIED: D-05/D-06]

### Pitfall 5: Publishing the replacement before service readiness

**What goes wrong:** `GetAddress()` advertises target identity, manager methods report startup state, or processing status reads a partially initialized replacement. [VERIFIED: current publication at `GeniusNode.cpp:2557-2560,997-1048`]

**How to avoid:** Build with pending owners and publish once after manager `READY` and processing owner readiness required by the node. [VERIFIED: D-03/D-09/D-16]

### Pitfall 6: Stale blockchain callback advances the wrong switch

**What goes wrong:** Current blockchain callbacks check only `NodeState::INITIALIZING_BLOCKCHAIN`; a callback from an older owner can advance a later switch in the same state. [VERIFIED: `GeniusNode.cpp:701-747`; `13-REVIEW.md` CR-01]

**How to avoid:** Capture generation and expected `Blockchain*` in start, retry, and posted transaction-init callbacks; revalidate both under the lifecycle lock. [VERIFIED: Phase 13 review fix guidance]

### Pitfall 7: Failure event before cleanup

**What goes wrong:** A caller receives `ACCOUNT_SWITCH_FAILED`, starts recovery, and overlaps with callbacks or resources from the failed pending generation. [VERIFIED: risk prohibited by D-12]

**How to avoid:** Move pending owners out under lock, stop/release outside lock, then commit `ACCOUNT_UNAVAILABLE` and emit the failure if the generation is still current. [VERIFIED: D-09/D-12 and existing move-then-stop pattern]

### Pitfall 8: Retired status leaks through processing APIs

**What goes wrong:** Inline `GetProcessingStatus()` races owner reset or returns the old manager's last processing status after selection acceptance. [VERIFIED: `GeniusNode.hpp:535-540`; `13-REVIEW.md` CR-02]

**How to avoid:** Snapshot node lifecycle first; return switching/unavailable without reading any service; only copy and query the processing owner from a ready bundle. [VERIFIED: D-16]

### Pitfall 9: Terminal classification is inconsistent

**What goes wrong:** The existing helper treats `CONFIRMED`, `UNCONFIRMED`, and `FAILED` as terminal, while other call sites also treat `INVALID` as terminal; drain can hang or double-complete if the new ledger does not define one canonical set. [VERIFIED: `TransactionManager.cpp:1014-1018`; `GeniusNode.cpp:3070-3088`]

**How to avoid:** Define one canonical terminal predicate used by state change, waits, retirement ledger, and drain accounting; make completion idempotent. [RECOMMENDED: current code evidence]

### Pitfall 10: Recovery after drain timeout starts replacement work too early

**What goes wrong:** A new explicit selection is accepted after failure while the timed-out retired runtime still has durable work, and replacement initialization begins immediately, violating D-07/D-08. [VERIFIED: D-07/D-08/D-10/D-12 interaction]

**How to avoid:** A recovery request may allocate a new switch generation, but its initialization must remain chained behind the same still-draining retire context. `ACCOUNT_SWITCH_FAILED` means failed pending resources are clean; it does not fabricate completion of old durable work. [RECOMMENDED: combined locked decisions]

### Pitfall 11: Breaking initial bootstrap identity while fixing active-address semantics

**What goes wrong:** Converting `GetAddress()` to a ready-generation-only typed result breaks code that currently uses the configured account address before node readiness, or tempts implementation to expose a failed/pending account publicly. [VERIFIED: `multi_account_sync.cpp:270-296`; D-11]

**How to avoid:** Add an internal/configured identity accessor for startup wiring and reserve the public active-account accessor for lifecycle-checked ready generations. The immutable node trust signer remains separate and must not be relabeled by selection. [VERIFIED: Phase 13 established `NodeTrustSigner`; D-11]

## Code Examples

The examples below are implementation sketches derived from locked decisions and current in-tree patterns; names may be adjusted during planning. [VERIFIED: 14-CONTEXT.md and codebase]

### Admission linearization and token transfer

```cpp
// Proposed pattern; source basis: D-05/D-06 and TransactionManager mutation paths.
outcome::result<AdmittedOperation> TransactionManager::TryAdmit(OperationKind kind) {
    std::lock_guard lock(admission_mutex_);
    if (lifecycle_ != ManagerLifecycle::ACTIVE) {
        return outcome::failure(Error::MANAGER_RETIRED);
    }
    const auto id = ++next_operation_id_;
    admitted_.emplace(id, OperationRecord{generation_, kind});
    return AdmittedOperation{shared_from_this(), id, generation_};
}

outcome::result<std::string> TransactionManager::TransferFunds(...) {
    BOOST_OUTCOME_TRY(auto admission, TryAdmit(OperationKind::TRANSFER));
    // All nonce/UTXO mutation occurs after admission.
    BOOST_OUTCOME_TRY(auto params, account_m->GetUTXOManager().CreateTxParameter(...));
    auto tx = BuildAndSignTransfer(params);
    account_m->GetUTXOManager().ReserveUTXOs(...);
    BOOST_OUTCOME_TRY(EnqueueTransaction(std::move(admission), {tx, std::nullopt}));
    return tx->GetHash();
}
```

### Acceptance boundary without blocking drain

```cpp
// Proposed pattern; source basis: D-01/D-04/D-05/D-07.
outcome::result<AccountSwitchAcceptance> GeniusNode::SelectAccount(std::string_view target) {
    std::shared_ptr<TransactionManager> retiring;
    uint64_t generation = 0;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (account_lifecycle_ == AccountLifecycle::SWITCHING) {
            return outcome::failure(Error::SWITCH_IN_PROGRESS);
        }
        generation = ++account_service_generation_;
        account_lifecycle_ = AccountLifecycle::SWITCHING;
        retiring = ready_generation_.manager;
        ready_generation_ = {};
        if (retiring) retiring->CloseAdmission(); // nonblocking, same boundary as TryAdmit
    }
    BeginDrain(generation, target, std::move(retiring));
    return AccountSwitchAcceptance{generation, std::string(target)};
}
```

### Generation-checked ready commit

```cpp
// Proposed pattern; source basis: D-02/D-09/D-16.
bool GeniusNode::PublishReady(PendingGeneration pending) {
    AccountSwitchEvent event;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (pending.generation != account_service_generation_ ||
            account_lifecycle_ != AccountLifecycle::SWITCHING ||
            !pending.account || !pending.manager || !pending.processing ||
            pending.manager->GetState() != TransactionManager::State::READY) {
            return false;
        }
        ready_generation_ = std::move(pending).IntoReadyBundle();
        account_lifecycle_ = AccountLifecycle::READY;
        event = {AccountSwitchEventKind::READY, ready_generation_.generation, {}};
    }
    EmitAccountSwitchEvent(event); // outside lifecycle lock
    return true;
}
```

### Lifecycle-specific processing status

```cpp
// Proposed pattern; source basis: D-16 and current ProcessingStatus API.
outcome::result<NodeProcessingStatus> GeniusNode::GetProcessingStatus() const {
    std::shared_ptr<processing::ProcessingServiceImpl> service;
    uint64_t generation = 0;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (account_lifecycle_ == AccountLifecycle::SWITCHING)
            return outcome::failure(Error::SWITCH_IN_PROGRESS);
        if (account_lifecycle_ != AccountLifecycle::READY)
            return outcome::failure(Error::ACCOUNT_UNAVAILABLE);
        generation = ready_generation_.generation;
        service = ready_generation_.processing;
    }
    return NodeProcessingStatus{generation, service->GetProcessingStatus()};
}
```

### Deterministic hook barrier

```cpp
// Proposed test-only pattern; source basis: existing mutex/CV fixture at
// test/src/multiaccount/multi_account_sync.cpp:618-634.
struct LifecycleHooks {
    std::function<void(uint64_t)> after_admission_closed;
    std::function<void(uint64_t, uint64_t)> after_operation_admitted;
    std::function<void(uint64_t, uint64_t)> before_enqueue;
    std::function<void(uint64_t)> before_replacement_initialization;
    std::function<void(uint64_t)> before_ready_publication;
    std::function<void(uint64_t)> after_failure_cleanup;
};
```

Hooks must be default-empty and invoked outside production locks unless the test explicitly needs to observe a documented linearization point. [RECOMMENDED: deterministic validation requirement]

## State of the Art

| Old/current approach | Required approach | Why it changes |
|----------------------|-------------------|----------------|
| Strong account/manager snapshot | Snapshot plus manager admission record | Strong ownership prevents UAF but not post-retirement mutation. [VERIFIED: `13-REVIEW.md` CR-02] |
| `account_service_switching_` bool | Explicit switching/ready/unavailable state with generation | D-03/D-11 require different errors and D-02 requires generation binding. [VERIFIED: 14-CONTEXT.md] |
| Clear switching after account assignment or manager construction | Publish once after complete readiness | Current code exposes partial and pre-ready generations. [VERIFIED: `GeniusNode.cpp:2557-2560,997-1048`] |
| `Stop()` as retirement | Close admission, drain, then stop/freeze | Current stop cancels accepted completion observation. [VERIFIED: D-06/D-15 and current code] |
| Generic not-ready/sentinel values | Explicit lifecycle errors | Locked decisions require `SWITCH_IN_PROGRESS`, `ACCOUNT_UNAVAILABLE`, and `MANAGER_RETIRED`. [VERIFIED: D-03/D-11/D-13] |
| State-only stale callback check | Generation plus expected owner identity | Two switches can occupy the same initialization state. [VERIFIED: `13-REVIEW.md` CR-01] |

**Deprecated/outdated for Phase 14:**

- `GetAddress()` returning `"UNVAILABLE"`: replace with typed lifecycle failure. [VERIFIED: D-11; current code]
- Calling `ReleaseTransactionManagerOwnership()` before admitted operations terminalize: split admission close/drain from final stop/release. [VERIFIED: D-06/D-08; current `GeniusNode.cpp:997-1000,2013-2042`]
- Cancelling all pending transaction waits from retirement: reserve cancellation for full node shutdown or operations that were never accepted; retirement preserves accepted terminal delivery. [VERIFIED: D-15]
- Using `ScheduleBlockchainRetry()` for replacement failure: keep it only for initial boot paths unless a later explicit selection creates a new generation. [VERIFIED: D-10]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A 60-second production drain timeout should be the default, with an injected deterministic scheduler in tests. | Architecture Pattern 3 | A shorter/longer operational bound may be required; wrong value affects switch availability, not safety if D-08 handling is correct. |
| A2 | Event callbacks should be invoked outside internal locks because callers may re-enter node APIs. | Anti-Patterns | If the project guarantees non-reentrant callbacks, the constraint is conservative; invoking under locks would still increase deadlock risk. |

## Open Questions (RESOLVED)

1. **RESOLVED — D-02 node-state callback/event path.** Add a new public generation-tagged node lifecycle callback registration surface on `GeniusNode`, and drive it from the existing centralized `StateTransition()` path/account-lifecycle commit. Ready and failure delivery use this one surface; polling is not a substitute and no parallel event channel is introduced. [RESOLVED: D-02]

2. **RESOLVED — typed-result migration breadth.** Change public active `GetAddress()` and `GetProcessingStatus()` to their final typed lifecycle results and mechanically inventory and migrate every compile-time caller. Callers that need identity only during startup/configuration use a separate private configured/bootstrap accessor (or fixture-owned configured identity), never the active-address API; no sentinel/default compatibility form remains. Other account-bound checked accessors change only where required by D-03/D-11 and their necessary callers. [RESOLVED: D-03/D-11/D-16]

3. **RESOLVED — explicit recovery while timed-out retirement remains unresolved.** Timeout cleanup and `ACCOUNT_SWITCH_FAILED` release the failed request slot. Exactly one later explicit `SelectAccount()` is accepted immediately with a fresh generation and target-only pending request even while the old runtime remains strongly owned and draining. That recovery generation cannot initialize account, blockchain, manager, or processing owners until `HandleRetiredGenerationDrained` observes real drain; any additional overlapping selection returns `SWITCH_IN_PROGRESS`. No cancellation, synthetic terminalization, old-runtime teardown, automatic retry, or implicit target replay occurs. [RESOLVED: combined D-04/D-08/D-10/D-12]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| CMake | Build and targeted tests | ✓ | 3.31.4 | — [VERIFIED: local probe] |
| Ninja | Existing build backend | ✓ | 1.13.0 | Use configured CMake backend. [VERIFIED: local probe] |
| CTest | Focused phase gate | ✓ | 3.31.4 | Direct GoogleTest binaries. [VERIFIED: local probe] |
| Apple Clang | C++17 compilation | ✓ | Apple clang 16.0.0 | Existing configured compiler. [VERIFIED: local probe] |
| Release `multi_account_test` binary | Quick/current regression inspection | ✓ | Current workspace build | Rebuild target. [VERIFIED: local filesystem and `--gtest_list_tests`] |
| Release `account_management_test` binary | Public API contract tests | ✓ | Current workspace build | Rebuild target. [VERIFIED: local filesystem and `--gtest_list_tests`] |

**Missing dependencies with no fallback:** None. [VERIFIED: local probe]

**Missing dependencies with fallback:** None. [VERIFIED: local probe]

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | GoogleTest/GoogleMock, existing project-resolved version. [VERIFIED: `cmake/functions.cmake:8-14`] |
| Config file | `test/src/multiaccount/CMakeLists.txt`, `test/src/account/CMakeLists.txt`. [VERIFIED: codebase] |
| Build command | `cmake --build build/OSX/Release --target multi_account_test account_management_test -j8` [VERIFIED: targets exist in current configured build] |
| Quick run command | `build/OSX/Release/test_bin/multi_account_test --gtest_filter='MultiAccountTest.AccountGeneration*:MultiAccountTest.RetiredManager*'` [RECOMMENDED: planned test names] |
| Full phase command | `ctest --test-dir build/OSX/Release -R '^(multi_account_test|account_management_test)$' --output-on-failure` [VERIFIED: CTest currently enumerates exactly these two targets] |

### Decision → Test Map

| Decisions | Behavior | Test type | Automated command | File exists? |
|-----------|----------|-----------|-------------------|-------------|
| D-01/D-02 | Acceptance returns generation immediately; ready event later carries the same generation. | integration | `account_management_test --gtest_filter='AccountManagement.SelectAccountReturnsGenerationBeforeReadyEvent'` | ❌ Wave 0 |
| D-03/D-04 | All account operations and a second selection return `SWITCH_IN_PROGRESS` while a barrier holds the active switch. | integration/concurrency | `account_management_test --gtest_filter='AccountManagement.SwitchInProgressRejectsAccountCallsAndOverlap'` | ❌ Wave 0 |
| D-05/D-06 | Admission before close continues; a caller held before admission loses the race and gets `MANAGER_RETIRED`; no post-acceptance admission occurs. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationAdmissionBoundaryIsLinearizable'` | ❌ Wave 0 |
| D-07 | Replacement initialization hook is not reached until old admitted operation emits terminal. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationDrainsBeforeReplacementInitialization'` | ❌ Wave 0 |
| D-08 | Injected drain timeout emits failed/unavailable without cancelling old durable work or starting replacement; old terminal event still arrives. | deterministic timer/concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationDrainTimeoutFailsClosedWithoutCancellation'` | ❌ Wave 0 |
| D-09/D-12 | Failure at each pending init stage cleans all pending owners before one generation-tagged failed event. | parameterized integration | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationInitializationFailureCleansBeforeEvent*'` | ❌ Wave 0 |
| D-10/D-11 | No auto retry/old republish; address is explicitly unavailable; a new explicit selection recovers. | integration | `account_management_test --gtest_filter='AccountManagement.FailedSwitchRequiresExplicitRecovery'` | ❌ Wave 0 |
| D-11/bootstrap compatibility | Internal configured identity remains usable for startup while public active address is unavailable until a ready generation and after failure. | integration | `account_management_test --gtest_filter='AccountManagement.ConfiguredIdentityDoesNotPublishUnavailableGeneration'` | ❌ Wave 0 |
| D-13 | Retained manager rejects transfer, mint, migration, hold/pay escrow, async pay, and direct enqueue paths with `MANAGER_RETIRED`; counters/reservations/queue remain unchanged. | unit/integration | `multi_account_test --gtest_filter='MultiAccountTest.RetiredManagerRejectsEveryMutationEntryPoint'` | ❌ Wave 0 |
| D-14 | Retained manager exposes only frozen generation/state/terminal ledger after runtime release. | unit | `multi_account_test --gtest_filter='MultiAccountTest.RetiredManagerDiagnosticsAreImmutable'` | ❌ Wave 0 |
| D-15 | Accepted terminal callback is delivered exactly once with old generation after selection acceptance and is not attributed to replacement. | deterministic integration | `multi_account_test --gtest_filter='MultiAccountTest.RetiredManagerDeliversAcceptedTerminalOutcomeWithOldGeneration'` | ❌ Wave 0 |
| D-16 | Processing status returns switching, unavailable, then replacement-only status at the causal boundary. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.AccountGenerationProcessingStatusTracksLifecycle'` | ❌ Wave 0 |
| CR-01 regression | Every observable snapshot has either both ready account/manager owners or neither; stale blockchain callback cannot advance a newer generation. | deterministic concurrency | `multi_account_test --gtest_filter='MultiAccountTest.ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent:MultiAccountTest.AccountGenerationRejectsStaleBlockchainCompletion'` | ⚠️ Existing first test must be strengthened; second Wave 0 |

### Required Barrier / Hook Points

| Hook | Interleaving proved |
|------|---------------------|
| After operation admission, before first mutation | Selection can close admission while an already admitted call is paused; the call remains allowed. [VERIFIED: D-05/D-06] |
| Before operation admission | Selection wins and the operation receives `MANAGER_RETIRED` without mutation. [VERIFIED: D-05/D-13] |
| Before reservation and before enqueue | No retirement window strands a nonce/UTXO reservation or appends work after rejection. [VERIFIED: CR-02] |
| After admission close, before `SelectAccount()` return | Once acceptance is observable, a new old-manager call cannot cross admission. [VERIFIED: D-05] |
| Before old terminal transition / drain-zero notification | Replacement initialization has not started. [VERIFIED: D-07] |
| Timeout trigger | Failure is deterministic and does not depend on wall-clock sleep. [VERIFIED: locked discretion] |
| After blockchain callback, before posted transaction init | Old callback cannot advance a newer same-state generation. [VERIFIED: CR-01] |
| Before ready publication | All public snapshots remain empty and processing status remains switching. [VERIFIED: D-03/D-16] |
| After failure cleanup, before failure event | Callback observes no pending/active partial owner. [VERIFIED: D-12] |

### Sampling Rate

- **Per task commit:** Build the touched target and run the exact new filter for that task. [RECOMMENDED: Nyquist validation]
- **Per wave merge:** Run both complete scoped GoogleTest binaries via the CTest command above. [RECOMMENDED: Nyquist validation]
- **Phase gate:** Both scoped binaries green, every new case listed by `--gtest_list_tests`, five consecutive runs of the deterministic lifetime filter, and ThreadSanitizer execution when a configured TSan target is available. [VERIFIED: Phase 13 verification precedent; TSan currently not evidenced as configured]

### Wave 0 Gaps

- [ ] Extend `MultiAccountTestAccess` with default-empty lifecycle hooks, manager admission/queue/terminal diagnostics, timeout trigger, and pending/ready bundle inspection. [VERIFIED: existing friend access]
- [ ] Strengthen `ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent` so `account` and `manager` presence must always match; remove the current `continue` blind spot. [VERIFIED: `multi_account_sync.cpp:693-700`; Phase 13 WR-02]
- [ ] Add the deterministic multi-account tests listed in the map to `test/src/multiaccount/multi_account_sync.cpp`. [RECOMMENDED: decision coverage]
- [ ] Add public acceptance/failure/recovery contract tests to `test/src/account/account_management_test.cpp`. [RECOMMENDED: decision coverage]
- [ ] Add a target-proven TSan build/run step if the repository configuration supports it; otherwise record `NOT_RUN` explicitly rather than implying sanitizer coverage. [VERIFIED: Phase 13 verification recorded sanitizer `NOT_RUN`]

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No new authentication mechanism | Preserve the existing account key and node trust signer behavior; this phase changes lifecycle only. [VERIFIED: scope fence] |
| V3 Session Management | No | Account generations are local service lifecycles, not user sessions. [VERIFIED: codebase scope] |
| V4 Access Control | Yes | Manager lifecycle/admission gate prevents retained old-generation authority from mutating account state. [VERIFIED: D-05/D-13/D-14] |
| V5 Input Validation | Yes | Continue address normalization/availability validation before accepting a target; add explicit lifecycle-state validation at every account-bound API. [VERIFIED: `GeniusNode.cpp:2493-2513`; D-03/D-04] |
| V6 Cryptography | No new cryptography | Do not change signatures, trust policy, or key ownership in this phase. [VERIFIED: scope fence] |

### Known Threat Patterns for the Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Retained old manager mutates nonce/UTXO/queue after switch | Tampering / Elevation of Privilege | One serialized admission gate plus permanent `MANAGER_RETIRED`. [VERIFIED: CR-02; D-13] |
| Stale async callback initializes or publishes the wrong generation | Tampering | Generation and expected-owner identity checks at every callback and commit. [VERIFIED: CR-01; D-02] |
| Partial target address/manager becomes visible | Information Disclosure / Tampering | Empty public snapshot until one complete ready commit; typed unavailable errors. [VERIFIED: D-09/D-11] |
| Nonterminal operation blocks account availability forever | Denial of Service | Finite drain timeout and fail-closed unavailable state without unsafe cancellation. [VERIFIED: D-08] |
| Old terminal event attributed to replacement | Repudiation / Tampering | Immutable manager generation in every operation record and terminal event. [VERIFIED: D-15] |
| Reentrant callback deadlocks lifecycle | Denial of Service | Copy callbacks/state under lock and invoke after unlock. [ASSUMED] |
| Processing status reads a concurrently reset owner | Denial of Service / Information Disclosure | Strong lifecycle snapshot and lifecycle-first mapping. [VERIFIED: CR-02; D-16] |

## Sources

### Primary (HIGH confidence)

- `.planning/phases/14-account-generation-publication-and-retired-manager-lifecycle/14-CONTEXT.md` — locked D-01 through D-16, discretion, and scope exclusions.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-REVIEW.md` — CR-01/CR-02 source-level interleavings and WR-02 test blind spot.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-VERIFICATION.md` — independently confirmed account-lifecycle gaps and sanitizer/evidence status.
- `src/account/GeniusNode.hpp` and `src/account/GeniusNode.cpp` — public API, owner fields, state machine, selection, shutdown, publication, status, and callback paths.
- `src/account/TransactionManager.hpp` and `src/account/TransactionManager.cpp` — manager errors, public mutators, stop behavior, queueing, terminal waits, and transaction state changes.
- `test/src/multiaccount/multi_account_sync.cpp` and `test/src/account/account_management_test.cpp` — current fixtures, deterministic synchronization precedent, public behavior, and coverage gaps.
- `test/src/multiaccount/CMakeLists.txt`, `test/src/account/CMakeLists.txt`, and `cmake/functions.cmake` — exact test targets and CTest registration.
- Local environment probes on 2026-08-18 — tool versions, configured binaries, and CTest enumeration.

### Secondary (MEDIUM confidence)

- None. This narrowly bounded phase is fully researchable from locked decisions and the current codebase. [VERIFIED: research scope]

### Tertiary (LOW confidence)

- The proposed 60-second default timeout and callback-outside-lock convention are marked `[ASSUMED]` and isolated in the Assumptions Log.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependency; all recommended facilities are already configured and used. [VERIFIED: codebase and local probes]
- Architecture: MEDIUM — the three-bundle and admitted-ledger decomposition is prescriptive and consistent with every locked decision, but it is proposed rather than implemented. [VERIFIED: D-01 through D-16]
- Mutation inventory: HIGH — traced from public manager entry points through nonce/UTXO/queue boundaries and node bypasses. [VERIFIED: codebase]
- Pitfalls: HIGH — principal races are independently documented by Phase 13 review/verification and reproduced by direct source trace. [VERIFIED: Phase 13 artifacts]
- Validation architecture: HIGH — targets and current tests were enumerated locally; new test names/hooks are proposed. [VERIFIED: local probe]

**Research date:** 2026-08-18
**Valid until:** 2026-09-17, or until `GeniusNode`/`TransactionManager` lifecycle code changes materially
