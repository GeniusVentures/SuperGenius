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
  warning: 1
  info: 0
  total: 1
status: issues_found
---

# Phase 9: Code Review Report

**Reviewed:** 2026-08-20T00:00:00Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

Re-review of `2130ba80` and `25ea07ab` confirms both critical findings are resolved: accepted certificates now complete when this validator has no local vote record, and certificate-scan failures are propagated as indeterminate so candidate admission and active-vote replay fail closed. The focused lifecycle and slot-key binaries pass (14/14 and 6/6). One liveness warning remains for a transient scan failure before a candidate window starts.

## Resolved Critical Issues

### CR-01: Accepted certificates complete without a local active vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:3499`

`ReleaseActiveVoteForAcceptedSlot()` returning `false` for `NOT_FOUND` is now a safe no-op: only actual release errors retain stalled work, while the durably read-back and validated certificate proceeds through slot cleanup and `ProcessCommittedCertificate()`. The focused regression asserts the other-slot certificate's journal work is removed without altering the original active record.

### CR-02: Finalized-slot scan errors no longer authorize a new vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:1063`

The finalized-slot scan now returns `outcome::result<bool>` and propagates enumeration, key-conversion, malformed-entry, and temporarily unverifiable-certificate failures. Candidate admission, recovery, due work, and exact-vote replay retain fail-closed state on errors. The focused regression injects a scan failure after durable same-slot finality and proves no announcement or replacement record is created.

## Warnings

### WR-01: A transient certificate-scan failure permanently drops the only candidate

**File:** `src/blockchain/Consensus.cpp:616`
**Issue:** When `ContinueProposalAfterSubject()` gets an indeterminate scan, it sets `certificate_scan_pending` and returns before placing the already-validated proposal in `eligible_candidates` or setting `candidate_deadline` (lines 616-621). `ProcessDueVoteWork()` skips slot states whose deadline is default before attempting another scan (lines 1323-1328). Therefore, if the same proposal is not delivered again after a brief database/registry failure, that slot never rechecks the scan and never opens its contention window even after the dependency recovers. This is fail-closed but unnecessarily loses consensus progress for the slot.

**Fix:** Retain the validated proposal as pending scan work (or initialize a retry deadline) and make the timer re-run `HasAcceptedCertificateForSlot()` until it succeeds. On a clear scan, start the two-second window using the recovery time and admit the retained proposal. Add a deterministic test that clears the injected failure without redelivering the proposal and verifies one normal durable vote is eventually produced.

---

_Reviewed: 2026-08-20T00:00:00Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
