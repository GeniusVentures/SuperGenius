# Phase 12: Multi-Node Finality Fault Proof - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-24
**Phase:** 12-multi-node-finality-fault-proof
**Areas discussed:** Test topology, Fault injection, Suite shape

---

## Test topology

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated in-process three-node harness | Real PubSub, CRDT, RocksDB, consensus, and transaction/Mint ingress assembled in one test process. | ✓ |
| Three full `GeniusNode` instances | Stronger facade-wiring coverage, but slower and more timing-fragile. | |
| Hybrid | Core faults in a dedicated harness plus one full-node smoke scenario. | |

**User's choice:** Dedicated in-process harness.

| Option | Description | Selected |
|--------|-------------|----------|
| Three validators plus one passive recipient | Separates quorum behavior from recipient behavior and proves recipients do not write `/cert/<slot>`. | ✓ |
| Three validators only | Smaller topology, with receipt behavior asserted only on validators. | |
| Recipients only in propagation scenarios | Adds a fourth node only where needed. | |

**User's choice:** Three validators plus one passive recipient.

| Option | Description | Selected |
|--------|-------------|----------|
| Separate on-disk RocksDB directories reused on restart | Exercises actual durable vote, certificate, UTXO, and marker recovery. | ✓ |
| Fresh databases after restart | Tests rejoin/sync but not local recovery. | |
| In-memory storage | Cannot prove restart requirements. | |

**User's choice:** Separate persisted storage per peer, reused on restart.

| Option | Description | Selected |
|--------|-------------|----------|
| All messages use real PubSub/CRDT routes | Test seams observe/control faults but never invoke author or receive shortcuts. | ✓ |
| Real transport only for certificates and CRDT | Directly inject proposals/votes. | |
| Current internal consensus helpers | Faster but not TEST-06 proof. | |

**User's choice:** All proposals, votes, certificates, and transactions use real ingress.

---

## Fault injection

| Option | Description | Selected |
|--------|-------------|----------|
| Control real connectivity and lifecycle | Disconnect/reconnect peers and stop/recreate nodes without mocked delivery. | ✓ |
| Test-only transport gate | Buffer or drop selected traffic before delivery. | |
| Mix both | Use partitions primarily and a gate for narrow timing. | |

**User's choice:** Control actual peer connectivity and lifecycle.

| Option | Description | Selected |
|--------|-------------|----------|
| Post-persistence/pre-PubSub barrier | Freeze after durable certificate persistence and before normal notification. | ✓ |
| Poll and race shutdown | Infer the boundary through timing. | |
| Loss before persistence only | Does not prove persistence-before-advertisement. | |

**User's choice:** A narrow barrier at the actual persistence/advertisement boundary.

| Option | Description | Selected |
|--------|-------------|----------|
| Real durable-boundary barriers | Pause after vote, certificate, or Mint durable boundaries before restart. | ✓ |
| Arbitrary restart timing | Cannot prove which boundary was exercised. | |
| Unit failure switches only | Precise but not multi-node production-path coverage. | |

**User's choice:** Barrier at each actual durable recovery boundary.

| Option | Description | Selected |
|--------|-------------|----------|
| Read-only boundary instrumentation plus durable assertions | Detects forbidden local actions as well as converged end state. | ✓ |
| Final state only | Cannot prove an invalid intermediate action never happened. | |
| Logs only | Not strong enough for regression proof. | |

**User's choice:** Read-only instrumentation plus durable-state assertions.

---

## Suite shape

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated five-scenario CTest binary | Named contention, late-delivery, restart, failover, and route-audit scenarios. | ✓ |
| One long end-to-end test | Single operator story but poor failure isolation. | |
| Extend existing binaries | Reuses fixtures but obscures the cross-node contract. | |

**User's choice:** A new dedicated `multi_node_finality_fault_test` binary.

| Option | Description | Selected |
|--------|-------------|----------|
| Normal non-disabled CTest target | Routine validation catches finality regressions. | ✓ |
| Integration label | Faster defaults but easier to omit. | |
| Nightly/manual only | Too weak for the safety contract. | |

**User's choice:** Normal CTest execution.

| Option | Description | Selected |
|--------|-------------|----------|
| About two minutes | Fast routine feedback. | |
| Up to five minutes | More tolerance for real local replication and restart. | ✓ |
| No fixed budget | Makes hangs and flakiness harder to identify. | |

**User's choice:** Up to five minutes for the suite.

| Option | Description | Selected |
|--------|-------------|----------|
| Per-node durable effects | Each peer proves one winning Mint, no loser, and no duplicate effect after recovery. | ✓ |
| Destination node only | Misses validator/recipient recovery. | |
| Network-wide aggregate | Can hide a duplicate and a missing effect. | |

**User's choice:** Per-node durable effects.

---

## the agent's Discretion

- Pick the narrowest component-level harness, port allocation, test-access shape, and bounded waits compatible with the decisions above.

## Deferred Ideas

- `bridge-startup-wiring-mock-rpc.md` remains out of scope: it is startup/RPC work, not finality fault proof.

---

## 12-07 Context Update — 2026-08-26

### Restart failure scope

| Option | Description | Selected |
|--------|-------------|----------|
| Deterministic root cause first | Reproduce and trace the first broken existing recovery boundary before changing behavior. | ✓ |
| Immediate recovery-path fix | Change Mint-marker recovery from the initial intermittent trace. | |
| Harness-only explanation | Treat the valid-topology fresh failure as test lifecycle work. | |

**User's choice:** Deterministic root cause first, with a one-boundary RED/GREEN TDD fix only after proof. If all durable state is correct, repair only the faulty test observer/fixture.

### Proof threshold

| Option | Description | Selected |
|--------|-------------|----------|
| Repeatable fresh failure | Require the same boundary failure in two independent real-socket processes. | ✓ |
| One fully traced fresh failure | Permit a fix after one failure. | |
| Controlled fault injection | Force the failure with test control. | |

**User's choice:** Require two fresh reproductions before a production fix, then three fresh targeted passes and three normal serial-suite passes. Otherwise, stop without a fix and retain structured diagnosis with stable raw-log paths.

### Observation boundary

| Option | Description | Selected |
|--------|-------------|----------|
| Friend-scoped passive snapshots | Read existing local recovery state without a production API or behavior change. | ✓ |
| Logs only | Infer the boundary from logging. | |
| New production diagnostics API | Expose recovery state in product code. | |

**User's choice:** Capture state/error snapshots at every existing certificate-to-Mint boundary. No pauses, retries, reordering, injected failures, serialized payloads, or key material.

### Late and publisher outcomes

| Option | Description | Selected |
|--------|-------------|----------|
| Focus only on restart | Keep late/publisher enabled but defer their distinct diagnostics. | ✓ |
| Investigate all three together | Broaden 12-07 into a general reliability effort. | |
| Remove other scenarios temporarily | Reduce suite noise. | |

**User's choice:** Focus 12-07 on restart. Record any late/publisher failures during it but do not act on them. Phase 12 remains blocked even if restart is fixed, until the other diagnostics are separately scoped.
