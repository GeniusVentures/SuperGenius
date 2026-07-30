---
phase: 12-consensus-race-and-compatibility-verification
plan: 04
subsystem: consensus-testing
tags: [consensus, bridge, race, pubsub, exactly-once]

requires:
  - phase: 12-01
    provides: Private consensus trace seam and deterministic race-test infrastructure
  - phase: 11-slot-owned-bridge-burn-reservations
    provides: Authoritative slot-owned burn lifecycle and atomic mint application
provides:
  - Mandatory real-network proof that eleven validators propose and vote for one burn slot
  - Byte-identical authority, one confirmed winner, exact balance, and stable once-only application evidence
  - Bounded friend-only diagnostics for readiness, proposals, votes, authority, transaction state, and live output indexing
affects: [12-05-full-suite-closure, phase-12-verification]

tech-stack:
  added: []
  patterns: [predicate-driven race barriers, staged real-mesh partition and healing, canonical big-endian bridge destinations]

key-files:
  created: []
  modified:
    - test/src/bridge_race/bridge_race_fixture.hpp
    - test/src/bridge_race/bridge_race_single_burn_test.cpp
    - src/account/UTXOManager.cpp
    - src/blockchain/Consensus.hpp
    - evmrelay/src/eth/secp256k1_utility.cpp

key-decisions:
  - "The race uses a bounded 60-second operator vote window while retaining the production 500 ms compiled default and a 60-second maximum ceiling."
  - "A finalized certified mint may reconcile a provisional bridge UTXO owner, but every other descriptor and lifecycle mismatch remains fail-closed."
  - "ABI bytes32 and GeniusAccount public-key coordinates use canonical big-endian order; bridge destination conversion performs no coordinate reversal."
  - "The real PubSub mesh is partitioned only at explicit proposal/vote barriers and healed through production peer admission before convergence assertions."

patterns-established:
  - "Long consensus tests assert structured private state and event counters; log text is diagnostic only and never the oracle."
  - "Exactly-once bridge convergence includes transaction confirmation, exact balance delta, confirm counter, process completion, loser exclusion, live UTXO state, and owner-index membership."

requirements-completed: [TEST-01]

duration: 2h 21m
completed: 2026-07-30
---

# Phase 12 Plan 04: Eleven-Validator Bridge Race Summary

**One post-readiness burn now produces eleven local proposals, one usable vote target per validator, one byte-identical authority, and exactly one confirmed mint across the real network.**

## Performance

- **Duration:** 2h 21m
- **Started:** 2026-07-30T17:28:00Z
- **Completed:** 2026-07-30T19:49:00Z
- **Tasks:** 2
- **Files modified:** 10 across the root project and `evmrelay`

## Accomplishments

- Installed shared-ownership, friend-only trace observers on all eleven live consensus managers with bounded concurrent snapshots and condition-variable predicates.
- Created the sole burn only after every endpoint configuration succeeded and every node reported READY, then proved all eleven distinct local proposals target one canonical slot.
- Staged real PubSub partition/healing barriers so every validator publishes exactly one usable vote target before certificate convergence.
- Proved every node exposes byte-identical authority, confirms only the winner, applies an exact `+1` balance and confirm-count delta, completes the slot process once, and remains unchanged through a 16-second event-driven stability window.
- Fixed two defects exposed by the stronger oracle: finalized winners now reconcile losing provisional bridge-input ownership, and v2 bridge destinations preserve canonical big-endian X and Y coordinates.

## Task Commits

1. **Task 1: Equip the fixture with structured race evidence** - `03b8cbe7` (test)
2. **Task 2: Assert one burn, eleven proposals, one vote each, and one winner** - `012f7d78` (test)

Supporting fixes discovered by the end-to-end oracle:

- `80c6d7b5` - reconcile finalized bridge input owner
- `9059a77c` - preserve canonical mint destination (`evmrelay` commit `4787e58`)
- `6920dc8c` - permit bounded one-minute vote windows

## Files Created/Modified

