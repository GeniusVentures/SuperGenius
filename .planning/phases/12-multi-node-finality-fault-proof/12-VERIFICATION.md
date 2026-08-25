---
phase: 12-multi-node-finality-fault-proof
verified: 2026-08-25T18:55:21Z
status: gaps_found
score: 3/5 must-haves verified
overrides_applied: 0
gaps:
  - truth: "Restart scenarios before certificate arrival, after durable certificate acceptance, and during mint application preserve the original vote and produce no duplicate mint."
    status: failed
    reason: "The clean real-socket target fails TEST-04: RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce times out waiting for the four peers to re-establish the libp2p connection and consensus-topic mesh."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "ConnectAndWaitForPeers at line 424 timed out during the restart scenario in the verifier's clean serial CTest run."
    missing:
      - "Make the durable-boundary restart scenario reliably reconnect all four recreated peers, then obtain a clean passing CTest result."
  - truth: "Publisher-loss scenarios prove persistence-before-advertisement and deterministic failover without conflicting slot certificate records."
    status: failed
    reason: "The clean real-socket target fails TEST-05 after CRDT-first publisher-loss recovery: AssertSingleDurableMint expected a freshly recreated peer's Mint-effect counter to be 0 but observed 1."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "Line 544 assertion failed in PublisherLossAfterPersistenceUsesDeterministicFailover; the phase cannot presently supply a passing regression proof of exact-once publisher-loss recovery."
    missing:
      - "Resolve the restart/counter behavior and demonstrate a clean TEST-05 pass while preserving CRDT-first recovery and no successor certificate re-advertisement."
---

# Phase 12: Multi-Node Finality Fault Proof Verification Report

**Phase Goal:** Operators have production-path regression proof that canonical slot finality remains safe and live through contention, propagation disorder, publisher loss, and restart.
**Verified:** 2026-08-25T18:55:21Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Competing proposals for one burn produce one canonical slot, authoritative certificate, and exact winner. | ✓ VERIFIED | Clean real-socket CTest passed `SameBurnContentionUsesOneCanonicalSlotAndExactMint` in 16.664s. The scenario compares equal slot IDs, submits through public `CreateProposal`/`SubmitProposal`, checks the slot certificate's embedded winning hash, and reopens every peer root. |
| 2 | A late contender cannot obtain a second usable vote/certificate; recipients stay receive-only. | ✓ VERIFIED | Clean CTest passed `LateContenderAndPassiveRecipientRemainReceiveOnly` in 17.153s. It checks durable active-vote identity before and after late submissions, passive `CertificateWriteAttempts == 0`, notification/readback, one winner output, and post-restart state. |
| 3 | Restart at vote, accepted-certificate, and Mint boundaries preserves the vote and causes no duplicate Mint. | ✗ FAILED | Clean CTest failed `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` after 53.410s: `ConnectAndWaitForPeers` timed out at line 424 waiting for the public libp2p connection and consensus mesh. |
| 4 | Publisher loss proves persistence-before-advertisement and safe deterministic recovery without conflicting slot authority. | ✗ FAILED | Static ordering is correct, but clean CTest failed `PublisherLossAfterPersistenceUsesDeterministicFailover` after 18.668s: a recreated peer reported Mint effects `1` where the durable assertion expected `0` (line 544). A regression proof must pass to establish this truth. |
| 5 | The suite uses production PubSub, CRDT, RocksDB persistence, and Mint ingress—not local-author shortcuts. | ✓ VERIFIED | Clean CTest passed `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress` in 17.194s; the four-peer fixture constructs real `GossipPubSub`/`GlobalDB`, invokes public proposal APIs, and observes registered Mint effects. Static source gate found no direct handlers, direct CRDT writes, forced timers, mock transport, or sleep synchronization. |

