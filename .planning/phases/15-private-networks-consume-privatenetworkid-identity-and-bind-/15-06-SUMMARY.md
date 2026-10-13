---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "06"
subsystem: processing
tags: [privatenetwork, crdt, taskqueue, escrow, topics, cplusplus, cmake]

# Dependency graph
requires:
  - 15-01 private_network_id config identity (private_network_id_ member on GeniusNode, 0x-hex-32B)
  - vendored 3rdparty install — MOVED per user instruction to /Users/henriqueklein/gnus/thirdparty (libp2p b28eed2 verified there)
provides:
  - Scope-aware TaskKeys builders + ScopePrefix/ScopedTopic/ScopedKeyPath/SubTaskResultKey helpers
    (empty scope = byte-identical public output; scoped = /chain/<id>/ branch)
  - TaskQueueImpl (all 12 non-lock TaskKeys sites) and SubTaskResultStorageImpl (results/ keys)
    carrying the node's private_network_id at their real data-path construction sites
  - GeniusNode::ScopedProcessingChannel / ScopedProcessingGridChannel used at all six channel
    consumption sites (listen x2, queue ctor, storage ctor, DHTInit CID, StartProcessing grid)
  - Scoped escrow CRDT path (task.escrow_path) + per-tx escrow chain-id override routed to the
    genius input validator
affects: [15-05-gater-membership, 15-07, 15-08, processing-data-path, escrow-lifecycle]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Trailing-defaulted private_network_id parameters on factories/ctors (existing callers compile unchanged; empty scope = public byte-compatibility)"
    - "Scope enters through the data, not the reader: escrow path scoped in task.escrow_path keeps PayEscrow/FetchTransaction read-side unchanged"
    - "Channel members stay public constants (ctor runs before LoadNetworkConfig); const Scoped*Channel() helpers derive at consumption time"

key-files:
  created:
    - test/src/processing/task_keys_scope_test.cpp
  modified:
    - src/processing/impl/TaskKeys.hpp
    - src/processing/impl/TaskQueueImpl.hpp
    - src/processing/impl/TaskQueueImpl.cpp
    - src/processing/impl/processing_subtask_result_storage_impl.hpp
    - src/processing/impl/processing_subtask_result_storage_impl.cpp
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp
    - src/account/EscrowTransaction.hpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/src/processing/CMakeLists.txt
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp

key-decisions:
  - "Dropped the plan's 1-arg scoped overload of the BARE SubTaskListKey(): it aritally collides
    with SubTaskListKey(taskId) and a std::string argument would silently bind as the scope
    (found by the golden test itself); bare scoped list composes as ScopePrefix(id) + SubTaskListKey()"
  - "SelectInputValidator genius-routing test compares against the member OR the IInputValidator
    registry singleton — GeniusInputValidator self-registers under supergenius/supergenius_chain/'',
    so address-identity with the member alone is wrong; a foreign-chain counterfactual proves the check discriminates"
  - "User instruction mid-plan: 3rdparty moved to /Users/henriqueklein/gnus/thirdparty — build tree
    reconfigured against the new path (libp2p b28eed2 confirmed); recorded in deferred-items.md"

patterns-established:
  - "Data-path placement tests: assert db Get success/failure on scoped vs public keys, not builder strings"

requirements-completed: [D-02, D-08, PNET-SCOPE]

# Metrics
duration: ~18h wall (2026-09-01 ~16:30 to 2026-09-02 10:13 -0300; includes overnight idle, one warm build, one full rebuild after the 3rdparty move, and multi-minute node/account suites)
completed: 2026-09-02
---

# Phase 15 Plan 06: Job Scope Propagation Through Every Job-Derived Artifact Summary

