---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
plan: "16"
subsystem: auth
tags: [networkregistry, securecrdt, ingestfilter, teardown, fail-closed, registry, toctou, regression, cplusplus]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
    plan: "09"
    provides: non-destructive duplicate-New pre-check, RegisterFilters re-run in New, drain-once refresh loop, the 12-case lifecycle regression battery this plan extends
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
    plan: "12"
    provides: live refresh wiring (tx_globaldb_ as NetworkRegistry::New trailing global_db) whose silent degradation G-WR-02 closed
provides:
  - G-WR-01 closure: SecureCrdt::UnregisterFiltersFor(escaped_base_key) + NetworkRegistry teardown removes the GlobalDB ingest element filter it installed (no stale captured-callback leak outliving the policy owner)
  - G-WR-02 closure: RegisterCrdtChangeCallback returns bool; New fails with address_in_use when registration fails (live membership refresh never silently degrades)
  - G-WR-04 closure: SecureCrdtRegistry::RegisterIfAbsent — atomic-detecting insert (find-then-emplace under one lock hold) that can never replace a live policy entry
  - UnregisterIf removal reporting (bool) enabling owner-scoped pattern-keyed teardown
  - IngestFilterPatternFor shared construction helper (install/removal pattern identity)
affects: [phase-15-reverification, securecrdt, trustedpeerregistry, burnconfig, geniusnode-teardown]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Owner-scoped pattern-keyed teardown: pattern-addressed cleanup (filters, callbacks) must be gated on a token-checked removal result, else a failed duplicate constructor's teardown strips the LIVE owner's resource under the same pattern"
    - "Factory failure paths must not rely on destructor-driven cleanup when the object registered itself into a structure that holds a strong reference back to it (self-pin cycle) — explicitly unregister before returning the failure"
    - "Mutation-verify the primitive, not the caller: a regression test that calls an API directly can only be proven non-vacuous by mutating that API's body, not a call site that happens to use it"

key-files:
  created: []
  modified:
    - src/securecrdt/SecureCrdtRegistry.hpp
    - src/securecrdt/SecureCrdt.hpp
    - src/securecrdt/SecureCrdt.cpp
    - src/networkregistry/NetworkRegistry.hpp
    - src/networkregistry/NetworkRegistry.cpp
    - test/src/networkregistry/network_registry_test.cpp

key-decisions:
  - "Filter teardown is token-guarded (removed_own_entry from UnregisterIf) rather than unconditional as the plan sketched: a pattern-keyed removal would let a duplicate-New loser's destructor strip the LIVE registry's ingest filter, re-opening the exact griefing vector G-WR-01/G-WR-04 exist to close; UnregisterIf changed void->bool (all existing callers ignore the result)"
  - "New's failure paths call instance->Unregister() explicitly instead of relying on ~NetworkRegistry: the just-registered policy entry's peer_registry strong capture pins the instance in the SecureCrdtRegistry, so the destructor never runs while the entry lives (applies to the new callback-failure branch AND the pre-existing 15-09 RegisterFilters io_error branch, which had been leaving a zombie policy entry blocking retries)"
  - "UnregisterFiltersFor step ordered LAST in Unregister with idempotent no-ops on re-entry (destruction-reentrancy): analysis shows the filter's entry capture keeps refcount >=1 while installed, so destructor re-entry can only arrive via a later ~NetworkRegistry whose steps all no-op"
  - "RegisterIfAbsent uses find-then-emplace under ONE continuous lock hold (not bare emplace): emplace can then never fail, so the moved entry is never destroyed under the mutex — mirroring Register/UnregisterIf's extract-then-destroy safety"
  - "Install/removal pattern identity enforced by one shared IngestFilterPatternFor helper in SecureCrdt.cpp's anonymous namespace (both RegisterFilters and UnregisterFiltersFor call it — no second construction site to drift)"

patterns-established:
  - "Two-phase ingest-filter regression on the single-node fixture: same attacker proves filter-ACTIVE (unsigned branch write rejected while a probe control replicates) then filter-REMOVED (a distinct-valued second write lands unfiltered) — the distinct value guarantees a novel delta element so the post-teardown push cannot be deduped away"

requirements-completed: [D-06, PNET-NETREG]

