---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "07"
subsystem: blockchain
tags: [privatenetwork, validatorregistry, consensus, crdt, topics, d09, cplusplus, cmake]

# Dependency graph
requires:
  - 15-01 private_network_id config identity (GeniusNode::private_network_id_, 0x-hex-32B validated fail-closed)
  - pre-existing ValidatorRegistry static identifiers (gnus-validator-registry / -cid) as the public byte-stable base
provides:
  - Instance-scoped ValidatorRegistry identifiers: New/ctor take trailing defaulted network_scope;
    registry_key_/validator_topic_/registry_cid_key_ members + RegistryKeyValue()/ValidatorTopicValue()/
    RegistryCidKeyValue() const accessors (empty scope = exact public constants; non-empty = base + "/" + scope)
  - Blockchain::New trailing defaulted network_scope (stored on the instance, forwarded as the last
    argument of the single ValidatorRegistry::New creation site); GeniusNode passes private_network_id_,
    so a private node's validator consensus runs on scoped key/topic/cid strings end to end
  - validator_registry_scope_test — 4-case isolation suite (public byte-stability, scoped suffixing,
    three-way disjointness, static-literal pinning)
affects: [15-08-closeout, validator-consensus, blockchain-startup, geniusnode]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Trailing defaulted network_scope on factory/ctor (existing callers compile unchanged; empty = public byte-compat) — same idiom as 15-06"
    - "Single source of truth: Blockchain's registry data-path sites (CID read, deferred head-request topic) derive from the registry instance accessors, not from re-derived strings"
    - "static_assert on retained public constants — compile-time drift tripwire next to the runtime tests"
key-files:
  created:
    - test/src/blockchain/validator_registry_scope_test.cpp
  modified:
    - src/blockchain/ValidatorRegistry.hpp
    - src/blockchain/ValidatorRegistry.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/GeniusNode.cpp
    - test/src/blockchain/CMakeLists.txt
    - .planning/phases/15-private-networks-consume-privatenetworkid-identity-and-bind-/deferred-items.md

key-decisions:
  - "Scoped identifier derivation lives in one private static helper (ScopedIdentifier(base, scope)); the
    static constexpr constants survive as the only base source — grep-clean acceptance (no bare-static
    data-path use) without duplicating literals"
  - "ValidatorRegistry::MigrateCids (static, friend-only) gained a defaulted network_scope parameter instead
    of keeping the bare static: the migration path now derives scoped CID keys the same way; its only caller
    (Migration3_5_0To3_6_0) compiles unchanged with the public default"
  - "Blockchain's own registry data-path sites (New-callback registry-CID read, deferred head-request topic
    set + log) read validator_registry_->RegistryCidKeyValue()/ValidatorTopicValue() — the registry owns the
    identifiers, so scope changes propagate without a second derivation site (null-safety unchanged: Start()
    already dereferenced validator_registry_ on the same path)"
  - "D-09 boundary kept: no NetworkRegistry/SecureCRDT authorization coupling added — ValidatorRegistry remains
    consensus-only for its own scope (T-15-24)"

# Metrics
duration: ~160min (2026-09-02T18:46Z to 21:27Z; includes submodule init + cmake configure on the fresh
  worktree, three long ctest rounds, and two base-binary A/B runs proving pre-existing failures)
completed: 2026-09-02
---

# Phase 15 Plan 07: Per-Scope ValidatorRegistry State and Quorum Summary

**ValidatorRegistry identifiers (CRDT key, gossip topic, CID key) are now instance state parameterized by network scope with the public instance byte-stable — Blockchain::New threads GeniusNode's private_network_id_ to the single registry creation site, and 4 test cases pin public byte-stability and three-way key/topic disjointness so two networks' consensus can never merge**

## Performance

- **Duration:** ~160 min (started 2026-09-02T18:46:54Z)
- **Tasks:** 3/3
- **Files:** 7 modified + 1 created (+150/-28 lines in source+test)

## Accomplishments

- **Task 1 — instance-scope ValidatorRegistry identifiers (`d0ef6c89`):**
  `New`/private ctor gained trailing defaulted `std::string network_scope = ""`; three members
  (`registry_key_`, `validator_topic_`, `registry_cid_key_`) are derived once via the new private static
  `ScopedIdentifier(base, scope)` (empty scope = exact constant; non-empty = constant + "/" + scope) and
  exposed through const accessors `RegistryKeyValue()`/`ValidatorTopicValue()`/`RegistryCidKeyValue()`.
  Every internal data-path site in the cpp now reads the members: filter/callback pattern + AddListenTopic
  (RegisterFilter/Close), CRDT Put key+topic (StoreGenesisRegistry, StoreRegistryUpdate), delta key match
  (LoadRegistryByCid), cache Get + CID persistence (InitializeCache, PersistLocalState), head-list topic
  lookup (RetryInitializationIfNeeded). Static migration path (`MigrateCids`) derives its CID key through
  the same helper via a defaulted scope parameter. The three `static constexpr` functions are untouched
  (diff shows no removed lines in their bodies). Quorum math, weight handling, and vote/certificate
  machinery untouched; no authorization coupling added.
