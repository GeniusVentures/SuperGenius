# Phase 12: Multi-Node Finality Fault Proof - Context

**Gathered:** 2026-08-24
**Status:** Ready for planning

<domain>
## Phase Boundary

Build a dedicated production-path multi-node regression suite proving that canonical-slot finality remains safe and live through contention, late delivery, publisher loss, and restart. The suite must use real local PubSub, CRDT, RocksDB, consensus, and transaction/Mint ingress; it validates the existing Phase 8–11 protocol rather than changing it.

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

</code_context>

<specifics>
## Specific Ideas

- Use the real local network even when delivery is deliberately delayed: disconnect and reconnect actual peers rather than replacing PubSub/CRDT with mocks.
- PubSub recipient cleanup safety needs an explicit passive-recipient assertion, not an inference from the final certificate record.
- “Production-path” means that test helpers coordinate faults and observe behavior only; they do not call local-author, local-receive, or direct Mint completion helpers.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `bridge-startup-wiring-mock-rpc.md` — matched only on generic “node” terms; startup/RPC wiring is unrelated to Phase 12's finality fault proof.

</deferred>

---

*Phase: 12-multi-node-finality-fault-proof*
*Context gathered: 2026-08-24*
