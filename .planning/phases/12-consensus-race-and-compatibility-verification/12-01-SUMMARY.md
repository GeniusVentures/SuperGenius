---
phase: 12-consensus-race-and-compatibility-verification
plan: 01
subsystem: consensus
tags: [consensus, observability, replay, finalization, testing]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Durable per-slot vote publication and unified authoritative finalization
  - phase: 11-slot-owned-bridge-burn-reservations
    provides: Winner-bound pending application and safety-stopped finality lifecycle
provides:
  - Private per-manager structured proposal, vote, and authority trace events
  - Stable envelope digests that distinguish exact replay from different proposal targets
  - Friend-only bridge-race traversal through existing shared ownership
  - Independent-manager behavior-neutrality and replay identity regression tests
affects: [12-02-finality-race, 12-04-eleven-node-race]

tech-stack:
  added: []
  patterns: [private per-instance observers, lock-free callbacks, deterministic public payload digests]

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/account/TransactionManager.hpp
    - test/src/blockchain/consensus_finalization_test.cpp

key-decisions:
  - "Trace callbacks carry copied public consensus identities and execute after production locks are released."
  - "Exact replay identity is validator, canonical slot, proposal target, and SHA-256 digest of the exact outbound envelope."
  - "A safety stop blocks new participation but does not block exact authoritative-winner application recovery."

patterns-established:
  - "Consensus trace observation is private, per manager, exception-neutral, and has no production return channel."
  - "Cross-node tests traverse existing shared ownership only through narrowly named friend access."

requirements-completed: [TEST-01]

duration: 57 min
completed: 2026-07-30
---

# Phase 12 Plan 01: Structured Consensus Trace Foundation Summary

**Private lock-safe consensus trace events expose stable proposal, replay, and authoritative-certificate identities for the real multi-node race.**

## Performance

- **Duration:** 57 min
- **Started:** 2026-07-30T15:57:25Z
- **Completed:** 2026-07-30T16:54:22Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Added private per-manager events for successful local proposal publication, successful initial/replayed vote publication, and authoritative certificate establishment.
- Bound vote evidence to the exact outbound envelope digest so byte-identical replay remains one identity while a different proposal target compares unequal.
- Proved observer isolation across concurrently active managers on independent datastores, re-entrant authority reads outside locks, exception neutrality, and unchanged application outcomes.
- Added friend-only traversal for the Phase 12 bridge-race fixture without changing `GeniusNode` or adding public diagnostics APIs.

## Task Commits

1. **Task 1: Add per-manager structured consensus trace events** - `c66d34eb` (feat)
2. **Task 2: Prove trace isolation, identity, and behavior neutrality** - `4d1be44a` (test)
3. **Baseline investigation correction** - `19c35293` (fix)
4. **Authorized authoritative-winner recovery fix** - `3c486152` (fix)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` - Private trace event, callback, emission helper, and focused friend access.
- `src/blockchain/Consensus.cpp` - Lock-safe proposal/vote/authority emissions and exact-winner safety recovery.
- `src/blockchain/Blockchain.hpp` - Friend-only consensus-manager traversal.
- `src/account/TransactionManager.hpp` - Friend-only blockchain traversal.
- `test/src/blockchain/consensus_finalization_test.cpp` - Independent-manager isolation, neutrality, re-entry, exception, and replay identity proofs.

## Decisions Made

- Events expose only validator, slot, proposal, winner/subject, deterministic public digest, and authority delivery source; no secret or unsigned signer payload is retained.
- Callbacks are invoked from copied values after manager/store/registry locks are released, and exceptions are swallowed.
- The existing exact authoritative winner may recover pending application after a safety stop because `FinalizeSlot` still validates that exact authority while all competing participation remains blocked.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Restored pending authoritative-winner recovery after a safety stop**
- **Found during:** Task 1 full verification
- **Issue:** `ValidConflictIsCanonicalDurableSlotLocalAndOriginalWinnerCanRetry` reproducibly failed without any trace observer because `RecoverRestoredCertificateWork` excluded every safety-stopped slot, including the exact durable authoritative winner.
- **Fix:** Retained the safety stop for new participation while allowing pending process records to re-enter `FinalizeSlot`, whose exact-authority comparison rejects competing certificates.
- **Files modified:** `src/blockchain/Consensus.cpp`
- **Verification:** Focused baseline test passed 1/1; complete finalization suite passed 10/10.
- **Committed in:** `3c486152`

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** The fix restores previously asserted authoritative application recovery without widening consensus participation or observer scope.

## Issues Encountered

- A single `GlobalDB` intentionally permits only one certificate-filter owner. The per-manager isolation test therefore constructs an independent second datastore while retaining both managers concurrently, matching the real node topology.
- Baseline diagnosis required restoring the original safety filter once (`19c35293`) to prove the failure existed without observers before applying the authorized production fix.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The private trace seam is ready for deterministic finality-gap tests and the real 11-node bridge-race collector.
- No public diagnostic API, raw lifetime pointer, or `GeniusNode.cpp` source change was introduced.

## Verification

- `consensus_finalization_test`: 10/10 passed.
- `consensus_vote_journal_test`: 31/31 passed.
- Exact structured trace discovery: 2 tests.
- Focused `*StructuredTrace*`: 2/2 passed.
- `git diff --check`: passed.

## Self-Check: PASSED

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-07-30*
