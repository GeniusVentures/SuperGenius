---
phase: 12-multi-node-finality-fault-proof
plan: 18
subsystem: crdt-graphsync/blacklist-seam + lifecycle-teardown + evidence
tags: [gap-closure, graphsync-blacklist, test-seam, cr-03, teardown-invariant, restart-at-vote, hypothesis-test, durable-evidence]
requires:
  - 12-17 NO-GO developer directive (verbatim in 12-17-SUMMARY.md "Decision Received" and STATE.md)
  - 12-15 Verdict 2 attribution (certificate-CID graphsync route loss + reused-identity blacklist)
  - 12-16 honest full-run record and 12-REVIEW.md CR-03
provides:
  - sgns::crdt::GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest(uint64_t override_ms) — static, process-wide, millisecond-resolution test seam; 0 restores production defaults (5000/30000 ever-connected, 10000/1800000 never-connected, duration-identical)
  - CR-03 closure: node.registry.reset() in both lifecycle teardown loops before pubsub->Stop(); STATE.md no longer asserts 12-16's false "gap 3 closed" claim
  - The recorded boot-window masking hypothesis verdict (DISPROVEN) with per-log line citations — the entry condition and mechanism attribution plan 12-19 branches on
affects:
  - 12-19 (fallback repair: post-restart certificate re-publication / surviving-replica serving — ACTIVATED by this verdict)
  - 12-20 gate evidence (the verdict token and durable logs feed any future gate re-entry attribution)
tech-stack:
  added: []
  patterns:
    - "static atomic test-only override consulted before the production formula (flat short-circuit, memory_order_relaxed)"
    - "RAII guard inside the test body restoring process-global test configuration on every exit path (ASSERT fatalities included)"
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/hypothesis-verdict.md
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/restart-focused-1.log
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/restart-focused-2.log
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/restart-focused-3.log
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/lifecycle-fixed.log
    - .planning/phases/12-multi-node-finality-fault-proof/12-18-SUMMARY.md
  modified:
    - src/crdt/graphsync_dagsyncer.hpp
    - src/crdt/impl/graphsync_dagsyncer.cpp
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - .planning/STATE.md
key-decisions:
  - "The blacklist seam is static/process-wide (not per-instance) because the fetchers' GraphsyncDAGSyncer instances live inside GlobalDB and are unreachable from the test; all four peers share one process (D-01)."
  - "Unit-only conversion: getBackoffTimeout/AddToBlackList/IsOnBlackList moved to a millisecond clock (GetCurrentTimestampMs) with production constants scaled 5000/30000 and 10000/1800000 — durations identical; GetCurrentTimestamp and the 180-second cid_failures_ subsystem stay on seconds, untouched; the :886 trace literal corrected to {}ms."
  - "CR-03 closed mechanically: node.registry.reset() between manager and db resets in BOTH lifecycle loops (registry co-owns GlobalDB via ValidatorRegistry.hpp:530); STATE.md:129 amended to 'gap 3 partially closed ... corrected in 12-18' and a dated 12-18 entry records the actual closure."
  - "Hypothesis verdict DISPROVEN per the plan's exact rule (run 2 not OK with the override active): the persisting mechanism is route erasure on blacklist + boot-window port handover + absent surviving replica/re-publication — NOT the blacklist duration; 12-19's fallback is activated."
metrics:
  duration: 37m
  completed: 2026-09-03
---

# Phase 12 Plan 18: Round-4 Gap Closure Part 1 (Blacklist Seam + CR-03 + Hypothesis Verdict) Summary

**One-liner:** Millisecond-resolution test-only GraphsyncDAGSyncer blacklist-backoff seam (production durations unchanged), CR-03 lifecycle registry-reset closure, and a three-focused-run DISPROVEN verdict on the boot-window masking hypothesis — with every evidence artifact at the durable repo-relative path round4-traces/.

## What Was Executed

### Task 1 — The seam (commit 9f888b3b)

`src/crdt/graphsync_dagsyncer.hpp` / `src/crdt/impl/graphsync_dagsyncer.cpp`:

- Public `static void SetBlacklistBackoffTimeoutForTest( uint64_t override_ms )` — test-only configuration seam (developer directive 2026-09-03); zero production callers (grep-verified across src/).
- Private `static uint64_t GetCurrentTimestampMs()` (milliseconds twin of the untouched `GetCurrentTimestamp`) and `static std::atomic<uint64_t> blacklist_backoff_override_ms_for_test_{ 0 }` — default 0 == production behavior.
- `getBackoffTimeout` consults the override FIRST (nonzero → returned verbatim, flat, `memory_order_relaxed`); otherwise the production formula in milliseconds: 5000·2^failures capped 30000 (ever-connected), 10000·2^failures capped 1800000 (never-connected). Durations identical to before — unit only changed.
- `AddToBlackList` and `IsOnBlackList` switched to `GetCurrentTimestampMs`; the IsOnBlackList trace literal corrected from "timeout: {}s" to "timeout: {}ms" in the same edit (the printed value is now milliseconds).
- Untouched, verified by grep: `GetCurrentTimestamp` (still called only by `RecordCIDFailure`/`HasRecentCIDFailure` — the 180-second cid_failures_ recency subsystem), `ClearCIDFailure`, `RecordSuccessfulConnection`, the fetchGraph skip loop, `BlackListPeer`.
- All four CRDT-dependent targets rebuilt clean (multi_node_finality_fault_test, consensus_pending_lifecycle_test, transaction_manager_certificate_fallback_test, multi_node_finality_fault_compatibility_smoke_test).

