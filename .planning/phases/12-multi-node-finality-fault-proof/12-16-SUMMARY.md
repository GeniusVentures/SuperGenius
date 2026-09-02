---
phase: 12-multi-node-finality-fault-proof
plan: 16
subsystem: testing
tags: [teardown-ordering, asio, io-context-lifetime, libp2p, gossip-pubsub, crdt-fixture, test-infra, gap-closure]

# Dependency graph
requires:
  - phase: 12-14
    provides: Peer::Stop teardown invariant (db/account released before pubsub->Stop) and its crash evidence
  - phase: 12-15
    provides: SameBurn wait-predicate fix plus per-signature attribution verdicts (RestartAtVote residual mechanism)
provides:
  - The 12-14 teardown invariant propagated to every remaining phase proof artifact teardown: ComponentPeer::Stop (compatibility smoke), ~CRDTFixture (shared fixture base inherited by 7 binaries), and both multi-validator teardown loops in consensus_pending_lifecycle_test
  - Zero-new-crash-report verification across the affected binary matrix on the propagated build
affects: [12-17 (three-pass gate), every CRDTFixture-derived test binary]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Asio io_context-outlives-I/O-objects teardown order at every test teardown site that wires graphsync::Network from a pubsub host: release GlobalDB/account co-owners BEFORE pubsub->Stop() so StopImpl is the final host release"

key-files:
  created: []
  modified:
    - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
    - test/testutil/storage/base_crdt_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp

key-decisions:
  - "Gap 3 (VERIFICATION) closed by mechanical propagation only: CR-01 and CR-02 applied exactly as 12-REVIEW.md specifies plus WR-01's two same-class lifecycle loops in the same pass — no production code, timeout, retry, or construction-side change."
  - "The one full serial multi_node_finality_fault_test run is preserved verbatim as FAILED (CTest Timeout) — the RestartAtVote wait-timeout cascade is 12-15's already-attributed certificate-CID graphsync route-loss race (WR-02 repair withheld), not a teardown-order regression; no reroll, the formal three-pass gate belongs to 12-17."

patterns-established:
  - "Teardown invariant comment form: cite Peer::Stop (multi_node_finality_fault_test.cpp:389-405) at every propagated site so future fixtures copy the order, not just the statements"

requirements-completed: [TEST-06]

# Metrics
duration: 20m
completed: 2026-09-02
---

# Phase 12 Plan 16: Teardown-Order Invariant Propagation (Gap 3) Summary

**The 12-14 Peer::Stop teardown invariant (db/account release before pubsub->Stop) propagated to ComponentPeer::Stop, ~CRDTFixture, and both lifecycle teardown loops, with all four affected suites green and zero new crash reports**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-09-02T22:30:54Z
- **Completed:** 2026-09-02T22:51:00Z (approx)
- **Tasks:** 3
- **Files modified:** 3

## Accomplishments

- **CR-01 closed:** `ComponentPeer::Stop` (compatibility smoke test) now releases `db`/`account` after the io_thread join and BEFORE `pubsub->Stop()` — `pubsub->Stop()` is the final host release — with the invariant comment citing `multi_node_finality_fault_test.cpp:389-405`.
- **CR-02 closed:** `~CRDTFixture` (shared base inherited by 7 binaries, including the TEST-06 fault suite, lifecycle, and cert-fallback) now resets `db_` first, then stops `pubs_`; the `fs::remove_all(keypair_path_)/remove_all(db_path_)` try/catch cleanup is byte-identical and last. Construction side (12-13 pid+counter paths and reap) untouched.
- **WR-01 same-class sites closed:** both multi-validator teardown loops in `consensus_pending_lifecycle_test.cpp` now run `node.manager.reset(); node.db.reset(); node.account.reset();` after `Close(node.manager)` and before `node.pubsub->Stop()`; the mid-test vote-recovery reconstruction reset (~:1999) is untouched.
- **Verification matrix green with zero new crash reports:** compatibility smoke, consensus_pending_lifecycle_test, transaction_manager_certificate_fallback_test, and multi_node_finality_fault_process_ownership_test all Passed on the propagated build; the `find -newer` crash gate over `~/Library/Logs/DiagnosticReports` returned exactly 0 across all affected binary names.

## Task Commits

Each task was committed atomically:

1. **Task 1: Mirror the 12-14 teardown order in ComponentPeer::Stop (CR-01) and ~CRDTFixture (CR-02)** - `414ca5ea` (fix)
2. **Task 2: Reset manager/db/account before pubsub->Stop() in both lifecycle teardown loops (WR-01)** - `f6f63b96` (fix)
3. **Task 3: Affected-suite verification matrix with crash-report baseline** - no commit (verification runs only; no file modifications)

## Files Created/Modified

- `test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp` - ComponentPeer::Stop reordered; db.reset()/account.reset() moved before pubsub->Stop(); invariant comment added
- `test/testutil/storage/base_crdt_test.cpp` - ~CRDTFixture: db_.reset() promoted to first release; invariant comment added; remove_all cleanup unchanged and last
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - both teardown loops insert manager/db/account resets between Close() and node.pubsub->Stop()

## Verification Matrix (Task 3)

Pre-run crash baseline: latest matching `.ips` reports dated 2026-09-02 12:53/12:55 (the 12-14 control-build pair); marker `/tmp/p12_16_ips_baseline` created before the first run. All runs serial (never `ctest -j`).

