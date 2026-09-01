---
phase: 12-multi-node-finality-fault-proof
verified: 2026-09-01T20:05:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 1/5
  gaps_closed:
    - "Competing same-burn proposals produce one canonical slot, one authoritative certificate, and one exact winner (fresh focused run PASSED 17.7s; full suite 13/13)."
    - "Late contender cannot acquire a second vote/certificate and passive recipient remains receive-only (fresh focused run PASSED; zero certificate write attempts asserted and observed)."
    - "Three-boundary restart proof preserves the original vote with no duplicate mint (fresh focused run PASSED 71.6s combined; full suite green)."
    - "Publisher-loss proves persist-before-advertise and deterministic non-conflicting recovery (fresh focused run PASSED 18.2s; full suite green)."
    - "UAT Test 1 same-burn FilterCertificate fixture repaired via production PutConvergentImmutable per direction (12-12, commit e08288c3); focused test and full lifecycle CTest target re-passed in this verification round."
  gaps_remaining: []
  regressions: []
---

# Phase 12: Multi-Node Finality Fault Proof Verification Report

**Phase Goal:** Operators have production-path regression proof that canonical slot finality remains safe and live through contention, propagation disorder, publisher loss, and restart.
**Verified:** 2026-09-01T20:05:00Z
**Status:** passed
**Re-verification:** Yes — prior round (2026-08-31, score 1/5) found all four fault scenarios failing at runtime; this round re-ran every scenario in fresh processes and all gaps are closed.

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | A multi-node production-path scenario with competing proposals for one burn produces one canonical slot, one authoritative certificate, and one exact winning proposal. | ✓ VERIFIED | `FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint` (multi_node_finality_fault_test.cpp:1995-2098): two distinct same-slot Mint proposals submitted through public `CreateProposal`/`SubmitProposal` on different peers; asserts canonical slot equality, certificate convergence on all three validators, uniform winner hash on every peer, exactly one mint effect per peer, loser no-output, bridge marker, and post-restart durability. **Fresh focused run in this verification: PASSED 17.7s, exit 0.** Full suite green. |
| 2 | A late contender cannot acquire a second usable vote or certificate for a slot, and PubSub recipients neither write the certificate key nor stall on a CID they wrote themselves. | ✓ VERIFIED | `LateContenderAndPassiveRecipientRemainReceiveOnly` (:2100-2202): late submissions from two peers after the durable active-vote boundary; asserts durable active-vote proposal ID unchanged on all validators, passive peer recovers via notification + accepted-certificate readback with `CertificateWriteAttempts == 0`, exactly one mint effect, and post-restart exactness. **Fresh focused run: PASSED** (71.6s combined with restart scenario). |
| 3 | Restart scenarios before certificate arrival, after durable certificate acceptance, and during mint application preserve the original vote and produce no duplicate mint. | ✓ VERIFIED | `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` (:2299-2478): three subcases — vote boundary (durable `/consensus/vote/<slot>` record across same-root StopPeer/recreate before reconnection, `DurableActiveVoteProposalId` equality), accepted-certificate boundary, and mint-application boundary (marker absent → apply → marker present, exact-once outputs). **Fresh focused run: PASSED.** |
| 4 | Publisher-loss scenarios prove persistence-before-advertisement and deterministic failover without conflicting slot certificate records. | ✓ VERIFIED | `PublisherLossAfterPersistenceUsesDeterministicFailover` (:2479-2617): arms the post-persist/pre-notify barrier, observes the production-selected publisher, asserts `CheckCertificateForSlot` true + `CertificateWriteSuccesses == 1` + `CertificateNotificationsPublished == 0` (persistence-before-advertisement), stops the publisher, restarts, and asserts single durable mint per peer, `CertificateWriteAttempts <= 1` (no conflicting slot record), one live mint effect, and full-network post-restart exactness. **Fresh focused run: PASSED 18.2s, exit 0.** |
| 5 | The regression suite exercises production PubSub, CRDT, RocksDB persistence, and mint ingress rather than direct local-author shortcuts. | ✓ VERIFIED | Shortcut scan of the fault test found no `sleep_for`, mock transport, direct certificate handler registration, or forced-timer pattern. `MultiNodeFinalityFaultTestAccess` friendship (Consensus.hpp:553, TransactionManager.hpp:298) is observe/pause-only at durable boundaries; all protocol entry is public `SubmitProposal`/`CreateNonceSubject`; persistence is production `PutConvergentImmutable` (Consensus.cpp:2115); mint effects flow through registered `TransactionManager` handling. Runtime proof: the suite passes over real sockets and real RocksDB roots. |

**Score:** 5/5 truths verified

### Deferred Items