- **Task 2 — scope threading through Blockchain and GeniusNode (`10217f49`):**
  `Blockchain::New` gained trailing defaulted `network_scope` (stored as `network_scope_` on the instance)
  and forwards `std::move(network_scope)` as the last argument of the `ValidatorRegistry::New` call at the
  single creation site. Blockchain's three registry-support sites (registry-CID datastore read in the New
  callback, deferred head-request topic set + log line) now derive from the registry instance accessors.
  `GeniusNode.cpp` INITIALIZING_BLOCKCHAIN passes `private_network_id_` as the trailing scope argument.
  All six other `Blockchain::New` call sites (2 migrations, 3 tests) compile unchanged via the default.
- **Task 3 — scope isolation tests (`abf25d96`):**
  `validator_registry_scope_test` (CRDTFixture-based, 150 lines): `PublicScopeByteStable` (instance
  accessors == exact public literals == retained statics), `PrivateScopeSuffixed` (0x-hex-32B scope →
  base + "/" + scope for all three identifiers, none equal to public), `DisjointScopes` (public/scopeA/
  scopeB → three-way disjoint sets for key, topic, and CID kinds), `StaticDefaultsPreserved`
  (file-level `static_assert` on the statics + runtime mirror).

## Task Commits

1. **Task 1: Instance-scope ValidatorRegistry identifiers** - `d0ef6c89` (feat)
2. **Task 2: Scope threading through Blockchain and GeniusNode** - `10217f49` (feat)
3. **Task 3: Scope isolation tests** - `abf25d96` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Verification Results

- `ninja blockchain_genesis genius_node migration` — all build clean (only the pre-existing GeniusNode.cpp:4707
  -Wswitch warning, untouched region, same as 15-05 observed)
- `ctest -R validator_registry_scope` — **1/1 PASS** (4.8s, all 4 cases)
- `ctest -R "blockchain|validator|genesis|multi_account"` (Task 1 gate): **8/9 PASS** — public_chain_input_validator_slot,
  genesis_manifest, trust_genesis_tool, trustedpeerregistry_genesis, validator_registry_slot_quorum,
  validator_registry_promotion, multi_account (215-365s), policy_lifetime_multi_account all PASS;
  `blockchain_genesis_test` fails its two `WithAuthorization*` scenes — **A/B-verified pre-existing**
  (identical 2-scene failure set + identical 50s READY timeouts on a base-family binary built from the main
  checkout without 15-07 changes; documented no-genesis READY stall, deferred-items.md §1)
- `ctest -R "startup|node|blockchain"` (Task 2 gate): **3/9 PASS** — startup_wiring_test, node_startup_test,
  node_shutdown_race_test PASS; the 6 failures are exactly 15-06's documented pre-existing set
  (node_type_derivation, node_initialization_progress, processing_nodes, full_node, genius_node_bootstrap_reconnect)
  plus blockchain_genesis_test (A/B-verified above). Identical failure set before/after — no regression
- `ctest -R "consensus_pending_lifecycle|consensus_subject|consensus_vote_slot|consensus_bridge_mint|consensus_slot_key"`
  — **5/5 PASS** against the final code (exercises the rewired StoreGenesisRegistry → CRDT Put → Load round trip)
- `ctest -R "burnconfig_policy_e2e|transaction_manager_pending_lifecycle|transaction_manager_certificate_fallback"`
  — **3/3 PASS** (existing Blockchain::New call sites compile and behave unchanged)
- `ctest -R migration_sync` — 3 scenes fail; **A/B-verified pre-existing** (base binary fails the identical
  3 scenes — ArchiveSurvivesMigrationWithoutJoiningRegistry + both BalanceAfterMigration params — with the same
  wait_condition timeouts and near-identical durations; run as extra diligence because 15-07 touches MigrateCids)
- Acceptance greps: `grep -c "registry_key_\|validator_topic_\|registry_cid_key_" ValidatorRegistry.hpp` = 3 (>= 3);
  bare statics in ValidatorRegistry.cpp survive only as ScopedIdentifier base arguments (ctor init list +
  static MigrateCids); zero bare `ValidatorRegistry::RegistryKey()/ValidatorTopic()/RegistryCidKey()` uses remain
  anywhere in src/; `git diff ValidatorRegistry.hpp` shows the three static constexpr bodies unchanged

## Deviations from Plan

### Plan-Snippet Adaptations (mandated by base drift)

