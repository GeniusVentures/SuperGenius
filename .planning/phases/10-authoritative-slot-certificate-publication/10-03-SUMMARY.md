---
phase: 10-authoritative-slot-certificate-publication
plan: "03"
subsystem: blockchain
tags: [consensus, certificates, canonical-slot, crdt]
requires:
  - phase: 10-authoritative-slot-certificate-publication
    provides: Authoritative canonical-slot certificate persistence and ingress validation.
provides:
  - Validated canonical-slot certificate retrieval and boolean checks.
  - Blockchain forwarding facade for authoritative slot lookup.
  - Regression coverage rejecting legacy, malformed, and key-mismatched records.
affects: [transaction-consumers, certificate-finality]
tech-stack:
  added: []
  patterns:
    - Certificate reads select `/cert/<slot>` and validate payload-to-key binding before use.
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
key-decisions:
  - Public authority is an explicit canonical slot, never a subject-hash certificate path.
  - Temporary compatibility overloads interpret their argument as a slot only, so the staged consumer migration cannot read legacy keys.
patterns-established:
  - Validate parse, quorum approval, and exact expected slot key in one retrieval helper.
requirements-completed: [CERT-01, COMP-01]
duration: 18min
completed: 2026-08-21
---

# Phase 10 Plan 03: Authoritative slot certificate lookup Summary

**Consensus and Blockchain now retrieve only approved certificates bound to their canonical `/cert/<slot>` record.**

## Accomplishments

- Added `GetCertificateBySlot` and `CheckCertificateForSlot` to Consensus and the Blockchain facade.
- Centralized parse, certificate approval, and exact record-key binding validation in slot retrieval.
- Kept strict subject checking exact after it derives the subject's canonical slot, preventing a competing same-slot certificate from satisfying an exact-subject check.
- Added lifecycle regressions for successful canonical retrieval and rejection of legacy subject-hash, malformed, unavailable, and mismatched records.

## Verification

- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4`
- `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` — 2/2 passed.
- `git diff --check` — passed.

## Task Commits

1. **Task 1: Add slot-lookup API regressions** — `fce24914` (`test`)
2. **Task 2: Expose validated slot-only certificate lookup through Consensus and Blockchain** — `5a4aadac` (`feat`)

## Deviations from Plan

The focused lifecycle target also compiles account consumers scheduled for Plan 10-04. Removed hash-named declarations would therefore prevent the required Plan 10-03 verification build. Temporary deprecated wrappers remain during the staged migration, but they delegate only to the canonical-slot helper and never construct or query a legacy subject-hash key. Plan 10-04 will migrate all callers to the explicit slot API.

## Next Phase Readiness

Plan 10-04 can migrate hash-starting transaction consumers by loading the transaction and deriving `GetSlotID()` before calling `Blockchain::GetCertificateBySlot`.
