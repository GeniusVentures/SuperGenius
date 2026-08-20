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
  critical: 2
  warning: 0
  info: 0
  total: 2
status: issues_found
---

# Phase 9: Code Review Report

**Reviewed:** 2026-08-20T00:00:00Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

The active-vote record has the intended local-only shape and persistence-before-publication ordering. However, the certificate recovery gate currently makes certificate processing contingent on this node possessing—and successfully deleting—an active vote record. It also treats an unavailable finalized-slot scan as proof that a slot is not finalized. Both behaviors violate the phase's finality/restart safety boundary.

## Critical Issues

### CR-01: Accepted certificates never complete on nodes without a local active vote

**File:** `src/blockchain/Consensus.cpp:3439`
**Issue:** `RecoverPendingCertificateWork()` validates a durably read-back certificate, then calls `ReleaseActiveVoteForAcceptedSlot()`. `NOT_FOUND` deliberately returns `false` at lines 1101-1107, but the caller treats both that normal no-local-vote case and an unsafe race as a reason to keep the journal entry stalled (lines 3440-3445). As a result, any validator that did not cast the local vote never reaches `ClearProposalSlot()` or `ProcessCommittedCertificate()`: certificate handlers, registry finalization, and dependency wakeups never run. Replayed/duplicate certificate work is permanently stalled for the same reason after the first successful deletion. The new regression at `consensus_pending_lifecycle_test.cpp:1360-1363` asserts this incorrect behavior for an accepted certificate in another slot.

**Fix:** Distinguish "no local active-vote record" from a read/decode/remove failure. Once the legacy value has been read back and approved, a missing active record is a safe no-op for local release; clear the corresponding volatile slot if present and call `ProcessCommittedCertificate()`. Continue to stall only if an existing record cannot be decoded or synchronously removed. Update the other-slot test to expect the accepted certificate's work to complete while proving it does not alter the original slot's record.

### CR-02: Finalized-slot fence fails open when certificate enumeration fails

**File:** `src/blockchain/Consensus.cpp:1055`
**Issue:** `HasAcceptedCertificateForSlot()` returns `false` both when no certificate matches and when `QueryKeyValues()` fails (lines 1061-1065); it also silently skips key-conversion failures (lines 1068-1071). Its callers consequently admit/freeze candidates and create a new active vote at `ContinueProposalAfterSubject()` lines 616-622 and `ProcessDueVoteWork()` lines 1294-1300. After a correctly finalized slot has had its local active record deleted, a restart or transient certificate-store read failure therefore becomes authorization to vote again in that finalized slot. This is particularly dangerous because the Phase 9 legacy scan is the only restart fence until slot-keyed certificate authority exists.

**Fix:** Make the scan tri-state (for example, `outcome::result<bool>`), propagating datastore/key-conversion/validation-unavailability as an indeterminate result. Candidate admission and vote recovery must fail closed—leave the slot locked or pending and do not create/publish a vote—until a complete scan proves no accepted certificate exists. Add a deterministic test that injects a scan failure after durable same-slot finality and verifies no replacement record/vote is created.

---

_Reviewed: 2026-08-20T00:00:00Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
