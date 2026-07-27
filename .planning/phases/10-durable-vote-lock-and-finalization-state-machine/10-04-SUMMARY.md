---
phase: 10-durable-vote-lock-and-finalization-state-machine
plan: 04
subsystem: consensus-voting
tags: [consensus, vote-lock, publication, replay, finalization]

requires:
  - phase: 10-durable-vote-lock-and-finalization-state-machine
    provides: Strict startup restoration and exact durable envelope replay from Plan 10-03
provides:
  - Immutable per-generation proposal selection deadlines using the canonical comparator
  - One-way slot lifecycle reservations across signing, durable storage, publication, replay, and finality
  - Persist-before-publish signed vote envelopes with exact-byte retry and no re-signing
  - Live first-observation certificate horizon validation separated from timeless structural normalization
affects: [phase-10, consensus-voting, finalization, vote-replay, certificate-validation]

tech-stack:
  added: []
  patterns:
    - Reserve irreversible consensus stages under the slot mutex, then perform storage and transport without it
    - Persist one signed envelope before its first raw publication and replay those exact bytes on retry
    - Retire an expired durable generation before admitting and signing a later generation

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - test/src/blockchain/consensus_vote_journal_test.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp

key-decisions:
  - "SigningPublishing owns the slot from comparator freeze through signing, durable storage, and completion of the first raw publish attempt."
  - "Publication failure returns to Voted with the persisted envelope intact; PublishingReplay retries exact bytes without invoking the signer."
  - "Finality and safety reservations suppress signing and replay, while a later generation requires durable retirement strictly after the acceptance horizon."
  - "Certificate first observation applies the live clock to proposal and vote horizons; stored authority uses timeless structural normalization."

patterns-established:
  - "Generation check: every irreversible stage revalidates lifecycle, generation, frozen proposal, and finality/safety suppression."
  - "Deadline boundary: steady_now == deadline freezes the winner, while now == acceptance horizon remains locked."

requirements-completed: [VOTE-01, VOTE-02, VOTE-04, VOTE-05, VOTE-07]

duration: 34 min
completed: 2026-07-27
---

# Phase 10 Plan 04: Durable Vote Publication State Machine Summary

**Consensus now deterministically freezes one proposal per generation, persists its single signed envelope before publication, and coordinates replay, finality, safety, and horizon retirement through explicit slot lifecycle reservations.**

## Performance

- **Duration:** 34 min
- **Started:** 2026-07-27T16:13:22Z
- **Completed:** 2026-07-27T16:47:06Z
- **Tasks:** 1
- **Files modified:** 4

## Accomplishments

- Replaced the proposal-ID vote set with an explicit lifecycle covering selection, signing/publication, durable voting, replay, retirement, finalization, application, and safety-stop states.
- Fixed each generation's steady-clock deadline at first valid admission and froze the canonical comparator winner exactly at the boundary.
- Held `SigningPublishing` through signing, exact serialization, durable `PutVote`, and the complete first raw publication attempt without holding the broad proposal mutex across storage or PubSub.
- Replayed only the exact persisted envelope bytes under `PublishingReplay`, never re-signing after storage or retrying after finality or safety authority appears.
- Enforced durable retirement strictly after the acceptance horizon before a later generation can select or sign.
- Split live first-observation horizon checks for proposal and vote timestamps from timeless structural certificate normalization used by stored authority.

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement durable slot voting and publication lifecycle** — `49858bd9` (feat)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — defines lifecycle state, generation/deadline authority, publication metadata, and deterministic test hooks.
- `src/blockchain/Consensus.cpp` — implements deadline freezing, persist-before-publish, exact replay, finality/safety suppression, retirement, startup restoration, and live certificate checks.
- `test/src/blockchain/consensus_vote_journal_test.cpp` — covers comparator/deadline boundaries, stage ordering, failure/replay bytes, finality reservation, certificate horizons, and retirement generations.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — adapts legacy pending assertions to lifecycle semantics and closes its long-running manager deterministically.

## Decisions Made

- A slot remains reserved as `SigningPublishing` until its full initial raw publish attempt completes; finalization can reserve only afterward.
- A failed initial publication does not weaken the durable vote lock. Duplicate delivery enters `PublishingReplay`, publishes stored bytes, and returns to `Voted` without signer use.
- Exact deadline equality selects; exact acceptance-horizon equality remains locked. Retirement and later-generation admission require `now > horizon`.
- Structural certificate normalization is clock-independent, while genuinely new network observations validate both proposal and every vote timestamp against the live clock.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Deterministically closed the pending-lifecycle timer in its long integration case**
- **Found during:** Focused full-binary verification
- **Issue:** All seven assertions passed, but the process could release the manager's final reference on its timer thread after the suite, causing a self-join teardown exception.
- **Fix:** Added the same explicit `Close()` already used by the neighboring fixture case.
- **Files modified:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- **Verification:** The exact focused command exits cleanly with 30/30 journal tests and 7/7 pending-lifecycle tests passing.

---

**Total deviations:** 1 auto-fixed blocking test teardown issue.
**Impact on plan:** No production scope expansion; the adjustment makes the required regression binary deterministic.

## Issues Encountered

- The pending-lifecycle binary initially reported all tests passing and then aborted during asynchronous fixture teardown. Explicit manager closure removed the ownership race.

## Verification

- Exact focused build-and-run command passes: `consensus_vote_journal_test` 30/30 and `consensus_pending_lifecycle_test` 7/7.
- Phase 10 CTest regex passes 8/8 targets: vote journal, finalization, pending lifecycle, certificate store, certificate compatibility, network configuration precedence, transaction-manager pending lifecycle, and UTXO manager.
- Comparator/deadline ordering, store-before-publish, byte-identical replay, failure behavior, finality reservation, certificate horizon, and retirement boundary acceptance cases pass.
- `git diff --check` passes.

## User Setup Required

None.

## Next Phase Readiness

- Durable vote publication now exposes the reservation and authority semantics required by the remaining finalization integration work.
- Strict startup validation remains intact and stored certificate authority remains timeless.
- No blocker remains for Plan 10-05.

## Self-Check: PASSED

- Implementation commit `49858bd9` exists and all four implementation/test files are present.
- Every Plan 10-04 acceptance criterion and the Phase 10 regression gate pass.
- Protected pre-existing dirty paths remain unstaged and uncommitted.

---
*Phase: 10-durable-vote-lock-and-finalization-state-machine*
*Completed: 2026-07-27*
