---
phase: 12-multi-node-finality-fault-proof
plan: "09"
subsystem: testing
tags: [gtest, evidence-gate, pubsub, real-socket]
requires: [12-08]
provides: [process-bound observer records, attributed publisher readiness evidence]
affects: [phase-12-verification, publisher-loss-regression]
requirements-completed: []
status: blocked
---

# Phase 12 Plan 09: Observer Output Attribution Summary

P12_09_START_BASELINE=c14f816a642fcc8a7438742ce2bb2c2cc3343508

Process-bound start and explicit post-release terminal records attribute the
publisher-readiness failures to the focused real-socket test binary. All three
fresh traces are complete failures at the existing public mesh readiness
predicate, not observer output/ownership failures; no observer repair is
authorized and Phase 12 remains blocked.

## Task 1: Process-bound Observer Protocol

- RED: the pre-change focused run produced no `P12_PUBLISHER_OBSERVER_*` records.
- GREEN: `PublisherReadinessObserver` emits one synchronized, flushed start record
  and a single matching terminal only after the outer scenario epilogue releases
  every test-owned peer runtime handle.
- The observer uses the required run token, schema, PID, resolved executable path,
  executable size, and executable modification time. It owns no peer or shutdown
  object and emits nothing from a destructor.
- Commit: `575a977e` (`test(12-09): attribute publisher observer output`).

## Task 2: Fully Attributed Evidence Gate

`PublisherObserverRecordClassifier.DistinguishesCompletePassFailurePartialAndForeignEvidence`
passed after the test-local RED/GREEN addition. It distinguishes complete pass,
complete failure, invalid/partial, and directly proven foreign-process evidence
without opening sockets or changing the scenario.

The retained Plan 12-08 records remain evidence only. Their canonical
`publisher-run-{1,2,3}.log` records lack the Plan 12-09 process-bound start and
terminal fingerprint grammar, so they are retained as
`invalid_or_partial_blocked` with `count_weight=0`; no foreign process or binary
is inferred from a missing fingerprint.

P12_PUBLISHER_OBSERVER_CLASSIFICATION source=12-08-traces/publisher-run-1.log status=invalid_or_partial_blocked count_weight=0 reason=old-format-no-process-bound-fingerprint
P12_PUBLISHER_OBSERVER_CLASSIFICATION source=12-08-traces/publisher-run-2.log status=invalid_or_partial_blocked count_weight=0 reason=old-format-no-process-bound-fingerprint
P12_PUBLISHER_OBSERVER_CLASSIFICATION source=12-08-traces/publisher-run-3.log status=invalid_or_partial_blocked count_weight=0 reason=mixed-old-format-or-incomplete-record

| Run | Exit | Classification | Boundary | State | Error |
| --- | ---: | --- | --- | --- | --- |
| 1 | 1 | fully_attributed_complete_failure | zero-consensus-topic-mesh | zero | no-consensus-neighbor |
| 2 | 1 | fully_attributed_complete_failure | zero-consensus-topic-mesh | zero | no-consensus-neighbor |
| 3 | 1 | fully_attributed_complete_failure | zero-consensus-topic-mesh | zero | no-consensus-neighbor |

Every trace has exactly one matching start and complete terminal record, all four
peer handles released before terminal emission, and the normal one-test GTest
footer. The run tokens, PIDs, and executable fingerprints are retained in the
trace files.

The repeated triple proves the existing `ConnectAndWaitForPeers` public
consensus-topic mesh readiness failure before the publisher-loss fault begins.
It does not identify an observer ownership/output defect, so D-25 and D-28 do
not authorize a RED/GREEN observer repair. Topology, peer lifecycle, waits,
timeouts, finality, CRDT, PubSub, Mint, and production logging remain unchanged.

P12_PUBLISHER_OBSERVER_TERMINAL_REPORT branch=complete-failure-blocked gate_status=fully-attributed-complete-failures repair_authorization=none observer_attribution_disposition=closed-no-observer-repair PHASE_12_STATUS=BLOCKED initial_trace_set=observer-attribution-run-1,2,3

Phase 12 remains blocked on the separately proven late-contender recovery,
three-boundary restart, and full publisher-loss proof gaps recorded in
`12-VERIFICATION.md`. This attribution result does not claim TEST-05 complete.

## Verification Results

- Focused target rebuild: passed.
- Classifier RED: no matching test existed; GREEN classifier test passed.
- Focused observer contract run: emitted one matching start and one complete
  failure terminal; the pre-fault public readiness predicate failed.
- Three sequential real-socket evidence runs: complete, attributed failures as
  recorded above; no fixed-port tests overlapped.
- `git diff --check` for Plan 12 test-source changes: passed.

## Deviations from Plan

### Auto-fixed Issues

1. [Rule 3 - Blocking issue] Rebuilt the focused test PCH before compiling the changed test source.
   - **Issue:** The existing PCH rejected a third-party Boost header whose modification time had changed.
   - **Fix:** Rebuilt only the target-owned x86_64 and arm64 PCH artifacts, then linked the focused binary.
   - **Source impact:** None.

2. [Rule 3 - Blocking issue] Preserved pre-existing user-owned planning edits during the additions-only audit.
   - **Issue:** `.planning/STATE.md`, `.planning/config.json`, and `12-VERIFICATION.md` were already modified outside this plan's scope.
   - **Fix:** Left them untouched and limited the Plan 12 source/evidence audit to its declared files.
   - **Source impact:** None.

## Known Stubs

None.

## Threat Flags

None. The only new code is test-local evidence classification and synchronized
observer metadata output; it adds no production endpoint, authorization path,
file trust boundary, or schema change.

## Self-Check: PASSED

- Task 1 commit `575a977e` exists in Git history.
- The focused test source, summary, and all three initial attributed trace files exist.
- Every initial trace contains exactly one start, one post-release complete terminal,
  and the normal one-test GTest footer.
