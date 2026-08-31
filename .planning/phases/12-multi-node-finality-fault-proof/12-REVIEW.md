---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-08-31T11:25:58Z
depth: standard
files_reviewed: 1
files_reviewed_list:
  - test/src/blockchain/multi_node_finality_fault_test.cpp
findings:
  critical: 2
  warning: 0
  info: 0
  total: 2
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-08-31T11:25:58Z
**Depth:** standard
**Files Reviewed:** 1
**Status:** issues_found

## Summary

The Plan 12-09 delta is confined to the test-only observer, its explicit cleanup epilogue, and a local classifier; it does not directly mutate the finality, CRDT, PubSub, Mint, or normal peer protocol paths. However, the classifier does not enforce the evidence conditions needed to keep abnormal process exits and non-observer failure classes out of repair authorization.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: BLOCKER — Any nonzero exit can be promoted to a completed GTest failure

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1105`
**Issue:** `gtest_completed` only proves that GTest reached its footer. The classifier calls a record a `fully_attributed_complete_failure` whenever `outcome == "failure"` and `process_exit != 0`, without requiring that the focused GTest itself reported the normal one-test failure result. A teardown/runner/binary failure after a completed GTest footer can therefore enter the failure count as evidence. That violates D-23 and can provide false input to the two-failure repair gate.
**Fix:** Capture and require an explicit focused-GTest result, not merely completion. For example, add `bool gtest_failed` (derived from the one-test footer) and require it for the failure branch:

```cpp
if (record.outcome == "failure" && record.process_exit != 0 && record.gtest_failed)
    return "fully_attributed_complete_failure";
```

Add negative tests for a nonzero exit after a passed/unknown GTest result.

### CR-02: BLOCKER — The classifier cannot fence repair authorization to an observer-lifecycle failure triple

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1089`
**Issue:** `PublisherObserverRecord` does not retain `boundary`, `state`, or `error`, nor whether that boundary is an observer-lifecycle defect. Consequently `ClassifyPublisherObserverRecord()` can only label a record as a generic completed failure; it cannot enforce D-25's requirement that two records match exactly on the observer-lifecycle boundary/state/error and that the class justifies an observer-only repair. In particular, the observed `zero-consensus-topic-mesh` pre-fault topology failure is indistinguishable from an observer ownership/output failure to this classifier.
**Fix:** Include normalized `boundary`, `state`, and `error` plus an explicit observer-lifecycle eligibility classification in the record, and implement a separate aggregate gate that requires two eligible records with an identical triple before returning repair-authorized. Test matching, non-matching, and non-observer triples.

---

_Reviewed: 2026-08-31T11:25:58Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
