---
phase: 12-multi-node-finality-fault-proof
source_plan: "12-06"
status: blocked
created: 2026-08-26T00:00:00Z
---

# Phase 12 Plan 07 Handoff: Fresh Production Failure

PHASE_12_STATUS=BLOCKED

Plan 12-06 reached its mandatory no-repair branch. No fixture, CMake, assertion, timeout, Consensus, CRDT, PubSub, TransactionManager, or other production change was made.

P12_HARNESS_CLASSIFICATION classification=inconclusive scenario=late terminal_outcome=inconclusive failure_phase=mixed-prefix-audit-or-contention-before-late repair_authorization=none

P12_HARNESS_CLASSIFICATION classification=fresh-production scenario=restart terminal_outcome=fresh-production failure_phase=mint-boundary-recovery-after-valid-topology fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-restart-1.log fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-restart-2.log fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-restart-3.log topology_evidence=valid-listeners-54621-54624,valid-GlobalDB-RocksDB-roots,valid-real-AddPeers,valid-three-peer-connections invariants=public-CreateProposal-SubmitProposal,real-AddPeers-PubSub,CRDT-durable-certificate-authority,registered-Mint-ingress,Phase-9-exact-local-vote-per-canonical-slot,no-successor-certificate-readvertisement

P12_HARNESS_CLASSIFICATION classification=pre-topology-failure scenario=publisher terminal_outcome=pre-topology-failure failure_phase=public-topology-readiness-before-fault repair_authorization=none

## Evidence

- Retained matrix: `12-06-DIAGNOSIS.md`.
- Fresh restart runs one and two passed. Fresh restart run three failed after the real four-peer topology and public route had been established.
- The exact failure at `test/src/blockchain/multi_node_finality_fault_test.cpp:1226` is `recreated Mint peer repaired its marker through normal certificate recovery`; the same run then timed out at `:1239` after reopening durable roots.
- Listener addresses, four RocksDB root openings, full `AddPeers` calls, and three active peer connections are present in `/private/tmp/p12-06-diagnosis/clean-fresh-restart-3.log` before the failed predicate.
- Late is not proven prefix contamination: fresh runs passed, but its ordered-prefix repetitions were mixed and the failures happened in prefix audit/contention before the intended late predicate.
- Publisher fresh runs one and two passed; fresh run three failed at the unchanged `ConnectAndWaitForPeers` predicate (`test/src/blockchain/multi_node_finality_fault_test.cpp:520`) before the publisher-loss fault executed. This is `pre-topology-failure`, not production-fix authorization.

## Required Next Step

No 12-07 production edit is authorized until a separately scoped, minimal TDD `12-07-PLAN.md` consumes this handoff. That plan must preserve public `CreateProposal`/`SubmitProposal`, real `AddPeers`/PubSub, CRDT durable certificate authority, registered Mint ingress, the Phase 9 exact local vote per canonical slot, and no successor certificate readvertisement. The incomplete late/publisher diagnosis must not be solved by fixture timing, retries, sleeps, direct handlers, direct CRDT writes, mock transport, forced timers, assertion weakening, or CMake timeout changes.
