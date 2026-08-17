# Phase 14: Account-generation publication and retired-manager lifecycle safety - Context

**Gathered:** 2026-08-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 14 closes the account-lifecycle portion of Phase 13's final verification gaps. It makes `SelectAccount()` an explicit asynchronous generation transition, prevents partial account/manager publication and overlapping switches, drains work already accepted by the retiring manager, rejects new work at the switch boundary, and makes retained retired-manager references incapable of further mutation.

This phase includes coherent node-level processing-status ownership because it is part of the same account generation. It does **not** include bridge provider/relayer/watcher ownership, trusted-peer refresh coalescing or timer lifetime, or a repository-wide `AtomicTransaction`/GlobalDB capability redesign; those are separate follow-up scopes.

</domain>

<decisions>
## Implementation Decisions

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

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Source findings and prior decisions
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-VERIFICATION.md` — authoritative CR-01/CR-02 account-lifecycle gaps and missing deterministic evidence.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-REVIEW.md` — exact partial-publication, overlapping-switch, retired-manager mutation, and processing-owner findings.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-CONTEXT.md` — prior node-scoped policy/account decisions and the complete-pair publication direction carried into this phase.
- `.planning/REQUIREMENTS.md` — v1.1 requirement history and traceability that Phase 14 must reconcile rather than reinterpret.

### Account lifecycle implementation
- `src/account/GeniusNode.hpp` — public account APIs, `AccountServiceSnapshot`, lifecycle ownership, processing-status surface, switching flag, and generation state.
- `src/account/GeniusNode.cpp` — `SelectAccount`, `ShutdownAccountBoundServices`, snapshot/publication checks, asynchronous blockchain/transaction initialization, and account-bound public call sites.
- `src/account/TransactionManager.hpp` — public mutation, queueing, stop, and diagnostic surfaces that must distinguish active from retired ownership.
- `src/account/TransactionManager.cpp` — current `Stop`, transfer/mint/migration/escrow submission, reservation, and enqueue paths implicated by retired-manager mutation.

### Existing verification assets
- `test/src/multiaccount/multi_account_sync.cpp` — generation snapshots, account-switch concurrency fixtures, lifecycle diagnostics, and the existing test blind spot called out by Phase 13 verification.
- `test/src/account/account_management_test.cpp` — current external `SelectAccount()` behavior and account-management expectations.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `GeniusNode::AccountServiceSnapshot`: already groups account, manager, and generation and is the natural public ownership snapshot to make unavailable throughout switching.
- `account_service_generation_` and owner-generation diagnostics: existing generation identifiers can bind asynchronous blockchain/transaction callbacks and node-state events.
- `ShutdownAccountBoundServices`: existing retirement orchestration can become the non-blocking-lock boundary for close, drain, stop, and release.
- `TransactionManager::Stop` plus existing submission/queue paths: current lifecycle hook and mutation surfaces can be unified behind an admission/retirement gate.
- `MultiAccountTestAccess` and current multi-account fixtures: reusable place for deterministic barriers around snapshot, admission close, drain, stale callback, publication, and failure cleanup.

### Established Patterns
- Public fallible operations use `outcome::result`, so lifecycle-specific errors should remain typed results rather than exceptions.
- Account/manager owners are copied under `lifecycle_mutex_`; blocking stop and join work happens after owners are moved out of the protected state.
- Asynchronous node work already uses generation checks and weak-owner callbacks; Phase 14 must extend that pattern to every switch completion path instead of adding a parallel lifecycle system.
- Phase 13 verification treats strong snapshots as lifetime-safe but not mutation-safe; Phase 14 must add admission/retirement semantics without weakening coherent snapshots.

### Integration Points
- `GeniusNode::SelectAccount` is the acceptance boundary and must close old admission before returning success.
- Blockchain start/retry/completion and transaction initialization callbacks must carry the requested generation and expected owner identity.
- Every account-bound `GeniusNode` operation must map switching, failed/unavailable, and ready states to the decisions above.
- Every mutation-capable `TransactionManager` entry point and its final reservation/enqueue boundary must honor retirement.
- `GetProcessingStatus` must snapshot lifecycle ownership instead of reading `processing_service_` concurrently with teardown.

</code_context>

<specifics>
## Specific Ideas

The intended API is explicitly asynchronous: `SelectAccount()` acknowledges acceptance, while generation-tagged node-state events announce readiness or a fully cleaned failure. The safety boundary is strict—old admission is closed before acceptance is returned, accepted work drains before replacement initialization, and no public API displays a partial, retired, or failed generation as active.

</specifics>

<deferred>
## Deferred Ideas

- Bridge provider/relayer/catch-up watcher ownership and publication races — separate follow-up phase.
- Trusted-peer refresh coalescing and retry-timer lifetime — separate follow-up phase.
- Repository-wide CRDT mutation capabilities and raw `AtomicTransaction`/GlobalDB authority — not required to close the account-manager lifecycle contract and must not be pulled into Phase 14 without its own phase.

### Reviewed Todos (not folded)
- **Secure trusted-peer genesis configuration** — belongs to the Phase 13 trust-root scope, not account-generation lifecycle.
- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix)** — bridge reliability work; outside Phase 14.
- **Bridge Startup Wiring + Mock RPC Endpoints** — bridge integration work; outside Phase 14.

</deferred>

---

*Phase: 14-Account-generation publication and retired-manager lifecycle safety*
*Context gathered: 2026-08-17*