- `test/src/bridge_race/bridge_race_fixture.hpp` - Eleven-node setup, real mesh controls, structured observers, predicates, and bounded diagnostics.
- `test/src/bridge_race/bridge_race_single_burn_test.cpp` - Sole-burn participation, vote, authority, application, loser, and stability proof.
- `src/account/UTXOManager.cpp` - Finalized-authority-only reconciliation of provisional bridge input ownership.
- `test/src/account/utxo_manager_test.cpp` - Retry/restart regression for a losing provisional owner.
- `src/blockchain/Consensus.hpp` - Bounded 60-second operator ceiling with the 500 ms default unchanged.
- `test/src/account/network_config_precedence_test.cpp` - Exact 60,000 ms acceptance and 60,001 ms rejection boundaries.
- `evmrelay/include/eth/secp256k1_utility.hpp` - Canonical destination byte-order contract.
- `evmrelay/src/eth/secp256k1_utility.cpp` - Big-endian X-only decompression output matching `GeniusAccount::GetAddress()`.
- `evmrelay/test/eth/secp256k1_utility_test.cpp` - Canonical coordinate-order vectors.
- `test/src/account/bridge_relayer_test.cpp` and `test/src/bridge_e2e/anvil_fixture.hpp` - Exact X||Y regression and correct Y-parity encoding.

## Decisions Made

- Used deterministic account preregistration before asynchronous node construction so every genesis validator identity exists before blockchain initialization.
- Required complete proposal replication before vote publication, then healed the production mesh for authority convergence; no direct consensus mutation is used as an oracle.
- Retained the existing 500-second mandatory CTest timeout. The verified healthy run took 260.44 seconds, leaving 239.56 seconds of margin.

## Deviations from Plan

### Automatically Fixed Issues

**1. Finalized winner could not consume a losing candidate's provisional bridge UTXO**

- **Found during:** Task 2 exact application convergence
- **Issue:** Ten nodes retained a provisional bridge input owned by their local losing candidate, so certified winner application failed despite unanimous authority.
- **Fix:** Permit owner-only reconciliation exclusively under an exact `FINALIZED_PENDING_APPLICATION` reservation; all identity, type, amount, token, outpoint, and lifecycle checks remain strict.
- **Verification:** Focused retry/restart test and the eleven-node exact application oracle pass.
- **Committed in:** `80c6d7b5`

**2. X-only bridge destination decompression reversed canonical coordinates**

- **Found during:** Task 2 exact destination balance assertion
- **Issue:** ABI bytes32 and Genius coordinates were incorrectly treated as little-endian, producing a confirmed output indexed to a different public key.
- **Fix:** Preserve canonical big-endian X and Y throughout the relayer and Anvil encoder; add a full known X||Y regression.
- **Verification:** `bridge_relayer_test` passes and every race node observes the exact destination balance delta.
- **Committed in:** `9059a77c` with submodule commit `4787e58`

---

**Total deviations:** 2 automatically fixed correctness defects.
**Impact on plan:** Both fixes were necessary for the specified exact-confirmation and balance oracle; no assertion, default, timeout, or safety boundary was weakened.

## Issues Encountered

- An exploratory GeniusAccount round-trip test initially shared process-global secure-storage state with the long fixture and caused an early process abort. The diagnostic was removed from the long binary; the isolated relayer test now owns the exact full-vector regression, and the mandatory binary again contains one E2E test.

## User Setup Required

None - the test provisions its local Anvil process, deterministic keys, network configuration, and node storage.

## Next Phase Readiness

- TEST-01 is closed with mandatory unrestricted evidence.
- Plan 12-05 can run the final full-suite, sanitizer, and milestone verification closure.

## Verification

- Focused `utxo_manager_test`, `bridge_relayer_test`, and `network_config_precedence_test`: 3/3 passed in 2.44 seconds.
- Mandatory unrestricted `bridge_race_single_burn_test`: 1/1 passed in 260.44 seconds.
- Race correctness `sleep_for` scan: none found.
- Root and `evmrelay` `git diff --check`: passed.

## Self-Check: PASSED

---
*Phase: 12-consensus-race-and-compatibility-verification*
*Completed: 2026-07-30*
