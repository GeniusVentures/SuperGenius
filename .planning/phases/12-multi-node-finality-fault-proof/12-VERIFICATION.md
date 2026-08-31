---
phase: 12-multi-node-finality-fault-proof
verified: 2026-08-31T17:22:42Z
status: gaps_found
score: 1/5 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 2/5
  gaps_closed: []
  gaps_remaining:
    - "Late-contender and passive-recipient real-socket proof cannot get past the public topology readiness predicate."
    - "The complete three-boundary restart proof remains unestablished by a fresh successful production-path run."
    - "Publisher-loss still records a real-socket readiness failure and no completed failover proof."
  regressions:
    - "The previously verified same-burn contention scenario now fails in a fresh focused real-socket process at public topology readiness, before a certificate is produced."
gaps:
  - truth: "A multi-node production-path scenario with competing proposals for one burn produces one canonical slot, one authoritative certificate, and one exact winning proposal."
    status: failed
    reason: "The named fresh contention regression cannot establish its validator topology and then times out waiting for a durable certificate."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "SameBurnContentionUsesOneCanonicalSlotAndExactMint fails at ConnectAndWaitForPeers (line 1119) and later at certificate convergence (line 2053)."
    missing:
      - "A reliable public real-PubSub topology that lets the contention scenario reach and assert its canonical certificate and exact-Mint outcomes."
  - truth: "A late contender cannot acquire a second usable vote or certificate for a slot, and PubSub recipients neither write the certificate key nor stall on a CID they wrote themselves."
    status: failed
    reason: "The named scenario fails before its no-second-vote and passive-recipient assertions can be completed."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "LateContenderAndPassiveRecipientRemainReceiveOnly times out at the line-1119 topology predicate, then at active-vote publication and no-replacement predicates (lines 2151 and 2161)."
    missing:
      - "A passing real-socket late-contender/passive-recipient execution retaining the zero-authoritative-write and durable recovery assertions."
  - truth: "Restart scenarios before certificate arrival, after durable certificate acceptance, and during mint application preserve the original vote and produce no duplicate mint."
    status: partial
    reason: "The isolated raw active-vote diagnostic is implemented, but it is not the three-boundary end-to-end proof and no fresh successful full restart scenario was obtained; its shared reconnect topology is currently failing in the suite."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce remains dependent on ConnectPeers/RestartAndReconnect, whose public topology predicate demonstrably fails in fresh peer scenarios."
    missing:
      - "A clean successful real-socket execution of all vote, accepted-certificate, and Mint-before-marker restart boundaries."
  - truth: "Publisher-loss scenarios prove persistence-before-advertisement and deterministic failover without conflicting slot certificate records."
    status: failed
    reason: "Collector-attributed real publisher-loss children are deliberately classified as complete readiness failures with repair_authorization=none; that observes the blocker but does not execute the publisher-loss/failover proof."
    artifacts:
      - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
        issue: "RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch asserts boundary=zero-consensus-topic-mesh/state=zero/error=no-consensus-neighbor and expects no authorization, while PublisherLossAfterPersistenceUsesDeterministicFailover reaches ConnectPeers before persistence or publisher stop."
    missing:
      - "A passing real-socket publisher-loss run that reaches persistence-before-advertisement, publisher stop, ordinary recovery, and exact durable state checks."
---

# Phase 12: Multi-Node Finality Fault Proof Verification Report

