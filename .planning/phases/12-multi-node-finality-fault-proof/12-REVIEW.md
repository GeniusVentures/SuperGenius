---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-08-31T11:47:31Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
findings:
  critical: 2
  warning: 1
  info: 0
  total: 3
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-08-31T11:47:31Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Reviewed the Phase 12 finality hooks, durability barriers, peer lifecycle helpers, and publisher-readiness observer. The production finality paths remain unchanged by the latest observer patch, but both claimed classifier repairs are only synthetic unit-test logic: no implementation parses or derives the required evidence fields from actual observer/GTest output. Consequently, neither the nonzero-exit guard nor the observer-lifecycle repair fence constrains a real evidence decision.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: Real observer failures cannot be gated on focused-GTest failure evidence

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1089-1116`
**Issue:** `gtest_completed` and `gtest_failed` exist only in a manually populated `PublisherObserverRecord`. The runtime observer emits only START/TERMINAL strings (lines 695-783), and no code parses a focused GTest footer/process result into this record or calls the classifier for an emitted run. The apparent CR-01 regression merely assigns `gtest_failed = true` by hand (line 1142). Thus a real nonzero exit remains subject to an out-of-band/manual classification with no code-enforced proof that the focused test failed.
**Fix:** Implement one test-owned record parser/evidence evaluator that consumes the START record, TERMINAL record, focused GTest footer, and process exit. Derive `gtest_completed`/`gtest_failed` solely there, reject any nonzero exit without a matching focused-test failure, and make the regression tests feed parsed fixture output rather than constructing the fields directly.

### CR-02: The repair-authorization fence is never applied to emitted evidence

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1119-1131`
**Issue:** `observer_lifecycle_eligible` and the boundary/state/error triple are likewise only synthetic fields. `IsObserverRepairAuthorized` has no caller outside its own unit test, and the emitted terminal format (lines 723-749) does not carry an eligibility decision. The test proves a pure helper can compare two hand-authored records, but it cannot prevent an actual observer-output failure from being used to authorize a repair without the required two fully attributed, eligible matching records.
**Fix:** Have the same evaluator derive eligibility from the parsed terminal classification (with an explicit, fail-closed observer-lifecycle boundary allowlist) and make repair authorization accept only evaluator-produced records. Add fixtures for actual serialized records covering matching, mismatching, non-observer, malformed, and foreign-process cases.

## Warnings

### WR-01: The claimed CR-02 regression was not present in the focused executable

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1165`
**Issue:** The source defines `AuthorizesRepairOnlyForMatchingEligibleObserverLifecycleFailures`, but the available focused binary predates the source (binary mtime `1788175110`; source mtime `1788176015`) and `--gtest_list_tests` contains only the earlier classifier test. Running `--gtest_filter=PublisherObserverRecordClassifier.*` therefore exercised one test, not the new repair-gate regression. This leaves the repair claim unverified and makes the test result misleading.
**Fix:** Rebuild `multi_node_finality_fault_test`, assert that `--gtest_list_tests` includes both classifier tests, then run the focused filter before treating either classifier fix as verified.

---

_Reviewed: 2026-08-31T11:47:31Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
