---
phase: 11-burnconfig-quorum-wiring
plan: 02
subsystem: account
tags: [burnconfig, trustedpeer, securecrdt, transactionmanager, geniusnode, gtest, cmake]

# Dependency graph
requires:
  - phase: 11-burnconfig-quorum-wiring
    plan: 01
    provides: BurnConfig/TrustedPeerRegistry (quorum-signed CRDT values, majority-floor validation)
provides:
  - "TransactionManager caches burn-rate in an atomic (burn_basis_points_), refreshed via BurnConfig::RegisterRefreshCallback -- no CRDT read on PayEscrow's hot path"
  - "GeniusNode constructs SecureCrdt -> TrustedPeerRegistry -> BurnConfig -> TransactionManager in that order inside INITIALIZING_TRANSACTIONS, halting startup on D-07 majority-floor construction failure"
  - "sgns_config.json carries trusted_peer_quorum_threshold/burn_config_quorum_threshold, defaulting to the majority floor when unset"
  - "genius_node/genius_node_test build with BurnConfig.cpp + trustedpeer/securecrdt linked"
  - "End-to-end BURN-03 regression test: a real GeniusNode reaching READY has the default 1% burn rate"
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Forward-declare CRDT-adjacent types in TransactionManager.hpp; only the .cpp includes account/BurnConfig.hpp -- keeps the hot-path header decoupled from quorum/CRDT machinery"
    - "Weak-capture refresh-callback idiom (same as existing RegisterNewElementCallback registrations in TransactionManager) reused to bridge BurnConfig's callback into an atomic store"
    - "Unset quorum-threshold config fields default to the majority floor (ceil(0.51*N)) for the parsed genesis peer count, never a magic low number, so a missing field can never trip D-07 by surprise"

key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - src/account/CMakeLists.txt
    - example/node_test/sgns_config.json
    - test/src/blockchain/node_startup_test.cpp

key-decisions:
  - "SecureCrdt/TrustedPeerRegistry/BurnConfig share the same pubsub topic TransactionManager already listens on for full nodes (GNUS_FULL_NODES_TOPIC), since trusted quorum signers are full nodes -- avoids introducing a new topic/transport"
  - "Combined Task 2 (construction) and Task 3 (wiring the trailing New() args) into logically separate commits even though they touch the same case block, per the plan's task-atomicity intent"

requirements-completed: [BURN-02, BURN-03]

duration: single session
completed: 2026-07-27
---

# Phase 11 Plan 02: BurnConfig/TrustedPeerRegistry Wired into GeniusNode + TransactionManager Summary

**TransactionManager's hardcoded BURN_BASIS_POINTS constant replaced by a cached atomic refreshed via BurnConfig's quorum-signed CRDT value; GeniusNode now constructs SecureCrdt->TrustedPeerRegistry->BurnConfig before TransactionManager at startup, halting cleanly on any majority-floor construction failure.**

## Performance