| Run | Suite | Result | Elapsed | 1-min load before |
| --- | --- | --- | --- | --- |
| 1 | multi_node_finality_fault_compatibility_smoke_test (CR-01) | **Passed** | 6.44s | 2.21 |
| 2 | consensus_pending_lifecycle_test (WR-01 + CR-02 inherited) | **Passed** | 44.25s | 2.67 |
| 3 | transaction_manager_certificate_fallback_test (CR-02 inherited) | **Passed** | 32.13s | 1.48 |
| 4 | multi_node_finality_fault_process_ownership_test (TEST-06 regression) | **Passed** | 0.98s | 1.58 |
| 5 | multi_node_finality_fault_test full serial | **FAILED (CTest Timeout, 300.06s)** | 300.06s | 1.58 |

- Crash gate: `find ~/Library/Logs/DiagnosticReports -newer /tmp/p12_16_ips_baseline -name '*.ips' | grep -E 'multi_node_finality_fault|consensus_pending_lifecycle|transaction_manager_certificate_fallback' | wc -l` → **0**; the assert form echoed `NO_NEW_CRASH_REPORTS`.
- Full-suite log preserved at `/tmp/p12_16_full_1.log` exactly as it came out. No run was rerun to replace a recorded failure.
- Post-run load spiked to 6.62 (the killed run's own CPU tail); no orphaned test processes remained (`ps` clean), load decaying to 3.07 within 2 minutes.

### Full-suite failure attribution (honest single outcome)

The run hung in `FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` (test started 19:38:02; CTest timeout 19:40:32). The log shows the restart sequence completing normally (per-peer `~GlobalDB CALLED with count 6` + reopen at 19:38:05–19:38:21) and then the wait-predicate timeout cascade:

- log line 4406: `Timed out waiting for condition: surviving peers retained the accepted certificate before receiver restart (timeout: 20000ms)`
- log lines 4672 / 5063 / 5731: three further 20–25s wait timeouts (recreated certificate recipient, boundary re-verification, recreated Mint peer marker repair)

The four waits (~90s) plus test work exceeded the remaining ~150s of the 300s CTest budget, so the process was killed before gtest could print FAILED.

This is 12-15's already-attributed residual signature, not a 12-16 regression: the failing predicate is the certificate-CID graphsync route-loss race after RestartPeer (fetchers CANNOT_CONNECT on the dead publisher host, blacklist the reused host identity, no surviving replica or re-publication; WR-02 repair WITHHELD by 12-15's authorization gate, and 12-15's own full-serial pass notes "RestartAtVote's pass is a race win — the unfixed cert-propagation race remains a per-run risk in full-suite order"). 12-16's changes are not in the failing path: `multi_node_finality_fault_test.cpp` is untouched by this plan, the failing waits are mid-test recovery predicates, and the only 12-16 change in this binary (~CRDTFixture reordering) completed cleanly after all 8 prior test cases in the same process. The teardown invariant this plan propagates is proven by the four green suites plus the zero-new-crash gate, which is exactly gap 3's closure condition; the three-consecutive-pass formal gate belongs to 12-17 with this run as risk context.

## Decisions Made

- Applied WR-01's two lifecycle loops in the same mechanical pass as CR-01/CR-02 because `consensus_pending_lifecycle_test` is a UAT round-2 full target and a phase proof artifact — excluding it would re-open gap 3's "holds across ALL phase proof artifacts" truth at the next verification.
- Preserved the full-suite Timeout outcome verbatim (no reroll) per the 12-08/12-12/12-14/12-15 honesty discipline; attribution recorded above for 12-17's gate risk assessment.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Rebuilt multi_node_finality_fault_test before Task 3's runs**
- **Found during:** Task 3 (verification matrix)
- **Issue:** Task 1's build line covers the three edited-source targets, but the fault-suite binary itself (used by both the full run and the process-ownership CTest entry) also links `base_crdt_test`; without a rebuild it would have run the stale pre-reorder ~CRDTFixture, making the propagation verification invalid.
- **Fix:** `cmake --build build/OSX/Release --target multi_node_finality_fault_test` before the matrix.
- **Files modified:** none (build artifacts only)
- **Verification:** all runs executed on the rebuilt binary.
- **Committed in:** no commit (build only)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Verification-validity only. No scope creep; no source change beyond the three planned sites.

## Issues Encountered

- Full-suite Timeout in RestartAtVote (see attribution above): pre-attributed residual race, preserved honestly, no repair attempted (WR-02 remains withheld and out of scope per the wave context).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap 3's truth now holds structurally across ALL phase proof artifacts: all three key_link patterns greppable (site 1 `db.reset();\naccount.reset();` verified; site 2 `db_.reset();` first; site 3 `node.db.reset();` x2), construction paths and the :1999 reconstruction site byte-identical.
- 12-17's three-pass gate inherits the standing per-run risks: RestartAtVote certificate-propagation race (this plan's full-run sample struck it; 12-15 recorded 3/3 focused passes), PublisherLoss child-readiness (~18%/run, un-repaired per 12-08 discipline), SameBurn closed by 12-15.

## Self-Check: PASSED

All 3 modified source files and the SUMMARY exist on disk; both task commits (414ca5ea, f6f63b96) found in git log.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-09-02*
