---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
plan: 17
subsystem: security
tags: [fail-closed, teardown, membership-filter, securecrdt, element-filter, weak-ptr-expiry, crdt-ingest]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind- (15-05/15-12)
    provides: NetworkRegistry fail-closed construction, broadcaster membership filter wiring, ShutdownNodePolicyServices teardown hook
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind- (15-09/15-16)
    provides: RegisterFilters idempotence, UnregisterFiltersFor, RegisterIfAbsent teardown semantics
provides:
  - CR-C2-01 closed — every policy-stack failure path (BurnConfig::New, SecureCrdt::RegisterFilters, NetworkRegistry::New in INITIALIZING_TRANSACTIONS) leaves a deny-all membership filter on a private node's still-live GlobalDB instead of restoring public pass-through ingest
  - WR-C2-01 closed — SecureCrdt element filters reject (empty replacement vector) on weak_self expiry, mirroring the candidate filter's reject-on-expiry policy
  - Gated teardown semantics: ShutdownNodePolicyServices(bool global_db_shutdown_follows) — only the destruction route (ShutdownNow follows) clears the filter
affects: [phase-15 re-verification, fail-closed teardown posture, SecureCrdt ingest filters]

# Tech tracking
tech-stack:
  added: []
  patterns:
  - "Gated teardown parameter (global_db_shutdown_follows) distinguishing still-live-GlobalDB failure paths from the destruction route"
  - "Reject-on-expiry symmetry: element and candidate datastore filters share one expiry policy (drop, never pass-through)"
  - "Two-datastore mutation-verified regression: revert the fix branch, prove the EXPECT fails, restore"

key-files:
  created: []
  modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
  - src/securecrdt/SecureCrdt.cpp
  - test/src/account/private_network_registry_binding_test.cpp
  - test/src/networkregistry/network_registry_test.cpp

key-decisions:
  - "Explicit deny-all (MakeBootstrapMembershipFilter({})) over leaving the interim filter installed on failure-path teardown: with the policy stack gone the membership authority is gone, and the explicit deny-all is uniform regardless of which filter was installed"
  - "Element-filter expiry policy mirrors the candidate filter (REJECT with empty replacement vector) — nullopt (ACCEPT) and empty-vector (DROP) are contradictory policies for the same expired-owner condition"
  - "Expiry regression releases the tpr_ pin BEFORE the RegisterFilters re-run: TrustedPeerRegistry pins secure_crdt_ directly (member) and via its registered entry's peer_registry, which a re-run would capture by value into an irremovable db lambda"

patterns-established:
  - "Destruction-route-only clear: a teardown that restores raw/public ingest state is legal only when the datastore stops immediately after (ShutdownNow)"

requirements-completed: [D-06, D-07, PNET-NETREG, PNET-GATE]

# Metrics
duration: 11min
completed: 2026-09-04
---

# Phase 15 Plan 17: Fail-Closed Policy-Stack Teardown (CR-C2-01/WR-C2-01) Summary

**Deny-all-on-private teardown gate in ShutdownNodePolicyServices plus SecureCrdt element-filter reject-on-expiry, both mutation-verified against live attacker datastores**

## Performance

- **Duration:** 11 min
- **Started:** 2026-09-04T11:05:32Z
- **Completed:** 2026-09-04T11:16:32Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- **CR-C2-01 closed:** on every policy-stack failure path that parks a private node in INITIALIZING_TRANSACTIONS with PubSub running and topics subscribed, the broadcaster now carries an explicit deny-all membership filter — no same-PSK peer can merge raw unsealed CRDT deltas into the stalled node's durable datastore. The "failing closed" log claim on those paths is now true end-to-end.
- **WR-C2-01 closed:** the SecureCrdt element-filter lambda rejects on weak_self expiry (empty replacement vector), byte-identical in policy to the candidate filter — expired-owner branches on a live GlobalDB (stalled path `:2401`, healthy-shutdown window, TPR-failure path) deny unsigned remote elements instead of accepting them.
- **Destruction route preserved:** `TeardownClearsBroadcasterMembershipFilter` stays green — the clear now lives only in the branch taken when `ShutdownNow()` follows three lines later; public nodes keep byte-identical raw behavior (no filter ever installed, clear remains a no-op).

