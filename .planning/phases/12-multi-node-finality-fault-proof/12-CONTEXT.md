# Phase 12: Multi-Node Finality Fault Proof - Context

**Gathered:** 2026-08-26
**Status:** Updated for scoped 12-08 planning; Phase 12 remains blocked

<domain>
## Phase Boundary

Build a dedicated production-path multi-node regression suite proving that canonical-slot finality remains safe and live through contention, late delivery, publisher loss, and restart. The suite must use real local PubSub, CRDT, RocksDB, consensus, and transaction/Mint ingress; it validates the existing Phase 8–11 protocol rather than changing it.

**12-07 scope:** Diagnose only the intermittent, fresh-process restart failure at the existing Mint completion boundary. It must identify the first broken existing transition before any recovery-code change is authorized. Late-contender and publisher-loss outcomes stay enabled as separate diagnostics; they do not broaden this plan.

**12-08 scope:** Diagnose only the intermittent publisher-loss scenario failure that occurs at public peer/topic readiness before its persistence-loss fault begins. It must distinguish real transport/topology construction failure from a test-harness lifecycle issue before any repair is authorized. Certificate authority, CRDT precedence, PubSub notification, selected-publisher behavior, restart recovery, and late-contender behavior remain out of scope.

</domain>

<decisions>
## Implementation Decisions

### Node topology and ingress

- **D-01:** Use a dedicated in-process harness with four real local peers: three validators and one passive non-validator recipient. Do not use the broad `GeniusNode` integration harness as Phase 12's primary test boundary.
- **D-02:** Each peer has an independent on-disk RocksDB directory. A restart recreates the peer from that same directory, so vote, certificate, transaction, UTXO, and bridge-marker durability are exercised.
- **D-03:** Proposals, votes, certificates, and transaction propagation must all travel through their normal PubSub/CRDT ingress routes. Test code may observe and synchronize those routes, but must not invoke local receive/author shortcuts.

### Fault control and observability

- **D-04:** Create propagation disorder through real local peer connectivity and lifecycle: deliberately start disconnected, connect/reconnect at named barriers, and stop/recreate peers. Do not substitute a mocked transport or direct delivery.
- **D-05:** A narrow test-only barrier may freeze the selected publisher after successful durable `/cert/<slot>` persistence and before the normal PubSub notification, proving the persistence-before-advertisement and failover boundary.
- **D-06:** Restart cases use barriers at actual durable boundaries: after vote persistence, after durable certificate acceptance, and between idempotent Mint effects and bridge-marker persistence. Then stop and recreate the affected peer.
- **D-07:** Add read-only instrumentation at existing production boundaries to count authoritative certificate-write attempts, vote publications, certificate notifications, and Mint effects. Combine those counters with durable-state assertions; observers must not alter control flow.

### Suite structure and proof

- **D-08:** Add a dedicated `multi_node_finality_fault_test` CTest binary with five named scenarios: same-burn contention; late contender plus passive-recipient behavior; restart recovery boundaries; publisher loss/failover; and a full production-route audit that demonstrates TEST-06 across the suite.
- **D-09:** Register the suite as a normal, non-disabled CTest target. It may take up to five minutes in total, but every wait must be bounded and condition-based.
- **D-10:** Each successful scenario proves per-node durable outcome: exactly one application of the exact winning Mint, no losing-Mint effect, and no duplicate UTXO or bridge-executed marker after recovery. Network-wide convergence alone is insufficient.

### 12-07 restart diagnosis and repair gate

