---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "09"
subsystem: auth
tags: [networkregistry, securecrdt, ingestfilter, refreshloop, regression, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "03"
    provides: sgns::networkregistry::NetworkRegistry (D-06), refresh-thread machinery, per-network base keys
  - phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
    provides: SecureCrdt/SecureCrdtRegistry policy + element-filter machinery
provides:
  - WR-02 fix: drain-once RefreshLoop (refresh_pending_ cleared per consumed notification) — prerequisite for 15-12 wiring tx_globaldb_ as the live refresh path
  - WR-03 fix: non-destructive duplicate NetworkRegistry::New (resolve-first pre-check, std::errc::address_in_use, live entry never touched)
  - WR-04 fix: New re-runs SecureCrdt::RegisterFilters() so network-registry/<id> receives its ingest element filter under both production wiring orders
  - NetworkRegistry::RefreshAttemptsForTesting() observability seam
affects: [15-11-filter-clear, 15-12-live-refresh, securecrdt, geniusnode-wiring]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Drain-once cv loop: clear the pending flag WHILE holding the mutex guarding the notify, then unlock before the work — clearing after the unlock can silently drop a notification that arrived in between (the element is already in the datastore, so the next TryConfirm would have seen it only if the flag survived)"
    - "Factory re-runs SecureCrdt::RegisterFilters() after late pattern registration (re-registration replaces-and-succeeds in CRDTDataFilter) instead of requiring callers to order registration before one global filter snapshot"
    - "Remote-ingest regression testing on the single-node fixture: second real GlobalDB node over loopback libp2p (AddPeers + connection wait), one delta carrying both a filtered garbage element and an unfiltered probe control — the probe proves delivery, the branch-key negative proves the filter"

key-files:
  created: []
  modified:
    - src/networkregistry/NetworkRegistry.hpp
    - src/networkregistry/NetworkRegistry.cpp
    - test/src/networkregistry/network_registry_test.cpp

key-decisions:
  - "WR-02 store placement follows the review's fix sketch over the plan's ambiguous parenthetical: refresh_pending_.store(false) executes while still holding refresh_mutex_ (before the unlock that precedes TryConfirm) — a clear-after-unlock can lose a notification arriving in that window, and the plan itself requires unlock-before-TryConfirm which is preserved"
  - "WR-03 pre-check resolves DefaultBaseKey(private_network_id) BEFORE make_shared and returns address_in_use without registering; RegisterSignerSetSource failure also remapped file_exists → address_in_use (misleading per the review)"
  - "WR-04 chosen over the alternative 'hook that registers its own filter': New-funnelled RegisterFilters() re-run covers BOTH wiring paths (direct GeniusNode.cpp:1037/1059 and TrustStartupController) with one change and is idempotent (CRDTDataFilter::RegisterElementFilter replaces-and-returns-true; PubSubBroadcasterExt::AddListenTopic is guarded)"
  - "Test 3 uses the plan's pre-authorized fallback (two-datastore sync): a local Put never traverses element filters (crdt_datastore.cpp:931 gates FilterElementsOnDelta on !created_by_self), verified from source AND empirically — mutation run showed the unsigned remote element landing pre-fix, rejected post-fix, with the probe control proving delta delivery in both"

patterns-established:
  - "Mutation-verify regression tests for review-finding fixes: revert only the fix hunks, prove the new tests fail with their specific assertions, restore, prove green — one build cycle for all three fixes at once"

requirements-completed: [D-06, PNET-NETREG]

# Metrics
duration: 18min
completed: 2026-09-03
---

# Phase 15 Plan 09: NetworkRegistry Lifecycle Gap Closure Summary

**Three VERIFICATION-gap fixes (WR-02/WR-03/WR-04) in NetworkRegistry — drain-once refresh loop ending the permanent TryConfirm busy-spin, non-destructive duplicate construction, and ingest-filter coverage for the late-registered network-registry/<id> branch — each pinned by a mutation-verified regression test**

## Performance

- **Duration:** ~18 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T11:16:53Z
- **Completed:** 2026-09-03T11:34:48Z
- **Tasks:** 2/2
- **Files modified:** 3 (0 created)

## Accomplishments

- **WR-02 (gap 8):** `RefreshLoop` now clears `refresh_pending_` while holding `refresh_mutex_` before unlocking and calling `TryConfirm` — one TryConfirm per consumed notification, then back to waiting. A notification arriving during TryConfirm re-sets the flag (no lost refresh); no retry semantics added (a `success(false)` re-set would resurrect the spin). Pre-fix measurement from the mutation run: **28,569 attempts in the 500ms grace window**; post-fix: ≤1.
- **WR-03 (gap 9):** `NetworkRegistry::New` resolves `DefaultBaseKey(private_network_id)` against the SecureCrdtRegistry BEFORE `make_shared`/`Register` and fails with `std::errc::address_in_use` without registering anything — the live registry's policy entry is never replaced-then-destroyed (the UNREGISTERED_KEY bricking path is gone). `RegisterSignerSetSource` failure remapped `file_exists` → `address_in_use`; hpp factory contract documents the new error alongside the floor errors.
- **WR-04 (gap 7):** `New` re-runs `SecureCrdt::RegisterFilters()` after registration (failure fails `New` with `io_error`, non-destructive teardown), so the `network-registry/<id>` pattern receives its ingest element filter even though both production wiring paths snapshot filters BEFORE constructing the registry. This covers remote-originated unsigned membership payloads and sig children at datastore ingest, same as trusted-peer-registry/burn-config.
- **Observability seam:** `refresh_attempts_` atomic + public `RefreshAttemptsForTesting()` (documented test seam).
- **Regression tests (3 new cases, all mutation-verified):**
  - `DuplicateNewDoesNotClobberLiveRegistry` — duplicate `New` errors with `address_in_use`, the live entry still resolves, and `SeedBootstrap` on the first registry succeeds.
  - `RefreshLoopDrainsOnceWithoutSpinning` — ≤1 attempt growth in a 500ms grace window after a callback-driven confirm; a later proposal still wakes the thread.
  - `IngestFilterCoversLateRegisteredNetworkRegistryPattern` — second real GlobalDB node over loopback libp2p pushes one delta containing an unsigned garbage element under the registry branch AND an unfiltered probe; the probe replicates (delivery proven), the branch element never lands.
- **Full suite:** network_registry_test 12/12 (9 fixture cases: 6 pre-existing + 3 new; plus 3 payload cases); regression guard `trustedpeerregistry|securecrdt|burnconfig|private_network_registry_binding` 13/13.

## Task Commits

Each task was committed atomically:

1. **Task 1: Harden NetworkRegistry lifecycle (WR-02/WR-03/WR-04)** - `274baebb` (fix)
2. **Task 2: Regression tests for the three lifecycle fixes** - `cc76a172` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/networkregistry/NetworkRegistry.hpp` - `refresh_attempts_` member, `RefreshAttemptsForTesting()` declaration, `New` contract documents `address_in_use`
- `src/networkregistry/NetworkRegistry.cpp` - drain-once `RefreshLoop`, duplicate pre-check + `FactoryLogger()` (file-local, created lazily for pre-instance failure logging), `RegisterFilters()` re-run in `New`, `RefreshAttemptsForTesting` definition
- `test/src/networkregistry/network_registry_test.cpp` - 3 new TEST_F cases (WR-03/WR-02/WR-04), `<system_error>` include

## Decisions Made

- See key-decisions. The single substantive interpretation call was the WR-02 store placement (mutex-held clear per the review sketch; the plan's "while still holding no lock" parenthetical read alone would permit a post-unlock clear that can drop a notification — both readings satisfy the plan's acceptance criterion of exactly one store between the stopping check and TryConfirm).
- Verified before relying on them: `CRDTDataFilter::RegisterElementFilter` replaces-and-returns-true (re-running `RegisterFilters` cannot fail `New` on already-registered patterns), `PubSubBroadcasterExt::AddListenTopic` is idempotent (no double subscription from the re-run), and the GeniusNode.cpp production call site checks only `has_error()` (error-code remap is safe).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's WR-02 wording permits a lost-notification ordering**
- **Found during:** Task 1
- **Issue:** The plan says to clear `refresh_pending_` "(while still holding no lock — the current code unlocks at :452 before TryConfirm; keep that ordering)". Read as clear-after-unlock, a callback firing in the unlock→clear window has its flag clobbered: the element is already in the datastore but no further TryConfirm runs until the next arrival — a lost refresh.
- **Fix:** Implemented the 15-REVIEW.md fix sketch exactly: `refresh_pending_.store(false)` while still holding `refresh_mutex_`, then `lock.unlock()`, then `TryConfirm()` (the "keep that ordering" constraint — unlock before TryConfirm — is preserved; notifications arriving during TryConfirm re-set the flag and are processed next iteration).
- **Files modified:** src/networkregistry/NetworkRegistry.cpp
- **Verification:** RefreshLoopDrainsOnceWithoutSpinning passes including its no-lost-wakeup assertion; mutation run (store removed) fails with 28,569 attempts in the window
- **Committed in:** 274baebb

**2. [Rule 3 - Blocking] Test 3's primary path (local Put through node_->db) cannot observe the ingest filter**
- **Found during:** Task 2 design (verified from source, then empirically)
- **Issue:** Element filters run only for remote-originated deltas — `crdt_datastore.cpp:931` gates `FilterElementsOnDelta` on `!created_by_self` — so a local GlobalDB Put would land regardless of the fix and the test could never go green against the fix or red against the bug.
- **Fix:** Used the plan's explicitly pre-authorized fallback: the two-datastore sync pattern (second real GlobalDB node, loopback libp2p `AddPeers` + bidirectional connection wait, one delta carrying both the unsigned branch element and an unfiltered probe control). The plan's required observable is preserved exactly: an unsigned element under network-registry/<id> never lands.
- **Files modified:** test/src/networkregistry/network_registry_test.cpp
- **Verification:** Mutation run (RegisterFilters re-run removed) — unsigned element LANDS and both negative assertions fail; with the fix — probe replicates, branch stays empty
- **Committed in:** cc76a172

---

**Total deviations:** 2 (1 bug-avoidance interpretation, 1 pre-authorized test-path fallback)
**Impact on plan:** All must_have truths and artifacts hold; no scope creep; no new dependencies; nothing under 3rdparty/ or thirdparty/ touched.

## Verification Evidence

- `ninja -C build/OSX/Release networkregistry` — clean; all Task 1 source assertions pass (exactly 1 `refresh_pending_.store( false` in RefreshLoop; `Resolve` pre-check at :376 above `make_shared` at :388; `RegisterFilters` call in `New` at :415 after `RegisterCrdtChangeCallback` at :403; `address_in_use` x2)
- `ctest -R network_registry_test` — Passed (binary: 12/12 cases)
- `ctest -R "trustedpeerregistry|securecrdt|burnconfig|private_network_registry_binding"` — 13/13 passed
- Mutation verification (fix hunks reverted, tests re-run, fix restored byte-exact from backup, re-verified green):
  - WR-03: pre-fix the duplicate `New` returned **success** (`has_error()==false`) — see Notes — and the test failed at its first assertion
  - WR-02: pre-fix attempts grew by 28,569 in the grace window (bound: ≤1)
  - WR-04: pre-fix the unsigned remote element landed (`Get` succeeded)
- Test-count nuance: the plan's acceptance criterion says "9/9 (6 pre-existing + 3 new)"; those are the `NetworkRegistryTest` fixture cases. The binary additionally carries the 3 pre-existing `NetworkMembershipPayloadTest` cases, so the full run is 12/12 — no pre-existing case regressed.

## Notes for Downstream Plans

- **15-12 dependency satisfied:** WR-02 is fixed on this branch before `tx_globaldb_` wiring; the refresh thread now parks between notifications (this was the live 100%-CPU + GlobalDB-scan spin).
- **15-11:** the ingest filter now also covers `network-registry/<id>` sig children remotely (canonical-signer check at ingest, SecureCrdt.cpp FilterSecureCrdtUpdate) — relevant when designing the from-field/membership reads.
- **Factual correction to 15-REVIEW WR-03 (found during mutation run):** `SecureCrdtRegistry::Register`'s extract-then-insert dance makes it return **true** for pattern replacements (the review claimed false), so pre-fix the duplicate `New` did not even fail — it constructed a second live registry whose teardown (`UnregisterIf` with the replaced entry's token) removed the entry and bricked the first registry. The regression test pins the observable that matters: duplicate `New` must fail with `address_in_use` and the first registry must keep working.
- Production impact check: `GeniusNode.cpp:1059` calls the 5-arg `New` (global_db defaulted null → no refresh thread in-node today); its `RegisterFilters` re-run adds only the missing per-pattern element filter; `private_network_registry_binding_test` (the in-node wiring test) is green. Public nodes construct no NetworkRegistry — no behavior change.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 3 modified source/test files exist on disk; SUMMARY.md created
- Commits verified in git log: 274baebb (fix), cc76a172 (test)
- No file deletions in either task commit (diff-filter=D empty for both)
- No new untracked files from this run (only the pre-existing snapshot untracked set plus this SUMMARY, committed below)
- `ctest -R network_registry_test`: 12/12; guard suites: 13/13

