# Round-4 Hypothesis Verdict: RestartAtVote Boot-Window Blacklist Masking

**Date:** 2026-09-03
**Plan:** 12-18 (developer directive of 2026-09-03, recorded verbatim in 12-17-SUMMARY.md "Decision Received" and STATE.md)
**Hypothesis under test:** a fetcher that hits CANNOT_CONNECT against a node still booting after RestartPeer gets blacklisted for the production 5-30s backoff (getBackoffTimeout) and stays skipped even once the host is reachable inside the suite's 25s recovery window — i.e., the blacklist DURATION masks the boot window, and a sub-second backoff would let fetchers retry the restarting peer's reused host identity in time.

**Override configuration during all three runs:** `sgns::crdt::GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest( 100 )` (flat 100 ms, static/process-wide), RAII-guarded back to 0 on test exit (multi_node_finality_fault_test.cpp RestartAtVote body).

## VERDICT: DISPROVEN

At least one focused run was not OK while the override was active (run 2 FAILED), and the persisting mechanism is NOT the blacklist duration: the fetchers retried the restarting peer within ~0.9 s of the first blacklist (impossible under the production >=5 s backoff — proof the override was live), the retry still failed with a transport-level connection error, and the route was erased on the first blacklist with no surviving replica for the certificate CID. Per the plan's rule, plan 12-19's fallback (post-restart certificate re-publication / surviving-replica serving) is ACTIVATED; no further repair was attempted inside 12-18.

## Run-by-Run Record

| Run | Pre-run 1-min load | Outcome | Wall time | Log |
|-----|--------------------|---------|-----------|-----|
| 1 | 3.90 (elevated external plateau, see below) | OK | 55.34 s (56 s wall) | round4-traces/restart-focused-1.log |
| 2 | 3.90 (elevated external plateau) | **FAILED** | 98.02 s (10:42:12 -> 10:43:49) | round4-traces/restart-focused-2.log |
| 3 | 3.69 (1-min; 5-min avg 5.08 external spike) | OK | 55.01 s | round4-traces/restart-focused-3.log |