## Task Commits

Each task was committed atomically:

1. **Task 1: Deny-all-on-private teardown + stalled-node filter regression** - `66e67ab9` (fix)
2. **Task 2: SecureCrdt element-filter reject-on-expiry + behavioral proof** - `ae662f0b` (fix)

## Files Created/Modified

- `src/account/GeniusNode.hpp` - `ShutdownNodePolicyServices(bool global_db_shutdown_follows = false)` declaration with gated-teardown Doxygen
- `src/account/GeniusNode.cpp` - deny-all-on-private teardown branch (private node + still-live GlobalDB), rewritten fail-closed contract comment, `/*global_db_shutdown_follows=*/ true` at the destruction-route call site
- `src/securecrdt/SecureCrdt.cpp` - element-filter expiry return flipped from `std::nullopt` (ACCEPT) to empty replacement vector (REJECT) with the WR-C2-01/CR-C2-01 branch comment
- `test/src/account/private_network_registry_binding_test.cpp` - stalled-node regression: `BroadcasterMembershipFilterInstalled` EXPECT inside `PrivateNodeWithoutBootstrapMembershipFailsClosed`
- `test/src/networkregistry/network_registry_test.cpp` - new scene (13) `ElementFilterRejectsOnSecureCrdtExpiry` (94 lines)

## Source Assertions (acceptance criteria, verified post-edit)

Task 1 (all at HEAD `ae662f0b`):

- `grep -n "global_db_shutdown_follows" src/account/GeniusNode.hpp` → `1327: void ShutdownNodePolicyServices( bool global_db_shutdown_follows = false );` (declaration present; Doxygen `@param` at :1320)
- `grep -cF "global_db_shutdown_follows=*/ true" src/account/GeniusNode.cpp` → **1** (fixed-string match, the `ShutdownForDestruction` call site at :2469)
- `grep -n "MakeBootstrapMembershipFilter( {} )" src/account/GeniusNode.cpp` → exactly 1 match at **:2388** (the deny-all teardown branch; the interim install at :779 passes `network_bootstrap_peers_`)
- `grep -c "ClearMembershipFilter()" src/account/GeniusNode.cpp` → **1** (now inside the else branch — public/destruction route only)
- `grep -c "would deny-all anyway" src/account/GeniusNode.cpp` → **0** (disproven rationale comment fully rewritten)
- Branch guard at :2386: `if ( !private_network_id_.empty() && !global_db_shutdown_follows )`
- Test scene `:197-236` contains `BroadcasterMembershipFilterInstalled` (EXPECT at :229)

Task 2:

- Element-lambda region re-anchored on the `[weak_self, entry]` capture (now **:679-694** after the branch comment): `sed -n '679,694p' | grep -cF 'return std::vector<sgns::crdt::pb::Element>{};'` → **1**; nullopt in region → **0**
- RegisterFilters function body region `:666-715`: `grep -c "return std::nullopt;"` → **0** (region inspected; the remaining file-level nullopt returns are the strong-path accepts in `FilterSecureCrdtUpdate`/`FilterCandidateApproval`, outside RegisterFilters)
- Whole-file empty-vector count → **11** (10 at HEAD + the new expiry return), matching the plan's prediction
- `--gtest_list_tests` → **16 cases** (NetworkMembershipPayloadTest 3 + NetworkRegistryTest 13); suite passes all 16
- New scene calls `MakeRegistry` **0** times; source order pinned: `tpr_->Unregister()` → `tpr_.reset()` (relative lines 16-17) → `secure_crdt_->RegisterFilters()` (23) → `secure_crdt_.reset()` (52) → `tx->Commit` (68)

## Mutation Evidence (T-15-17-06)

