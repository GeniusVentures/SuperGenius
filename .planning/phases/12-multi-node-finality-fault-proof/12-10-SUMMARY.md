---
phase: 12-multi-node-finality-fault-proof
plan: "10"
subsystem: testing
tags: [gtest, process-collection, observer-evidence, pipe-protocol]
requires:
  - phase: 12-09
    provides: process-bound publisher observer records
provides:
  - parent-captured focused-child observer evidence
  - opaque collector-only repair authorization boundary
  - no-repair two-run observer decision
affects: [phase-12-verification, publisher-loss-regression]
tech-stack:
  added: []
  patterns: [test-owned fork-exec collector, fixed observer control frame]
key-files:
  created: [.planning/phases/12-multi-node-finality-fault-proof/12-10-SUMMARY.md]
  modified: [test/src/blockchain/multi_node_finality_fault_test.cpp]
key-decisions:
  - "Only collector-minted evidence can reach observer repair authorization."
  - "The two real-socket publisher-loss runs remain a no-repair readiness result."
requirements-completed: [TEST-05, TEST-06]
duration: 42min
completed: 2026-08-31
---

# Phase 12 Plan 10: Captured Child Evidence Summary

**Focused GTest child collection now binds observer records to parent-derived process identity, a fixed terminal control frame, and normal child completion while preserving the blocked no-repair result.**

P12_10_SCOPE_MANIFEST=/private/tmp/p12-10-scope-vbgteZ
P12_10_SCOPE_MANIFEST_SHA256=4bee6e2ce13d685941ee8f5baeaaeefae7412f33a7a49f56dbf8a556d9fabcff
P12_10_SCOPE_AUDIT baseline_index_mode_oid=passed baseline_worktree_mode_type_content=passed baseline_untracked_mode_type_content=passed post_baseline_paths=passed scan=passed
P12_10_SCOPE_CHANGED_PATH_SHA256 path=test/src/blockchain/multi_node_finality_fault_test.cpp sha256=b121f873612fc53a8beb7901a7ab2c679cf3855ccf3633d717f235139969bcde

## Performance

- **Completed:** 2026-08-31T16:00:39Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Added a test-owned fork/exec collector that combines child stdout/stderr, drains both nonblocking pipes to EOF, normalizes `waitpid`, and derives the expected executable fingerprint from the launched PID and statted executable.
- Added the exact 66-byte, big-endian `P12_OBSERVER_CONTROL_V1` terminal frame. Missing, malformed, foreign, partial, duplicate, token-mismatched, PID-mismatched, or unrecorded terminal results remain zero-weight invalid evidence.
- Restricted repair authorization to `CollectedEvidence`, whose constructor is private to the collector. Plain parsed records, strings, and lifecycle triples cannot cross that boundary.
- Exercised real writer `write-failed`, `stream-closed`, and terminal-stream-failure probe modes. Probe origin is permanently non-qualifying; only two separately launched real-socket publisher-loss children can enter the D-25 decision.

## Task Commits

1. **Task 1: RED/GREEN captured-child evidence contract**
   - `43e1d95d` `test(12-10): add failing captured child evidence contract`
   - `48af88a3` `feat(12-10): collect focused child observer evidence`
2. **Task 2: RED/GREEN opaque no-repair decision**
   - `f84d337a` `test(12-10): add failing opaque evidence decision`
   - `08c6e52a` `feat(12-10): persist opaque no-repair decision`
   - `4b3322ea` `fix(12-10): retain real child gtest footer`

## Captured Evidence

