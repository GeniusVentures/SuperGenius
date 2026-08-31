---
phase: 12-multi-node-finality-fault-proof
plan: "11"
subsystem: testing
tags: [ctest, posix, process-groups, real-socket]
requires:
  - phase: 12-10
    provides: "Independent real-socket PublisherLoss collector evidence"
provides:
  - "Invocation-owned POSIX session/process-group launcher for the Phase 12 real-socket test"
  - "Serial CTest registrations that lock the four fixed Phase 12 ports"
affects: [phase-12-verification, real-socket-test-harness]
tech-stack:
  added: []
  patterns: ["Fail-closed exact-PGID cancellation and neutral loopback-port rebind checks"]
key-files:
  created: [test/src/blockchain/multi_node_finality_fault_runner.cpp]
  modified: [test/src/blockchain/CMakeLists.txt]
key-decisions:
  - "Only the direct child session handshaken by the runner may receive a negative-PGID signal."
  - "Socket observations establish availability only; they never attribute a listener to a process group."
requirements-completed: []
verification_status: blocked
completed: 2026-08-31
---

# Phase 12 Plan 11: Process-Ownership Runner Summary

**A fail-closed POSIX CTest launcher now creates and reaps only its own real-socket test session, while the final six-run proof remains blocked by a pre-existing stale collector expectation.**

## Performance

- **Duration:** approximately 40 minutes
- **Started:** 2026-08-31T19:20:00Z
- **Completed:** 2026-08-31T19:59:05Z
- **Tasks:** 2 implementation tasks committed; final verification gate blocked
- **Files modified:** 2

## Accomplishments

- Added a test-only POSIX runner that handshakes a child-created session/PGID, forwards normal GTest execution unchanged, and signals only its negative owned PGID on cancellation.
- Added controlled-cancellation verification: fixed-port bind preflight, bounded availability gate, TERM/KILL escalation only for the owned group, direct-child reap, group disappearance check, and neutral rebind of ports 54631–54634.
- Registered the ordinary and cancellation CTests with `phase12_real_socket_ports`, `RUN_SERIAL TRUE`, and target-name `TEST_LAUNCHER` wiring that preserves the ordinary GTest XML argument.

## Task Commits

1. **Task 1: RED/GREEN an invocation-owned POSIX launcher and cancellation reaper** - `ea4cbc32` (`test`)
2. **Task 2: Register the fail-closed launcher and serial ownership regression through CTest** - `4f144532` (`test`)

## Files Created/Modified

- `test/src/blockchain/multi_node_finality_fault_runner.cpp` - Test-only ownership boundary, cancellation/reap verification, and loopback reuse checks.
- `test/src/blockchain/CMakeLists.txt` - POSIX/CMake preflight, launcher-aware target-name registration, and shared CTest port lock.

## Verification

- Configure/build passed with CMake 3.31.4; configure-time primitive checks passed for `fork`, `setsid`, `getpgid`, `getsid`, `kill`, `waitpid`, `socket`, `bind`, and `sigwait`.
- `ctest -N -V` confirmed the ordinary CTest command is prefixed by `multi_node_finality_fault_runner` and preserves its XML argument.
- Standalone normal forwarding passed for `PublisherObserverProcessChild.WriterProbe` when executed with local TCP permission.
- Standalone controlled cancellation passed: `P12_PROCESS_OWNERSHIP=passed`, group exit/reap completed, and ports 54631–54634 rebound.
- Three fresh controlled-cancellation CTest invocations passed with actual exit status 0. Complete CTest logs are preserved at `/private/tmp/phase12-11-controlled-{1,2,3}.log` for this execution environment.
- One fresh normal serial `multi_node_finality_fault_test` invocation completed its teardown and released every fixed port, but exited nonzero after 215.53 seconds: 12 of 13 GTests passed.

## Decisions Made

- macOS does not expose `sigtimedwait`; the runner uses the portable blocked-signal pattern (`sigpending` polling plus `sigwait` only after detection), while configure-time and runtime checks still fail closed if signal support is unavailable.
- Local TCP permission is required for all real-socket verification. The restricted sandbox correctly fails runner preflight without launching an unwrapped test.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 3 - Blocking] Used macOS-supported signal waiting.**
   - **Found during:** Task 1 build.
   - **Issue:** The initial `sigtimedwait` preflight is not available in the supported macOS SDK.
   - **Fix:** Replaced it with the equivalent fail-closed `sigpending`/`sigwait` blocked-signal pattern and checked `sigwait` at configure time.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_runner.cpp`, `test/src/blockchain/CMakeLists.txt`.
   - **Verification:** Runner built; normal forwarding and controlled cancellation passed under local TCP permission.
   - **Committed in:** `ea4cbc32`, `4f144532`.

**Total deviations:** 1 auto-fixed (Rule 3: 1). No consensus, CRDT, PubSub, Mint/finality, collector, topology, timeout, retry, or user-data change was made.

## Issues Encountered

The required six-run gate is not satisfied and is not claimed closed. The first normal serial run failed only in the pre-existing `PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch` assertion. Its real child runs now pass with `complete_pass`, `normal-exit-0`, `boundary=none`, `state=ready`, and `error=none`, while the test still hard-codes the former sandbox-induced failure tuple. The PublisherLoss production scenario itself passed, and no listener remained after teardown.

That assertion lives in `multi_node_finality_fault_test.cpp`, which Plan 12-11 explicitly forbids changing. Normal runs 2 and 3 were not launched because a later pass cannot replace the recorded first failure, and the six-pass criterion is already impossible without a separately scoped expectation correction.

## User Setup Required

None. Real-socket test execution needs the local TCP bind permission already used for this plan.

## Next Phase Readiness

- The runner and CTest ownership boundary are ready for the six-run final proof.
- A separately scoped correction must first update the stale collector expectation to accept the real-socket pass/no-repair result; then rerun all three controlled-cancellation and all three normal serial invocations from a clean port state.

## Self-Check: PASSED

- Commits `ea4cbc32` and `4f144532` exist.
- Both owned source files and this summary exist.
- No protocol or collector source changed in these task commits.

---
*Phase: 12-multi-node-finality-fault-proof*
*Completed: 2026-08-31*
