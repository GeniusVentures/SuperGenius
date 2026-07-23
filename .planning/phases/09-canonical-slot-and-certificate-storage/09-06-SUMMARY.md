---
phase: 09-canonical-slot-and-certificate-storage
plan: 06
subsystem: bridge-catchup
tags: [receipt-retry, transactional-staging, cursor-safety, ordinal-narrowing]

requires:
  - phase: 09-02
    provides: Receipt-local bridge event identity and catch-up receipt resolution
  - phase: 09-04
    provides: Fail-closed canonical lookup semantics
provides:
  - Transactional catch-up chunks that publish only after every enabled query and receipt dependency succeeds
  - Retry-safe cursors with fresh per-poll receipt resolution
  - Production uint64_t-to-uint32_t receipt ordinal boundary checking
affects: [10-finalization-state-machine, 11-bridge-reservations, 12-race-verification]

tech-stack:
  added: []
  patterns:
    - Chunk-local staging separates untrusted receipt validation from external burn publication
    - Cursor advancement is the commit point after complete v1/v2 validation and publication
    - Wide receipt ordinals narrow through one directly tested production helper

key-files:
  created: []
  modified:
    - evmrelay/include/eth/eth_receipt_source.hpp
    - evmrelay/src/eth/eth_receipt_source.cpp
    - src/watcher/impl/bridge_catchup_watcher.hpp
    - src/watcher/impl/bridge_catchup_watcher.cpp
    - test/src/account/bridge_event_identity_test.cpp

key-decisions:
  - "A catch-up chunk publishes no burn until both v1 and enabled v2 queries, receipt identity resolution, ordinal derivation, and ABI decoding all succeed."
  - "Receipt cache and failed null results live for one poll attempt only, so retry performs fresh receipt requests while preserving the failed chunk start."
  - "Receipt-local ordinals are computed as uint64_t and narrowed only through checked_receipt_log_ordinal at the full uint32_t production boundary."

patterns-established:
  - "Catch-up transaction boundary: query -> validate/stage -> publish -> merge dedup -> advance cursor."
  - "Retry identity: last_block_per_chain_ remains the next uncommitted chunk start."

requirements-completed:
  - SLOT-03
  - SLOT-04

duration: 9 min
completed: 2026-07-23
---

# Phase 09 Plan 06: Transactional Catch-Up Cursor Summary

**Bridge catch-up now stages receipt-proven burns transactionally, retries failed chunks with fresh receipt requests, and advances its cursor only after complete v1/v2 success.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-07-23T20:44:06Z
- **Completed:** 2026-07-23T20:53:03Z
- **Tasks:** 1
- **Files modified:** 5

## Accomplishments

- Converted missing, malformed, mismatched, ambiguous, overflowing, and undecodable receipt dependencies from skipped burns into explicit chunk failure.
- Added chunk-local burn and dedup staging so an early valid v1 burn cannot escape before a later receipt or v2 query fails.
- Preserved the failed chunk start in `last_block_per_chain_`; a retry creates a new transport/cache attempt and fetches every receipt afresh.
- Added a directly testable `checked_receipt_log_ordinal(uint64_t)` production helper with exact `0`, `UINT32_MAX`, and `UINT32_MAX + 1` coverage.
- Added deterministic one-poll test access and scripted transport regressions for late receipt failure, v1/v2 atomicity, decode failure, receipt identity mismatches, malformed arrays, and missing/duplicate block-wide indices.

## Task Commits

Each task was committed atomically across the nested dependency and parent repository:

1. **Task 1: Preserve the catch-up cursor until all receipt dependencies resolve**
   - `62a9bbb101732a222466de19b80aca905af37e23` — nested `evmrelay`
   - `f0da25d7` — parent watcher, tests, and exact nested pointer

## Files Created/Modified

- `evmrelay/include/eth/eth_receipt_source.hpp` - Declares checked wide-to-receipt-ordinal narrowing.
- `evmrelay/src/eth/eth_receipt_source.cpp` - Implements the exact `UINT32_MAX` production boundary.
- `src/watcher/impl/bridge_catchup_watcher.hpp` - Adds narrowly scoped deterministic single-poll test access.
- `src/watcher/impl/bridge_catchup_watcher.cpp` - Stages complete chunks, gates v1/v2 success, refreshes retry receipts, and commits cursor/dedup state after publication.
- `test/src/account/bridge_event_identity_test.cpp` - Exercises transactionality, retries, mismatches, ambiguity, decoding, and ordinal boundaries through scripted JSON-RPC.

## Decisions Made

- Receipt identity and ABI decoding are validation dependencies, not reasons to skip an individual burn.
- A successful v1 result is insufficient when v2 is enabled; both families form one chunk commit.
- `BurnProcessor` retains its existing false/exception skip behavior, but it is invoked only in the post-validation publication sweep.
- The canonical transaction hash delivered by catch-up is lowercase hexadecimal without an `0x` prefix.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The first test compile exposed a missing direct include for `base::parse`; adding the owning header resolved it without production changes.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Prescribed target build — PASS: `bridge_event_identity_test` and `bridge_anvil_catchup_e2e_test`.
- Prescribed focused filter — PASS: 10/10 transactional catch-up tests.
- Full `bridge_event_identity_test` — PASS: 14/14, including existing multiple-burn and re-inclusion cases.
- Receipt failure audit — PASS: receipt/identity/decode failures return chunk failure rather than continuing to success.
- Publication audit — PASS: `process_logs` contains no `burn_processor_` call; one publication sweep follows complete v1/v2 staging.
- Cursor audit — PASS: `from_block = chunk_to + 1` occurs only after successful enabled-family staging and publication.
- Boundary audit — PASS: production and tests call the same `checked_receipt_log_ordinal(uint64_t)` helper.
- Parent and nested `git diff --check` — PASS.

## Next Phase Readiness

- Catch-up can no longer suppress a historical burn by committing a cursor after transient receipt failure.
- Phase 10 can rely on catch-up candidates being emitted only from a fully validated chunk.
- No blockers.

## Self-Check: PASSED

- All five modified plan files exist.
- Nested commit `62a9bbb101732a222466de19b80aca905af37e23` and parent task commit `f0da25d7` are present.
- All prescribed and full focused verification passes after both commits.
- SLOT-03 and SLOT-04 retain same-transaction multi-burn coverage with retry-safe receipt-local identity.
- Protected user-owned dirty and untracked paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