**Phase Goal:** Operators have production-path regression proof that canonical slot finality remains safe and live through contention, propagation disorder, publisher loss, and restart.
**Verified:** 2026-08-31T17:22:42Z
**Status:** gaps_found
**Re-verification:** Yes — after prior gap closure work

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Competing same-burn proposals produce one slot, certificate, and exact winner. | ✗ FAILED | Fresh `SameBurnContentionUsesOneCanonicalSlotAndExactMint` fails line 1119 topology readiness, then line 2053 certificate convergence; no certificate exists at line 2056. |
| 2 | A late contender cannot gain a second vote/certificate and passive recipients remain receive-only/live. | ✗ FAILED | Fresh named late-contender test hits the same topology failure, then times out at durable active-vote publication and no-replacement predicates. |
| 3 | Vote, accepted-certificate, and Mint-boundary restarts retain the exact vote and mint once. | ✗ FAILED | The raw active-vote diagnostic is a useful narrow seam, but it does not replace a fresh passing execution of all three restart boundaries; shared reconnect readiness is currently red. |
| 4 | Publisher loss proves persist-before-advertise and deterministic non-conflicting recovery. | ✗ FAILED | The new parent collector attributes actual publisher-loss children to `zero-consensus-topic-mesh`; its asserted decision is `repair_authorization=none`, not a successful failover. |
| 5 | The regression suite uses production PubSub, CRDT, RocksDB, consensus, and Mint ingress, without local-author shortcuts. | ✓ VERIFIED | The fixture starts real `GossipPubSub` and `GlobalDB`, submits public proposals through `ConsensusManager::SubmitProposal`, reads certificates by slot, and uses registered `TransactionManager` Mint handling. The Phase-12 test delta has no direct certificate receive/handler, CRDT write, mock, forced timer, or sleep synchronization. |

**Score:** 1/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | Real-socket four-peer route plus TEST-01–TEST-05 scenarios | ⚠️ PARTIAL | 2,617 substantive lines; registered and wired, but current focused production scenarios fail before finality. |
| `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` | Production `Blockchain` + `TransactionManager` lifecycle composition | ✓ VERIFIED | Exists, substantive, and remains registered by `test/src/blockchain/CMakeLists.txt`. |
| `test/src/blockchain/CMakeLists.txt` | Normal bounded serial CTest registration | ✓ VERIFIED | Registers `multi_node_finality_fault_test` with `TIMEOUT 300` and `RUN_SERIAL TRUE`. |
| `src/blockchain/Consensus.cpp` | Durable active-vote/certificate observation boundaries | ✓ VERIFIED | `PersistOrLoadExactActiveVote`, `RecoverActiveVotes`, and `ReleaseActiveVoteForAcceptedSlot` are connected; immutable certificate persistence occurs before normal `Publish`. |
| `src/account/TransactionManager.cpp` | Mint-effect observation before durable marker | ✓ VERIFIED | The boundary is ordered after successful `ParseTransaction` and before `PersistBridgeExecutedMarker`. |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| Fault test | `ConsensusManager::SubmitProposal` | Public proposal ingress | ✓ WIRED | Every substantive scenario calls public proposal creation/submission. |
| `Consensus.cpp` | `PutConvergentImmutable` then `Publish` | Persistence-before-advertisement | ✓ WIRED | Lines 2115–2135 persist successfully, then cross the test barrier, then publish. |
| `Consensus.cpp` | active-vote recovery/release | durable record → recovery → accepted-certificate release | ✓ WIRED | Lines 1183, 1282, 1358, and 3770 show the production path. |
| `TransactionManager.cpp` | `PersistBridgeExecutedMarker` | Mint effects → barrier → marker | ✓ WIRED | Lines 5444–5450 preserve the required order. |
| Four-peer fixture | Real topology and finality flow | `AddPeers` → connected-host/topic predicate → PubSub/CRDT | ✗ NOT WIRED AT RUNTIME | The source is connected, but fresh runs fail the line-1119 predicate, so the dynamic flow does not reach the assertions. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| --- | --- | --- | --- | --- |
| Contention/late/restart/publisher scenarios | proposal → slot certificate → exact Mint/marker | Public consensus ingress → GossipPubSub/CRDT/RocksDB → registered Mint consumer | No: fresh topology readiness fails before certificate flow. | ✗ DISCONNECTED |
| Active-vote diagnostic | direct durable vote record and recovered proposal ID | Same-root RocksDB snapshot → `RecoverActiveVotes` | Yes in the isolated diagnostic, but not sufficient for all restart boundaries. | ⚠️ PARTIAL |
| Publisher observer collector | child output/frame → opaque evidence decision | fork/exec child capture, `waitpid`, validated control frame | Yes; it produces attributable `repair_authorization=none` evidence, not finality recovery. | ✓ FLOWING (diagnostic only) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Focused target build | `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4` | Completed successfully. | ✓ PASS |
| Registered serial CTest | `ctest --test-dir build/OSX/Release --output-on-failure --timeout 300 -R '^multi_node_finality_fault_test$'` | Started, but the available runner returned no terminal result; it is not counted as pass evidence. | ? INCONCLUSIVE |
| Fresh same-burn contention | `multi_node_finality_fault_test --gtest_filter='FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint' --gtest_brief=1` | Failed in 32.288s: line 1119 topology timeout, line 2053 certificate timeout, no certificate at line 2056. | ✗ FAIL |
| Fresh late/passive scenario | `multi_node_finality_fault_test --gtest_filter='FinalityFaultNetwork.LateContenderAndPassiveRecipientRemainReceiveOnly' --gtest_brief=1` | Line 1119 topology timeout, then lines 2151 and 2161 active-vote/no-replacement timeouts. | ✗ FAIL |
| Route/shortcut scan | Phase-12 source delta scan | No new direct receive/author handler, CRDT write, mock transport, forced timing, or sleep synchronization found. | ✓ PASS |