### Task 2 — CR-03 closure + STATE.md:129 correction (commit e121f634)

- `node.registry.reset(); // registry co-owns GlobalDB (ValidatorRegistry.hpp:530)` inserted between `node.manager.reset()` and `node.db.reset()` in BOTH multi-validator teardown loops of consensus_pending_lifecycle_test.cpp (the :1332-1335 and :2033-2036 regions); the invariant comments now read "release the GlobalDB host co-owners (registry, db, account)".
- STATE.md:129 amended from the false "gap 3 closed - teardown invariant now holds across all phase proof artifacts" to "gap 3 partially closed - CR-01/CR-02 hold; the lifecycle loops' leg was found ineffective by fresh-review CR-03 (registry co-owns the GlobalDB and was never reset), corrected in 12-18"; a dated 12-18 entry records the actual closure. No other historical note rewritten (grep for the uncorrected phrase: zero matches).
- Lifecycle suite re-run green serially on the fixed build: Passed 43.08s, 100% tests passed, 0 failed — log at round4-traces/lifecycle-fixed.log. No crash report newer than the run start (newest .ips is 2026-09-02, pre-run).

### Task 3 — Seam wiring, three focused runs, verdict (commit 0a9b76a8)

- `#include "crdt/graphsync_dagsyncer.hpp"` added (alphabetical, after keypair_file_storage.hpp); at the RestartAtVote body top (after SetSecureStorageFactory, before the first StartNetwork block): `sgns::crdt::GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest( 100 );` plus a local RAII guard struct whose destructor calls `SetBlacklistBackoffTimeoutForTest( 0 )` — every exit path, including ASSERT fatalities, restores production backoff so later tests in the binary inherit nothing. No wait predicate, timeout bound, restart barrier, or other test logic altered.
- Port preflight: `lsof -nP -iTCP:54601-54634 -sTCP:LISTEN` empty before the first run.
- Three consecutive focused runs of `./build/OSX/Release/test_bin/multi_node_finality_fault_test --gtest_filter='FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce'`, logs preserved durably:

| Run | Pre-run 1-min load | Outcome | Wall | Log (line refs) |
|-----|--------------------|---------|------|-----------------|
| 1 | 3.90 | OK | 55.34 s | round4-traces/restart-focused-1.log:2744 |
| 2 | 3.90 | **FAILED** | 98.02 s | round4-traces/restart-focused-2.log:2943 |
| 3 | 3.69 | OK | 55.01 s | round4-traces/restart-focused-3.log:2738 |

## VERDICT: DISPROVEN (12-19 fallback activated)

Run 2 was not OK while the 100 ms override was active, so the plan's DISPROVEN branch applies. The persisting mechanism, attributed by log line (restart-focused-2.log):