No later milestone phase exists (Phase 12 is the final v3.0 phase), so nothing is deferred to a future phase. The following are explicitly tracked, out-of-scope production follow-ups recorded in `.planning/STATE.md` by plan 12-12 (category `crdt-hardening`, both rows verified present at STATE.md:118-119):

| # | Item | Addressed In | Evidence |
| --- | --- | --- | ---|
| 1 | Equal-priority different-value overwrite guard in `CrdtSet::SetValue` | STATE.md deferred item | crdt-hardening row; not required by any Phase 12 truth (the canonical slot key is written only via reserved-priority `PutConvergentImmutable`) |
| 2 | DAG head advancement for no-topic self-created writes | STATE.md deferred item | crdt-hardening row; same rationale |

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | ----------- | ------ | ------- |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | Four-peer real-route fault scenarios (TEST-01–TEST-05) | ✓ VERIFIED | 2,617 lines; 13 GTests listed (`--gtest_list_tests` = 13); all four fault scenarios plus production-route audit are substantive (full durable-state assertions, not smoke); **passed 13/13 in a fresh full-suite CTest run (217.18s)** |
| `test/src/blockchain/multi_node_finality_fault_runner.cpp` | POSIX invocation-owned session launcher (12-11) | ✓ VERIFIED | 416 lines; registered as CTest `TEST_LAUNCHER` (CMakeLists.txt:116); `multi_node_finality_fault_process_ownership_test` **passed 0.22s in this round** |
| `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` | Production `Blockchain` + `TransactionManager` lifecycle composition (TEST-06) | ✓ VERIFIED | 407 lines; registered (CMakeLists.txt:57, TIMEOUT 120 RUN_SERIAL); **passed 6.21s in this round** |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | 12-12 fixture repair: per-direction production-path slot writes | ✓ VERIFIED | `WriteConvergentCertificateAtKey` (:157-165) writes via `db_->PutConvergentImmutable`, mirroring production SubmitCertificate; `verify_order` uses per-direction fresh node DBs with Approve + `GetCertificateBySlot` readback guards (:1287); **focused test PASSED in this round (6.1s) and full lifecycle CTest target PASSED (42.17s)** |
| `test/src/blockchain/CMakeLists.txt` | Serial bounded CTest registration with port lock | ✓ VERIFIED | 4 targets registered: lifecycle (#23), smoke (#24), fault test (#25, TIMEOUT 300 RUN_SERIAL RESOURCE_LOCK phase12_real_socket_ports TEST_LAUNCHER runner), ownership (#26) |
| `src/blockchain/Consensus.cpp` | Durable vote/certificate boundaries (12-01/12-05) | ✓ VERIFIED | `PersistOrLoadExactActiveVote` (:1282), `RecoverActiveVotes` (:1358, called at :121), `ReleaseActiveVoteForAcceptedSlot` (:1183, :3770), `PutConvergentImmutable` before Publish (:2115) |
| `src/account/TransactionManager.cpp` | Mint-effect boundary before durable marker | ✓ VERIFIED | `PersistBridgeExecutedMarker` (:2025) ordered after successful parse and applied at :5450; exercised at runtime by the passing scenarios |
| `.planning/STATE.md` | Two crdt-hardening deferred rows | ✓ VERIFIED | Rows present at lines 118-119 |

### Key Link Verification

| From | To | Via | Status | Details |
| ---- | --- | --- | ------ | ------- |
| Fault scenarios | `ConsensusManager::SubmitProposal` | public proposal ingress | ✓ WIRED | Every substantive scenario calls public create/submit; verified at runtime by green runs |
| `Consensus.cpp` | `PutConvergentImmutable` then `Publish` | persistence-before-advertisement | ✓ WIRED | :2115 persists into CRDT topic before notification; publisher-loss test observes the barrier (CertificateNotificationsPublished == 0 with certificate durable) |
| `Consensus.cpp` | active-vote recovery/release | durable record → recovery → accepted-slot release | ✓ WIRED | :121, :1183, :1282, :1358, :3770; restart scenario proves the record survives same-root reopen |
| `TransactionManager.cpp` | `PersistBridgeExecutedMarker` | mint effects → marker | ✓ WIRED | :5450 order preserved; exact-once marker asserted across all restart/publisher scenarios |
| `multi_node_finality_fault_test` CTest | `multi_node_finality_fault_runner` | CMake TEST_LAUNCHER | ✓ WIRED | CMakeLists.txt:108-123; runner forwards normal GTest execution (xunit argument preserved); target passed in this round |
| Lifecycle fixture `verify_order` | `WriteConvergentCertificateAtKey` → `PutConvergentImmutable` | production slot-write path | ✓ WIRED | :1287 → :163; focused and full lifecycle runs green in this round |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| -------- | ------------- | ------ | ------------------ | ------ |
| Fault scenarios | canonical slot certificate / durable mint state | public ingress → GossipPubSub → CRDT/RocksDB → registered Mint consumer | Yes — every assertion reads durable per-peer state (`GetCertificateBySlot`, UTXO/bridge-marker readback) after real propagation and after same-root peer recreation | ✓ FLOWING |
| Lifecycle same-burn fixture | existing certificate at canonical slot | per-direction fresh node GlobalDB written via production `PutConvergentImmutable` | Yes — readback guard via public `GetCertificateBySlot` proved the written record is the read record | ✓ FLOWING |
| Evidence collector (12-09/12-10) | child process observer records | fork/exec child capture, waitpid, 66-byte control frame | Yes — two-run real-socket collector classification exercised and passing inside the suite | ✓ FLOWING |

### Behavioral Spot-Checks

All checks executed in this verification round on `build/OSX/Release` (binaries confirmed newer than all sources; incremental build up to date).

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Full serial fault suite (all scenarios) | `ctest --test-dir build/OSX/Release --timeout 300 -R '^multi_node_finality_fault_test$'` | Passed 217.18s, 100% (0 failed out of 1) | ✓ PASS |
| GTest count within target | xunit output from the run | `tests="13" failures="0"` | ✓ PASS |
| Focused same-burn contention | binary `--gtest_filter='FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint'` | PASSED 1 test, 17.7s, exit 0 | ✓ PASS |
| Focused publisher loss | binary `--gtest_filter='...PublisherLossAfterPersistenceUsesDeterministicFailover'` | PASSED 1 test, 18.2s, exit 0 | ✓ PASS |
| Focused late-contender + restart | binary `--gtest_filter='...LateContender...:...RestartAtVoteCertificateAndMintDurableBoundaries...'` | PASSED 2 tests, 71.6s, exit 0 | ✓ PASS |
| Focused 12-12 lifecycle fixture | binary `--gtest_filter='ConsensusPendingLifecycleTest.FilterCertificateTreatsSameMintAlternatesAsNormal...'` | PASSED 1 test, 6.1s, exit 0 | ✓ PASS |
| Full lifecycle CTest target | `ctest ... -R '^consensus_pending_lifecycle_test$'` | Passed 42.17s | ✓ PASS |
| Smoke + ownership targets | `ctest ... -R 'compatibility_smoke_test|process_ownership_test'` | Both Passed (6.21s / 0.22s) | ✓ PASS |
| Executor evidence corroboration | inspect `/tmp/p12_mn_triple_{a,b,c}.log`, `/tmp/p12_lifecycle_full{1,2,3}.log`, `/private/tmp/phase12-11-normal-final-{1,2,3}.log` | All 9 logs exist; sampled logs show 100% passes (211.48s, 43.21s) | ✓ PASS |

The four runtime gaps from the prior round (topology readiness failures blocking every scenario) did not reproduce: every previously-failing scenario now passes in a fresh isolated process and in the full serial suite.

### Probe Execution

Step 7c: SKIPPED — no declared or conventional `scripts/**/tests/probe-*.sh` probes exist in this repository. Behavioral coverage was provided instead by direct CTest/GTest execution (above).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| TEST-01 | 12-02, 12-06, 12-12 | Same-burn contention yields one slot, certificate, winner | ✓ SATISFIED | Contention scenario green (focused + suite); deterministic same-burn FilterCertificate regression repaired and green (focused + full lifecycle target) |
| TEST-02 | 12-02, 12-06 | Late contender cannot obtain second usable vote/certificate | ✓ SATISFIED | Late-contender scenario green; durable active-vote proposal ID asserted unchanged on all validators |
| TEST-03 | 12-02, 12-06 | Recipient receive-only, no self-CID stall | ✓ SATISFIED | `CertificateWriteAttempts == 0` on passive peer with notification + accepted-certificate readback + exactly one mint effect |
| TEST-04 | 12-03, 12-04, 12-05, 12-07, 12-06 | Three restart boundaries, no changed vote, no duplicate mint | ✓ SATISFIED | Three-boundary scenario green; vote record proven durable across same-root reopen before reconnection |
| TEST-05 | 12-03, 12-04, 12-08–12-11 | Publisher loss: persist-before-advertise, deterministic failover, no conflicting records | ✓ SATISFIED | Publisher-loss scenario green; `CertificateNotificationsPublished == 0` with durable certificate, `CertificateWriteAttempts <= 1`, exact-once durable mint on every peer |
| TEST-06 | 12-01, 12-08–12-11 | Production route only, no local-author shortcuts | ✓ SATISFIED | Static shortcut scan clean; ownership runner + serial CTest registration; suite runs over real PubSub/CRDT/RocksDB |

No requirements are orphaned: all six Phase 12 requirement IDs from REQUIREMENTS.md are claimed by plans and satisfied. Roadmap contains no later milestone phase, so no gap deferral applies.

### Anti-Patterns Found

Debt-marker scan (TBD/FIXME/XXX) across all four phase test files: clean. No placeholder, mock-transport, sleep-synchronization, or forced-timer pattern in the fault test. Findings below are from `12-REVIEW.md` (2026-09-01 round), independently confirmed in current source during this verification.

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| `src/blockchain/Consensus.cpp` | 3017-3036 | CR-01: `CreateProposalState` mutates `proposals_`/`slot_states_` without `proposals_mutex_` while called unlocked from `HandleCertificate` (:2993) on the pubsub receive thread; ~20 other sites lock the same mutex | ⚠️ Warning | Real pre-existing production data race (UB window on every received certificate). Confirmed real. Does not falsify any Phase 12 truth — the proof scenarios pass deterministically — but it is an unresolved Critical production defect on the exact path this phase certifies. Not tracked in STATE.md. |
| `src/account/TransactionManager.cpp` | 3430-3435, 3962-3965 | CR-02: `tx_processed_m.find( GetTransactionPath(...) )` dereferenced without `end()` check; key namespace can miss for multi-network tracked entries on DEV_NET | ⚠️ Warning | Real pre-existing UB/crash path on the conflicting-transaction ingress. Confirmed real (misleading "No need to check" comment at first site). Not goal-falsifying; not tracked in STATE.md. |
| `src/blockchain/Consensus.cpp` | 1566 | WR-05: `active_vote_announcements_for_test_.push_back` runs unconditionally in the production vote loop, unbounded and outside `fault_test_mutex_` | ⚠️ Warning | Phase-12-introduced instrumentation (unlike CR-01/CR-02): unbounded memory growth on long-running production nodes plus an unsynchronized test read. Not goal-falsifying; should be gated/bounded. |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | — | Residual documented low-rate readiness flake in the collector child (`RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch`) | ℹ️ Info | Recorded in STATE.md under the Plan 12-08 discipline (repair requires two matching fresh failures); did not occur in this verification round's runs. |

### Code Review Findings Assessment (12-REVIEW.md, status issues_found)

The review's 2 Critical / 7 Warning / 8 Info findings were checked against the goal:

- **CR-01 and CR-02 are real** — both were independently re-confirmed in current source (unlocked `proposals_` mutation on the certificate receive path; unchecked `tx_processed_m.find` dereference). Both are pre-existing defects (`CreateProposalState` predates Phase 12; the only Phase 12 production commits touched Consensus.cpp observation accessors, +8 lines), not regressions introduced by this phase.
- **They do not undermine goal achievement.** The goal is production-path regression proof; the proof artifact exists, runs the production path with no shortcuts (SC-5), and passes deterministically — reproduced in this round by one full-suite run, four focused fresh-process runs, and corroborated by the executor's nine preserved green logs. Neither defect falsifies any of the five success criteria, and neither manifested in any Phase 12 run.
- **However, neither Critical is tracked anywhere** (no STATE.md row, unlike the two crdt-hardening deferrals from 12-12, and no review-fix round exists for the 2026-09-01 review). Recommendation: record CR-01, CR-02, and WR-05 as tracked deferred items in STATE.md, or open a follow-up repair plan, before milestone close — otherwise these Critical findings are an untracked audit gap. WR-05 deserves priority since Phase 12 itself introduced it into production code.

### Human Verification Required

None. All goal-relevant checks were verified programmatically in this round (source inspection plus fresh test execution). The prior UAT round (12-UAT.md) provided human acceptance for 4/5 tests, and the single blocker (same-burn canonical finality) was closed by 12-12 and independently re-verified here; no visual, external-service, or otherwise un-testable items remain.

### Gaps Summary

No gaps. All five roadmap success criteria are verified with fresh in-process evidence: the four fault scenarios (contention, late-contender/passive-recipient, three-boundary restart, publisher loss) each pass in isolated fresh runs and in the full serial 13-test CTest suite, over the real PubSub/CRDT/RocksDB/Mint production route with no shortcuts. The prior round's four runtime gaps and the UAT Test 1 fixture defect are all closed. The 2026-09-01 code review's two Critical production findings (CR-01 data race, CR-02 unchecked dereference) are confirmed real but pre-existing, non-goal-falsifying, and advisory — with the explicit recommendation above to give them tracked follow-up before milestone close.

---

_Verified: 2026-09-01T20:05:00Z_
_Verifier: Claude (gsd-verifier)_
