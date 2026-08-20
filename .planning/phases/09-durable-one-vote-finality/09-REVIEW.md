---
phase: 09-durable-one-vote-finality
reviewed: 2026-08-20T00:00:00Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/blockchain/impl/proto/Consensus.proto
  - src/blockchain/Consensus.hpp
  - src/blockchain/Consensus.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
findings:
  critical: 0
  warning: 0
  info: 0
  total: 0
status: clean
---

# Phase 9: Code Review Report

**Reviewed:** 2026-08-20T00:00:00Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** clean

## Summary

Final re-review of `5b85d0ae` is clean. Each scan-buffered contender carries its monotonic admission time, successful scan recovery merges only contenders admitted before the unchanged window deadline, and recovery after close clears the buffer without changing the frozen candidate set. The focused lifecycle and slot-key binaries pass (17/17 and 6/6), including the new pre-deadline winner, post-deadline exclusion, and exactly-one-vote regressions.

## Resolved Critical Issues

### CR-01: Accepted certificates complete without a local active vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:3499`

`ReleaseActiveVoteForAcceptedSlot()` returning `false` for `NOT_FOUND` is now a safe no-op: only actual release errors retain stalled work, while the durably read-back and validated certificate proceeds through slot cleanup and `ProcessCommittedCertificate()`. The focused regression asserts the other-slot certificate's journal work is removed without altering the original active record.

### CR-02: Finalized-slot scan errors no longer authorize a new vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:1063`

The finalized-slot scan now returns `outcome::result<bool>` and propagates enumeration, key-conversion, malformed-entry, and temporarily unverifiable-certificate failures. Candidate admission, recovery, due work, and exact-vote replay retain fail-closed state on errors. The focused regression injects a scan failure after durable same-slot finality and proves no announcement or replacement record is created.

### CR-03: Scan-retry drops a valid contender from an already-open window — resolved

**Verified in:** `src/blockchain/Consensus.cpp:616`, `src/blockchain/Consensus.cpp:1351`

`ScanPendingCandidate` now retains its admission time. Recovery merges it into the eligible set only while the original deadline remains in the future; it clears the buffer after that deadline without extending the window or adding late contenders. Winner selection still uses the existing generic ranking, and a successful freeze persists and announces exactly one vote. Focused regressions cover both pre-deadline recovery selecting the lower-ranked contender and post-deadline recovery preserving the original winner.

---

_Reviewed: 2026-08-20T00:00:00Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
