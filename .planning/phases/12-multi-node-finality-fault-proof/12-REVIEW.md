---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-08-27T20:32:00Z
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

**Reviewed:** 2026-08-27T20:32:00Z
**Depth:** standard
**Files Reviewed:** 1
**Status:** issues_found

## Summary

The 12-08 delta only adds `ReadinessDiagnostics`; the existing topology helper, waits, selected-publisher barrier, and later publisher-loss scenario body are unchanged. The new calls are otherwise passive public reads. However, the observer can report a fabricated failed readiness predicate after the timed wait has recovered, and its records omit the required per-peer intended-connectivity facts. Those defects make the evidence gate unreliable.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: A recovered topology is reported as a nonexistent disconnected link

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:621`

**Issue:** `Classify()` always falls through to `disconnected-intended-peer` after the reachability loops. A five-second readiness wait can time out and then the network can finish connecting before the destructor samples it. In that case every current readiness check, including every reachability flag, is true, but no loop iteration returns and line 621 still emits `state=disconnected error=no-intended-peer-link`. The diagnostic therefore invents the first failed predicate and can taint the D-19 matching-failure/repair-authorization evidence.

**Fix:** Handle the all-reachable result explicitly and mark it as a recovered-after-deadline observation that cannot count as a first failed predicate. For example:

```cpp
if ( std::all_of( reachable.begin(), reachable.end(), []( bool connected ) { return connected; } ) )
    return Describe( peers.front(), "none", "ready", "recovered-after-deadline",
                     "all-intended-peers-connected" );
```

Update the evidence parser/summary policy so this state is excluded from D-19 matching rather than relabelled as a disconnection. If a true first-false predicate is mandatory, capture the same read-only facts at the predicate evaluation that times out; do not infer them after recovery.

### CR-02: The records do not snapshot each intended peer connection

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:511`

**Issue:** Successful records set `intended_connectedness=all-intended-peers-connected` without calling `Host::connectedness()` for any pair. Failure records at lines 617-619 contain only one reachable-to-unreachable edge, while missing/mesh failures leave the field at `not-evaluated`. D-18 requires a passive snapshot of every peer's host connectedness to its intended peers. The current output cannot distinguish, for example, an asymmetric link or a partially connected mesh, so it is insufficient to prove or rule out a fixture-owned lifecycle defect.

**Fix:** Add a read-only formatter that iterates every ordered source/target pair (excluding self), calls the existing public `connectedness()` query, and emits a stable per-pair matrix (including unavailable hosts). Use that formatter for both success and every failure diagnosis; retain the first-failure boundary separately.

---

_Reviewed: 2026-08-27T20:32:00Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