**Task 1 — unconditional-clear revert:** disabled the deny-all branch (`if ( false && ... )`, behaviorally identical to the pre-fix unconditional `ClearMembershipFilter()`), rebuilt, reran `PrivateNodeWithoutBootstrapMembershipFailsClosed`:

```
test/src/account/private_network_registry_binding_test.cpp:229: Failure
Value of: GeniusNodeTestAccess::BroadcasterMembershipFilterInstalled( node )
  Actual: false
Expected: true
CR-C2-01 regression: policy-stack teardown cleared the membership filter on a still-live GlobalDB
[  FAILED  ] PrivateNetworkRegistryBinding.PrivateNodeWithoutBootstrapMembershipFailsClosed
```

Fix restored → full suite green again (all 4 scenes incl. `TeardownClearsBroadcasterMembershipFilter`).

**Task 2 — :686 reverted to `return std::nullopt` (pre-fix accept-on-expiry):** rebuilt, reran `ElementFilterRejectsOnSecureCrdtExpiry`:

```
network_registry_test.cpp:962: Failure  Actual: true   Expected: false   (branch element LANDED)
network_registry_test.cpp:965: Failure  Actual: false  Expected: true    (Get no longer has_error)
[  FAILED  ] NetworkRegistryTest.ElementFilterRejectsOnSecureCrdtExpiry
```

The branch element landing under the revert also proves `weak_self` genuinely expired under the corrected pin-release sequence — the delta took the expiry branch, not the strong path (a live owner would have crashed on `entry.make_instance()`'s null std::function, never silently landed). Fix restored → all 16 green again.

## Verification

- `ninja -C build/OSX/Release genius_node private_network_registry_binding_test` — OK
- `ctest -R "^private_network_registry_binding_test$"` — **1/1 passed** (21s)
- `ninja -C build/OSX/Release securecrdt networkregistry network_registry_test` — OK
- `ctest -R "^network_registry_test$"` — **1/1 passed**, 16/16 cases (22s)
- Zero-regression battery (15-REVERIFICATION-2 spot checks): `ctest -R "^(network_membership_filter_test|network_registry_test|private_network_registry_binding_test|network_config_private_network_test|securecrdt_registry_test|securecrdt_quorum_gate_test|burnconfig_test|trustedpeerregistry_quorum_test)$"` — **8/8 passed** (120s)

## Decisions Made

- **Explicit deny-all over leaving the interim filter installed** (report blessed either): with the policy stack gone, membership authority is gone — the deny-all is uniform regardless of which filter was installed before the failure.
- **Destruction route keeps the clear** via the `global_db_shutdown_follows` gate rather than removing `ClearMembershipFilter` entirely — `TeardownClearsBroadcasterMembershipFilter` pins that behavior and the raw state must return exactly when the datastore stops.
- **Expiry regression avoids `MakeRegistry` and releases `tpr_` before the re-run** — both the NetworkRegistry entry capture and the TPR's dual pin (member + registered entry) would keep `weak_self` alive forever; the ordering-failure signature (bad_function_call crash on the strong path) makes mis-ordering loud, and the mutation run proves the expiry branch itself.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None. The pre-existing `-Wswitch` warning at `GeniusNode.cpp:4865` (NodeState enumeration) is untouched by this change (no enum values added) and out of scope per the scope boundary.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- CR-C2-01 and WR-C2-01 are closed with behavioral, mutation-verified regressions; truth 7 of 15-REVERIFICATION-2 ("fail-closed throughout") should flip to VERIFIED on the next re-verification pass (10/10 expected).
- Binding residual dispositions honored: the interim install (:774-784), the registry-backed install (:1149-1155), and the three failure-path log texts are unchanged; WR-C2-02/03/04, WR-05, IN-02/03, and the vendored-gater skip stay as recorded in deferred-items.md.

## Self-Check: PASSED

All 5 modified files exist on disk; both task commits (`66e67ab9`, `ae662f0b`) present in git log; SUMMARY.md created in the plan directory.
