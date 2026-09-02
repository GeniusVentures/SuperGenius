# Phase 12 Deferred Items (out-of-scope discoveries)

Discoveries made during execution that are NOT caused by the current plan's changes and
were deliberately not repaired (scope boundary). Recorded here so a future plan can pick
them up with full context.

## 2026-09-02 — Plan 12-14: multi_node_finality_fault_test ambient assertion flakiness (post-review-fix build)

Observed while gathering teardown-regression evidence for 12-14. Full-suite serial runs
fail ~40-60% of the time from at least three independent, order-independent failure
signatures (reproduced on BOTH the pre-fix Stop order and the 12-14 reordered build;
none present in the 12-12 triple-pass evidence, which predates review fixes
2b1a8e47..caf34458 — the UAT round-2 full run could not serve as a green baseline
because it crashed mid-RestartAtVote):

1. `FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint` —
   `HasBridgeMarker(*peer, *winner)` false at multi_node_finality_fault_test.cpp:2081/2098.
   The assertion runs BEFORE any Peer::Stop call in that test, so it cannot be a teardown
   ordering effect. Seen on: pre-fix control run (12:47), reordered build (12:27, under
   residual load), canonical run C (13:42).
2. `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` —
   block-3 (restart-mint) wait "recreated Mint peer repaired its marker through normal
   certificate recovery" (25s) times out with `CheckCertificateForSlot` false on three
   peers (multi_node_finality_fault_test.cpp:2471-2476). Intermittent ~50% on the
   reordered build (4/7 full runs green incl. two consecutive); pre-fix control completed
   it twice but its third attempt crashed mid-test at that same restart boundary. A
   rejection candidate fix (closing host connections before db release inside Peer::Stop)
   removed this signature but introduced a new 5s topology-readiness flake at
   multi_node_finality_fault_test.cpp:1155 and was rejected; the rejected variant is
   preserved at /tmp/p12_14_fix_v2_stop.cpp.
3. `PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch` —
   child-readiness signature (multi_node_finality_fault_test.cpp:1871-1877). Already
   documented in STATE.md under the Plan 12-08 discipline ("pre-existing intermittence
   stands"); struck again on 3 of 11 runs this session across both build orders.

Consequence: the "three consecutive serial full passes" evidence standard (12-12
discipline) is currently unattainable for this suite regardless of teardown order. Needs
its own scoped diagnosis (suspect area: WR-07 mint-v2 retry path, commit caf34458, whose
burn-input warning "did not consume every burn input" now appears ~26x per full run —
4 per passing test block — and whose UAT round-2 introduction coincides with the flakes;
also the certificate re-advertisement timing after mid-test restarts).

Evidence logs (all serial, ctest -R '^multi_node_finality_fault_test$'):
- Reordered build: /tmp/p12_14_full_{a,b,c}.log (canonical series: PASS, PASS, FAIL),
  /tmp/p12_14_full_a_rerun_failed.log, /tmp/p12_14_full_b_load_failed.log,
  /tmp/p12_14_full_b_load_contaminated.log, /tmp/p12_14_ab_neworder_{4,5}.log
- Pre-fix control build: /tmp/p12_14_ab_oldorder_{1,2,3}.log
- Rejected connection-close variant: /tmp/p12_14_fixcheck_{1,2,3}.log
