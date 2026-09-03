---
phase: 12-multi-node-finality-fault-proof
verified: 2026-09-03T18:46:49Z
status: passed
score: 27/27 must-haves verified
overrides_applied: 1
overrides:
  - must_have: "12-15: WR-02 notify-under-paired-mutex in Stop()/Close()"
    reason: "Production hardening item withheld under the 12-15 repair-authorization gate (both authorization clauses failed); round-5 production budget was explicitly limited to the WR-06 clamp alone (12-21). Carried as a production follow-up in STATE.md Deferred Items and 12-REVIEW.md WR-02. Not part of the phase goal (regression proof of finality safety/liveness) — the six scenario proofs do not depend on it. Recorded as an override for milestone-audit visibility."
    accepted_by: "developer (12-15 authorization gate; 12-21 scope decision 'round-5 production budget is the clamp alone'; STATE.md Deferred Items table)"
    accepted_at: "2026-09-03T18:26:41Z"
re_verification:
  previous_status: gaps_found
  previous_score: 24/27
  gaps_closed:
    - "Round-5 gap 1 (three-consecutive-serial-pass evidence gate): CLOSED. The developer resolved the routed (a)/(b)/(c) decision on 2026-09-03 as DIRECTED REMOVAL (verbatim directive recorded in 12-23-APPARATUS-REMOVAL.md and STATE.md:158): the seven PublisherObserver*/collector meta-tests and their dead machinery deleted (commit 6fa285fe, test file +4/-800, 2724->1928 lines; runner +4/-1 filter repoint; ZERO src/ changes — verified via numstat). The round-6 gate (evidence commit 4f7f674f, round6-traces/) delivered THREE CONSECUTIVE SERIAL FULL PASSES on the reduced 6-case suite: 132.65s / 134.77s / 136.88s, each ctest trailer '100% tests passed, 0 tests failed', ctest-rc=0, git-head 6fa285fe. Independently parsed all three xunit XMLs with ElementTree this session: root tests='6' failures='0' errors='0' each; 6 testcase entries per file, every one status='run' result='completed' with zero <failure>/<error> children; three distinct timestamps (15:29:46 / 15:32:17 / 15:34:43 local) and three distinct sha256 prefixes (026fbd1f/2bcc9857/d94210f0) — genuine per-run captures, not copies. XML testcase line attributes (1115/1217/1324/1428/1523/1790) match the committed working-tree source exactly. Crash-freedom independently confirmed live: newest .ips is still processing_core_gating_test-2026-09-02-175810, zero newer than the gate window (crash-check.txt PASS corroborated by direct find). Build provenance: gate binary epoch 15:28:33 strictly newer than both modified sources (15:27:58/15:28:03); no commit after 6fa285fe touches test/ or src/; working tree clean for both. The suite reduction 13->6 is a developer-directed, fully recorded scope change (12-23 table + STATE.md:158), not a silent reduction; no must-have and no REQUIREMENTS TEST-01..06 item referenced the deleted apparatus."
    - "Round-5 gap 2 (restart/publisher-loss stable property inside full-suite runs): CLOSED. RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce is green inside ALL THREE round-6 gate XMLs (53.878/54.384/53.931s) — 8 consecutive full-suite greens on the Option-A repaired build across rounds 5-6, plus the 3 focused graduation greens. PublisherLossAfterPersistenceUsesDeterministicFailover is green inside ALL THREE round-6 XMLs (18.163/19.648/18.182s) and in every full run ever recorded. Exact-once held in every run this phase (no duplicate-mint assertion failure in any log); CERT-02 (PutConvergentImmutable count 0 in test) and CERT-05 (single SubmitCertificate call site, :1763) re-verified in the reduced file. The completing three-XML demonstration now exists."
  gaps_remaining: []
  regressions: []
---

# Phase 12: Multi-Node Finality Fault Proof Verification Report