- **Tasks:** 4 completed
- **Files modified:** 7 (no new files -- all extensions of Plan 01's shipped types)

## Accomplishments

- `TransactionManager::BURN_BASIS_POINTS` renamed to `BURN_BASIS_POINTS_DEFAULT` (pre-quorum/genesis-absent fallback only); a new `std::atomic<uint64_t> burn_basis_points_` member is the live source `PayEscrow` reads, with zero CRDT access on that call path.
- `TransactionManager::New()` extended with two trailing default-valued params (`initial_burn_basis_points`, `burn_config`); when a `BurnConfig` is supplied, `New()` registers a weak-capture refresh callback that stores new quorum-confirmed values into the atomic.
- `TransactionManager.hpp` only forward-declares `sgns::account::BurnConfig` — no CRDT-adjacent header leaks into the hot-path header (verified via grep, matches T-11-08's accepted-by-design mitigation).
- `GeniusNode` parses two new `sgns_config.json` fields (`trusted_peer_quorum_threshold`, `burn_config_quorum_threshold`), defaulting unset (0) values to the majority floor for the parsed genesis peer count (T-11-06).
- `GeniusNode::StateTransition(INITIALIZING_TRANSACTIONS)` now constructs `SecureCrdt` → `TrustedPeerRegistry` → `BurnConfig` before `TransactionManager::New`, sharing `TransactionManager::GNUS_FULL_NODES_TOPIC` as the propagation topic for proposals/signatures. Any D-07 majority-floor construction failure logs an error and returns immediately, mirroring the existing `!blockchain_` guard (T-11-07).
- `TransactionManager::New`'s call site now threads `burn_config_->GetCachedBasisPoints()` and `burn_config_` as trailing args — the actual live-wiring line connecting Plan 01's `BurnConfig` to `TransactionManager`'s cached burn rate.
- `src/account/CMakeLists.txt`: `BurnConfig.cpp` added to `GENIUS_NODE_SOURCES`; `trustedpeer` (transitively pulling `securecrdt`) linked into `GENIUS_NODE_LIBS`'s `PUBLIC` section.
- `test/src/blockchain/node_startup_test.cpp`: new `GenesisNodeDefaultBurnRateIsOnePercent` test brings up a real `GeniusNode` through the full `INITIALIZING_TRANSACTIONS` construction path and asserts `GetBurnBasisPoints() == 100` / `GetBasisPointsTotal() == 10000` — the phase's only end-to-end regression check against the shared production startup path (BURN-03).
- `example/node_test/sgns_config.json` gained both new threshold fields (value `2`, matching its 2-entry `trusted_peers` list's majority floor).

## Task Commits

1. **Task 1: TransactionManager cached burn-rate member + New() signature extension** - `5d621f78` (feat)
2. **Task 2: GeniusNode config fields + object construction with failure handling** - `174c26dd` (feat)
3. **Task 3: Wire constructed BurnConfig into TransactionManager::New call site** - `bc6d52cd` (feat)
4. **Task 4: CMake linkage + real GeniusNode startup end-to-end regression** - `a15ca519` (feat)

## Files Created/Modified

- `src/account/TransactionManager.hpp` - `BURN_BASIS_POINTS_DEFAULT` rename, `burn_basis_points_` atomic member, extended `New()` signature, forward-declared `sgns::account::BurnConfig`
- `src/account/TransactionManager.cpp` - constructor/New() threading of new params, weak-capture refresh-callback registration, `PayEscrow` reads the atomic instead of the constant
- `src/account/GeniusNode.hpp` - forward declarations (`SecureCrdt`, `TrustedPeerRegistry`, `BurnConfig`), two new quorum-threshold members, three new object members, `GetBurnBasisPoints()` updated to the renamed constant
- `src/account/GeniusNode.cpp` - config parsing + majority-floor defaulting, `INITIALIZING_TRANSACTIONS` construction sequence + failure handling, `TransactionManager::New` call site wiring
- `src/account/CMakeLists.txt` - `BurnConfig.cpp` added to `GENIUS_NODE_SOURCES`; `trustedpeer` linked into `GENIUS_NODE_LIBS`
- `example/node_test/sgns_config.json` - two new example config fields
- `test/src/blockchain/node_startup_test.cpp` - new BURN-03 end-to-end regression test case

## Decisions Made

- Reused `TransactionManager::GNUS_FULL_NODES_TOPIC` as the shared pubsub topic for `SecureCrdt`/`TrustedPeerRegistry`/`BurnConfig` propagation, since trusted quorum signers are expected to be full nodes and this avoids introducing a new topic/transport (out of scope per the milestone's constraints).
- Kept Task 2 (construction) and Task 3 (wiring the trailing `TransactionManager::New` args) as separate commits even though both touch the `INITIALIZING_TRANSACTIONS` case block, to preserve the plan's intended task-level atomicity and rollback granularity.

## Deviations from Plan

None — plan executed as written. One clarification: the plan's Task 2 action text left the exact `SecureCrdt` topic argument to be "confirmed by reading how tx_globaldb_ was constructed"; `tx_globaldb_` itself has no single fixed topic (topics are added dynamically via `AddListenTopic`/`AddTopicName`), so `TransactionManager::GNUS_FULL_NODES_TOPIC` was chosen as the most direct existing precedent for a topic already understood as "the full/trusted-node channel" and additionally registered via `tx_globaldb_->AddListenTopic()` before `SecureCrdt` construction so puts on that topic are received.

## Issues Encountered

Same limitation as Plan 01: this worktree has no configured CMake build directory (`build/OSX/Release` etc. exist only in the orchestrator's merged main checkout), so this session could only verify structurally (grep-based acceptance criteria per task) — not by actually compiling or running `ctest`. All plan-specified `<source>` acceptance-criteria greps pass; the `<build-command>`/`<test-command>` criteria (`cmake --build . --target genius_node`, `ctest -R node_startup`, full `ctest`) require the orchestrator's real-build follow-up, exactly as documented in 11-01-SUMMARY.md's "Build/Test Verification (orchestrator follow-up)" section.

## Build/Test Verification

**Not run in this worktree** — no configured build directory available. The orchestrator's merge + real-build pass (as it did for Plan 01) should run:
- `cmake --build . --target genius_node` (Task 2/3 acceptance)
- `ctest -R "transaction_manager|burnconfig|trustedpeer|securecrdt"` (no regressions from the `New()` signature extension / `GeniusNode` construction sequence)
- `ctest -R node_startup --output-on-failure` (new BURN-03 end-to-end regression, plus the two pre-existing startup-timing tests)
- `ctest --output-on-failure` (full-suite gate, per this plan's `<verification>` block)

Given Plan 01's precedent (a real, previously-hidden `crdt_set.cpp` callback-timing bug was only caught by an actual build+test run, not structural review), the orchestrator's real-build pass is the load-bearing verification step for this plan's correctness claims, not this session's grep-based checks.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- BURN-02/BURN-03 are structurally complete: `TransactionManager`'s burn rate is live-wired to `BurnConfig`'s quorum-signed CRDT value, with a clean fallback default (100 basis points) preserved for pre-quorum/unset-config nodes.
- Phase 11 (`burnconfig-quorum-wiring`) has no further plans after this one, per `.planning/STATE.md`'s roadmap snapshot (Plan 2 of 2). The next roadmap phase (12, ValidatorRegistry Migration) depends only on Phase 9 and is unaffected by this plan's changes.
- Recommend the orchestrator's real-build pass runs the full test list above before marking Phase 11 complete, given Plan 01's precedent of catching a real production bug only via actual compilation/execution.

---
*Phase: 11-burnconfig-quorum-wiring*
*Completed: 2026-07-27*