- **D-11:** Scope 12-07 to the fresh-process restart/Mint-marker recovery failure only. Do not mix in late-contender or publisher-readiness diagnosis.
- **D-12:** Use friend-scoped, test-only, passive snapshots at existing recovery boundaries: accepted certificate readback, exact transaction lookup/binding, UTXO or outpoint state, local `/bridge/executed/<chain>:<burn>` marker read/write result, certificate-work journal state, and tracked transaction status. Snapshots record canonical identifiers, state, error code, and sequence number only—never serialized certificate/transaction payloads or keys.
- **D-13:** Tracing must not pause, reorder, retry, or inject failures. The existing real restart scenario remains the sole driver of consensus, CRDT, PubSub, and Mint behavior.
- **D-14:** A production recovery fix is authorized only after the same broken boundary reproduces in at least two independently started real-socket processes. Add a RED production-path regression for that exact boundary, then make the smallest TDD fix there. If snapshots prove durable state is correct and only the observer is wrong, repair only that test observer/fixture.
- **D-15:** A repaired restart boundary requires three fresh targeted passes and three normal serial full-suite passes. If the failure cannot reproduce twice, record a structured diagnosis and keep Phase 12 blocked—do not compensate with retries, wider recovery, timing changes, or a test-only workaround.
- **D-16:** Keep late-contender and publisher-loss scenarios enabled in the serial suite. If they fail during 12-07, retain their traces but do not act on them; even a completed restart fix leaves Phase 12 blocked pending separately scoped plans for those diagnostics.

### 12-08 publisher-readiness diagnosis and repair gate

- **D-17:** Scope 12-08 to publisher-loss failures at `ConnectAndWaitForPeers` before the selected publisher has persisted a certificate or the publisher-loss barrier has been reached. The two successful fresh publisher runs establish that the certificate protocol is not the initial target.
- **D-18:** Use passive, test-owned snapshots of each peer's existing public readiness facts: `GossipPubSub::IsStarted()`, host connectedness to intended peers, consensus-topic mesh peer count, peer identity, listener/root lifecycle, and the first failed predicate. Observation must not add peers, retry publishing, pause transport, alter waits, or otherwise steer readiness.
- **D-19:** Authorize a repair only if at least two independent fresh real-socket runs fail at the same first readiness boundary with the same normalized state/error. If the evidence is not repeatable, record the diagnosis and keep Phase 12 blocked; do not tune timeouts, add retries, or modify product behavior.
- **D-20:** If a repeated boundary proves a fixture lifecycle defect, change only the smallest test-harness lifecycle/ownership behavior and prove it with three targeted publisher-loss passes plus three normal serial-suite passes. Do not change `SubmitCertificate`, CRDT writes/filtering, deterministic publisher selection, PubSub retries, or receiver behavior.

### the agent's Discretion

- Choose the smallest existing component-level harness shape, port allocation strategy, test-access seams, and scenario partitioning consistent with the locked real-transport and durable-boundary rules.
- Choose bounded per-scenario timeouts within the five-minute total suite budget. Reuse the project wait-condition utilities rather than sleeps.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone contract and verified behavior

- `.planning/PROJECT.md` — v3.0 core value, production-path verification constraint, and explicit exclusions.
- `.planning/REQUIREMENTS.md` — authoritative TEST-01 through TEST-06 requirements and the completed protocol requirements that this suite proves.
- `.planning/ROADMAP.md` § Phase 12 — fixed goal, dependency on Phase 11, and success criteria.
- `.planning/STATE.md` — current milestone position and accumulated finality decisions.
- `.planning/phases/09-durable-one-vote-finality/09-CONTEXT.md` — persisted exact-vote and bounded contention rules that late-contender/restart scenarios must preserve.
- `.planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md` — selected publisher, persist-before-PubSub, failover, and receiver-no-write rules.
- `.planning/phases/11-convergent-certificate-consumption-mint-recovery/11-CONTEXT.md` — exact transaction binding and UTXO-before-marker recovery contract.
- `.planning/phases/11-convergent-certificate-consumption-mint-recovery/11-VERIFICATION.md` — prior phase's verified local recovery evidence and its multi-node coverage gap.
- `.planning/phases/12-multi-node-finality-fault-proof/12-06-DIAGNOSIS.md` — retained 18-run fresh-versus-prefix matrix; establishes restart as a valid-topology fresh-process failure, late as inconclusive, and publisher as pre-topology failure.
- `.planning/phases/12-multi-node-finality-fault-proof/12-07-HANDOFF.md` — mandatory no-repair handoff and invariants preserved by any future restart investigation.
- `.planning/phases/12-multi-node-finality-fault-proof/12-07-SUMMARY.md` — restart gate outcome: three fresh passes, no repair authorization, and boundaries that must remain untouched.

### Existing production-path test building blocks

- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — existing three-validator component fixture, MemorySecureStorage setup, consensus lifecycle seams, and real local `GossipPubSub`/`GlobalDB` construction.
- `test/src/crdt/globaldb_integration.cpp` — real local multi-peer PubSub, CRDT replication, reconnection, and convergence test pattern.
- `test/src/multiaccount/multi_account_sync.cpp` — existing multi-node network lifecycle reference; do not inherit its broad scope or sleep-based timing blindly.
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — Mint application, exact winner binding, recovery, and duplicate-effect assertions to extend or reuse.
- `test/testutil/wait_condition.hpp` — mandatory bounded condition-wait utilities for asynchronous test assertions.
- `test/src/blockchain/CMakeLists.txt` and `test/src/crdt/CMakeLists.txt` — test target registration patterns.

### Production boundaries under test

- `src/blockchain/Consensus.cpp` and `src/blockchain/Consensus.hpp` — vote persistence/recovery, selected publisher, certificate ingress/recovery, and round lifecycle.
- `src/account/TransactionManager.cpp` — certificate-to-exact-Mint consumption and durable Mint completion path.
- `src/account/UTXOManager.cpp` — idempotent UTXO durability used by Mint recovery.
- `src/crdt/globaldb/globaldb.hpp` and `src/crdt/impl/crdt_datastore.cpp` — CRDT replication and convergent immutable certificate record semantics.
- `test/src/blockchain/multi_node_finality_fault_test.cpp` — existing real-socket restart scenario and its durable assertions; any 12-07 observation seam must remain passive.
- `test/src/blockchain/multi_node_finality_fault_test.cpp` — existing `ConnectAndWaitForPeers` readiness predicate and publisher-loss scenario; any 12-08 diagnostic seam must remain passive and pre-fault only.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ConsensusPendingLifecycleTest` multi-validator construction: creates independent local PubSub, GlobalDB, account, registry, and consensus peers; it is the nearest focused starting point.
- `GlobalDBIntegrationTest::TestNodeCollection`: demonstrates actual local GossipPubSub peer connection plus CRDT synchronization and convergence checks.
- `MemorySecureStorage`: provides isolated account setup for consensus fixtures without persistent keystore coupling.
- `ASSERT_WAIT_FOR_CONDITION` and `waitForCondition`: provide the required bounded async assertions.

### Established Patterns
- CRDT callbacks are pre-commit; durable readback/recovery, never callback provenance, is the authority boundary.
- Certificates are authoritative only at `/cert/<canonical-slot>` and byte-distinct encodings converge through lower serialized SHA-256 ordering; receiver-side PubSub does not grant write authority.
- Mint completion remains UTXO effects first, then the existing bridge marker; exact transaction binding prevents a same-slot losing Mint from applying.

### Integration Points
- A new test target belongs under `test/src/blockchain/` or the closest existing integration location, with production components—not direct internal handlers—driving the scenarios.
- Narrow friend/test-access hooks may expose barriers and read-only counters at vote, certificate persistence/publication, and Mint boundaries; they cannot inject or alter protocol behavior.
- Per-node filesystem state must survive fixture-level restart recreation while test-generated paths remain isolated.
- `TransactionManager::PersistBridgeExecutedMarker()` writes the local RocksDB `/bridge/executed/<chain>:<burn>` idempotency marker only after Mint effects; recovery must complete this suffix without duplicating UTXOs.

</code_context>

<specifics>
## Specific Ideas

- Use the real local network even when delivery is deliberately delayed: disconnect and reconnect actual peers rather than replacing PubSub/CRDT with mocks.
- PubSub recipient cleanup safety needs an explicit passive-recipient assertion, not an inference from the final certificate record.
- “Production-path” means that test helpers coordinate faults and observe behavior only; they do not call local-author, local-receive, or direct Mint completion helpers.
- The 12-07 diagnostic is intentionally a root-cause gate, not permission to increase timeouts, add retries, alter protocol behavior, or hide other suite failures.
- The 12-08 diagnostic is likewise a root-cause gate: it may explain the readiness fixture, but cannot convert a pre-fault setup failure into a certificate-publication change.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `bridge-startup-wiring-mock-rpc.md` — matched only on generic “node” terms; startup/RPC wiring is unrelated to Phase 12's finality fault proof.

</deferred>

---

*Phase: 12-multi-node-finality-fault-proof*
*Context gathered: 2026-08-24*
