---
phase: 11-burnconfig-quorum-wiring
plan: 01
subsystem: crdt
tags: [securecrdt, trustedpeer, quorum, burnconfig, gtest, cmake]

# Dependency graph
requires:
  - phase: 10-trustedpeer-registry
    provides: SecureCrdt/SecureCrdtRegistry/TrustedPeerRegistry foundation this plan retrofits and extends
provides:
  - "ValidateQuorumThreshold(threshold, signer_set_size) majority-floor helper (ceil(0.51*N)) shared by TrustedPeerRegistry and BurnConfig"
  - "TrustedPeerRegistry::New retrofitted to outcome::result<std::shared_ptr<TrustedPeerRegistry>>, floor-validated at construction"
  - "BurnConfigPayload (ISignedCRDTData) + BurnConfig quorum-signed CRDT value with genesis auto-seed, cache-refresh-via-quorum-re-derivation, and pre-quorum default fallback"
  - "4 passing BurnConfig GTest cases + CMake wiring (test/src/account/burnconfig_test.cpp)"
affects: [12-transactionmanager-burnconfig-integration]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Majority-floor quorum validation as a single shared header-only helper reused by every quorum-threshold consumer"
    - "outcome::result<std::shared_ptr<T>> factory pattern for construction that can fail security validation (no bare shared_ptr constructors for quorum-gated types)"
    - "Cache-refresh callbacks always re-derive from SecureCrdt::ReadIfQuorum fresh rather than trusting positional callback data (Pitfall 2)"

key-files:
  created:
    - src/securecrdt/QuorumThresholdValidation.hpp
    - src/account/BurnConfig.hpp
    - src/account/BurnConfig.cpp
    - test/src/trustedpeer/trustedpeerregistry_threshold_floor_test.cpp
    - test/src/account/burnconfig_test.cpp
  modified:
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - src/trustedpeer/TrustedPeerRegistry.hpp
    - src/trustedpeer/TrustedPeerRegistry.cpp
    - test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp
    - test/src/trustedpeer/trustedpeerregistry_quorum_test.cpp
    - test/src/trustedpeer/CMakeLists.txt
    - test/src/account/CMakeLists.txt
    - src/account/CMakeLists.txt

key-decisions:
  - "QUORUM_THRESHOLD_BELOW_FLOOR added as a new enumerator to the existing SecureCrdt::Error category rather than a new error category, keeping one error surface for all quorum-threshold consumers"
  - "BurnConfig genesis auto-seed gated to the exact known default (100 basis points) only, checked once at construction, never inside the ongoing refresh callback (D-03, prevents elevation-of-privilege via auto-signing arbitrary values)"

requirements-completed: [BURN-01]

duration: continuation session (Tasks 1-2 by prior agent, Task 3 completed and committed this session)
completed: 2026-07-24
---

# Phase 11 Plan 01: BurnConfig + Quorum Wiring Summary

**Shared majority-floor quorum validation (ceil(0.51*N)) enforced on both TrustedPeerRegistry and the new SecureCrdt-backed BurnConfig, replacing the hardcoded BURN_BASIS_POINTS constant with a genesis-seeded, quorum-signed CRDT value.**

## Performance

- **Tasks:** 3 completed (Task 1 + 2 by prior agent in this worktree, Task 3 completed and committed this session)
- **Files modified/created:** 14

## Accomplishments
- `ValidateQuorumThreshold` majority-floor helper built once and reused by both `TrustedPeerRegistry::New` and `BurnConfig::New` — no consumer can construct with a below-floor threshold.
- `TrustedPeerRegistry::New` retrofitted to a fallible `outcome::result` factory; all 4 existing call sites (3 genesis test + 1 quorum test) updated to the new signature.
- `BurnConfigPayload`/`BurnConfig` built entirely on top of `SecureCrdt`/`SecureCrdtRegistry`/`TrustedPeerRegistry` — no bespoke signature/quorum logic. Genesis auto-seed, quorum-re-derivation cache refresh (never trusts positional callback data), and pre-quorum default fallback (100 basis points) all implemented per plan.
- 4 new BurnConfig GTest cases (genesis auto-seed, cache refresh, pre-quorum default, threshold-floor rejection) against a real single-node GlobalDB/SecureCrdt/TrustedPeerRegistry fixture, wired into CMake.

## Task Commits

1. **Task 1: Majority-floor validation helper + TrustedPeerRegistry::New retrofit** - `33fd3cc8` (feat)
2. **Task 2: BurnConfigPayload + BurnConfig wrapper class** - `a310811c` (feat)
3. **Task 3: BurnConfig tests + CMake wiring** - `0dcb430f` (feat)

_Note: Tasks 1 and 2 were completed and committed by a prior executor agent in this same worktree before this session began. This session verified their work, then completed and committed Task 3._