Load-discipline deviation (recorded honestly, applies to all runs and to Task 2's lifecycle run): the plan requires 1-min load below 2; the machine sat on a sustained 3.3-5.0 plateau for 20+ minutes of polling, driven by post-reboot system daemons (mediaanalysisd ~90% CPU, mds_stores ~50% Spotlight indexing — external, not test artifacts, 8 cores ~= 44-60% utilization), not a drainable spike per the 12-14 precedent. Far below the 12-12 contamination regime (load 15-20 on 8 cores). Runs 1 and 3 passed at this load; run 2's failure signature is the load-independent transport/route mechanism quoted below.

### Run 1 — PASSED

- restart-focused-1.log:2744 — `[       OK ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (55339 ms)`
- Zero occurrences in the whole log of: "CANNOT_CONNECT", "No usable route candidates left", "Timed out waiting for condition", "Skipping blacklisted peer", "due to recent CID-specific failure".

### Run 2 — FAILED (the disproof)

Failing CID: `QmZqTKnCVUT9b26ByT34opXDQNXak3mUXEh4Y81MgRfJBF` (certificate data), restarting peer's reused identity: `12D3KooWQSSqWnraG4kihedqLXntYSVwh7tq6FQJVsQhYsB4bf4k` (short `B4bf4k`, restart-mint-validator-one block per restart-focused-2.log:2236 KeyPairFileStorage path).

Sequence (restart-focused-2.log):

- :2167 (10:42:54.413852) — `graphsync cannot connect, peer=B4bf4k, msg='Address already in use' state=1` — the old listener's port is still bound while the replacement host boots; the FIRST failure is a port-handover race, not a blacklist artifact.
- :2169 (10:42:54) — `Request failed for CID QmZqTKnC... from peer 12D3KooWQSS... with connection error CANNOT_CONNECT. Blacklisting peer and trying fallback.`
- :2170 — `Erasing route for CID QmZqTKnC... to blacklisted peer` — BlackListPeer erases the route when blacklisted; the CID's only route (route_count=1 pattern) is gone.
- :2171 — `No usable route candidates left for CID QmZqTKnC...`
- :2172 (10:42:55.325047) — `graphsync cannot connect, peer=B4bf4k, msg='Connection reset by peer' state=1` — **0.91 s after the first blacklist, a fresh dial to the same reused identity was attempted and failed at the transport layer.**
- :2215 (10:42:55) — `No usable route candidates left for CID QmZqTKnC...` (retry round)
- :2259 (10:42:55) — second `CANNOT_CONNECT. Blacklisting peer and trying fallback.` on the same peer.
- :2261 — `No usable route candidates left for CID QmZqTKnC...`
- :2422 — `Timed out waiting for condition: recreated Mint peer repaired its marker through normal certificate recovery (timeout: 25000ms)` (test :2499)
- :2832 — `Timed out waiting for condition: Mint-boundary recovery stayed exact after reopening every durable root (timeout: 20000ms)` (test :2512)
- :2943 — `[  FAILED  ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (98020 ms)`

Skip-mechanism discrimination (the plan's mandatory distinction): ZERO occurrences in run 2 of `Skipping blacklisted peer` (the IsOnBlackList route-loop skip) and ZERO of `due to recent CID-specific failure` (the 180-second cid_failures_ subsystem). The operative mechanism is the third listed signature: `CANNOT_CONNECT. Blacklisting peer and trying fallback` + `No usable route candidates left`, cascading into the wait timeouts.

### Run 3 — PASSED

- restart-focused-3.log:2738 — `[       OK ] FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (55011 ms)`
- Zero occurrences of all failure signatures (as run 1).

## Proof the Override Was Live During the Runs

1. Retry cadence (run 2): the second fetch attempt that logged :2259/:2172 occurred 0.91 s after the first blacklist at :2169 (10:42:54.414 -> 10:42:55.325). The route loop consults IsOnBlackList before each dial; under the production formula the minimum backoff is 5000 ms (ever-connected) / 10000 ms (never-connected), so at 0.91 s of blacklist age the peer would still have been skipped and NO fresh dial (hence no fresh `graphsync cannot connect` transport error) could have occurred. With the 100 ms override, 0.91 s > 100 ms -> not blacklisted -> dial attempted -> `Connection reset by peer`. The dial happened; therefore the override was active.
2. Trace-level blacklist lines (`Peer ... blacklist timeout expired`, `BLACKLISTED (failures: ..., timeout: {}ms)`) are not emitted in these logs (the binary's log level filters trace output; only 255 debug lines appear), so the cadence proof above is the operative liveness evidence, alongside the seam's static process-wide scope (all four peers share one process, D-01).
3. Runs 1 and 3 completed with zero CANNOT_CONNECT events at all, consistent with the race being per-run timing (boot-window port handover), not a deterministic mask.

## Why the Hypothesis Is Disproven (mechanism attribution)

The hypothesis predicts: with a sub-second backoff, fetchers retry the restarting peer once it is reachable inside the 25 s window, and the certificate propagates. Run 2 falsifies the prediction — the retry DID happen within ~1 s (override live), and the peer STILL could not serve the CID:

- the first CANNOT_CONNECT cause is `Address already in use` (:2167) — the replacement host cannot bind while the old listener's socket lingers — followed by `Connection reset by peer` (:2172) on the immediate retry: a transport-layer boot-window failure, not a skip-decision;
- on the first blacklist, `BlackListPeer` erases the CID's route (`Erasing route ... to blacklisted peer`, :2170; 3 occurrences in the run), so even an expired blacklist leaves no route candidate — `No usable route candidates left` (:2171, :2215, :2261);
- no surviving replica holds the certificate CID and no recovery path re-publishes a completed certificate (12-15 Verdict 2 / 12-16 attribution), which is exactly the gap the directive's fallback (12-19: post-restart certificate re-publication / surviving-replica serving) targets.

Blacklist duration is therefore NOT the mask: the mask is (a) route erasure on blacklist and (b) the restarted host's port-handover/boot window during which dials fail outright, with (c) no surviving replica/re-publication. The blacklist-duration seam alone cannot stabilize this signature (2/3 focused passes here vs. the historical ~50% is consistent with the race, not with a fix).

## Consequence

Plan 12-19 (fallback: post-restart certificate re-publication / surviving-replica serving) is ACTIVATED per the directive: "only if it is disproven fall back to post-restart certificate re-publication / surviving-replica serving as the repair." No repair beyond the developer-directed seam and the CR-03 one-liners was attempted by 12-18.
