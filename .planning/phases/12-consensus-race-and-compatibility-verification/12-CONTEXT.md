# Phase 12: Consensus Race and Compatibility Verification - Context

**Gathered:** 2026-07-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 12 proves the complete v2.0 finality path delivered by Phases 9-11. It strengthens the real 11-node single-burn race and adds deterministic regression coverage for the certificate-before-application gap, durable restart, candidate ordering, certificate indexes, duplicate/conflicting delivery, and ordinary-transaction compatibility. It may add private test seams and structured test instrumentation, but it does not redesign consensus semantics, bridge startup wiring, or RPC transport.

</domain>

<decisions>
## Implementation Decisions

### Race Setup and Participation Evidence
- **D-01:** Create exactly one external burn only after all 11 nodes have been constructed and verified `READY`; then configure every node's RPC endpoint back-to-back and check every `ConfigureRpcEndpoint` return value.
- **D-02:** All 11 nodes must independently observe that same burn and submit their own mint proposal for the same canonical slot. Merely becoming ready, joining a slot, or eventually learning the winner is insufficient evidence that a node competed.
- **D-03:** Every node must converge on the same certified winning transaction hash. That transaction is confirmed once, the destination balance increases by exactly the burn amount, and every losing proposal remains unconfirmed.
- **D-04:** Capture each validator's published signatures for the contested slot and prove that no validator produces more than one usable signature across all competing proposal IDs. Exactly one valid certificate may reach quorum.
- **D-05:** Race failures must emit a bounded structured summary per node containing the canonical burn slot, observed proposal IDs, published vote target, certificate winner, relevant transaction status, and final balance. Do not rely on manual full-log archaeology as the primary diagnostic.

### Deterministic Interleavings
- **D-06:** Reproduce the original `HandleCertificate()`-before-CRDT/application gap with a friend-only deterministic pause in the real production path after authoritative slot finality is established but before transaction application or cleanup.
- **D-07:** Coordinate interleaving tests with predicate-based barriers or test-controlled triggers. Do not use repeated scheduling attempts, short sleeps, or detached threads to manufacture correctness.
- **D-08:** While application is paused, authoritative slot lookup must already return the winner; a competing proposal must be unable to obtain another signature or certificate; identical certificate delivery must be idempotent; and a different certificate must be rejected and recorded as a conflict.
- **D-09:** Model restart by destroying the consensus manager and reconstructing it over the same durable datastore. The new instance must restore and, where applicable, replay the exact stored vote, never sign a competitor.
- **D-10:** Control candidate ordering with a test-controlled clock/deadline trigger. One case submits a better candidate before selection freezes and proves it can win; another publishes the first vote before the better candidate arrives and proves no second signature is created.

### Test Organization and Instrumentation
- **D-11:** Keep `bridge_race_single_burn_test` as the genuine 11-node end-to-end proof. Use focused deterministic tests for exact certificate/application ordering, restart, candidate timing, storage/index behavior, and compatibility.
- **D-12:** Extend the existing subsystem test targets that own each behavior. Add a dedicated Phase 12 integration target only for deterministic behavior that genuinely crosses component boundaries; do not create one monolithic `phase_12_test.cpp`.
- **D-13:** Collect race evidence through test-only observers/accessors around the real node components. Do not parse `spdlog` text and do not add permanent public diagnostic APIs to production classes.
- **D-14:** Keep the 11-node race as an isolated long-running CTest target with an explicit timeout, but make it a mandatory Phase 12 verification gate. Focused deterministic tests remain part of the normal fast suite.
- **D-15:** Preserve production-realistic watcher, proposal, vote, certificate, and transaction paths in the 11-node test. Private controls are for deterministic focused tests, not a replacement consensus or mock race.

### Compatibility and Completion Gate
- **D-16:** Phase 12 completion requires the entire repository test suite, including unrelated components and external integrations, rather than only a curated compatibility subset.
- **D-17:** Every test whose declared prerequisites are available must run and pass. An unavailable external prerequisite may only produce an explicit, reviewed skip that records exactly what dependency was missing; silent omission is not acceptable.
- **D-18:** Slot/index corruption tests assert the typed failure category, unchanged authoritative state, and diagnostic emission. They must distinguish absence from corruption without binding to exact log wording.
- **D-19:** Exercise every externally reachable certificate ingress path. Identical delivery must apply the winner once, while a conflicting certificate preserves the original winner and updates one deduplicated conflict record.
- **D-20:** Existing nonce-chain and producer-UTXO consumers must continue resolving certificates through the verified transaction-hash secondary index backed by slot-keyed authoritative storage.

### the agent's Discretion
- Exact names and placement of friend-only observers, pause points, controlled clocks, and the cross-component integration target.
- The bounded diagnostic summary's formatting, provided every field required by D-05 is present and no test depends on parsing human log text.
- Concrete timeout values based on measured startup, race, assertion, and teardown budgets; increasing a timeout alone is not evidence that a stuck node is correct.
- The full-suite invocation and reviewed-skip report format, provided every configured test and prerequisite is accounted for.

### Folded Todos
- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix):** The existing fixture reaches 11/11 watcher startup but does not prove all nodes complete the single-burn race within the current window. Phase 12 owns diagnosing this as liveness versus a stuck retry, adding per-node structured timing/state evidence, proving all 11 proposals participate, and ensuring teardown completes within its explicit budget. A timeout-only workaround does not satisfy the phase.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone Scope and Requirements
- `.planning/PROJECT.md` — v2.0 project intent, safety problem, architectural boundary, and original race evidence.
- `.planning/ROADMAP.md` — Phase 12 goal, dependencies, TEST-01..06 mapping, and success criteria.
- `.planning/REQUIREMENTS.md` — normative TEST-01 through TEST-06 requirements.

