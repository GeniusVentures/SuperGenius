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
  critical: 1
  warning: 0
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

Re-review of `007b1e80` confirms the initial scan-failure retry is retained and the focused lifecycle and slot-key binaries pass (15/15 and 6/6). However, a validated contender received during an already-open contention window is still dropped when that slot's finality scan is temporarily indeterminate. Since it can be the deterministic winner, this is a consensus-correctness blocker.

## Resolved Critical Issues

### CR-01: Accepted certificates complete without a local active vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:3499`

`ReleaseActiveVoteForAcceptedSlot()` returning `false` for `NOT_FOUND` is now a safe no-op: only actual release errors retain stalled work, while the durably read-back and validated certificate proceeds through slot cleanup and `ProcessCommittedCertificate()`. The focused regression asserts the other-slot certificate's journal work is removed without altering the original active record.

### CR-02: Finalized-slot scan errors no longer authorize a new vote — resolved

**Verified in:** `src/blockchain/Consensus.cpp:1063`

The finalized-slot scan now returns `outcome::result<bool>` and propagates enumeration, key-conversion, malformed-entry, and temporarily unverifiable-certificate failures. Candidate admission, recovery, due work, and exact-vote replay retain fail-closed state on errors. The focused regression injects a scan failure after durable same-slot finality and proves no announcement or replacement record is created.

## Critical Issues

### CR-03: Scan-retry drops a valid contender from an already-open window

**File:** `src/blockchain/Consensus.cpp:1345`
**Issue:** If proposal A opens a slot window, then proposal B is fully validated before that window's deadline while `HasAcceptedCertificateForSlot()` fails, `ContinueProposalAfterSubject()` stores B in `scan_pending_candidates` (lines 616-628). When the timer's scan succeeds, it merges those candidates only if `candidate_deadline` is unset (lines 1359-1375). The existing window already has a deadline, so B stays stranded in `scan_pending_candidates` and is never eligible for selection. The later freeze selects only A (lines 1395-1407), despite B having been validated before the deadline and possibly ranking lower. Different validators can consequently vote for different winners in the same slot.

**Fix:** On a successful retry, merge pending scan candidates that were received before the established deadline into `eligible_candidates` regardless of whether the window began before or after the scan outage; discard candidates captured after that deadline. Record the candidate admission time with the pending entry (or reject/avoid retaining candidates once `steady_clock::now() >= candidate_deadline`) so a late contender cannot be backfilled after close. Add a regression with an already-open window, a lower-ranked second candidate arriving during a scan failure before close, scan recovery, and an assertion that it becomes the sole persisted/published winner. Also assert a candidate arriving after close cannot be added by the recovery path.

---

_Reviewed: 2026-08-20T00:00:00Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