- :2167 (10:42:54.413852) `graphsync cannot connect, peer=B4bf4k, msg='Address already in use' state=1` — first failure is a boot-window port-handover race (old listener's port still bound), not a skip decision.
- :2169 `Request failed for CID QmZqTKnCVUT9b26ByT34opXDQNXak3mUXEh4Y81MgRfJBF from peer 12D3KooWQSS... with connection error CANNOT_CONNECT. Blacklisting peer and trying fallback.`
- :2170 `Erasing route for CID QmZqTKnC... to blacklisted peer` — BlackListPeer erases the CID's only route on blacklist.
- :2171 / :2215 / :2261 `No usable route candidates left for CID QmZqTKnC...`
- :2172 (10:42:55.325047) `graphsync cannot connect, peer=B4bf4k, msg='Connection reset by peer' state=1` — **a fresh dial 0.91 s after the first blacklist**.
- :2422 / :2832 `Timed out waiting for condition: recreated Mint peer repaired its marker through normal certificate recovery (timeout: 25000ms)` / `... Mint-boundary recovery stayed exact after reopening every durable root (timeout: 20000ms)`.

Why this disproves the hypothesis: the 0.91 s re-dial is only possible because the 100 ms override let the fetcher retry the reused identity (under the production >=5 s backoff the route loop would have skipped it — no fresh transport error could be logged); the retry STILL failed transport-level, the route was already erased, and no surviving replica or re-publication exists for the certificate CID. Zero occurrences in ANY run of `Skipping blacklisted peer` or `due to recent CID-specific failure` — neither skip mechanism was operative. The mask is route erasure + the boot-window port handover + absent re-publication, not the blacklist duration. Full citations: round4-traces/hypothesis-verdict.md (verdict token appears exactly once).

Per the directive, plan 12-19's fallback (post-restart certificate re-publication / surviving-replica serving) is the activated repair path. No repair beyond the developer-directed seam and the CR-03 one-liners was attempted by this plan.

## Durable Evidence Paths (nothing in /tmp)

- round4-traces/lifecycle-fixed.log — Task 2 lifecycle green run (with pre-run load header)
- round4-traces/restart-focused-1.log / -2.log / -3.log — Task 3 focused runs (with pre-run load headers)
- round4-traces/hypothesis-verdict.md — the recorded verdict with per-log citations

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking/environment] Load-below-2 discipline unreachable; proceeded at recorded external plateau**
- **Found during:** Task 2 (and applying to Task 3's runs)
- **Issue:** The plan requires 1-min load below 2 before each evidence run ("wait for external spikes to drain per the 12-14 precedent"). The machine sat on a sustained 3.3-5.0 plateau for 20+ minutes of polling — post-reboot system daemons (mediaanalysisd ~90% CPU, mds_stores ~50% Spotlight indexing), external and not drainable, unlike the 12-14 self-inflicted orphans.
- **Fix:** Proceeded with the actual load recorded in each log header and in the verdict file, rather than blocking indefinitely. 8 cores at ~44-60% utilization is far below the 12-12 contamination regime (15-20 on 8 cores). Both green runs and the failing run occurred at comparable load; the failing run's signature is the load-independent transport/route mechanism quoted above.
- **Files modified:** log headers only (evidence integrity); deviation recorded in STATE.md 12-18 entry.
- **Commit:** e121f634 (log header), 0a9b76a8 (verdict + STATE)

**2. [Rule 3 - Evidence method] Trace-level blacklist lines absent from focused-run logs**
- **Found during:** Task 3 verdict writing
- **Issue:** The plan suggested citing "Peer ... blacklist timeout expired" lines at millisecond cadence as override-liveness proof; the binary's log level filters trace output, so those lines do not appear (255 debug lines do).
- **Fix:** Liveness proven instead by the retry cadence: the second CANNOT_CONNECT/blacklist event on the same reused identity occurred 0.91 s after the first (restart-focused-2.log:2169 → :2259/:2172), impossible under the production >=5 s backoff because the route loop would have skipped the peer. Documented in the verdict file's "Proof the Override Was Live" section.
- **Files modified:** round4-traces/hypothesis-verdict.md (method note included)

Otherwise the plan executed exactly as written.

## Auth Gates

None.

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| Backoff timeout test-configurable sub-second while unconfigured processes keep exact production 5s/30s and 10s/1800s durations | MET — override-then-formula in getBackoffTimeout; constants 5000/30000/10000/1800000; atomic default 0; zero production callers |
| Both lifecycle teardown loops release node.registry between manager and db resets, before pubsub->Stop() | MET — both loops, co-ownership comment, grep count 2, order verified |
| RestartAtVote boot-window masking hypothesis has a recorded verdict backed by focused-run logs at a durable repo-relative path | MET — DISPROVEN token, three logs + cited verdict file under round4-traces/ |
| STATE.md no longer asserts 12-16's false "gap 3 closed" claim; records CR-03 and the actual round-4 closure | MET — :129 amended, two dated 12-18 entries, uncorrected-phrase grep returns nothing |

## Known Stubs

None — the seam, the guard, and both test edits are functional code; no placeholder flows anywhere.

## Threat Flags

None. T-12-18-01 (no production caller — grep-verified), T-12-18-02 (ms constants asserted; cid_failures_ untouched; {}ms suffix), T-12-18-03 (ports preflight clean; load deviation honestly recorded), T-12-18-04 (all evidence committed under round4-traces/, zero /tmp references), T-12-18-05 (mixed 2/3 outcome recorded honestly with the DISPROVEN token the plan's rule prescribes, full citations) — all honored as planned.

## Self-Check: PASSED

- All 10 created/modified key files present on disk (SUMMARY, 5 round4-traces artifacts, 4 source/test files) — FOUND each.
- All 3 task commits verified in git log: 9f888b3b (Task 1 seam), e121f634 (Task 2 CR-03), 0a9b76a8 (Task 3 wiring + verdict).
- Task gates re-verified at commit time: seam greps (1 decl/1 def, 0 production callers, 3x GetCurrentTimestampMs, ms constants, 1x {}ms, 0 stale {}s), registry.reset count 2 in order, lifecycle 100% passed, verdict token count 1, setter/release counts 1/1.