**Phase Goal:** Operators have production-path regression proof that canonical slot finality remains safe and live through contention, propagation disorder, publisher loss, and restart.
**Verified:** 2026-09-03T18:46:49Z
**Status:** passed
**Re-verification:** Yes — round-6 close after the developer-directed apparatus removal (12-23) resolved the round-5 routed decision

## Round-6 Adjudication (scope of this verification)

1. **The round-5 gaps are closed, and the closure mechanism is legitimate.** The 12-22-routed decision (repair / re-scope / accept) was resolved by the developer on 2026-09-03 as a directed removal — recorded verbatim ("I don't want failing tests and apparatus that serves nothing but to follow some rule of thumb regarding approval of a test. This seems like huge overengineering.") in 12-23-APPARATUS-REMOVAL.md and STATE.md:158. This verification confirmed the removal is exactly what the record claims: commit 6fa285fe numstat = runner +4/-1, test file +4/-800 (2724 -> 1928 lines exactly), zero src/ changes; the only 4 added lines in the test file are one 3-line comment and one `std::cerr << record` line simplifying the retained observer's `Write` — **no scenario body was touched**; all ten deleted-symbol greps return 0; all six scenario names plus PublisherReadinessObserver/Snapshot and MintRecoveryDiagnostics grep counts confirm the kept surface; the runner filter now points at `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` (:413), the same real-socket child shape/ports the retired meta-test used, and the ownership CTest re-verified green (0.21s). The suite change 13 -> 6 is developer-directed and explicitly recorded — not a silent reduction — and no must-have or TEST-01..06 requirement referenced the deleted meta-apparatus (it was evidence-collection tooling, not finality behavior).
2. **The three-consecutive-serial-pass standard is met on the developer-defined suite.** All three gate runs passed in series on build 6fa285fe; each xunit XML independently parsed: tests="6" failures="0" errors="0", six green testcase entries, zero failure elements. Runs are distinct (timestamps and hashes differ). RestartAtVote and PublisherLoss are green in every one of the three XMLs — the exact completing demonstration round 4 demanded. Crash-freedom re-verified live by this verifier (newest .ips still 2026-09-02; zero new). Binary provenance verified (binary epoch > source epochs; XML line attrs match committed source; no src/test commits after 6fa285fe; tree clean).
3. **Load disclosure adjudication.** Pre-run 1-min loads were 2.45 / 19.90 / 27.72 — runs B and C executed inside/above the historical 15-20 contamination band. The earlier quiet-load entry clause (load < 2) belonged to the failure-attribution discipline: it existed so a mid-series strike could not be blamed on ambient load. With zero strikes there is nothing to mis-attribute, and heavy load stresses timeouts rather than masking state-based assertions (exact-once, single-canonical-slot, bridge markers are state predicates, not timing passes); run durations moved only 132.65 -> 136.88s across the series, showing no degradation. The 12-23 procedure the developer directed replaced load gating with honest per-header recording, which the logs carry; 12-23-APPARATUS-REMOVAL.md and STATE.md disclose the B/C contamination-regime fact explicitly. Passing under load is accepted as stronger, not weaker, stability evidence.
4. **Quick regression on all previously-passed truths found no damage from the removal.** Teardown order intact in the fault test (:396-400), smoke (:287-291), and lifecycle (registry.reset() x2); SameBurn four-peer predicate intact (:1206-1212); Option-A waits intact with distinct messages (:1716 retention, :1742 readiness gate); observer scenario integration intact (:1829 -> :1927); backoff seam intact (:1539 setter, :1544 RAII reset; src untouched since 0e99efa3); CERT-02/CERT-05 intact; CTest registrations untouched. Sibling matrix 4/4 green on the gate generation (compat 5.66s, consensus_pending 41.55s, tx_cert_fallback 30.93s, ownership 0.21s).
5. **Truth 20 (WR-02) is carried as an explicit override** (frontmatter) — the plan-authorized withholding already adjudicated non-goal-blocking in round 5, now formalized for milestone-audit visibility.

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | SC-1: Competing same-burn proposals produce one canonical slot, one authoritative certificate, one exact winner | ✓ VERIFIED | `SameBurnContentionUsesOneCanonicalSlotAndExactMint` intact (:1217, predicate :1206-1212); green in all 3 round-6 XMLs (17.17/17.163/18.696s) |
| 2 | SC-2: Late contender cannot acquire second vote/certificate; recipients receive-only, no self-CID stall | ✓ VERIFIED | `LateContenderAndPassiveRecipientRemainReceiveOnly` intact (:1324); green in all 3 round-6 XMLs |
| 3 | SC-3: Three restart boundaries preserve the original vote, no duplicate mint | ✓ VERIFIED | `RestartAtVote...RecoversExactlyOnce` intact (:1523); green in all 3 round-6 XMLs (53.878/54.384/53.931s); exact-once held in every run ever recorded |
| 4 | SC-4: Publisher-loss proves persist-before-advertise and deterministic failover | ✓ VERIFIED | `PublisherLossAfterPersistenceUsesDeterministicFailover` intact (:1790) with observer integration (:1829/:1927); green in all 3 round-6 XMLs and every recorded full run |
| 5 | SC-5: Suite exercises production PubSub/CRDT/RocksDB/mint ingress, no local-author shortcuts | ✓ VERIFIED | `ProductionRouteAudit...` green in all 3 XMLs; CMake registration untouched by 6fa285fe; ownership CTest green 0.21s |
| 6 | 12-13: Stale CRDT.Datastore.TEST.* db cannot be reopened (pid+counter paths) | ✓ VERIFIED | base_crdt_test.cpp untouched (6fa285fe touched only the two multi_node files; nothing after) |
| 7 | 12-13: cert-fallback passes with stale legacy db dir present | ✓ VERIFIED | Round-6 sibling matrix: Passed 30.93s |
| 8 | 12-13: No-quorum certificate rejection emits warn-level log | ✓ VERIFIED | Consensus.cpp untouched (zero src/ changes in 6fa285fe; zero src/ commits after) |
| 9 | Sibling CRDTFixture suites green (no blast-radius regression) | ✓ VERIFIED | Round-6 matrix 4/4: compat 5.66s, lifecycle 41.55s, cert-fallback 30.93s, ownership 0.21s (incl. the changed runner) |
| 10 | 12-14: Peer::Stop releases GlobalDB host co-owners BEFORE GossipPubSub::Stop | ✓ VERIFIED | Order intact in reduced file (:396-400: db.reset -> account.reset -> pubsub->Stop -> pubsub.reset -> io.reset) |
| 11 | Evidence standard: three consecutive serial full passes, zero new crash reports | ✓ VERIFIED | **Round-6 gate**: 3/3 passes (132.65/134.77/136.88s), each XML tests="6" failures="0" with zero failure elements (ElementTree-parsed); distinct timestamps/hashes; zero new .ips (independently confirmed live); binary provenance verified. Load disclosure adjudicated in Round-6 Adjudication point 3 |
| 12 | 12-14: consensus_pending_lifecycle_test passes on the same build | ✓ VERIFIED | Passed 41.55s in round-6 matrix; its source untouched by the removal, binary current |
| 13 | Restart exact-once and publisher-loss still pass as a stable property inside full-suite runs | ✓ VERIFIED | **Completing demonstration exists**: RestartAtVote green in all 3 round-6 XMLs (8 consecutive full-suite greens on the repaired build); PublisherLoss green in all 3; zero failure elements anywhere |
| 14 | 12-15: SameBurn wait predicate covers the exact durable boundary it asserts | ✓ VERIFIED | Four CheckCertificateForSlot + four HasBridgeMarker terms at :1206-1212 (re-grepped in reduced file) |
| 15 | 12-15: Every intermittent signature has an attribution verdict backed by citations | ✓ VERIFIED | Attribution records (12-06/12-15 docs) untouched; round-6 adds the terminal gate outcome — the sole remaining intermittent (collector child-readiness) was removed by developer direction, with its Verdict 3 characterization preserved in the record |
| 16 | 12-16: ComponentPeer::Stop releases db/account before pubsub->Stop (CR-01) | ✓ VERIFIED | Smoke :287-291 intact; suite green 5.66s in round-6 matrix |
| 17 | 12-16: ~CRDTFixture releases db_ before pubs_->Stop (CR-02) | ✓ VERIFIED | base_crdt_test.cpp untouched since its verification |
| 18 | 12-16: teardown invariant holds across ALL phase proof artifacts (WR-01 loops) | ✓ VERIFIED | registry.reset() count exactly 2 in consensus_pending_lifecycle_test.cpp (re-grepped); lifecycle green 41.55s |
| 19 | 12-20: gate-entry check consults the record and routes go/no-go BEFORE the series | ✓ VERIFIED | Round-6 mirror: 12-23 recorded the developer decision before the gate; crash marker created before run A (crash-check.txt window definition); loads recorded per header; no strike occurred so the stop rule was never needed |
| 20 | 12-15: WR-02 notify-under-paired-mutex in Stop()/Close() | PASSED (override) | Override: plan-authorized withholding under the 12-15 gate; round-5 budget scoped to the clamp alone; tracked in STATE.md Deferred Items + 12-REVIEW.md WR-02 — accepted by developer, recorded 2026-09-03 |
| 21 | 12-18: Backoff seam is test-configurable sub-second while unconfigured processes keep exact production durations | ✓ VERIFIED | Seam intact (:1539 setter + :1544 RAII reset in reduced file); graphsync_dagsyncer.cpp untouched since 0e99efa3 |
| 22 | 12-18/12-19: CRDT write authority not granted to test code (CERT-02) and byte-identical replay only (CERT-05) | ✓ VERIFIED | PutConvergentImmutable count 0; exactly one SubmitCertificate call site (:1763), after the readiness gate; re-publication via GetCertificateBySlot |
| 23 | 12-21: Option A implemented verbatim as the composed repair — test-file only, no existing bound relaxed, CERT-02/CERT-05 preserved | ✓ VERIFIED | Both waits present with exact distinct messages (:1716, :1742 — line shift from :2512/:2538 is the removal delta, content unchanged) |
| 24 | 12-21: The composed repair graduates to an attributed fix — 3/3 focused all-green, durable in-repo logs | ✓ VERIFIED | round5-traces/option-a-restart-{1,2,3}.log unchanged on disk (no commits touched them); corroborated by 8 subsequent full-suite greens |
| 25 | 12-21: getBackoffTimeout exponent clamped at both branches — no UB, production behavior identical, override path unchanged (WR-06) | ✓ VERIFIED | src/ untouched by 6fa285fe and by everything after (git-verified this session); round-5 verification + 12-REVIEW.md stand |
| 26 | 12-22: gate-entry re-evaluated on the record BEFORE any run — GO with all clauses on direct evidence | ✓ VERIFIED | Historical record intact (STATE.md:152); round-5 verification's evidence unchanged |
| 27 | 12-22: sibling matrix passes serially on the same build generation, cert-fallback included | ✓ VERIFIED | Round-6 matrix 4/4 green on the gate generation incl. cert-fallback 30.93s and the repointed ownership target 0.21s |