### Finality Architecture Established by Prior Phases
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-CONTEXT.md` — canonical burn slot identity, slot-keyed authoritative certificate storage, verified transaction index, and fail-closed corruption rules.
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-VERIFICATION.md` — implemented Phase 9 behavior and focused verification evidence.
- `.planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-CONTEXT.md` — durable one-vote-per-slot contract, candidate window, finalization ordering, idempotency, and conflict evidence.
- `.planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-VERIFICATION.md` — implemented Phase 10 behavior and remaining end-to-end boundary assigned to Phase 12.
- `.planning/phases/11-slot-owned-bridge-burn-reservations/11-CONTEXT.md` — slot-owned burn reservation, finalization, atomic application, consumption, and release rules.
- `.planning/phases/11-slot-owned-bridge-burn-reservations/11-VERIFICATION.md` — implemented Phase 11 behavior and focused safety evidence.

### Race Harness and Observed Failure
- `.planning/todos/pending/bridge-race-not-all-11-mint-within-window.md` — folded failure report, ruled-out causes, current timeout, and teardown observations.
- `src/account/log_bridge_race.txt` — user-provided runtime evidence for the observed multi-proposal/mint race; diagnostic input, not a normative design document.
- `test/src/bridge_race/bridge_race_fixture.hpp` — existing 11-node fixture, readiness barrier, watcher configuration, per-node storage, and teardown.
- `test/src/bridge_race/bridge_race_single_burn_test.cpp` — current single-burn setup, endpoint release, balance-only completion assertion, and sleep-based stability check to replace with stronger evidence.
- `test/src/bridge_race/CMakeLists.txt` — isolated bridge-race targets and timeout configuration.

### Focused Compatibility Tests and Test Conventions
- `.planning/codebase/TESTING.md` — test layout, CTest conventions, async predicate barriers, and focused/full-suite commands.
- `.planning/codebase/CONVENTIONS.md` — repository conventions including private friend-only test seams.
- `test/src/blockchain/consensus_certificate_store_test.cpp` — slot and transaction-index lookup, integrity, idempotency, and conflict coverage.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — durable vote lock and restart coverage.
- `test/src/blockchain/consensus_finalization_test.cpp` — finalization ordering, application, duplicate, and conflict behavior.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` — burn reservation lifecycle, restart, terminal identity, and application safety.
- `test/src/blockchain/certificate_compatibility_test.cpp` — normal transaction and certificate compatibility coverage.
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — transaction-manager certificate lookup and nonce/double-spend compatibility behavior.
- `test/src/account/utxo_manager_test.cpp` — producer input and atomic mint-effect persistence behavior.
- `test/testutil/wait_condition.hpp` — established predicate-based bounded-wait utility for asynchronous tests.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `BridgeRaceE2ETest`: Already creates one full and ten light nodes, waits for all 11 to become `READY`, leaves endpoint configuration to the test body, and isolates per-node persistent state.
- `ASSERT_WAIT_FOR_CONDITION`: Provides bounded predicate synchronization and should replace correctness assertions based on elapsed sleeps.
- Existing consensus friend-access fixtures: Phase 10 and Phase 11 tests already exercise private durable state, finalization, reconciliation, and restart without widening public APIs.
- Existing slot/index and compatibility targets: Most TEST-03 through TEST-06 primitives already have owning focused binaries suitable for extension.

### Established Patterns
- All nodes are constructed first and collectively checked for readiness before the burn/race trigger.
- `ConfigureRpcEndpoint` returns a boolean and every node's result is already asserted in the single-burn loop.
- Certificate storage is authoritative by canonical slot; transaction hash is a verified secondary lookup only.
- Finality must be durably established before application, cleanup, or resource retirement.
- Asynchronous tests use predicate barriers and bounded timeouts rather than detached threads or sleep-based correctness.
- Tests mirror subsystem ownership under `test/src/`; private test access is preferred over public test hooks.

### Integration Points
- Extend the race fixture at proposal observation, vote publication, certificate observation, and transaction-state convergence boundaries to collect structured evidence.
- Pause focused certificate processing after slot finalization and before application/cleanup so competing ingress can be injected deterministically.
- Reconstruct consensus managers with the same durable store for restart/non-equivocation proof.
- Drive the candidate selection deadline explicitly in focused consensus tests.
- Run authoritative slot lookup and verified transaction-index lookup through each public certificate ingress and existing nonce/UTXO consumers.

</code_context>

<specifics>
## Specific Ideas

- “All competing” means all 11 nodes independently observe the one burn and publish distinct proposals into the same canonical finality slot; it does not mean 11 independent confirmed mints.
- Consensus finality outranks a node's local proposal preference. A node may accept and apply another proposal's valid certificate, but its own durable vote lock must prevent it from signing a competing proposal.
- The application gap is intentional test pressure: slot finality must already prevent new signatures even though CRDT/transaction application has not completed.
- Phase 12 should explain slow nodes with structured per-node state and timing, not make a 90-second assertion pass merely by extending the timeout.

</specifics>

<deferred>
## Deferred Ideas

- Bridge relayer startup wiring, `InitializeRpcEndpoints()` startup integration, and configurable in-process mock RPC transport remain outside Phase 12.

### Reviewed Todos (not folded)
- **Bridge Startup Wiring + Mock RPC Endpoints:** Reviewed and kept separate because it changes node startup and RPC verification infrastructure rather than proving the Phase 9-11 consensus safety boundary.

</deferred>

---

*Phase: 12-consensus-race-and-compatibility-verification*
*Context gathered: 2026-07-30*