### Probe Execution

Step 7c: SKIPPED — no declared or conventional `scripts/**/tests/probe-*.sh` probes exist.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| TEST-01 | 12-02 | Same-burn contention yields one slot, certificate, and winner. | ✗ BLOCKED | Fresh named test fails before certificate creation. |
| TEST-02 | 12-02 | Late contender cannot obtain a second usable vote/certificate. | ✗ BLOCKED | Fresh named test fails before its decisive durable-vote assertions. |
| TEST-03 | 12-02 | Recipient remains receive-only and avoids self-CID stall. | ✗ BLOCKED | It shares the failed named late/passive execution, so zero-write/recovery behavior is not demonstrated. |
| TEST-04 | 12-03, 12-04, 12-05 | Three restart boundaries retain vote and avoid duplicate Mint. | ✗ BLOCKED | Narrow active-vote instrumentation exists, but no fresh complete three-boundary success and its reconnect path is presently failing. |
| TEST-05 | 12-03, 12-04, 12-08–12-10 | Publisher loss persists before advertising and recovers without conflict. | ✗ BLOCKED | Collector-attributed evidence confirms a pre-fault readiness failure, not persistence/failover completion. |
| TEST-06 | 12-01 | Production PubSub/CRDT/RocksDB/Mint route only. | ✓ SATISFIED | Static source and wiring inspection confirm the required production route and no shortcut. |

No requirements are orphaned. Roadmap analysis contains no later milestone phase, so no gap is deferred.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | 1111–1119 | Bounded public topology readiness fails in fresh real-socket scenarios. | 🛑 Blocker | Prevents TEST-01–TEST-05 end-to-end proof from reaching finality conditions. |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | 1859–1892 | Collector assertions encode `fully_attributed_complete_failure` and `repair_authorization=none` for real publisher-loss children. | ℹ️ Info | Correctly fails closed; does not repair or prove TEST-05. |
| Phase-12 test delta | — | No new TBD/FIXME/XXX, placeholder, direct-author/receive, CRDT-write, mock, forced-timer, or sleep synchronization marker. | ℹ️ Info | TEST-06 route discipline is preserved. |

### Gaps Summary

The new collector-attributed publisher records close an evidence-attribution problem only. They explicitly classify the actual publisher-loss child as a completed readiness failure and preserve a fail-closed `repair_authorization=none`; they do not move persistence, publisher stop, failover, or durable exact-once assertions past the failed topology predicate.

The regression is broader than the prior report: a fresh isolated TEST-01 contention run now fails at the same public topology readiness predicate, and the fresh TEST-02/03 run does too. Because the primary production-path proof cannot pass even its contention baseline, the phase goal is not achieved. This is an Escalation Gate: restore reliable real-socket topology/finality execution, then rerun the whole registered serial target before advancing.

---

_Verified: 2026-08-31T17:22:42Z_
_Verifier: the agent (gsd-verifier)_