**Score:** 27/27 truths verified (26 VERIFIED + 1 PASSED (override))

### Deferred Items

None within verification scope. Phase 12 is the final v3.0 phase; the round-6 work was in-phase gap closure. STATE.md's Deferred Items table (crdt-hardening x2, thirdparty GossipPubSub::StopImpl hardening, teardown-uaf diagnostics) and WR-02 remain recorded production follow-ups outside the phase goal — surfaced via the override above and the STATE record at milestone audit.

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | ----------- | ------ | ------- |
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | Six FinalityFaultNetwork scenarios intact; meta-apparatus deleted | ✓ VERIFIED | 1928 lines; TEST_F count exactly 6 (verified by grep and by binary `--gtest_list_tests`); all 10 deleted symbols return 0 hits; scenario bodies untouched (+4 comment/cerr lines only) |
| `test/src/blockchain/multi_node_finality_fault_runner.cpp` | Controlled-cancellation filter repointed to a surviving real-socket scenario | ✓ VERIFIED | :413 filter = `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover`; diff is the repoint + 3-line comment; ownership CTest green 0.21s |
| `round6-traces/gate-{a,b,c}.log` | Three serial full-pass runs with honest headers | ✓ VERIFIED | Each: "100% tests passed, 0 tests failed", ctest-rc=0, git-head 6fa285fe, per-run 1-min load recorded (2.45/19.90/27.72); note-line blemish flagged under Anti-Patterns |
| `round6-traces/gate-{a,b,c}.xml` | Per-run xunit proof, tests=6 failures=0 | ✓ VERIFIED | ElementTree-parsed: root and suite tests="6" failures="0" errors="0"; 6 green cases each; distinct timestamps (15:29:46/15:32:17/15:34:43) and distinct sha256 — not copies; line attrs match committed source |
| `round6-traces/crash-check.txt` | Zero new crash reports across gate + matrix | ✓ VERIFIED | PASS recorded; independently re-confirmed live by this verifier (newest .ips still 2026-09-02-175810; zero newer than the window) |
| `round6-traces/sibling-matrix.log` | Four sibling suites green on the gate generation | ✓ VERIFIED | 4/4 Passed: 5.66/41.55/30.93/0.21s, each "100% tests passed" |
| `12-23-APPARATUS-REMOVAL.md` | Verbatim developer directive + deletion inventory + gate record | ✓ VERIFIED | Directive quoted verbatim; 13->6 composition table; deleted/kept inventory matches the code exactly (all claims re-checked against commit 6fa285fe) |
| `.planning/STATE.md` | Decision chain with the 12-23 entry and honest load disclosure | ✓ VERIFIED | STATE.md:158 carries the directed-removal entry, gate outcome, and load disclosure; Session Continuity updated |