# Metrics
duration: 30min
completed: 2026-09-03
---

# Phase 15 Plan 16: Registry Lifecycle Gap Closure (G-WR-01/02/04) Summary

**Teardown removes the GlobalDB ingest element filter it installed (owner-token-guarded), callback-registration failure fails NetworkRegistry::New with address_in_use, and SecureCrdtRegistry gains an atomic RegisterIfAbsent that can never replace a live policy entry — each closed by a named, mutation-verified regression**

## Performance

- **Duration:** ~30 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T18:08:14Z
- **Completed:** 2026-09-03T18:38:04Z
- **Tasks:** 2/2
- **Files modified:** 6 (0 created)

## Accomplishments

- **G-WR-01 (stale ingest filter):** `SecureCrdt::UnregisterFiltersFor(escaped_base_key)` removes the per-pattern GlobalDB element filter via the same `IngestFilterPatternFor` construction helper `RegisterFilters` uses (byte-identical pattern, single construction site). `NetworkRegistry::Unregister` runs it as Step 4 — LAST, after policy entry (UnregisterIf), refresh-thread stop/join, and change-callback removal — with removal of a missing pattern a verified no-op (`CRDTDataFilter::UnregisterElementFilter` is an erase-remove). GeniusNode teardown picked this up with no GeniusNode edit (`ShutdownNodePolicyServices` already calls `Unregister()`).
- **G-WR-02 (silent live-refresh degradation):** `RegisterCrdtChangeCallback` now returns bool; `New` treats a false return as `outcome::failure(std::errc::address_in_use)` with an error naming the pattern — a node either runs live-refreshing membership or fails closed.
- **G-WR-04 (duplicate-New TOCTOU):** `SecureCrdtRegistry::RegisterIfAbsent` inserts via find-then-emplace under one continuous `registry_mutex_` hold — no replace path, no destruction under the lock. `RegisterSignerSetSource` uses it; the Resolve pre-check in `New` remains as the descriptive fast path.
- **Regression proofs (3 new cases, all mutation-verified):**
  - `TeardownRemovesIngestFilter` — attacker node over loopback libp2p proves the filter ACTIVE (unsigned branch write rejected, probe control replicates), then after `Unregister()` a distinct-valued second unsigned write LANDS unfiltered (branch now an unmanaged key).
  - `CallbackRegistrationFailureFailsNew` — occupied change-callback pattern → `New` fails `address_in_use`, `Resolve(base_key)` returns none (no half-constructed state), and a retry succeeds once the pattern is freed.
  - `RegisterIfAbsentDoesNotReplaceLiveEntry` — refused insert for a live pattern (original `owner_token` still resolves) + a fresh-pattern positive control proving the refusal is not vacuous.
- **Full battery green:** network_registry_test 18/18 (15 fixture: 12 prior + 3 new; plus 3 payload cases); private_network_registry_binding (GeniusNode teardown scene) green against freshly relinked binaries; guard suites network_membership_filter, network_config_private_network, burnconfig x2, securecrdt x7, trustedpeerregistry x3 all green — zero regressions.

## Task Commits

Each task was committed atomically:

