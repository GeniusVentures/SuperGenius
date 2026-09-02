---
phase: 12-multi-node-finality-fault-proof
plan: 14
subsystem: test-infrastructure/libp2p-teardown-lifecycle
tags: [test-fixture, teardown-order, asio-lifetime, libp2p-host, kqueue-sigsegv, uat-gap-closure, evidence-gate]
requires:
  - FinalityFaultNetwork::Peer::Stop release order (test/src/blockchain/multi_node_finality_fault_test.cpp)
  - GossipPubSub::StopImpl host/context ownership (thirdparty, read-only)
provides:
  - Peer::Stop releases every external co-owner of the pubsub libp2p host (db, account) BEFORE pubsub->Stop(), satisfying asio's io_context-outlives-I/O-objects invariant
  - Zero teardown SIGSEGV across 8 full serial suite runs on the reordered build (vs 2 fresh crash reports in 3 pre-fix control runs)
  - Deferred-item rows for thirdparty StopImpl hardening and the MintRecoveryDiagnostics UAF
affects:
  - multi_node_finality_fault_test restart/teardown paths (RestartPeer, RestartAndReconnect, StopNetwork, ~Peer, move-assign)
  - Any fixture following Peer::Stop as a teardown pattern
tech-stack:
  added: []
  patterns:
    - co-owner release before io_context-owner stop (asio teardown invariant at fixture level)
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/deferred-items.md
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - .planning/STATE.md
decisions:
  - Peer::Stop resets db and account after the io_thread join and BEFORE pubsub->Stop() so GossipPubSub::StopImpl's m_host/m_context reset is the final host release — the fixture-level fix for the kqueue deregister SIGSEGV.
  - A candidate variant that also force-closed all host connections before the db release was REJECTED (no demonstrated benefit; introduced a new 5s topology-readiness flake) — the shipped diff is exactly the plan-prescribed reorder plus the invariant comment.
  - The three-consecutive-pass evidence gate is reported NOT MET (2 of 3) because the suite currently flakes ~40-60% per full run from order-independent signatures predating this plan in the current build generation; per the 12-08/12-12 no-retry discipline the mismatch is preserved and recorded instead of rerolled.
metrics:
  duration: 90m
  completed: 2026-09-02
---

# Phase 12 Plan 14: Multi-Node Teardown SIGSEGV Gap Closure (Peer::Stop Host Co-Owner Release) Summary

Peer::Stop now resets db and account after the io_thread join and before pubsub->Stop() — every co-owner of the libp2p host is released before the io_context owner stops — eliminating the teardown kqueue SIGSEGV (8 crash-free full serial runs on the reordered build vs 2 fresh crash reports in 3 pre-fix control runs); the three-consecutive-pass evidence gate remains open due to order-independent suite flakiness documented in deferred-items.md.

## What Was Done

### Task 1: Release the GlobalDB host co-owner before GossipPubSub::Stop in Peer::Stop
Commit `b9ad7d2b` — `test/src/blockchain/multi_node_finality_fault_test.cpp`

- Rewrote `FinalityFaultNetwork::Peer::Stop` to the plan's exact statement order: transactions/blockchain/consensus stops and both active-vote snapshots (while db is alive) are byte-identical; `io->stop(); io_thread.join()` unchanged; then `db.reset(); account.reset()` moved up, then `pubsub->Stop()`, then `pubsub.reset(); io.reset()`.
- Added the block comment documenting the invariant: the asio io_context owned by GossipPubSub must outlive every I/O object touching it; GlobalDB's graphsync chain co-owns the host wired from `pubsub->GetHost()` at StartPeer, so db/account must be reset before `pubsub->Stop()`, otherwise ~BasicHost (reverse member order: transport_manager_ before network_) deregisters leftover TcpConnections from the freed kqueue reactor (crash reports 2026-08-26..2026-09-02, e.g. multi_node_finality_fault_test-2026-09-02-075842.ips).
- Stop() remains noexcept with all null guards; RestartPeer/StartPeer/StopPeer/member declarations untouched; thirdparty not modified.
- Structural verification: `db.reset()` (line 406) and `account.reset()` (line 407) precede `pubsub->Stop()` (line 408); both `Snapshot(db, ...)` calls execute earlier (lines 383/386); io_thread joined before db release; target builds clean.

### Task 2: Teardown evidence and deferred-item recording
Commit `ec9980a9` — `.planning/STATE.md` (plus evidence-only artifacts in /tmp and deferred-items.md)

