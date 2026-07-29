---
phase: 11-slot-owned-bridge-burn-reservations
plan: 12
subsystem: blockchain-consensus
tags: [consensus, bridge, burn-reservation, terminal-identity, composed-recovery]
requires:
  - phase: 11-11
    provides: consumed-derived terminal safety and restart suppression
provides:
  - exact authoritative identity gate for live terminal replay
  - fail-closed handling for readable terminal identity contradictions
  - composed production-path proof from certified mint through consumed-artifact recovery
affects: [phase-12, bridge-mint-recovery, consensus-safety-audit]
tech-stack:
  added: []
  patterns: [exact-terminal-identity, fail-closed-live-replay, production-restart-corruption-proof]
key-files:
  created: []
  modified:
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_burn_reservation_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp
key-decisions:
  - "Live terminal idempotence requires one exact comparison of canonical outpoint, reciprocal generation, certificate digest, proposal ID, and winner ID against process and normalized certificate authority."
  - "A readable terminal identity contradiction remains pending and returns StorageFailure, so certificate work cannot be marked done."
  - "The composed recovery proof uses a real two-validator certificate and physical persisted-artifact deletion under the production UTXO lock order."
patterns-established:
  - "Both early terminal replay and the post-handler reread use the same exact authoritative identity predicate."
  - "Consumed artifact corruption is reconstructed over the same RocksDB and driven by real TransactionManager handler registration exactly once."
requirements-completed:
  - BURN-03
  - BURN-04
duration: 20 min
completed: 2026-07-29
---

# Phase 11 Plan 12: Exact Terminal Identity and Composed Recovery Summary

**Live terminal replay now succeeds only for the exact authoritative certificate, and a full production restart test proves consumed artifact corruption becomes permanent, non-retryable consumed safety.**

## Performance

- **Duration:** 20 min
- **Started:** 2026-07-29T20:24:26Z
- **Completed:** 2026-07-29T20:44:26Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments

- Required exact canonical outpoint, reciprocal generation, certificate digest, proposal ID, and winner ID agreement before either live terminal shortcut can return idempotent success.
- Made readable terminal identity mismatches fail closed with actionable expected/observed evidence while preserving the process, terminal records, certificate work, handlers, cleanup, and dependency waiters.
- Added a composed RocksDB-backed proof that a genuinely certified mint applies through Consensus, TransactionManager, and UTXOManager, then recovers a physically deleted application artifact to exact `CONSUMED_SAFETY_ERROR` once.
- Proved another restart, later handler registration, reconciliation, recovery, and duplicate ingress cannot retry, complete, clean, wake, release, remint, or apply an alternate winner.

## Task Commits

Each task was committed atomically:

1. **Task 1: Require exact authoritative identity for live terminal replay** - `443c0e9f` (fix)
2. **Task 2: Prove composed certified-mint corruption recovery through production managers** - `c725d1fc` (test)

## Files Created/Modified

- `src/blockchain/Consensus.cpp` - Shared exact terminal-identity predicate and fail-closed mismatch diagnostics.
- `test/src/blockchain/consensus_burn_reservation_test.cpp` - Exact live idempotence and terminal mismatch matrix coverage.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Composed certified mint, physical corruption, restart recovery, and permanent suppression proof.

## Decisions Made

- Derived the expected terminal identity from the authoritative process and normalized certificate, with the certificate-derived canonical mint outpoint as the sole outpoint authority.
- Kept terminal mismatch handling non-mutating and failure-preserving so neither process completion nor certificate-work retirement can hide the contradiction.
- Used the production registry's default quorum in the composed test by creating a second real validator and genuinely signing both votes instead of weakening certificate validation.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The production registry required two public validator votes at its default quorum. The fixture now creates a second real validator only for the composed test and signs a valid second vote with populated slot hashes.

## User Setup Required

None - no external service configuration required.

## Verification

- Task 1 exact discovery found both required tests once; the focused slice passed 2/2.
- Task 2 exact discovery found the required composed test once; the focused test passed.
- Exact Phase 11 discovery reported 9 targets and the guarded suite passed 9/9 in 156.55 seconds.
- Exact Phase 10 compatibility discovery reported 8 targets and the guarded suite passed 8/8 in 111.68 seconds.
- `git diff --check` passed.

## Next Phase Readiness

- Phase 11's final live-terminal identity and composed consumed-corruption verification gaps are closed.
- Phase 12 can rely on exact non-releasable terminal protection while expanding race and compatibility coverage.
- No blockers; WR-02 callback replacement, mock RPC, broad fuzzing, and Phase 12 work remain untouched.

## Self-Check: PASSED

- Task commits `443c0e9f` and `c725d1fc` exist.
- All three planned modified files are present and committed.
- Both exact focused guards and the guarded 9-target and 8-target closures passed.
- Protected pre-existing dirty paths remain untouched.

---
*Phase: 11-slot-owned-bridge-burn-reservations*
*Completed: 2026-07-29*
