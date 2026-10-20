---
phase: 21-consensus-trust-mint-authorization
plan: "01"
subsystem: auth
tags: [validator-registry, trust, authorization, cid, crdt, consensus, outcome]

# Dependency graph
requires:
  - phase: 15-private-network-binding
    provides: instance-scoped ValidatorRegistry CRDT keys/topics and the verified registry-update filter (FilterRegistryUpdate) this plan reuses
provides:
  - LoadVerifiedRegistryByCid — authorization-verifying registry CID loader (chain-walk to genesis authority)
  - LoadUnverifiedRegistryByCid — UNTRUSTED raw loader (verification internals + grandfathered path only)
  - Deprecated inline alias LoadRegistryByCid (kept green for Consensus.cpp:1474/:2504/:2600/:2846/:3513 until 21-04 migrates callers)
  - VerifyRegistryChainClassified pure static helper + VerifiedRegistryMemo (cap 64, oldest-first eviction)
  - VerifyGenesisAuthoritySignature — cache-independent genesis-root termination check
  - Strict internal loads at TryCreateAndSubmitBatchProposal / EvaluateBatchSubject / HandleBatchCertificate
affects: [21-04-consensus-consumption, 21-02, 21-03, TRUST-02-audit]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Chain-walk authorization: classify every prev-registry hop to a genesis-anchored root; verdict-to-errc mapping (kInvalid→permission_denied, kMissingDependency→resource_unavailable_try_again)"
    - "Bounded FIFO memo with single-oldest eviction at cap (never wholesale clear), inserted ancestor-first"
    - "Recursion guard: verification internals fetch base registries through the raw loader only"

key-files:
  created:
    - test/src/blockchain/validator_registry_verified_load_test.cpp
  modified:
    - src/blockchain/ValidatorRegistry.hpp
    - src/blockchain/ValidatorRegistry.cpp
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "Verified loader reuses VerifyUpdateClassified(enforce_time_window=false) per hop; genesis-root termination uses a cache-independent genesis-authority signature check because VerifyUpdateClassified's genesis branch only runs while no cached registry exists"
  - "Verified-CID memo stores cid→Registry (not a bare set) so a memo hit returns the snapshot with zero fetches; inserted ancestor-first so FIFO eviction drops oldest chain positions and the head survives longest"
  - "permission_denied takes each strict site's existing Reject/abort path; retryable errors keep the Pending/Stalled/failure paths — behavior-preserving for honest data"
  - "TRUST-01 NOT marked complete: this plan is the first half (registry-side); consensus-side consumption migrates Consensus.cpp callers in 21-04"

patterns-established:
  - "Adversarial+compat unit pairs for chain decisions via injectable fetch/classify callables (no GlobalDB) — extends the EvaluateRegularPromotionStatic seam to authorization walks"

requirements-completed: []  # TRUST-01 intentionally NOT marked complete — closes with plan 21-04 (Consensus.cpp caller migration)

# Metrics
duration: 31min
completed: 2026-10-20
---

# Phase 21 Plan 01: Verified Registry CID Loader Summary

**Chain-walking CID registry loader that authorizes against the genesis authority (forged/self-certified chains → permission_denied, unsynced ancestors → retryable), with a bounded verified-CID memo and the raw loader quarantined for verification internals**

## Performance

- **Duration:** 31 min
- **Started:** 2026-10-20T18:56:52Z
- **Completed:** 2026-10-20T19:27:31Z
- **Tasks:** 2 (TDD: RED → GREEN for Task 1, completion commit for Task 2)
- **Files modified:** 4