**Score:** 3/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | Persistent four-peer production-route audit and all TEST-01–05 scenarios | ⚠️ PARTIAL | Exists and is substantive (1,147 lines; five named scenarios); scenario wiring is real, but the TEST-04 and TEST-05 behaviors fail in a clean execution. |
| `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` | Production Blockchain + TransactionManager lifecycle compatibility | ✓ VERIFIED | Exists and substantive; uses real `GossipPubSub`, `GlobalDB`, `Blockchain::New`, `TransactionManager::New`, stop/recreate at the unchanged root, and `AddPeers`. Its CTest passed in 6.12s. |
| `test/src/blockchain/CMakeLists.txt` | Enabled bounded normal CTest registration | ✓ VERIFIED | Both real-socket targets are registered with `RUN_SERIAL TRUE`; the fault target has `TIMEOUT 300`. |
| `src/blockchain/Consensus.hpp` / `.cpp` | Friend-only observers and durable-write boundaries | ✓ VERIFIED | Friend access is private; production path increments write counters around `PutConvergentImmutable`, pauses only after success, then calls the unchanged `Publish`. |
| `src/account/TransactionManager.hpp` / `.cpp` | Friend-only Mint observer after effects and before bridge marker | ✓ VERIFIED | `ParseTransaction` succeeds before the counter/barrier; `PersistBridgeExecutedMarker` remains afterward. |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| Fault test | `ConsensusManager::SubmitProposal` | Public production submission | ✓ WIRED | Every scenario uses `CreateProposal` then `SubmitProposal`; no direct receive/handler path appears in the test source. |
| `Consensus.cpp` | `GlobalDB::PutConvergentImmutable` → `Publish` | Durable certificate before notification | ✓ WIRED | Lines 2107–2127 perform immutable write, increment success, optionally pause, then construct and publish the certificate message. |
| Certificate callback | registered TransactionManager Mint consumer | committed readback → `ParseTransaction` → marker | ✓ WIRED | Accepted certificate work calls its handler only after durable readback; `TransactionManager.cpp` lines 5441–5450 parse effects before persisting the bridge marker. |
| Fault test | peer lifecycle | `StopPeer`/recreate unchanged root/`AddPeers` | ✓ WIRED | `RestartPeer` rebuilds the same root; `ConnectAndWaitForPeers` uses public `AddPeers` and public host/topic readiness. Its execution reliability is the TEST-04 gap above. |
| Publisher-loss test | CRDT-first recovery | persisted original record, original notification count zero, later-round eligibility, restart/reconnect | ⚠️ PARTIAL | The code intentionally does not add successor certificate re-advertisement, consistent with the Phase 12 decision that CRDT authority precedes PubSub cleanup. The final exact-once assertion fails, so the end-to-end link is not proven. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| --- | --- | --- | --- | --- |
| Four-peer fault fixture | proposals/certificates/Mint effects | public `SubmitProposal` → real GossipPubSub → consensus → immutable CRDT → committed certificate handler → TransactionManager | Yes; the clean audit and contention/late-recipient scenarios passed over local sockets | ✓ FLOWING |
| Restart fixture | recreated peers and durable state | same RocksDB roots reopened by `RestartPeer`, then public `AddPeers` | Real durable state, but mesh reconnection timed out in TEST-04 | ✗ DISCONNECTED IN EXECUTION |
| Publisher-loss fixture | slot certificate and Mint-effect counter | successful immutable write precedes paused notification; recovery reads CRDT finality | Durable record flows, but the final fresh-instance exact-once counter assertion fails | ⚠️ INCOMPLETE PROOF |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Build current Phase 12 targets | `cmake --build build/OSX/Release --target multi_node_finality_fault_test multi_node_finality_fault_compatibility_smoke_test --parallel 4` | Both targets built successfully. | ✓ PASS |
| Production composition/lifecycle smoke | `ctest --test-dir build/OSX/Release --output-on-failure -R '^(multi_node_finality_fault_compatibility_smoke_test)$'` | Passed in 6.12s. | ✓ PASS |
| All five four-peer scenarios, serial local sockets | `ctest --test-dir build/OSX/Release --output-on-failure -R '^multi_node_finality_fault_test$'` | 3/5 passed (audit 17.194s, contention 16.664s, late/passive 17.153s); restart and publisher-loss failed. Total 123.11s. | ✗ FAIL |

### Probe Execution

Step 7c: SKIPPED — no phase-declared or conventional `scripts/**/tests/probe-*.sh` probes exist.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| TEST-01 | 12-02 | Same-burn contention produces one slot, certificate, and winning proposal. | ✓ SATISFIED | Named contention scenario passed in the clean target. |
| TEST-02 | 12-02 | Late contender cannot obtain a second usable vote or certificate. | ✓ SATISFIED | Named late-contender scenario passed; durable active-vote identity is asserted. |
| TEST-03 | 12-02 | PubSub recipient does not write certificate authority or stall on self-written CID. | ✓ SATISFIED | Named passive-recipient scenario passed with zero authority writes, notification/readback, and Mint completion. |
| TEST-04 | 12-03 | Three restart boundaries preserve vote and avoid duplicate Mint. | ✗ BLOCKED | Named restart scenario fails cleanly at mesh reconnection. |
| TEST-05 | 12-03 | Publisher loss proves persistence-before-advertisement and deterministic recovery without conflict. | ✗ BLOCKED | Named CRDT-first publisher-loss scenario fails its final exact-once durable assertion. |
| TEST-06 | 12-01 | Tests use production PubSub, CRDT, persistence, and Mint ingress. | ✓ SATISFIED | Audit scenario and compatibility smoke use real runtime composition and passed; source gate is clean. |

No requirement is orphaned: Plan 01 claims TEST-06, Plan 02 claims TEST-01 through TEST-03, and Plan 03 claims TEST-04 through TEST-05.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| `src/blockchain/Consensus.cpp` | 2822 | Existing `TODO` | ℹ️ Info | Pre-dates Phase 12 (`52fc1c253`, 2026-02-07); unrelated to the finality proof. |
| `src/account/TransactionManager.cpp` | 869, 931, 2655, 3046, 3194, 4461 | Existing `TODO` comments | ℹ️ Info | All pre-date Phase 12 and are outside its new fault seams; no Phase-12-introduced `TBD`/`FIXME`/`XXX` marker found. |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | — | Direct local-author/CRDT-write/forced-timer/sleep shortcut scan | ✓ Clean | No forbidden pattern matched. |

### Gaps Summary

The implementation has real, non-stub production-path coverage and cleanly demonstrates contention, late-contender/passive-recipient safety, and production-route discipline. It does **not** yet deliver the phase goal because the only end-to-end proof for restart boundaries and CRDT-first publisher loss fails in the verifier's clean serial run.

The earlier combined-Ctest failure was order-dependent `SameBurnContentionUsesOneCanonicalSlotAndExactMint` bridge-marker failure. The post-fix topology/RAII/serialization work has enough current evidence to clear that particular scenario: the same-burn case passed in this clean target. It does not clear the phase: the clean target remains red for separate restart mesh and publisher-loss exact-once-counter failures. No later milestone phase exists to defer either gap to.

---

_Verified: 2026-08-25T18:55:21Z_
_Verifier: the agent (gsd-verifier)_