P12_10_EVIDENCE_REPORT origin=child-writer-probe filter=PublisherObserverProcessChild.WriterProbe classification=invalid_or_partial_blocked count_weight=0 child_status=normal-exit-0 footer=one-normal-footer control=validated-terminal-recorded repair_authorization=none PHASE_12_STATUS=BLOCKED
P12_10_EVIDENCE_REPORT origin=child-writer-probe filter=PublisherObserverProcessChild.WriterProbe classification=invalid_or_partial_blocked count_weight=0 child_status=normal-exit-0 footer=one-normal-footer control=validated-terminal-unrecorded repair_authorization=none PHASE_12_STATUS=BLOCKED
P12_10_REAL_SOCKET_EVALUATION origin=real-socket-publisher-loss filter=FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover classification=fully_attributed_complete_failure count_weight=1 child_status=normal-exit-nonzero footer=one-normal-footer control=validated-terminal-recorded boundary=zero-consensus-topic-mesh state=zero error=no-consensus-neighbor repair_authorization=none PHASE_12_STATUS=BLOCKED
P12_10_EVIDENCE_REPORT origin=real-socket-publisher-loss pair=two-distinct-parent-token-pid classification=fully_attributed_complete_failure count_weight=1 repair_authorization=none PHASE_12_STATUS=BLOCKED

The real-socket pair reached the existing readiness/topology boundary, not the allowlisted `observer-output/flush/(write-failed|stream-closed)` lifecycle triple. It therefore remains `repair_authorization=none`. CR-01 and CR-02 are closed as evidence-collector enforcement only. Any observer-output repair is separately scoped future work; Phase 12 remains blocked on the distinct late-contender, restart, and publisher-loss proof gaps in `12-VERIFICATION.md`.

## Verification Results

- `cmake --build build/OSX/Release --target multi_node_finality_fault_test --parallel 4`: passed.
- `multi_node_finality_fault_test --gtest_filter='PublisherObserverProcessEvidenceCollector.*:PublisherObserverProcessChild.*' --gtest_brief=1`: passed (five focused collector/child tests).
- `multi_node_finality_fault_test --gtest_filter='PublisherObserverProcessEvidenceCollector.*' --gtest_brief=1`: passed, including the two-run collector-only no-repair decision.
- Opaque type and control-frame static checks: passed.
- Scope-manifest replay, post-baseline additions-only scan, and permitted source whitespace check: passed.

## Decisions Made

- A successful terminal frame requires a matching serialized complete terminal; a terminal write or flush failure is explicitly terminal-unrecorded and cannot be promoted to eligible evidence.
- The collector reads the inherited environment except for its three parent-issued controls, which it replaces exactly to prevent duplicate environment keys from changing the child’s observed identity.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 3 - Blocking] Corrected the temporary baseline manifest encoder.**
   - **Found during:** Task 1 setup
   - **Issue:** The first temporary manifest attempt used an invalid Node buffer encoding name.
   - **Fix:** Recreated the unique private manifest with byte-wise NUL splitting before any repository edit.
   - **Files modified:** None in the repository.

2. **[Rule 1 - Bug] Compared control-token bytes as unsigned bytes.**
   - **Found during:** Task 1 writer-probe verification
   - **Issue:** A signed `char` comparison rejected valid raw token bytes above `0x7f`.
   - **Fix:** Validated each decoded token byte as `uint8_t` against the captured frame.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
   - **Verification:** All focused collector and child-probe tests passed.

3. **[Rule 1 - Bug] Preserved the child’s normal GTest completion footer.**
   - **Found during:** Task 2 real-socket attribution verification
   - **Issue:** The focused child’s terse GTest mode omitted the failure footer, so valid frame evidence correctly remained zero-weight invalid instead of proving the real readiness result.
   - **Fix:** Removed only the child’s terse-output flag and added assertions for the parent-observed real-socket PID, footer, frame, classification, and readiness triple.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
   - **Verification:** Five focused collector/child tests passed, including two independently launched real-socket publisher-loss children.
   - **Committed in:** `4b3322ea`

**Total deviations:** 3 auto-fixed (Rule 1: 2, Rule 3: 1). No production, topology, finality, timeout, retry, or user-owned planning change was introduced.

## Known Stubs

None. `terminal=incomplete` and zero-weight invalid results are intentional fail-closed evidence states, not placeholders.

## Threat Flags

None. The addition is confined to test-process collection and observer output; it adds no production endpoint, authentication path, file trust boundary, or schema change.

## Self-Check: PASSED

- Task commits `43e1d95d`, `48af88a3`, `f84d337a`, `08c6e52a`, and `4b3322ea` exist.
- The test source and this summary exist.
- The private scope manifest, NUL path sets, byte copies, and digest exist at the recorded location.