**1. [Rule 3 - Blocking] Blockchain header path in plan does not exist**
- **Found during:** Task 2 (plan's files list `src/blockchain/impl/Blockchain.hpp`)
- **Issue:** Only `impl/Blockchain.cpp` lives under `impl/`; the header is at `src/blockchain/Blockchain.hpp` (post-closeout layout)
- **Fix:** Edited the actual header; everything else as planned
- **Files modified:** src/blockchain/Blockchain.hpp

**2. [Rule 2 - Missing critical functionality] Blockchain's own registry data-path sites still used the bare statics**
- **Found during:** Task 2 (repo-wide grep for the statics after Task 1)
- **Issue:** Blockchain.cpp had three registry-support sites using the public constants directly — the registry-CID
  datastore read in New's callback (case 2) and the deferred head-request topic set + log in
  RequestValidatorRegistryWhileDeferred. Left as-is, a scoped registry's support machinery would read/request on
  the PUBLIC key/topic, violating the must-have disjointness truth ("consensus never merges across networks")
- **Fix:** All three now derive from `validator_registry_->RegistryCidKeyValue()/ValidatorTopicValue()` (public
  values byte-identical; no new null-deref surface — Start() already dereferenced validator_registry_ on the same path)
- **Files modified:** src/blockchain/impl/Blockchain.cpp
- **Committed in:** 10217f49

**3. [Rule 1 - Bug] Dead static-derived local removed; static MigrateCids scoped**
- **Found during:** Task 1 (grep for internal static uses)
- **Issue:** (a) anonymous-namespace `ExtractPrevRegistryCid` declared `registry_key` from `RegistryKey()` but never
  used it — the last bare-static reference in the cpp; (b) static `MigrateCids` persisted the CID pointer under the
  bare `RegistryCidKey()`
- **Fix:** (a) dead local deleted; (b) `MigrateCids` gained defaulted `network_scope` and derives its CID key via
  `ScopedIdentifier` — its only caller (Migration3_5_0To3_6_0.cpp:97) compiles unchanged with the public default
  (correct: no pre-3.6.0 database carries scoped keys — private networks are new in this phase)
- **Files modified:** src/blockchain/ValidatorRegistry.{hpp,cpp}
- **Committed in:** d0ef6c89

**4. Plan verify grep window does not match the post-closeout call shape**
- `grep -A6 "Blockchain::New" src/account/GeniusNode.cpp | grep private_network_id_` cannot match: the real call
  spans ~55 lines of inline lambdas, so the trailing argument sits far beyond 6 lines. The argument IS passed —
  verified directly at GeniusNode.cpp:794-798 (`node_type_,` ... `private_network_id_ );`). Acceptance intent met.

**5. Pre-existing suite failures (out of scope, A/B-proven)**
- `blockchain_genesis_test` (in this plan's gates) and `migration_sync_test` (extra diligence) fail on the
  documented no-genesis READY-stall mechanism (deferred-items.md §1). Both A/B-verified against base-family
  binaries from the main checkout: identical failure sets, identical timeout signatures. Observations appended
  to deferred-items.md §1. No fix attempted (pre-existing, out of scope).

---

**Total deviations:** 4 adaptations/auto-fixes + 1 documented pre-existing-failure note
**Impact on plan:** All must-have truths, artifacts, and key links delivered; no scope creep.

## Threat Flags

None beyond the plan's threat model. Register mitigations landed: T-15-22 (instance-scoped identifiers +
three-way disjointness test), T-15-23 (statics retained byte-stable + static_assert/byte-stability tests),
T-15-24 (no authorization coupling added — ValidatorRegistry remains consensus-only for its own scope).
Scope format is regex-safe: private_network_id is validated 0x + 64 hex digits before it can reach the
filter pattern (15-01 fail-closed validation).

## Known Stubs

None. The scoped identifiers are consumed at every real data-path site (CRDT key, topic, CID persistence,
filter/callback registration, head discovery, Blockchain CID read + head request); the empty-scope public
path is exercised end-to-end by multi_account/consensus/transaction-manager suites; the non-empty-scope
identifier properties are pinned by the new 4-case suite (live two-network consensus rounds are 15-08's
closeout scope, per the plan's "one scope per node from config is this phase's wiring").

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 15-08 (closeout) can rely on: `ValidatorRegistry::New(..., init_callback, network_scope)`,
  `Blockchain::New(..., node_type, network_scope)`, and the three instance accessors for any integration scenes
- Scoped filter patterns (`/?gnus-validator-registry/<id>`) are full-match std::regex; the 0x-hex-32B scope
  format contains no regex metacharacters
- Known pre-existing failures for 15-08's gates: the six "startup|node|blockchain" no-genesis READY suites
  (blockchain_genesis_test A/B-proven this plan; the other five documented by 15-06) — identical failure set
  expected on unchanged code paths

## Self-Check: PASSED

- All 8 created/modified files exist on disk (verified 2026-09-02T21:27Z)
- Task commits verified in git log: d0ef6c89 (feat), 10217f49 (feat), abf25d96 (test)
- Working tree clean of task files before this docs commit
- `ctest -R validator_registry_scope` green on the final code; consensus + caller suites green on the final code
---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-02*