### Key Link Verification

| From | To | Via | Status | Details |
| ---- | --- | --- | ------ | ------- |
| Six scenario tests | Production Consensus/CRDT/PubSub/mint paths | real socket fixture, no shortcuts | ✓ WIRED | ProductionRouteAudit green x3; CMake registration untouched |
| PublisherLoss scenario | PublisherReadinessObserver | `observer(network)` :1829 -> `EmitTerminal(all_released)` :1927 | ✓ WIRED | Retained observer integration verified in the reduced file |
| RestartAtVote | Option-A waits | retention wait :1716, readiness gate :1742 before re-publication :1763 | ✓ WIRED | Gate precedes the single SubmitCertificate call site |
| Ownership runner child | Surviving publisher-loss scenario | `--gtest_filter` :413 | ✓ WIRED | Repoint verified in source + diff; CTest green 0.21s (connect gate exercised) |
| Gate evidence chain | Build 6fa285fe | git-head headers + XML line attrs + binary epoch | ✓ WIRED | Binary (15:28:33) newer than sources (15:27:58/15:28:03); XML line attrs 1115/1217/1324/1428/1523/1790 match committed file; no code commits after 6fa285fe |

### Data-Flow Trace (Level 4)

Not applicable — no dynamic-data rendering artifacts. The phase's assertions read durable per-peer RocksDB state and pubsub topic-peer counts; the evidence chain (logs -> XMLs -> STATE.md) was verified line-by-line against the raw committed artifacts including independent XML parsing of all three gate files.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Suite composition (developer-defined suite) | `multi_node_finality_fault_test --gtest_list_tests` | Exactly the six FinalityFaultNetwork cases, nothing else | ✓ PASS |
| Gate XML integrity x3 | ElementTree parse of gate-{a,b,c}.xml | tests=6 failures=0 errors=0; 6 green cases each; distinct hashes/timestamps | ✓ PASS |
| Crash-report absence (live, independent) | `find ~/Library/Logs/DiagnosticReports -name "*.ips" -newermt <gate window>` | 0; newest remains processing_core_gating_test-2026-09-02-175810 | ✓ PASS |
| Removal surgicality | `git show --numstat 6fa285fe` + added-lines extraction | test +4/-800 (2724->1928), runner +4/-1, zero src/; only 4 added lines (comment + one cerr) | ✓ PASS |
| Full-suite re-run by verifier | not executed | No-reroll budget discipline; each run ~135s exceeds spot-check constraints; on-disk evidence independently corroborated (5 points per run: log trailer, XML root, per-case entries, timestamps, hashes) | ? SKIP |