**Scope-aware TaskKeys/chain-id helpers with every real data-path site routed — queue keys (12 sites), results/ keys, escrow CRDT path + chain id, processing channel (listen+commit) and grid channel (DHT CID + subscription) — public scope byte-identical everywhere, private scope fully branched under /chain/<id>/**

## Performance

- **Duration:** ~18h wall (incl. overnight idle + 2 full build-tree builds + long ctest suites)
- **Tasks:** 3/3 (each as RED test commit + GREEN implementation commit per tdd="true")
- **Files:** 13 modified (1 created, 12 modified); +650/-51 lines

## Accomplishments

- **Task 1:** `TaskKeys` scope helpers — `ScopePrefix` (/chain/<id> via `HierarchicalKey("chain").ChildString(id)`), `ScopedTopic` (public + "/" + id), `ScopedKeyPath` (single-slash joins, raw path unchanged when public), `SubTaskResultKey` (empty scope byte-identical to the legacy `boost::format("results/%s")` output), and scoped overloads of all seven list/key builders with a leading `const std::string &private_network_id`; `LockKey` stays scope-agnostic (embeds the already-scoped task key). `TransactionManager::ScopedChainId` (private static, friend-test-accessible): "supergenius" / "supergenius/<id>". Both `GENIUS_CHAIN_ID` constants untouched; zero `SetNetworkId` usage.
- **Task 2:** `TaskQueueImpl::New`/ctor take trailing defaulted `private_network_id` (stored as `network_scope_`); all 12 non-lock `TaskKeys::` sites in TaskQueueImpl.cpp (writes AND reads) routed through the scoped overloads. `SubTaskResultStorageImpl` ctor takes the trailing scope and its three literal `results/%s` constructions now go through `TaskKeys::SubTaskResultKey`. `GeniusNode` gains `ScopedProcessingChannel()` / `ScopedProcessingGridChannel()` const helpers (members stay public constants — ctor runs before LoadNetworkConfig) used at all six consumption sites: both `AddListenTopic` (listen set aligned with the scoped commit channel), `TaskQueueImpl::New` + result-storage construction, `DHTInit` (scope FIRST + net-and-version appendix LAST so an empty scope hashes today's exact byte string), and `StartProcessing`'s grid subscription.
- **Task 3:** `ProcessImage` writes the escrow CRDT under `ScopedKeyPath(private_network_id_, lock_id)` and carries it in `task.escrow_path` (write/read symmetric with `PayEscrow`→`FetchTransaction`, no read-side change); `HoldEscrow` takes trailing defaulted `network_scope` and applies `SetChainIdOverride(ScopedChainId(scope))` only when non-empty; `EscrowTransaction::GetChainId()` override returns a member defaulting to `GeniusTransaction::GENIUS_CHAIN_ID` (routing metadata, not signed); `SelectInputValidator` additively routes `supergenius/<id>` prefixed chain ids to `genius_input_validator_`.

## Task Commits

1. **Task 1: Scope-aware key, topic, path, and chain-id helpers** — `5f6274da` (test/RED) + `b7a9b73e` (feat/GREEN)
2. **Task 2: Route the node scope through every data-path key site (queue, result storage, channel topics incl. grid)** — `b7a2f0b9` (test/RED) + `f50919ee` (feat/GREEN)
3. **Task 3: Scoped escrow CRDT path + escrow chain-id override** — `471a40a6` (test/RED) + `b714baea` (feat/GREEN)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/processing/impl/TaskKeys.hpp` — four scope helpers + scoped overloads of all builders; includes crdt/hierarchical_key.hpp
- `src/processing/impl/TaskQueueImpl.hpp/.cpp` — trailing defaulted scope on New/ctor, `network_scope_` member, 12 key sites routed
- `src/processing/impl/processing_subtask_result_storage_impl.hpp/.cpp` — trailing ctor scope, `m_private_network_id`, boost::format keys replaced by SubTaskResultKey
- `src/account/GeniusNode.hpp/.cpp` — two const channel helpers + six scoped consumption sites + scoped escrow path block in ProcessImage
- `src/account/TransactionManager.hpp/.cpp` — ScopedChainId (declared beside GENIUS_CHAIN_ID, defined in cpp), HoldEscrow trailing scope + conditional override, SelectInputValidator prefix branch
- `src/account/EscrowTransaction.hpp` — chain_id_ member + SetChainIdOverride + GetChainId override
- `test/src/processing/task_keys_scope_test.cpp` — 7 helper-golden tests + 3 data-path placement tests (286 lines)
- `test/src/processing/CMakeLists.txt` — task_keys_scope_test registered (same idiom as task_queue_test)
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` — ScopedChainId/SelectsGeniusValidator test access + EscrowChainIdDefaultAndScopedOverride scene
- `.planning/phases/.../deferred-items.md` — wave-2 pre-existing-failure observation + 3rdparty move note

## Verification Results

- `ninja` full build (all targets incl. every test binary): exit 0 on the NEW thirdparty path
- `ctest -R "task_keys_scope|task_queue"`: 2/2 PASS
- `ctest -R "task_keys_scope|transaction_manager"`: 3/3 PASS (incl. the new funded-escrow chain-id scene, 1.4s)
- `ctest -R "startup|account"`: 6/6 PASS (multi_account 215s, policy_lifetime 34s, startup_wiring, node_shutdown_race among them)
- `ctest -R "startup|node"` (plan gate): 3/8 pass; the 5 failures are the DOCUMENTED pre-existing no-genesis READY stall (deferred-items.md) — identical suite set and signatures before and after this plan's changes; the READY-independent suites that exercise this plan's changed paths (startup_wiring, node_shutdown_race) pass. No regression introduced (regression-standard evidence: same failures on base family verified by 15-01; all empty-scope paths byte-identity-tested green).
- `grep -n "TaskKeys::" src/processing/impl/TaskQueueImpl.cpp`: 15 sites — 12 carry `network_scope_`; 3 `LockKey` sites scope-agnostic by design (taskKey argument already scoped)
- `grep -n "ScopedProcessingGridChannel" src/account/GeniusNode.cpp`: definition + DHTInit derivation + StartProcessing subscription
- `git diff 52501d57 HEAD` on `src/base/sgns_version.hpp` and `src/account/GeniusTransaction.hpp`: EMPTY; both GENIUS_CHAIN_ID constants unchanged; `GeniusNode::CreateEscrowInfoCRDTTransaction` definition unchanged; PayEscrow/FetchTransaction signatures unchanged
- `grep SetNetworkId` in TaskKeys.hpp / TransactionManager.cpp: 0 (Pitfall 6)

## Deviations from Plan

### Plan-Snippet Adaptations (mandated by base drift)

**1. [Rule 1 - Bug] Dropped the plan's 1-arg scoped overload of the bare SubTaskListKey()**
- **Found during:** Task 1 GREEN (the golden test itself failed with `SubTaskListKey("t1")` resolving to the scoped overload)
- **Issue:** The plan's overload set ("both forms") creates an arity collision: `SubTaskListKey(const std::string&)` (scope) vs `SubTaskListKey(std::string_view)` (taskId). A `std::string` argument binds exactly to the const-ref overload, silently reinterpreting a task id as a network scope — it would have broken `TaskQueueImpl::GetSubTasks` semantics between the Task 1 and Task 2 commits.
- **Fix:** Only the (scope, taskId) 2-arg scoped overload exists; a bare scoped subtask list composes as `ScopePrefix(scope) + SubTaskListKey()` (noted in-code). No caller needed the colliding form.
- **Files modified:** src/processing/impl/TaskKeys.hpp, test/src/processing/task_keys_scope_test.cpp
- **Commit:** b7a9b73e

**2. Line anchors drifted from the plan (post-closeout GeniusNode)**
- **Found during:** Task 2 (plan cites ctor 251/656/2278/1636-1655/2155-2173/2924-2934; actual: 307-308/729/2731/2037-2041/2593/3420)
- **Fix:** Located every anchor fresh by grep; the plan's intent (six consumption sites, ctor topic init untouched) implemented exactly. Prior-wave warning followed.
- **Files modified:** src/account/GeniusNode.cpp

### Auto-fixed Issues

**3. [Rule 1 - Bug] Genius-validator test assertion initially too strict**
- **Found during:** Task 3 GREEN (first SelectsGeniusValidator call failed even with the default chain id)
- **Issue:** `GeniusInputValidator::Register()` self-registers a static singleton under supergenius/supergenius_chain/"" in the `IInputValidator` registry; `SelectInputValidator` returns the REGISTERED instance for those ids, so address-identity with the `genius_input_validator_` member can never hold for default ids.
- **Fix:** The test helper accepts the member OR the registry singleton; a foreign-chain counterfactual ("0xforeignchain" → public validator) proves the check discriminates genius routing.
- **Files modified:** test/src/account/transaction_manager_pending_lifecycle_test.cpp
- **Verification:** EscrowChainIdDefaultAndScopedOverride passes; full transaction_manager suites green

**4. [Rule 3 - Blocking] Escrow test needed a funded UTXO**
- **Found during:** Task 3 GREEN (CreateTxParameter(1, ...) returned failure — fixture account has no spendable UTXO)
- **Fix:** Moved the scene to the TransactionManagerPreviousHashTest fixture and funded via the proven mint ceremony (StoreCertificate + StoreTransaction + ProcessStoredTransaction), the same prerequisite the existing escrow-deletion scene uses.
- **Files modified:** test/src/account/transaction_manager_pending_lifecycle_test.cpp

**5. [Rule 3 - Blocking] Build environment: 3rdparty relocated mid-plan (user instruction)**
- **Found during:** Task 2 verification (user: "The thirdparty is now on gnus/thirdparty", "forget about 3rdparty directory")
- **Fix:** Verified libp2p b28eed2 (the dev_pnets pin) at /Users/henriqueklein/gnus/thirdparty, recreated the build tree against the new THIRDPARTY_DIR, full rebuild green; recorded in deferred-items.md for later plans.
- **Files modified:** deferred-items.md (build trees are gitignored — no source change)

---

**Total deviations:** 2 adaptations + 3 auto-fixes
**Impact on plan:** No scope creep; every must-have truth and artifact delivered.

## Issues Encountered

- Fresh worktree: submodule init + two full configure/build cycles (initial warm build, then full rebuild after the 3rdparty move).
- The `ctest -R "startup|node"` gate cannot exit 0 on this base (documented pre-existing no-genesis READY stall — see deferred-items.md); satisfied in the regression sense: identical failure set with and without this plan's changes, and every suite that can run without READY passes.

## Known Stubs

None. Every helper is consumed at its real data-path site, and every data-path site is covered by placement or suite tests. The scoped channels/grid derivation are only *exercised* end-to-end with a live private network by later phase plans (15-07/15-08 integration scenes); the placement/byte-identity properties they rely on are proven here.

## Threat Flags

None beyond the plan's threat model. Register mitigations landed: T-15-19 (placement tests assert public-key Gets fail), T-15-20 (golden byte-stability + untouched public constants), T-15-21 (no SetNetworkId; per-instance threading; grep gate 0), T-15-19b (both AddListenTopic sites on ScopedProcessingChannel), T-15-19c (escrow path scoped in task.escrow_path — symmetric read), T-15-19d (DHTInit + StartProcessing on ScopedProcessingGridChannel; empty scope hashes today's exact string).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `TaskKeys::Scoped*` helpers, scoped `TaskQueueImpl`/`SubTaskResultStorageImpl`, `GeniusNode::ScopedProcessingChannel()/ScopedProcessingGridChannel()`, scoped escrow path + `ScopedChainId` routing are all available for 15-07/15-08 integration plans
- Later plans/worktrees MUST configure against `THIRDPARTY_DIR:PATH=/Users/henriqueklein/gnus/thirdparty` (new location; see deferred-items.md)
- No-genesis READY stall still open (base repair options in deferred-items.md)

## Self-Check: PASSED

- All 13 task files + deferred-items.md exist on disk (verified 2026-09-02)
- All 6 task commits (5f6274da, b7a9b73e, b7a2f0b9, f50919ee, 471a40a6, b714baea) verified in git log
- Working tree clean of task files before this docs commit
- Full ninja build exit 0; in-scope ctest suites green
---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-02*
