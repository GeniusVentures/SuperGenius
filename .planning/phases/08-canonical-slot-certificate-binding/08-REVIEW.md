---
phase: 08-canonical-slot-certificate-binding
reviewed: 2026-08-20T15:48:56Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/blockchain/Consensus.hpp
  - src/blockchain/Consensus.cpp
  - test/src/blockchain/consensus_slot_key_test.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
findings:
  critical: 1
  warning: 1
  info: 0
  total: 2
status: issues_found
---

# Phase 08: Code Review Report

**Reviewed:** 2026-08-20T15:48:56Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

The Mint-slot tests preserve the existing slot formula and the CRDT key check remains scoped to key-aware ingress. However, certificate validation can stop at an unavailable registry before it checks intrinsic proposal/signature/slot binding, while keyless ingress treats that `Stalled` result as acceptable and clears proposal state. The supplied regression does not cover this failure path or successful key-aware CRDT ingress.

## Critical Issues

### CR-01: A stalled registry lets malformed certificates clear a canonical slot

**File:** `/Users/henriqueklein/gnus/SuperGenius/src/blockchain/Consensus.cpp:2190-2237`, `/Users/henriqueklein/gnus/SuperGenius/src/blockchain/Consensus.cpp:2437-2467`

**Issue:** `ValidateCertificate` loads the claimed registry and returns `Check::Stalled` on failure before it reaches `ValidateSubject`, `CheckProposal`, proposal-ID recomputation, or the Phase-8 binding guard. `HandleCertificate` rejects only `Check::Reject`, so a certificate whose proposal and certificate both name an unavailable registry proceeds to `CreateProposalState` and `ClearProposalSlot`. An attacker can therefore combine a nonexistent registry CID with an unsigned/malformed proposal (or invalid slot binding) to erase all local contenders for the derived slot. This violates the required fail-closed behavior before state mutation.

**Fix:** Validate all intrinsic certificate/proposal/slot properties before the first operation that can return `Stalled`, then require `Check::Approve` in `HandleCertificate` before creating or clearing any state. For example:

```cpp
// ValidateCertificate: before LoadRegistryByCid(...)
if (!ValidateSubject(proposal.subject()) || !CheckProposal(proposal) ||
    CreateProposalId(proposal) != certificate.proposal_id() ||
    !ValidateCertificateBinding(certificate)) {
    return Check::Reject;
}

// HandleCertificate
if (ValidateCertificate(certificate) != Check::Approve) {
    return;
}
```

Keep a truly well-formed certificate with a temporarily unavailable registry in a non-destructive deferred path; it must not pass the state-cleanup path.

## Warnings

### WR-01: The ingress regression omits required valid-CRDT and unavailable-registry malformed cases

**File:** `/Users/henriqueklein/gnus/SuperGenius/test/src/blockchain/consensus_pending_lifecycle_test.cpp:555-621`

**Issue:** The sole new ingress test checks a mismatching key and a valid keyless call. It never sends a valid certificate through `FilterCertificate` and `CertificateReceived` using its matching legacy `/cert/<subject-hash>` key, nor does it combine an invalid proposal/binding with an unavailable registry. Consequently it misses the critical fail-open path above and does not establish that the key-aware happy path remains usable.

**Fix:** Add separate controls for (1) matching-key filter and receipt acceptance, with handler/finality observations, and (2) a malformed or unsigned certificate naming an unavailable registry. For the latter, seed a tracked same-slot proposal and assert filter rejection plus unchanged proposal/slot state, no handler call, no registry finalization, and no journal/write side effect.

---

_Reviewed: 2026-08-20T15:48:56Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
