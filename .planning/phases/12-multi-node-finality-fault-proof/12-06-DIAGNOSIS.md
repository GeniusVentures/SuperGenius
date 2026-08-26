---
phase: 12-multi-node-finality-fault-proof
plan: "06"
status: blocked-diagnostic-stop
started: 2026-08-26T00:00:00Z
---

# Phase 12 Plan 06: Fresh-versus-Prefix Harness Diagnosis

This retained baseline runs the existing registered real-socket test binary without changing fixture or production behavior. The complete matrix, raw trace paths, and terminal classifications will be appended from those runs.

## Invocation Contract

- Binary: `build/OSX/Release/test_bin/multi_node_finality_fault_test`
- Run environment: `P12_HARNESS_RUN={fresh|exact-prefix}-{late|restart|publisher}-{1|2|3}`
- Fresh filters: the named red scenario alone.
- Ordered-prefix filters: the exact prefixes declared in `12-06-PLAN.md`.
- Topology validity: `valid` only when the pre-fault `ConnectAndWaitForPeers` predicate has completed; `invalid` for an observed topology assertion failure; `not-reached` otherwise.

## Baseline Matrix

Raw stdout/stderr is retained under `/private/tmp/p12-06-diagnosis/`. The first loop attempt was interrupted and quarantined because its detached launcher overlapped socket processes; no row below relies on it. The rows below are individually launched only after `pgrep -fl multi_node_finality_fault_test` was empty.

| Scenario | Mode | Run | Exact GTest filter | Exit | Topology | Failure phase | Retained trace | Process result |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| late | fresh | 1 | `FinalityFaultNetwork.LateContenderAndPassiveRecipientRemainReceiveOnly` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-late-1.log` | PASS (one test, ~17s) |
| late | fresh | 2 | `FinalityFaultNetwork.LateContenderAndPassiveRecipientRemainReceiveOnly` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-late-2.log` | PASS (one test, ~17s) |
| late | fresh | 3 | `FinalityFaultNetwork.LateContenderAndPassiveRecipientRemainReceiveOnly` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-late-3.log` | PASS (one test, ~17s) |
| late | exact-prefix | 1 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly` | 1 | valid | prefix audit certificate CRDT propagation, `multi_node_finality_fault_test.cpp:708` | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-late-1.log` | 1/3 failed; late body did not provide the target phase |
| late | exact-prefix | 2 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-late-2.log` | PASS (3/3, 50.565s) |
| late | exact-prefix | 3 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly` | 1 | valid | prefix contention bridge-marker assertion, `multi_node_finality_fault_test.cpp:849` | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-late-3.log` | 2/3 passed, 1 failed (49.997s) |
| restart | fresh | 1 | `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-restart-1.log` | PASS (54.438s) |
| restart | fresh | 2 | `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-restart-2.log` | PASS (55.465s) |
| restart | fresh | 3 | `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 1 | valid | Mint-boundary recovery after valid topology, `multi_node_finality_fault_test.cpp:1226`, then reopened-root exactness at `:1239` | `/private/tmp/p12-06-diagnosis/clean-fresh-restart-3.log` | FAIL (100.376s) |
| restart | exact-prefix | 1 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-restart-1.log` | PASS (5/5, 116.229s) |
| restart | exact-prefix | 2 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-restart-2.log` | PASS (5/5, 116.886s) |
| restart | exact-prefix | 3 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-restart-3.log` | PASS (5/5, 115.865s) |
| publisher | fresh | 1 | `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-publisher-1.log` | PASS (18.296s) |
| publisher | fresh | 2 | `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` | 0 | valid | none | `/private/tmp/p12-06-diagnosis/clean-fresh-publisher-2.log` | PASS (18.306s) |
| publisher | fresh | 3 | `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` | 1 | invalid | public topology readiness, `multi_node_finality_fault_test.cpp:520` | `/private/tmp/p12-06-diagnosis/clean-fresh-publisher-3.log` | FAIL (22.873s) before fault execution |
| publisher | exact-prefix | 1 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce:PublisherLossAfterPersistenceUsesDeterministicFailover` | 1 | invalid | publisher public topology readiness, `:520` | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-publisher-1.log` | 5/6 passed; publisher failed (138.624s) |
| publisher | exact-prefix | 2 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce:PublisherLossAfterPersistenceUsesDeterministicFailover` | 1 | valid | earlier prefix contention bridge-marker assertion, `:849` | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-publisher-2.log` | 5/6 passed (133.715s) |
| publisher | exact-prefix | 3 | `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress:SameBurnContentionUsesOneCanonicalSlotAndExactMint:LateContenderAndPassiveRecipientRemainReceiveOnly:ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary:RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce:PublisherLossAfterPersistenceUsesDeterministicFailover` | 1 | valid | earlier prefix restart Mint-boundary recovery, `:1226`/`:1239` | `/private/tmp/p12-06-diagnosis/clean-exact-prefix-publisher-3.log` | 5/6 passed (177.332s) |

## Lifecycle and Topology Evidence

- The restart fresh-production trace records real listeners on ports 54621–54624, `GlobalDB` openings at each same-root RocksDB path, `GossipPubSub Adding new peer` for every peer pair, and a later three-connection state for each surviving peer. The first failed predicate therefore follows successful listener, datastore, real `AddPeers`, and public consensus/Mint setup rather than construction or pre-topology failure.
- The failure is at the unchanged durable Mint-recovery assertion: after restarting the Mint-boundary peer, `HasSingleDurableMint` never became true for every peer. The later reopened-root predicate also timed out. No handler, direct CRDT write, forced timer, mock, retry, sleep, timeout, assertion, CMake, fixture, or production source code was changed.
- Existing tests do not emit the planned `P12_HARNESS_STATE` or `P12_HARNESS_EXCEPTION` labels. Per the execution directive, this baseline was intentionally run before modifying test behavior; the retained native real-socket logs are the available lifecycle trace. Adding the requested instrumentation is not authorized after the fresh-production stop.

## Terminal Classifications

P12_HARNESS_CLASSIFICATION classification=inconclusive scenario=late terminal_outcome=inconclusive failure_phase=mixed-prefix-audit-or-contention-before-late fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-late-1.log topology_evidence=/private/tmp/p12-06-diagnosis/clean-exact-prefix-late-1.log

P12_HARNESS_CLASSIFICATION classification=fresh-production scenario=restart terminal_outcome=fresh-production failure_phase=mint-boundary-recovery-after-valid-topology fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-restart-3.log topology_evidence=/private/tmp/p12-06-diagnosis/clean-fresh-restart-3.log

P12_HARNESS_CLASSIFICATION classification=pre-topology-failure scenario=publisher terminal_outcome=pre-topology-failure failure_phase=public-topology-readiness-before-fault fresh_trace=/private/tmp/p12-06-diagnosis/clean-fresh-publisher-3.log topology_evidence=/private/tmp/p12-06-diagnosis/clean-fresh-publisher-3.log

The classifications are mutually exclusive under the plan's precedence: late cannot be prefix-contamination because its exact prefixes are mixed and fail before the target late phase; restart is fresh-production because a fresh process failed after valid topology; publisher is pre-topology-failure because fresh run three failed at the public topology readiness predicate before the publisher-loss fault executed.