- Crash-report baseline captured before any evidence run (9 pre-existing .ips, newest 2026-09-02-075842); marker planted at /tmp/p12_14_ips_baseline.
- Evidence series (all serial, `ctest -R '^multi_node_finality_fault_test$'`, load conditions recorded per the 12-12 discipline — see Deviations): canonical runs A PASS (217.95s), B PASS (219.11s), C FAIL (SameBurnContention + RestartAtVote; PublisherLoss green). Logs: /tmp/p12_14_full_{a,b,c}.log.
- `consensus_pending_lifecycle_test` PASSED (44.30s) on the same build — UAT round-2 test 1's second target.
- Deferred Items rows appended for the thirdparty StopImpl hardening and the MintRecoveryDiagnostics UAF (see below); no other STATE.md content changed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Investigation] Evidence runs failed; controlled A/B attribution executed before accepting the fix**
- **Found during:** Task 2 step 2 (three-pass series)
- **Issue:** The first three-pass attempt produced failures (one under residual load ~5-11 from an external clang build spike that had driven load to 19; two at 1-min load ~1.1-1.4), including RestartAtVote failures inside a code path (mid-test RestartPeer) that the reorder touches. Attribution was required before shipping.
- **Fix:** Built a pre-fix control binary (HEAD~1 fixture, everything else identical) and ran the full suite 3 times: 2 assertion flakes (PublisherLoss child-readiness; SameBurnContention) AND 2 fresh SIGSEGV crash reports (multi_node_finality_fault_test-2026-09-02-125309.ips and -125541.ips — exact known signature: kqueue_reactor::deregister_descriptor via ~basic_stream_socket/~TcpConnection, fault at 0x88, main thread), both timestamped inside the control run 6 window (12:52:28 + 193.53s ≈ 12:55:41). The reordered build produced ZERO crash reports across all 8 of its full runs. Result: the reorder is retained as the crash fix; the RestartAtVote/SameBurn assertion flakes reproduce on BOTH build orders and are recorded as out-of-scope ambient flakiness (deferred-items.md), not effects of this change.
- **Files modified:** none (evidence-only; logs preserved)
- **Commit:** none (investigation)

**2. [Rule 1 - Rejected variant] Connection-close-before-db-release attempted and discarded**
- **Found during:** Deviation 1 follow-up (hypothesis: stream-level vs connection-level graphsync closure errors explained the RestartAtVote recovery timeouts)
- **Issue:** A variant Peer::Stop that force-closed all host connections (`pubsub->GetHost()->getNetwork().getConnectionManager()` + `connection->close()` + `collectGarbage()`) before db.reset() removed the observed recovery-wait failure signature (RestartAtVote green in its first run) but introduced a previously unseen 5s topology-readiness flake ("every peer is started in one public libp2p topology with a consensus-topic neighbor", multi_node_finality_fault_test.cpp:1155) in 2 of its 3 runs, with no net reliability gain (RestartAtVote 1/3 vs 2/5 for the plain reorder).
- **Fix:** Rejected the variant; restored the working tree to the committed plan-exact reorder. The variant source is preserved at /tmp/p12_14_fix_v2_stop.cpp and its run logs at /tmp/p12_14_fixcheck_{1,2,3}.log for the future scoped diagnosis.
- **Files modified:** none shipped (working tree restored to HEAD)
- **Commit:** none (rejected)

**3. [Rule 3 - Blocking] External CPU spike contaminated one preliminary evidence run**
- **Found during:** Task 2 step 2, first pass B
- **Issue:** ~10 concurrent clang compile jobs (external build) drove load to 19.16 on 8 cores during the first pass-B run (12:27-12:31, run time 254.93s vs the ~215s norm). The 12-12 precedent records this suite failing at load 15-20.
- **Fix:** Waited for the spike to drain (1-min load 1.42) before all subsequent runs; preserved the contaminated log at /tmp/p12_14_full_b_load_contaminated.log and restarted the series fresh. The canonical A/B/C series and every attribution run executed at 1-min load 1.0-1.5.
- **Files modified:** none
- **Commit:** none (procedure)

### Evidence-Gate Outcome (reported honestly, not auto-closed)

