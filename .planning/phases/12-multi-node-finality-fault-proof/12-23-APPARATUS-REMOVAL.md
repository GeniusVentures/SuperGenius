# 12-23 Apparatus Removal — Developer-Directed Cleanup Record

**Date:** 2026-09-03
**Type:** Directed scope change (not a GSD plan; no plan checkbox consumed)
**Build:** commit `6fa285fe` on `gsd/v3.0-canonical-burn-finality-rebuild`
**Files changed:** `test/src/blockchain/multi_node_finality_fault_test.cpp` (-796 net lines), `test/src/blockchain/multi_node_finality_fault_runner.cpp` (filter repoint, +3/-1)

## Developer Directive (verbatim)

> "I don't want failing tests and apparatus that serves nothing but to follow some rule of thumb regarding approval of a test. This seems like huge overengineering."

Decision, 2026-09-03: the publisher-observer/collector meta-tests are removed as a directed scope decision; the six FinalityFaultNetwork scenario tests remain as the phase's proof. This resolves the (a)/(b)/(c) decision routed by 12-22-SUMMARY as **directed removal — stronger than (b)**: rather than re-examining whether the collector meta-test belongs in the gated pass condition, the meta-test and its supporting apparatus are deleted outright.

This is NOT a silent reduction. The suite composition change is recorded explicitly below.

## Suite Composition Change: 13 → 6 gtest cases

| Before (13 cases) | After (6 cases) |
|---|---|
| PublisherObserverRecordClassifier.DistinguishesCompletePassFailurePartialAndForeignEvidence | (deleted) |
| PublisherObserverRecordClassifier.ParsesEligibleTriplesButCannotAuthorizeThem | (deleted) |
| PublisherObserverProcessChild.WriterProbe | (deleted) |
| PublisherObserverProcessEvidenceCollector.CapturesFocusedChildOutputAndRejectsUnattributedEvidence | (deleted) |
| PublisherObserverProcessEvidenceCollector.WriterProbeEvidenceNeverQualifies | (deleted) |
| PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch | (deleted) |
| PublisherObserverProcessEvidenceCollector.CollectorOnlyDecisionPersistsNoRepairForReadinessFailure | (deleted) |
| FinalityFaultNetwork.ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress | KEPT |
| FinalityFaultNetwork.SameBurnContentionUsesOneCanonicalSlotAndExactMint | KEPT |
| FinalityFaultNetwork.LateContenderAndPassiveRecipientRemainReceiveOnly | KEPT |
| FinalityFaultNetwork.ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary | KEPT |
| FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce | KEPT |
| FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover | KEPT |

Rationale: the 12-22 gate series struck on run B at exactly `PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch` — a meta-test whose subject was the evidence-collection tooling itself (fork/pipe launch of child processes, control-frame pipes, writer-mode fault injection), not the finality behavior the phase's truths require. Its ~18%/run intermittence (12-15 Verdict 3) was a property of the observation apparatus, not of the six scenarios. The developer directed removal of the apparatus rather than further repair of it.

## What Was Deleted (test/src/blockchain/multi_node_finality_fault_test.cpp, 2724 → 1928 lines)

- The seven meta-tests listed above, the empty `PublisherObserverProcessChild` fixture, and the `static_assert` on `IsObserverRepairAuthorized` (~135 lines, formerly :1776-:1909).
- The entire second anonymous namespace (formerly :1278-:1774, ~497 lines):
  - `class PublisherObserverEvidenceEvaluator` — parsed/validated observer log lines, classified pass/failure/partial/foreign evidence.
  - `class PublisherObserverProcessEvidenceCollector` — fork/execve launch of focused child gtest runs over non-blocking pipes with poll-loop draining, control-frame validation (Get16/32/64, ValidateControl), footer counting (`HasOneGTestFooter`, `SplitLines`), and run-token minting (`NewRunToken`, std::random_device).
  - `static bool IsObserverRepairAuthorized` and `class PublisherObserverEvidenceDecision` — the two-run matching gate that always answered "none"/no-repair.
- `struct P12ObserverControlFrame` (formerly :702-:784, ~83 lines) — the 66-byte pipe frame codec (D-29) emitted only when `P12_OBSERVER_CONTROL_FD`/`P12_10_RUN_TOKEN` were set, i.e. only inside collector-launched children. Unreferenced after the collector deletion.
- Inside `PublisherReadinessObserver`: `EmitProcessWriterProbe()` and `FingerprintFromEnvironment()` (probe-only statics), and the `P12_10_WRITER_MODE` fault-injection branches plus the control-frame emission inside `Write()` — `Write` is now a plain mutex-synchronized stderr write.
- Dead includes and the `extern char **environ` declaration: `<cerrno>`, `<cstring>`, `<fcntl.h>`, `<iomanip>`, `<map>`, `<poll.h>`, `<random>`, `<type_traits>`, `<sys/wait.h>`.

## What Was Kept and Why