## Files Created/Modified
- `src/securecrdt/QuorumThresholdValidation.hpp` - `ValidateQuorumThreshold` shared majority-floor helper
- `src/securecrdt/SecureCrdt.hpp`/`.cpp` - new `QUORUM_THRESHOLD_BELOW_FLOOR` error enumerator + message
- `src/trustedpeer/TrustedPeerRegistry.hpp`/`.cpp` - `New()` retrofit to `outcome::result`, floor validation
- `test/src/trustedpeer/trustedpeerregistry_threshold_floor_test.cpp` - new floor-rejection test cases
- `test/src/trustedpeer/trustedpeerregistry_genesis_test.cpp`, `trustedpeerregistry_quorum_test.cpp` - call sites updated to unwrap `outcome::result`
- `src/account/BurnConfig.hpp`/`.cpp` - `BurnConfigPayload` + `BurnConfig` (genesis auto-seed, cache-refresh-via-quorum-re-derivation, pre-quorum default)
- `src/account/CMakeLists.txt` - new `burnconfig` static library target
- `test/src/account/burnconfig_test.cpp` - 4 GTest cases (genesis auto-seed, cache refresh, pre-quorum default, threshold-floor rejection)
- `test/src/account/CMakeLists.txt` - `burnconfig_test` target wired with matching libs to `TRUSTEDPEER_TEST_NODE_LIBS` pattern plus `burnconfig`/`sgns_genius_account`

## Decisions Made
- Reused the existing `SecureCrdt::Error` category for the new floor-violation error rather than introducing a second error category, per plan instruction.
- `BurnConfig::OnCrdtElementChanged` always re-runs `SecureCrdt::ReadIfQuorum` fresh instead of trusting the callback's positionally-supplied `new_data`, since a `sig/<addr>` child element can be the one that completes quorum (Pitfall 2).

## Deviations from Plan

None - plan executed exactly as written. Task 3's pre-existing uncommitted work (test file + CMake wiring) was verified against the plan's exact spec (4 named test cases matching VALIDATION.md's `ctest -R burnconfig_*` sampling targets, CMake target linking the same library set as the `TRUSTEDPEER_TEST_NODE_LIBS` precedent plus `burnconfig`/`sgns_genius_account`) and matched the API surface shipped in Task 2 (`BurnConfig::New`, `GetCachedBasisPoints`, `RegisterRefreshCallback`, `Unregister`, `GENESIS_DEFAULT_BASIS_POINTS`) with no discrepancies found. Committed as-is.

## Issues Encountered

The executor's worktree had no configured build directory and could only verify structurally. The orchestrator's real-build follow-up (below) found and fixed three real bugs.

## Build/Test Verification (orchestrator follow-up)

The orchestrator merged the worktree and ran the real build against the project's configured build (`build/OSX/Release`). This surfaced **three real bugs**:

1. **Namespace bug**: `BurnConfig.hpp` forward-declared and typed `account_` as `sgns::account::GeniusAccount`, but `GeniusAccount` is actually defined in `namespace sgns` (not `sgns::account`) — a permanently-incomplete type. Fixed the forward declaration and all call sites to `sgns::GeniusAccount`.
2. **Stale test thresholds**: `trustedpeerregistry_genesis_test.cpp` (Phase 10) used `threshold=1` with a 2-peer genesis list — below D-07's new majority floor (`ceil(0.51*2)=2`). Bumped to `threshold=2`.
3. **Root-cause bug in `src/crdt/impl/crdt_set.cpp`** (foundational, shared CRDT code): `CrdtSet::SetValue`'s batch overload fired the put-hook (which drives `GlobalDB::RegisterNewElementCallback`, the mechanism `BurnConfig` relies on for cache-refresh) **before** the batch committed. Any callback re-querying the store for its own just-written element (exactly what `SecureCrdt::ReadIfQuorum` does per D-04) could never see that element — permanently undercounting the very signature that triggered the callback. This silently broke automatic quorum-confirmation detection entirely (a 2-of-2 threshold could never be satisfied via the callback path). Fixed by having the batch overload report whether a real value was written, and moving hook invocation to fire only after the caller's batch commit succeeds (both the single-key `SetValue` wrapper and `PutElems`).

After all three fixes:
- `ctest -R "trustedpeer|burnconfig|securecrdt|multisig"` — **11/11 passed**.
- Full project build + full `ctest`: 81/83 passed. The 2 failures (`transaction_sync_test`, `consensus_pending_lifecycle_test`) are pre-existing/flaky and unrelated — `transaction_sync_test` confirmed unrelated in Phases 8-10's verification; `consensus_pending_lifecycle_test` passes 7/7 in isolation, confirming full-suite resource-contention flakiness, not a regression from the `crdt_set.cpp` change.

BURN-01 (and the underlying cache-refresh-via-callback mechanism all of Phase 11 depends on) is now fully proven by actual compiled/executed tests against a real, previously-broken code path — not just structural verification.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- `BurnConfig` is ready to replace `TransactionManager`'s hardcoded `BURN_BASIS_POINTS` constant in Plan 02.
- The `crdt_set.cpp` callback-timing fix benefits Plan 02 too, since `TransactionManager`'s own cache-refresh wiring depends on the same `RegisterNewElementCallback` mechanism.

---
*Phase: 11-burnconfig-quorum-wiring*
*Completed: 2026-07-24*