1. **Task 1: Fail-closed callback registration + atomic RegisterIfAbsent** - `3d6ed29d` (fix)
2. **Task 2: Teardown removes ingest filter + lifecycle regressions** - `1138acbc` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/securecrdt/SecureCrdtRegistry.hpp` - `RegisterIfAbsent` (find-then-emplace, no replace, no destruction under lock); `UnregisterIf` now returns bool (true = this call removed the entry; all existing callers ignore it — source-compatible)
- `src/securecrdt/SecureCrdt.hpp` - `UnregisterFiltersFor(escaped_base_key)` public declaration with the teardown-order contract
- `src/securecrdt/SecureCrdt.cpp` - file-local `IngestFilterPatternFor` shared construction helper (used by `RegisterFilters` install AND `UnregisterFiltersFor` removal); `UnregisterFiltersFor` implementation
- `src/networkregistry/NetworkRegistry.hpp` - `RegisterCrdtChangeCallback` returns bool; `New`/`Unregister` contracts document the new failure mode and teardown steps
- `src/networkregistry/NetworkRegistry.cpp` - `RegisterSignerSetSource` → `RegisterIfAbsent`; fail-closed callback branch + explicit `Unregister()` on BOTH failure branches; `Unregister` restructured into 4 ordered steps with the token-guarded filter removal last
- `test/src/networkregistry/network_registry_test.cpp` - `EscapeRegexForTest` mirror helper + 3 new TEST_F cases (10/11/12)

## Decisions Made

See key-decisions. The two substantive interpretation calls beyond the plan's letter:

1. **Token guard on filter removal** — the plan's literal step ("append `secure_crdt_->UnregisterFiltersFor(...)` if secure_crdt_") would have introduced a new bug: `UnregisterFiltersFor` is pattern-keyed, and the duplicate-New loser's destructor (or a stale teardown) would remove the LIVE registry's filter. Gating on `UnregisterIf`'s removal result keeps the plan's T-15-16-01/T-15-16-05 mitigations and adds T-15-16-04-consistent ownership scoping.
2. **Explicit failure-path cleanup** — the plan asserted "~NetworkRegistry -> Unregister removes the just-registered policy entry"; verifying that assumption exposed the self-pin cycle (entry.peer_registry strong capture), so the cleanup is explicit in `New`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's destructor-driven failure-path cleanup cannot run (self-pin cycle)**
- **Found during:** Task 1 (verifying the plan's mandated failure-path analysis)
- **Issue:** The plan requires that on callback-registration failure "the instance shared_ptr is dropped, ~NetworkRegistry -> Unregister removes the just-registered policy entry". But `RegisterSignerSetSource` stores `entry.peer_registry = shared_from_this()` — the SecureCrdtRegistry entry holds a strong reference back to the instance, so dropping `New`'s reference never runs the destructor while the entry lives. The failed construction would leave a live zombie policy entry that blocks every retry with `address_in_use`.
- **Fix:** `New` calls `instance->Unregister()` explicitly before returning failure — on the new callback-failure branch AND on the pre-existing 15-09 `RegisterFilters` io_error branch (same defect class, same function).
- **Files modified:** src/networkregistry/NetworkRegistry.cpp
- **Verification:** `CallbackRegistrationFailureFailsNew` asserts `Resolve(base_key)` returns none after the failed attempt and the retry succeeds once the pattern is free
- **Committed in:** 3d6ed29d (branch), 1138acbc (io_error branch comment context)

**2. [Rule 1 - Bug] Unconditional pattern-keyed filter teardown would strip a LIVE registry's filter**
- **Found during:** Task 2 design (destruction-reentrancy analysis)
- **Issue:** `UnregisterFiltersFor` addresses the filter by pattern, not owner. A duplicate-`New` loser (whose teardown runs via destructor) or any teardown racing a newer owner would remove the live registry's ingest filter — silently re-opening the unsigned-base-element griefing vector this plan exists to close.
- **Fix:** `SecureCrdtRegistry::UnregisterIf` changed void→bool (reports whether THIS call removed the entry; every existing caller ignores the result). `NetworkRegistry::Unregister` removes the filter only when `removed_own_entry` is true.
- **Files modified:** src/securecrdt/SecureCrdtRegistry.hpp, src/networkregistry/NetworkRegistry.cpp
- **Verification:** `DuplicateNewDoesNotClobberLiveRegistry` + `RegisterIfAbsentDoesNotReplaceLiveEntry` green; the live entry (and now its filter) survives refused registrations
- **Committed in:** 3d6ed29d (bool return), 1138acbc (guard + Step 4)

**3. [Rule 3 - Blocking] First mutation run for the RegisterIfAbsent regression was vacuous**
- **Found during:** Task 2 mutation verification
- **Issue:** Mutating the CALLER (`RegisterSignerSetSource` → plain `Register`) left `RegisterIfAbsentDoesNotReplaceLiveEntry` green — the test exercises the registry primitive directly, so a caller-side mutation cannot redden it.
- **Fix:** Re-ran the mutation round against the primitive itself (`RegisterIfAbsent` body → `insert_or_assign` replace semantics): the test then failed exactly at the `owner_token` equality assertion. Both files restored byte-exact from backups (`diff` clean) and re-verified green.
- **Files modified:** none (verification-process deviation only)
- **Verification:** mutation log in Verification Evidence below
- **Committed in:** n/a

---

**Total deviations:** 3 auto-fixed (2 bugs avoided in the plan's literal mechanism, 1 verification-process correction)
**Impact on plan:** All must_have truths and artifacts hold with stronger semantics than the plan's letter; no scope creep; no new dependencies; nothing under 3rdparty/ or thirdparty/ touched; no GeniusNode.cpp edit (as the plan required).

## Verification Evidence

- `ninja -C build/OSX/Release securecrdt networkregistry genius_node` + full `ninja -C build/OSX/Release` — clean (one pre-existing GeniusNode switch-warning, unrelated)
- `ctest -R "^network_registry_test$"` — Passed: 18/18 cases (`--gtest_list_tests` shows the 3 new: TeardownRemovesIngestFilter, CallbackRegistrationFailureFailsNew, RegisterIfAbsentDoesNotReplaceLiveEntry)
- `ctest -R "^private_network_registry_binding_test$"` — Passed against a freshly relinked binary (initially caught a STALE binary predating the new libs — rebuilt first; GeniusNode teardown → Unregister → filter removal, no crash)
- Guard battery: network_membership_filter_test, network_config_private_network_test, burnconfig_test, burnconfig_policy_e2e_test, securecrdt_interface/registry/candidate/candidate_race/quorum_gate/propose_sign_quorum/quorum_contract_e2e, trustedpeerregistry_genesis/quorum/threshold_floor — all green
- Source-order assertion (Task 2 criterion): inside `NetworkRegistry::Unregister`, `UnregisterNewElementCallback` at relative line 27 precedes `UnregisterFiltersFor` at relative line 40 — filter removal LAST
- Pattern-identity assertion: `IngestFilterPatternFor` defined once (SecureCrdt.cpp:80), called by `RegisterFilters` (:676) and `UnregisterFiltersFor` (:720)
- Mutation verification (fix reverted → specific test failure → restored byte-exact → green):
  - G-WR-01 (Step 4 commented out): TeardownRemovesIngestFilter FAILED — post-teardown unsigned write never landed (filter still active)
  - G-WR-02 (failure branch → old warn-and-continue): CallbackRegistrationFailureFailsNew FAILED — New succeeded with a dead refresh
  - G-WR-04 (RegisterIfAbsent body → insert_or_assign): RegisterIfAbsentDoesNotReplaceLiveEntry FAILED at `owner_token` equality (0x16bc25fac vs 0x12b65e8ac — entry replaced)

## Notes for Downstream Plans

- **Phase-15 re-verification:** G-WR-01/G-WR-02/G-WR-04 now have named, runnable, mutation-verified closures in network_registry_test; the 15-REVERIFICATION anti-pattern rows for NetworkRegistry.cpp:476-481 (silent degradation) and :375-400 (TOCTOU) are addressed at the mechanism level.
- **UnregisterIf bool return** is a small public-surface change to SecureCrdtRegistry (src/securecrdt + trustedpeer + burnconfig callers recompiled and green); future pattern-keyed teardowns should use the same removal-reporting pattern.
- **The self-pin cycle is general:** any factory that registers `shared_from_this()` into a structure holding strong references (SecureCrdtRegistry entries, element-filter lambdas capturing entries BY VALUE) must explicitly unregister on failure paths — destructor-driven cleanup is unreachable while pinned. The remaining accepted pin case (WR-05, filter lambda pinning a live registry whose owner dropped all refs) is unchanged and still deferred.
- **15-11 expiry-scene interplay:** the 15-11 direct-ctor (unregistered) registries never installed a filter, so the token guard correctly leaves their scenes untouched (network_membership_filter_test green).

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 6 modified source/test files exist on disk; SUMMARY.md created
- Commits verified in git log: 3d6ed29d (fix), 1138acbc (test)
- No file deletions in either task commit (diff-filter=D empty for both)
- No new untracked files from this run (pre-existing snapshot untracked set unchanged; mutation backups lived in /tmp)
- `ctest -R network_registry_test`: 18/18; binding + 15-suite guard battery green

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-*
*Completed: 2026-09-03*