The must-have "three consecutive serial full multi_node_finality_fault_test runs pass" is **NOT MET**: canonical series A PASS, B PASS, C FAIL (SameBurnContention — pre-teardown assertion, also failed on the pre-fix control build; RestartAtVote block-3 recovery wait — intermittently failing on both build orders). Per the plan's own step-4 discipline analog and the 12-08/12-12 no-retry rule, the mismatch is preserved, not rerolled: rerunning until a lucky triplet (~12% per attempt at the observed ~50% per-run pass rate) would launder the evidence. The full attribution record (11 full-suite runs across 3 build variants, all logs preserved) is in deferred-items.md and the Findings section.

The plan's crash-report gate ("find -newer marker | wc -l == 0") outputs **2**, not 0: both reports were produced by the deliberately-built pre-fix CONTROL binary during deviation 1's A/B (timestamps 12:53:09 and 12:55:41 fall exclusively inside the control run-6 window 12:52:28-12:55:41; every run of the shipped reordered build — windows 12:18-12:36, 13:00-13:04, 13:26-13:30, 13:33-13:45 — generated none). UAT round-2 tests 1 and 5 are therefore closed at the defect level (the shipped fixture no longer exhibits the crash; the control still does), while the three-pass regression-evidence standard for the suite as a whole remains open pending the scoped flakiness diagnosis.

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| Peer::Stop releases the GlobalDB host co-owner before GossipPubSub::Stop destroys the asio io_context | MET — db.reset()/account.reset() before pubsub->Stop(), invariant comment present, verified structurally |
| Three consecutive serial full runs pass with no teardown crash and no new crash reports | PARTIAL — zero teardown crashes across 8 reordered-build full runs (crash fix confirmed vs 2 control crashes); three consecutive PASSES not achieved (A/B PASS, C FAIL) due to order-independent ambient flakiness; crash-report count is 2, both from the A/B control build |
| consensus_pending_lifecycle_test passes on the same build | MET — Passed 44.30s |
| Restart exact-once and publisher-loss cases still pass | PARTIAL — RestartAtVote and PublisherLoss green in canonical runs A and B (exit 0); RestartAtVote failed in run C (SameBurn also); both signatures reproduce on the pre-fix control build |

## Findings

- The teardown SIGSEGV is decisively a fixture teardown-order defect: with only the release order changed, 8 full serial runs produced zero crash reports, while the identical-everything-else control build produced the exact historical signature twice in 3 runs — including one mid-RestartAtVote crash matching the UAT round-2 event.
- The suite's current ~40-60% per-run flakiness (SameBurnContention marker timing, RestartAtVote block-3 certificate reconvergence, PublisherLoss child-readiness) is independent of the teardown order (reproduced on both builds) and postdates the 12-12 triple-pass evidence, which ran before review fixes 2b1a8e47..caf34458. The UAT round-2 full run never provided a green post-review-fix baseline (it crashed mid-RestartAtVote). Prime suspect for a future scoped diagnosis: the WR-07 mint-v2 retry change (caf34458) — its "did not consume every burn input" warning now fires ~26x per full run (4 per passing test block) and its UAT round-2 introduction coincides with the flakes.
- Cross-run database contamination was eliminated as a cause: FSFixture::clear() reaps the whole multi_node_finality_fault base_path at fixture construction and destruction, so peer roots are per-test-instance (verified in test/testutil/storage/base_fs_test.cpp).
- The two Deferred Items rows recorded in STATE.md: (1) thirdparty-hardening — GossipPubSub::StopImpl should defer m_context destruction while m_host.use_count() > 1 and force-close unresolved-remotePeer() connections (out of phase per thirdparty change control; now also carries the fresh control-build crash confirmations); (2) teardown-uaf — the MintRecoveryDiagnostics -> UTXOManager::GetUTXOs destroyed-mutex UAF (2026-08-26-173919.ips), distinct signature, needs its own scoped diagnosis.

## Known Stubs

None — the change is real fixture behavior (release ordering); no placeholders, no unwired data.

## Threat Flags

None — no new security surface beyond the plan's threat model. T-12-14-01 stands as assessed: GlobalDB::ShutdownNow stops the broadcaster and closes the datastore synchronously while Peer.pubsub still holds the object alive; the passing restart-recovery runs (A, B, plus four other green full runs) demonstrate no teardown hang or lost durability from the reorder itself.

## Self-Check: PASSED

All key files present (test/src/blockchain/multi_node_finality_fault_test.cpp, .planning/STATE.md, deferred-items.md, /tmp/p12_14_full_{a,b,c}.log); task commits b9ad7d2b and ec9980a9 found in git log.