## Accomplishments
- `LoadVerifiedRegistryByCid` walks the prev-registry chain, classifying each hop with `VerifyUpdateClassified(update, false)` down to a genesis-anchored root (empty prev + genesis-authority signature) or a memoized-verified ancestor — closing the raw-DAG-read bypass (finding #1 / T-21-0101) on the registry's own consensus-critical loads.
- Distinct errc semantics: forged/self-certified chain → `std::errc::permission_denied` (permanent, unauthorized); unsynced ancestor or `kMissingDependency` → `std::errc::resource_unavailable_try_again` (retryable — ordering problem, never an authorization failure; T-21-0102).
- `LoadUnverifiedRegistryByCid` (UNTRUSTED-tagged Doxygen) holds the moved raw body; `VerifyUpdateClassified`'s base fetch uses it (recursion guard, Pitfall 2 / T-21-0104); `LoadRegistryByCid` remains only as a deprecated inline alias so Consensus.cpp and tests stay green for 21-04's migration.
- Three internal consensus-critical loads (`TryCreateAndSubmitBatchProposal`, `EvaluateBatchSubject`, `HandleBatchCertificate`) now use the verified loader; `permission_denied` takes each site's Reject/abort path.
- 10 unit tests (adversarial + compat + memo + determinism) exercising the pure static `VerifyRegistryChainClassified` helper with counting in-memory fetch callables — no GlobalDB, no node.

## Task Commits

Each task was committed atomically:

1. **Task 1: Raw core + verified loader with memoized chain-walk (tdd)** - `eb15d78fb` (test: RED — 8 failing cases against the not-yet-existing API) + `4f252707a` (feat: GREEN — loaders, walk, memo, genesis check, strict sites)
2. **Task 2: Unit adversarial+compat pairs for the chain walk** - `74234e126` (test: fetch-count negative control, determinism pair, fixture/convention cleanup)

**Plan metadata:** (this commit)

## Files Created/Modified
- `src/blockchain/ValidatorRegistry.hpp` — `LoadVerifiedRegistryByCid`, `LoadUnverifiedRegistryByCid`, deprecated inline `LoadRegistryByCid` alias, `VerifiedRegistryMemo` (public nested class), `VerifyRegistryChainClassified` (public static), `kMaxVerifiedRegistryMemoEntries = 64`, private raw update-fetch + genesis-signature declarations, `mutable verified_registry_memo_` member.
- `src/blockchain/ValidatorRegistry.cpp` — moved raw body into private `LoadUnverifiedRegistryUpdateByCid` (verbatim element-scan loop, returns the update so the walk can read prev links); chain walk + memo implementation; genesis branch of `VerifyUpdateClassified` delegated to `VerifyGenesisAuthoritySignature` (behavior-preserving extraction); base fetch at the former :1581 site switched to the raw loader; three strict internal sites.
- `test/src/blockchain/validator_registry_verified_load_test.cpp` — 10 tests: forged self-certified chain, cyclic forged chain, mid-chain non-quorum (all → permission_denied); unfetchable ancestor, kMissingDependency (→ retryable, asserted NOT permission_denied); genesis-terminated honest chain (head snapshot returned, all CIDs memoized); memo zero-fetch termination (head and ancestor); cap eviction oldest-first single-entry (just-inserted survives, re-insert evicts next-oldest); first-walk exact fetch count; cross-walk determinism.
- `test/src/blockchain/CMakeLists.txt` — `addtest(validator_registry_verified_load_test ...)` linking `blockchain_genesis` only; `consensus_certificate_test` remains commented out per planner disposition.

## Decisions Made
- Genesis-root termination is cache-independent: extracted the :1591-1610 genesis-signature loop into `VerifyGenesisAuthoritySignature` and the walk's classifier uses it for empty-prev updates (see Deviation 2 for why `VerifyUpdateClassified` alone cannot root a walk on a synced node).
- Memo maps CID → Registry snapshot (plan said "mutable set"): a memo hit must return the registry with zero fetches — the plan's own memo test contract (case 5) — and the loader's return type demands a snapshot without a re-fetch.
- Memoization order is ancestor-first: FIFO eviction then drops the oldest chain positions while the most recently verified head (the CID consensus re-loads) survives longest.
- Memo has its own dedicated mutex (short-scoped, never held across fetch/classify) rather than `cache_mutex_`: the walk calls `VerifyUpdateClassified`, which takes `cache_mutex_` internally — holding it across the walk would deadlock on the non-recursive `std::shared_mutex`.
- `[[deprecated]]` attribute intentionally NOT used (Doxygen only): the attribute would emit warnings at the 7 remaining alias call sites and violate the no-new-warnings build gate.
- TRUST-01 stays open — see Requirements Status.

## Requirements Status

`requirements: [TRUST-01]` in the plan frontmatter is intentionally NOT marked complete. This plan delivered the registry-side half (verified loader + strict internal loads); the requirement's wording — "a registry loaded by CID **for certificate vote tallying** is authorization-verified" — closes when plan 21-04 migrates the five `Consensus.cpp` raw call sites (:1474/:2504/:2600/:2846/:3513 + `LoadRegistryByCidCached`) to the verified loader. Marking it complete now would falsify the REQUIREMENTS.md traceability row.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical] Cycle guard in the chain walk**
- **Found during:** Task 1 (GREEN)
- **Issue:** The plan's walk terminates at genesis or a memoized ancestor, but a forged cyclic chain (A.prev=B, B.prev=A) self-certifies kValid at every hop and never terminates — infinite loop / DoS (T-21-0102 adjacent).
- **Fix:** `visited_cids` set inside one walk; revisiting a CID → `permission_denied` (a cyclic chain cannot be genesis-rooted).
- **Files modified:** src/blockchain/ValidatorRegistry.cpp
- **Verification:** `CyclicForgedChainIsUnauthorized` unit test (walk terminates, verdict unauthorized, memo untouched).
- **Committed in:** 4f252707a

**2. [Rule 1 - Bug] Genesis root misclassification on cache-initialized nodes**
- **Found during:** Task 1 (GREEN design review before wiring)
- **Issue:** `VerifyUpdateClassified` runs its genesis branch only when NO cached registry exists; on any synced node the honest genesis root (empty prev) classifies kInvalid, so a full honest walk to genesis would return permission_denied — breaking the strict sites for exactly the data they must keep serving.
- **Fix:** Behavior-preserving extraction of the genesis-signature loop into `VerifyGenesisAuthoritySignature(update, signing_bytes)`; `VerifyUpdateClassified`'s genesis branch delegates to it (unchanged semantics), and the walk's classify callable uses it directly for empty-prev updates, independent of cache state.
- **Files modified:** src/blockchain/ValidatorRegistry.cpp (+ header declaration)
- **Verification:** Existing registry tests green; multi-node smoke + multi_account_test (311s) + policy_lifetime + blockchain_genesis_test green; unit `GenesisTerminatedHonestChainLoads`.
- **Committed in:** 4f252707a

**3. [Rule 3 - Blocking] Static helper takes the head CID, not a pre-fetched update**
- **Found during:** Task 1 (RED authoring)
- **Issue:** Plan action 3 sketched the helper as "taking the update"; a pre-fetched head makes the plan's own memo contract impossible ("CID present in the verified memo terminates the walk **without further fetches**" / case 5 "zero additional fetch calls") because the head fetch would already have happened.
- **Fix:** `VerifyRegistryChainClassified(cid, fetch_update, classify_update, memo)` — memo check precedes the first fetch inside the walk.
- **Files modified:** src/blockchain/ValidatorRegistry.hpp/.cpp
- **Verification:** `MemoizedCidTerminatesWalkWithoutFetches` (fetch counter asserted 0 on second load, 1 for a memoized-ancestor extension).
- **Committed in:** eb15d78fb / 4f252707a

**4. [Rule 3 - Blocking] Raw core split into a private update-level fetch**
- **Found during:** Task 1 (GREEN)
- **Issue:** Plan action 1 said move the old body verbatim into `LoadUnverifiedRegistryByCid` (returns `Registry`), but the walk needs the full `RegistryUpdate` (prev link, signatures) at each hop; duplicating the element-scan loop would violate the one-home DRY invariant.
- **Fix:** Verbatim loop lives in private `LoadUnverifiedRegistryUpdateByCid` (returns the update); public `LoadUnverifiedRegistryByCid` wraps it (`.registry()`); `:1581` base fetch and the walk both use the raw core. One scan loop, three consumers.
- **Files modified:** src/blockchain/ValidatorRegistry.cpp
- **Verification:** Acceptance greps: base fetch uses `LoadUnverifiedRegistryByCid`; zero `LoadRegistryByCid(` matches remain in the .cpp.
- **Committed in:** 4f252707a

**5. [Rule 3 - Blocking] CMake reconfigure + untested-target discovery**
- **Found during:** Task 1 (RED build)
- **Issue:** New target invisible until re-configure (build root is `build/OSX/CMakeLists.txt`, not the repo root); `validator_registry_scope_test` had never been built in this tree ("Not Run") — pre-existing, not caused by this plan.
- **Fix:** `cmake -S build/OSX -B build/OSX/Release`; built the missing target and re-ran the suite (5/5).
- **Verification:** ctest 5/5 validator_registry targets.
- **Committed in:** (build-tree only; no source change)

---

**Total deviations:** 5 auto-fixed (1 missing-critical, 1 bug, 3 blocking/test-shape)
**Impact on plan:** All fixes were required for the plan's own contracts (memo zero-fetch, honest-chain compat, termination) — no scope creep.

## Issues Encountered
- clang-tidy cannot consume this tree's `compile_commands.json` directly (dual `-arch x86_64 -arm64` compile commands → "expected exactly one compiler job"). Worked around with a filtered single-arch compile database at /tmp/sgns-tidy-db. New-code regions are clean of Required-group findings; the file carries thousands of pre-existing Recommended-group warnings (scoped_lock, const-correctness, naming) that predate this plan and stay out of scope, as do pre-existing `PriceHttpClient.cpp` -Wmissing-field-initializers warnings surfaced by rebuilding dependents.
- Full `multi_account_test` takes ~311s — allowed it to run to completion for the honest-path E2E proof rather than relying on unit tests alone.

## Verification

- `cmake --build build/OSX/Release -j --target blockchain_genesis` — clean, no new warnings (changed files warning-free).
- `ctest -R "validator_registry"` — 5/5 passed (slot_quorum, promotion, **verified_load (new)**, scope, batch_slot).
- Regression: `blockchain_genesis_test`, `multi_node_finality_fault_compatibility_smoke_test`, `multi_account_test`, `policy_lifetime_multi_account_test` — 4/4 passed (real nodes through the strict sites; raw alias callers).
- clang-format applied to all changed files; clang-tidy clean on new-code regions (see Issues Encountered for methodology).

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all paths are wired to the real verifier; no placeholder data.

## Threat Flags
None - all four `mitigate` dispositions from the plan's threat register are implemented and unit-proven (T-21-0101 verified walk; T-21-0102 retryable mapping + bounded memo; T-21-0103 post-verified-only insertion, single-oldest eviction; T-21-0104 raw base fetch). No new security surface beyond the plan's model.

## Next Phase Readiness
- 21-04 (Consensus-side consumption) can migrate `Consensus.cpp:1474/:2504/:2600/:2846/:3513` + `LoadRegistryByCidCached` to `LoadVerifiedRegistryByCid`, then delete the deprecated alias and the two remaining test callers (`multi_account_sync.cpp:503/:511` expectations will need updating: unauthorized loads now return permission_denied instead of succeeding raw).
- The `VerifiedRegistryMemo` cap-64 finding from RESEARCH Pitfall 3 also applies to `Consensus`'s own `registry_cache_` (clear-all at :2589-2614) — flagged for 21-04's scope, not touched here.

## Self-Check: PASSED

All 4 key files exist on disk; all 3 task commits (eb15d78fb, 4f252707a, 74234e126) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-20*
