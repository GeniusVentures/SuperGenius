---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-08-31T14:38:18Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
findings:
  critical: 2
  warning: 0
  info: 0
  total: 2
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-08-31T14:38:18Z
**Depth:** standard
**Files Reviewed:** 8
**Status:** issues_found

## Summary

The finality fault barriers are friend-scoped and the reviewed production paths do not introduce an apparent consensus, topology, or protocol behavior change when the test seams are inactive. However, the claimed process-bound observer evidence gate is not connected to any real process output, and its sole eligible lifecycle triples cannot be emitted by the observer. The evidence/repair decision therefore remains unenforced.

## Critical Issues

### CR-01: Real observer output is never evaluated or used to authorize a repair

**File:** `/Users/henriqueklein/gnus/SuperGenius/test/src/blockchain/multi_node_finality_fault_test.cpp:1143`
**Issue:** `PublisherObserverEvidenceEvaluator::Evaluate` accepts an already-split vector of strings and is called only by the two classifier unit tests (lines 1294-1307 and 1317-1342). `IsObserverRepairAuthorized` (line 1271) likewise has no production/evidence-run caller outside its own unit test. The observer merely writes records to `std::cerr` (lines 696-750), while the CTest target is registered as a normal executable with no wrapper, capture, parsing, evaluator, or authorization step (CMake lines 59-68). Consequently, actual serialized START/TERMINAL/GTest output can never be attributed or passed through the exact two-run repair gate; the test only demonstrates behavior on synthetic strings.

**Fix:** Add a test-owned process runner/evidence collector that launches the focused GTest, captures combined stdout/stderr, splits the captured bytes into lines, derives a normalized process completion result, constructs `ExpectedProcess` from that launched child, and calls `Evaluate`. Feed only those returned `Record` values into a persisted two-run `IsObserverRepairAuthorized` decision. Treat malformed/missing output, a missing GTest completion footer, and abnormal child termination as fail-closed, zero-weight evidence.

### CR-02: The only repair-eligible lifecycle triples are impossible for the observer to produce

**File:** `/Users/henriqueklein/gnus/SuperGenius/test/src/blockchain/multi_node_finality_fault_test.cpp:779`
**Issue:** The allowlist accepts only `boundary=observer-output`, `state=flush`, and `error=write-failed|stream-closed` (lines 1263-1268). But `PublisherReadinessObserver::Write` unconditionally writes to `std::cerr` and discards the stream result (lines 779-784); no observer path emits either allowed error or a `flush` state. Its real failure path instead reports a readiness snapshot such as `zero-consensus-topic-mesh` (lines 709-715 and 608-665). Thus eligible failures exist only as hand-authored strings in lines 1289-1292 and 1321-1340, and no actual observer-output/lifecycle failure can satisfy the gate.

**Fix:** Make the observer output path report a structured write/flush result to the evidence collector, and derive the lifecycle triple from that result rather than from fixture literals. If the terminal record itself cannot be flushed, mark the run incomplete/invalid; only a separately captured, attributable observer-output failure should be eligible for the explicit allowlist. Add integration fixtures using captured serialized output for both allowed triples and for failed/partial writes.

---

_Reviewed: 2026-08-31T14:38:18Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