### Probe Execution

Step 7c: SKIPPED — no declared or conventional `scripts/**/tests/probe-*.sh` probes exist in this repository (re-checked this session).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| TEST-01 | 12-01..12-05 | Same-burn contention -> one slot, one certificate, one winner | ✓ SATISFIED | SameBurn case green in all 3 round-6 XMLs |
| TEST-02 | 12-01..12-05 | Late contender cannot obtain second vote/certificate | ✓ SATISFIED | LateContender case green in all 3 round-6 XMLs |
| TEST-03 | 12-01..12-05 | Recipients receive-only; no self-CID sync timeout | ✓ SATISFIED | Passive-recipient assertions inside LateContender case; green x3 |
| TEST-04 | 12-01..12-05 | Restart at three boundaries; no changed vote, no duplicate mint | ✓ SATISFIED | RestartAtVote green in all 3 round-6 XMLs (8 consecutive full-suite greens); exact-once never violated |
| TEST-05 | 12-01..12-05 | Publisher-loss: persist-before-advertise + deterministic failover | ✓ SATISFIED | PublisherLoss case green in all 3 round-6 XMLs and every recorded run |
| TEST-06 | 12-01..12-05 | Production paths exercised, no local-author shortcuts | ✓ SATISFIED | ProductionRouteAudit green x3; ownership target green |