- **`PublisherReadinessObserver`** (class + `EmitStart`/`MarkReady`/`MarkFailure`/`MarkUnclassifiedExit`/`EmitTerminal`/`EmitIncomplete`/`Write`/`Header`): used directly by the surviving `PublisherLossAfterPersistenceUsesDeterministicFailover` scenario (`PublisherReadinessObserver observer(network)` → `EmitStart` → `MarkReady`/`MarkFailure` → `MarkUnclassifiedExit` → `EmitTerminal(all_released)`), where `all_released` is the scenario's own peer-handle release assertion. Emission is gated on a run-token env var, so plain ctest runs print nothing; when a token is present the `P12_PUBLISHER_OBSERVER_*` lines may still appear and are harmless — nothing parses them after this change.
- **`PublisherReadinessSnapshot`** and its full readiness classifier (peer identity, listener, root lifecycle, consensus mesh, intended connectedness, reachability): the diagnosis source behind `MarkFailure()` in the surviving scenario.
- **`MintRecoveryDiagnostics`**: used by `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` (P12_MINT_MARKER_DIAG emission under `P12_07_RUN`).
- All six `FinalityFaultNetwork` scenario tests and the fixture (`MultiNodeFinalityFaultTestAccess`, `Peer` with the D-29/WR-01 teardown ordering, barriers, helpers) — untouched.
- The `multi_node_finality_fault_test` binary target and its CTest registration in `test/src/blockchain/CMakeLists.txt` — untouched.

## Collateral Fix (blocking-issue, applied inline)

`multi_node_finality_fault_runner.cpp` (the process-ownership launcher, also the TEST_LAUNCHER of the gate CTest) launched its controlled-cancellation child with `--gtest_filter=PublisherObserverProcessEvidenceCollector.RealSocketPublisherLossOnlyQualifiesWhenTwoRunsMatch`. After the deletion that filter would select zero tests, the child would bind none of ports 54631-54634, and `multi_node_finality_fault_process_ownership_test` would fail its connect gate. The filter now points at `FinalityFaultNetwork.PublisherLossAfterPersistenceUsesDeterministicFailover` — the same real-socket scenario (same ports) the retired meta-test used as its own child. Verified green: process_ownership Passed 0.21s (its only exit-0 path requires gate-connect, group-signal, cleanup, and port-rebind to all succeed).

## Gate Evidence — three consecutive serial full passes on the reduced suite

Command: `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` (RUN_SERIAL + RESOURCE_LOCK phase12_real_socket_ports via CTest properties; TEST_LAUNCHER = the runner).

| Run | Result | Wall | Pre-run 1-min load | XML assertions |
|---|---|---|---|---|
| A | 100% tests passed, 0 failed | 132.65 s | 2.45 | tests="6" failures="0", 6 testcase entries, 0 `<failure>` |
| B | 100% tests passed, 0 failed | 134.77 s | 19.90 | tests="6" failures="0", 6 testcase entries, 0 `<failure>` |
| C | 100% tests passed, 0 failed | 136.88 s | 27.72 | tests="6" failures="0", 6 testcase entries, 0 `<failure>` |

Honest load disclosure: runs B and C executed inside/above the historical 15-20 contamination regime (12-12). The procedure required recording the actual pre-run load in each header, not withholding runs under it; the recorded loads are in `gate-{a,b,c}.log`. The six scenarios passed with zero failures and zero crashes even at 19.9 and 27.7 on 8 cores — strictly stronger evidence than a quiet-regime-only series. Per-run XML copies captured immediately after each run (distinct timestamps 15:29/15:32/15:34 local).

Artifacts (all committed, never /tmp): `round6-traces/gate-{a,b,c}.log`, `round6-traces/gate-{a,b,c}.xml`, `round6-traces/crash-check.txt`, `round6-traces/sibling-matrix.log`.

## Crash Check

Marker file created before run A; `find ~/Library/Logs/DiagnosticReports -name "*.ips" -newer <marker>` at completion: **0 new crash reports** across the three gate runs and the sibling matrix. (Newest multi_node .ips remains 2026-09-02-125541 from before the WR-01 fix era.)

## Sibling Matrix (serial, one run each, same build generation)

| Target | Result | Wall |
|---|---|---|
| multi_node_finality_fault_compatibility_smoke_test | Passed | 5.66 s |
| consensus_pending_lifecycle_test | Passed | 41.55 s |
| transaction_manager_certificate_fallback_test | Passed | 30.93 s |
| multi_node_finality_fault_process_ownership_test (extra: runner changed this round) | Passed | 0.21 s |

## Build Verification

- Clean rebuild of all four phase targets (`multi_node_finality_fault_test`, `multi_node_finality_fault_compatibility_smoke_test`, `consensus_pending_lifecycle_test`, `transaction_manager_certificate_fallback_test`) plus the runner: zero warnings, zero errors (existing warning level).
- `git diff` for the deletion commit touches only the two test files — **no `src/` changes**.
- `--gtest_list_tests` on the rebuilt binary lists exactly the six FinalityFaultNetwork cases.

## Commits

1. `6fa285fe` — refactor(12): remove publisher-observer meta-test apparatus per developer directive
2. (evidence commit) — docs(12-23): round-6 gate evidence and apparatus-removal record
