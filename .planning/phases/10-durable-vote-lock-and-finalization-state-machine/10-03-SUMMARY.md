---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 03
subsystem: consensus-startup
tags: [consensus, restart, vote-journal, finalization, configuration]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Strict local consensus records and exact stored vote-envelope bytes from Plan 10-02
provides:
  - Fail-closed consensus restoration before transport, filters, timers, handlers, or publication
  - Exact raw-envelope replay for active unexpired vote locks after transport startup
  - Immutable bounded vote-selection-window configuration propagated before manager construction
  - Pending certificate recovery that survives stale leases and absent subject handlers
affects: [phase-10, consensus-startup, vote-replay, certificate-processing, node-config]

tech-stack:
  added: []
  patterns:
    - Restore and cross-check all durable consensus authority before observable startup side effects
    - Replay persisted signed envelopes byte-for-byte without parsing, reconstruction, or signer use
    - Resolve immutable consensus configuration at the node boundary and pass it by value

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/ConsensusStateStore.hpp
    - src/blockchain/ConsensusStateStore.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/src/account/network_config_precedence_test.cpp
    - test/src/blockchain/consensus_vote_journal_test.cpp

key-decisions:
  - "Consensus startup treats the certificate store as authoritative and permits only one synthesized recovery gap: an authoritative certificate without a process marker becomes Pending."
  - "Startup replay publishes the exact persisted outbound envelope only after transport initialization and records publication failure without retiring or rewriting the lock."
  - "Stale Processing records and missing certificate handlers remain Pending; registering a handler wakes the durable pending processor."

patterns-established:
  - "Side-effect gate: local queries, record validation, certificate reconciliation, and state restoration all succeed before subscribe/filter/timer/publish operations begin."
  - "Restart-safe processing: in-flight leases are not trusted across process lifetime, and durable Pending remains the retry authority."

requirements-completed: [VOTE-03, VOTE-04]

duration: 24 min
completed: 2026-07-27
---

# Phase 10 Plan 03: Fail-Closed Consensus Startup Summary

**Consensus now restores and validates durable vote/finality state before any live side effect, then replays only eligible stored envelopes byte-for-byte through an immutable pre-start configuration.**

## Performance

- **Duration:** 24 min
- **Started:** 2026-07-27T15:34:00Z
- **Completed:** 2026-07-27T15:58:15Z
- **Tasks:** 1
- **Files modified:** 10

## Accomplishments

- Added strict startup restoration that scans local vote, process, conflict, safety, and authoritative certificate state before subscribing, registering filters/listeners, starting timers, processing recovered work, or publishing.
- Added exact stored-envelope replay for active unexpired locks, suppressing finalized and safety-stopped slots and preserving the original retryable record plus publication metadata on failure.
- Propagated an immutable vote selection window from bounded node JSON parsing through `Blockchain::New` into `ConsensusManager::New`, retaining the compiled 500ms default for missing or invalid values.
- Restored stale Processing records as Pending and made late certificate-handler registration wake pending durable work without falsely completing it.
- Expanded focused coverage to 24 journal/startup cases and 4 network configuration cases, including corrupt-state zero-side-effect tables and same-database restart behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Propagate config and restore local safety state before startup side effects** — `136f5aaa` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — defines immutable timing configuration, restored state ownership, and private test observation hooks.
- `src/blockchain/Consensus.cpp` — restores state before side effects, replays exact envelopes, wakes pending work, and closes registered startup resources.
- `src/blockchain/ConsensusStateStore.hpp` — declares restart recovery of stale process records.
- `src/blockchain/ConsensusStateStore.cpp` — atomically restores stale Processing/Pending records to Pending.
- `src/blockchain/Blockchain.hpp` — accepts value-based consensus configuration with a default for direct callers.
- `src/blockchain/impl/Blockchain.cpp` — propagates configuration and fails construction when consensus restoration fails.
- `src/account/GeniusNode.hpp` — stores the resolved pre-construction vote-selection window.
- `src/account/GeniusNode.cpp` — parses bounded JSON configuration and passes it into blockchain construction while preserving pre-existing logger edits.
- `test/src/account/network_config_precedence_test.cpp` — verifies valid and invalid timing configuration and explicitly disables UPnP in fixtures.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — verifies startup ordering, corruption rejection, exact replay, finality/safety suppression, and pending recovery.

## Decisions Made

- Authoritative certificate records are canonicalized and cross-checked against processing and safety records before the manager gains any live transport capability.
- Replay eligibility is derived from restored durable authority: only active, unexpired locks without finality or SafetyViolation are republished.
- A publication retry updates only metadata; signed vote and outbound envelope bytes remain unchanged.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added a store operation for stale processing recovery**
- **Found during:** Task 1 startup restoration
- **Issue:** The planned file list omitted the state-store files, but safely converting restart-stale Processing records to Pending required a durable store mutation rather than an in-memory override.
- **Fix:** Added `ConsensusStateStore::RestorePending` and used it during restoration.
- **Files modified:** `src/blockchain/ConsensusStateStore.hpp`, `src/blockchain/ConsensusStateStore.cpp`
- **Verification:** Focused journal restart tests pass.

**2. [Rule 3 - Blocking] Closed consensus callback/filter lifetime on same-database restart**
- **Found during:** Same-database manager restart tests
- **Issue:** Manager shutdown left certificate callbacks and filters registered, so constructing a replacement manager could observe stale registrations.
- **Fix:** Made shutdown join the timer and unregister callback/filter resources; the destructor invokes the same idempotent close path.
- **Files modified:** `src/blockchain/Consensus.cpp`
- **Verification:** The full 24-case journal suite passes, including same-database restart cases.

---

**Total deviations:** 2 auto-fixed (1 missing critical, 1 blocking)
**Impact on plan:** Both changes were necessary to implement the specified durable restart semantics; no unrelated scope was added.

## Issues Encountered

- Sandboxed runs cannot bind the local PubSub interface. The focused tests were rerun with local-socket permission after every network fixture explicitly disabled UPnP; both CTest targets passed without router mapping activity.

## Verification

- `cmake --build build/OSX/Release --target consensus_vote_journal_test network_config_precedence_test -j2` passes.
- `ctest --output-on-failure -R '(consensus_vote_journal|network_config_precedence)'` passes 2/2 targets.
- `consensus_vote_journal_test` passes all 24 tests; `network_config_precedence_test` passes all 4 tests.
- `git diff --check` passes.
- Cached and committed `GeniusNode.cpp` changes exclude the user's pre-existing Blockchain/ValidatorRegistry logger-level edits.

## User Setup Required

None.

## Next Phase Readiness

- Consensus startup now has a strict durable-state gate and exact replay path ready for subsequent finalization state-machine work.
- Pending certificate application can recover across restart and handler registration without losing durable authority.
- No blocker remains.

## Self-Check: PASSED

- Implementation commit `136f5aaa` exists and all ten implementation/test files are present.
- Every plan acceptance criterion and focused test target passes.
- Protected pre-existing dirty paths remain unstaged and uncommitted.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