Orphaned requirements: none — REQUIREMENTS.md maps exactly TEST-01..TEST-06 to Phase 12 (all Complete), and all six are covered above.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| round6-traces/gate-b.log, gate-c.log | header `note:` | Stale template note "today's load is far below it" contradicts the same headers' numeric loads (19.90/27.72 vs the 15-20 band) | ℹ️ Info | Cosmetic evidence blemish only — the load-bearing `pre-run-1min-load:` values are honest, and 12-23/STATE.md disclose the contamination-regime fact explicitly |
| multi_node_finality_fault_test.cpp | observer `Write` | P12_PUBLISHER_OBSERVER_* telemetry lines now unparsed when a run token is present | ℹ️ Info | Documented in 12-23 as intentionally harmless; plain ctest runs print nothing |

Debt markers: zero `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` in either modified file. Dead includes confirmed removed (all nine listed headers absent; `environ` declaration gone).

### Human Verification Required

None. All goal evidence is machine-verifiable and was independently parsed this session (XML integrity, crash-report absence, binary composition, commit surgicality, provenance chain). The phase produces a regression suite, not user-facing behavior; prior rounds likewise carried no human items for this phase.

### Gaps Summary

No gaps. The two round-5 failures shared one root cause — the collector meta-test's child-readiness intermittence — and the developer resolved it by directed removal of the apparatus (fully recorded, zero src/ impact, scenario bodies untouched). The round-6 gate then completed the exact standard round 4 set: three consecutive serial full passes with per-run xunit proof and zero new crash reports, on the developer-defined six-scenario suite, with the restart and publisher-loss stable properties demonstrated inside all three runs. One plan-authorized withholding (WR-02) is carried as a documented override; STATE.md's production follow-ups remain recorded for milestone audit.

---

_Verified: 2026-09-03T18:46:49Z_
_Verifier: Claude (gsd-verifier)_
