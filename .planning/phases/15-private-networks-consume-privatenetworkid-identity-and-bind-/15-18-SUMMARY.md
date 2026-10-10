---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
plan: 18
subsystem: testing
tags: [e2e, private-network, job-flow, taskqueue, scoped-replication, pnet-isolation, deny-all-teardown, globaldb, graphsync, uat-gap-closure]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind- (15-11)
    provides: NetworkMembershipFilterFlowTest fixture (PSK GlobalDB nodes, graphsync, gated broadcasters, negative-window helpers)
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind- (15-04/15-17)
    provides: TaskKeys scope helpers, TaskQueueImpl/SubTaskResultStorageImpl scoped data path, MakeBootstrapMembershipFilter deny-all teardown shape
provides:
  - UAT-1 closed — the E2E two-node private-network job flow with public-node control is machine-checked in one automated, stable, non-vacuous test (NetworkMembershipFilterFlowTest case 11)
  - AssertKeysNeverPresentWithin multi-key negative-window helper beside the single-key one
  - Ordered-teardown pattern for wrong-PSK GlobalDB nodes (db + graphsync Network released before pubsub Stop; globaldb_integration TestNodeCollection shape)
affects: [15-HUMAN-UAT Test 1 re-testing, phase-15 verification battery, future multi-PSK GlobalDB test fixtures]

# Tech tracking
tech-stack:
  added: [processing_service + SGProcessingProto linked into NETWORKREGISTRY_TEST_NODE_LIBS]
  patterns:
  - "Real-data-path E2E composition: publish through TaskQueueImpl::EnqueueTask + SubTaskResultStorageImpl::AddSubTaskResult on TaskKeys::ScopedTopic(\"SGNUS.Processing.Channel\", id), assert TaskKeys-produced keys on the replica (not hand-rolled strings)"
  - "Data-level isolation: public control owns a real GlobalDB on the unscoped topic, dialed both directions, asserted absent for scoped AND public-scope job keys"
  - "Mutation-verified non-vacuity: scope mutation ("" on B's publishers) must fail leg (a); deny-all-swap removal must fail leg (c)"

key-files:
  created: []
  modified:
  - test/src/networkregistry/network_membership_filter_test.cpp
  - test/src/networkregistry/CMakeLists.txt

key-decisions:
  - "GlobalDB-level composition (no Blockchain/genesis) so the phase-13 quorum-broken suites stay untouched; every production API used was already public and green in isolation"
  - "Ordered teardown for the wrong-PSK pair: release db (ShutdownNow + reset) and the graphsync Network BEFORE pubsub Stop — GossipPubSub::StopImpl destroys its io_context right after its own host reference, so a host surviving Stop via the fixture's Network member destroyed parked never-negotiated pnet handshake connections against a dead kqueue reactor (deregister use-after-free)"
  - "Public-scope negative keys use the 2-arg SubTaskResultKey(\"\", id) form — there is no 1-arg overload (plan checker patch)"

patterns-established:
  - "First wrong-PSK node WITH a GlobalDB: any fixture dialing cross-PSK pairs must release the pair's db+graphsync Network before pubsub Stop (existing same-PSK-only nodes tolerate the fixture's regular order)"

requirements-completed: [D-02, D-07, D-08]

# Metrics
duration: 27min
completed: 2026-10-02
---

# Phase 15 Plan 18: Automated E2E Private-Network Job Flow (UAT-1) Summary

**One automated, mutation-verified NetworkMembershipFilterFlowTest case joining the three UAT legs: real-data-path scoped job replication, data-level public-node isolation, and deny-all teardown on a still-live GlobalDB**

## Performance

- **Duration:** 27 min
- **Started:** 2026-10-02T20:00:03Z
- **Completed:** 2026-10-02T20:27:00Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- **UAT Test 1 is now machine-checked.** New case (11) `PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown`: two symmetric gated+sealed same-PSK GlobalDB members publish a job through `TaskQueueImpl::EnqueueTask` + `SubTaskResultStorageImpl::AddSubTaskResult` on the scoped `SGNUS.Processing.Channel/<id>` topic, and the full scoped key set (TaskKey, SubTaskKey, ClaimableTaskKey, SubTaskResultKey under `/chain/<id>/`) replicates to the other member through real gossip+graphsync.
- **Public-node isolation proven at the DATA level.** The public control node owns a live GlobalDB listening on the unscoped topic, is dialed from both directions, and a 4s multi-key negative window (new `AssertKeysNeverPresentWithin` helper) proves it holds NONE of the six scoped keys nor the four public-scope forms of the same job — a strict upgrade over flow-5's bare-pubsub transport-only control.
- **Teardown deny-all proven live.** Installing `MakeBootstrapMembershipFilter({})` on member A whose GlobalDB stays live (the `GeniusNode::ShutdownNodePolicyServices` production shape) stops B's subsequent sealed scoped writes (task2/result2 never land on A) and `HasMembershipFilter()` stays true across and after the window.
- **Non-vacuity proven by two mutations** (Task 2): scope mutation and deny-all-removal each produce their exact expected failure, then full restoration (working tree returned to the Task-1+fix diff only).

## Task Commits

Each task was committed atomically:

1. **Task 1: Job-flow E2E composition test (three UAT legs) + CMake link wiring** - `eafb89742` (test)
2. **Task 1 follow-up [Rule 1]: ordered teardown for the wrong-PSK pair (crash fix)** - `d1ffbc415` (fix)
3. **Task 2: green/stable/mutation verification — verification-only, no lasting diff** (mutations fully restored; nothing to commit)

## Files Created/Modified

- `test/src/networkregistry/network_membership_filter_test.cpp` - case (11) with legs (a)/(b)/(c); MakeJobFlowTask/MakeJobFlowSubTask/MakeJobFlowResult proto builders; AssertKeysNeverPresentWithin multi-key helper; processing includes + `using namespace sgns::processing;`; file-header doc line; ordered teardown replacing TearDownNodes for this case only
- `test/src/networkregistry/CMakeLists.txt` - `processing_service` + `SGProcessingProto` appended to NETWORKREGISTRY_TEST_NODE_LIBS

## Verification

- Build: `cmake --build build/OSX/Release --target network_membership_filter_test -j 8` — clean, no new warnings.
- Registration: `--gtest_list_tests | grep -c PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown` → 1.
- Standalone stability: 3/3 consecutive green runs (8647 ms / 8539 ms / 8650 ms).
- Full suite: `ctest -R "network_membership_filter" --test-dir build/OSX/Release` — **1/1 ctest entry passed (35.13s), all 13 cases green** (NetworkMembershipFilterTest 4 + NetworkMembershipFilterFlowTest 7 incl. the new case + GossipPayloadAuthDecisionTable 1 + BootstrapMembershipFilterSemantics 1).
- Untouched (explicitly): blockchain_genesis_test, processing_nodes_test, test/src/CMakeLists.txt, all src/ production code — no production file changed in this plan, so cross-suite regression reduces to this target (phase-13 quorum todo unaffected).

## Mutation Evidence (15-11 mutation-verified non-vacuity discipline)

**Mutation A — scope binding (leg (a) binds the `/chain/<id>/` branch, D-08):** changed ONLY B's publisher scope arguments to `""` (`TaskQueueImpl::New(pnetB->db, scoped_topic, "")` + `SubTaskResultStorageImpl(pnetB->db, scoped_topic, "")`), rebuilt, ran:

```
Timed out waiting for condition: scoped task key did not replicate to the other member (timeout: 25000ms)
Timed out waiting for condition: scoped subtask key did not replicate to the other member (timeout: 25000ms)
Timed out waiting for condition: scoped claimable entry did not replicate to the other member (timeout: 25000ms)
Timed out waiting for condition: scoped subtask result key did not replicate to the other member (timeout: 25000ms)
[  FAILED  ] NetworkMembershipFilterFlowTest.PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown (108339 ms)
```

All four scoped-key positive assertions fail exactly as required (the job lands unscoped). Arguments restored.

**Mutation B — teardown binding (leg (c) window is live, not vacuous):** commented out ONLY the deny-all swap `broadcaster_a->SetMembershipFilter( MakeBootstrapMembershipFilter( {} ) )` (the symmetric member filter stays installed, so B remains a member), rebuilt, ran:

```
post-teardown scoped write on the deny-all member replicated although it must be denied: /chain/0x6d...7839/processing_1/tasks/pnet_job_task_2
[  FAILED  ] NetworkMembershipFilterFlowTest.PrivateNetworkJobFlowReplicatesIsolatesAndDeniesOnTeardown (4800 ms)
```

task2 arrives on A when the swap is absent — the negative window catches real replication. Swap restored.

**Final state:** rebuilt clean; `git diff` showed only the pre-existing `.planning/STATE.md` modification (mutations fully restored); full suite green as above.

## Decisions Made

- **GlobalDB-level composition, not GeniusNode-level** — per the diagnosed fix direction: single-peer quorum floors avoid the phase-13 quorum regression entirely, and the NetworkMembershipFilterFlowTest fixture already carries real PSK GlobalDBs with graphsync replication.
- **Public control REUSES MakeNode** (real GlobalDB + graphsync, wrong PSK) — the data-level upgrade over flow-5's bare-pubsub control; its broadcaster installs NOTHING (public-node shape, EXPECT_FALSE on HasMembershipFilter as an explicit pin).
- **Ordered teardown local to this case** (see Deviations #1): pnetB keeps the fixture's regular order because queue/storage still reference its db and its same-PSK connections are closed cleanly inside Stop.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Case segfaulted after teardown; ordered teardown for the wrong-PSK pair**
- **Found during:** Task 2 (standalone green run)
- **Issue:** The case completed all assertions but the process exited 139 (SIGSEGV) after teardown. lldb backtrace: `~PnetGdbNode(publicControl)` → `graphsync::Network::~Network` → `~BasicHost` → `~ListenerManagerImpl` → `~Multiselect` → `~MultiselectInstance` → `~PnetProtectedConnection` → `~TcpConnection` → `kqueue_reactor::deregister_descriptor` — use-after-free. Root cause: `GossipPubSub::StopImpl` (thirdparty/ipfs-pubsub gossip_pubsub.cpp:803-963) destroys its io_context (`m_context.reset()`) immediately after its own host reference (`m_host.reset()`); this case is the FIRST whose wrong-PSK node owns a GlobalDB, so the fixture's retained `graphsync_network` member keeps the host alive past Stop, and the host's parked never-negotiated pnet handshake connections (from the both-direction A<->publicControl dials) then deregister their sockets from a dead reactor. Same-PSK-only cases never hit this (all connections are closed cleanly via the connection manager inside StopImpl), which is why cases (5)-(10) pass with the shared TearDownNodes helper.
- **Fix:** For pnetA and publicControl only: after legs complete and io stop/join, call `db->ShutdownNow()`, then reset db / graphsync_network / generator / scheduler BEFORE `pubs->Stop()` — making StopImpl's `m_host.reset()` the host's final release while its reactor is alive (the green `globaldb_integration` TestNodeCollection teardown shape: `db->ShutdownNow(); db.reset(); pubsub->Stop();`). pnetB keeps the fixture's regular order. The case then passed 3/3 standalone.
- **Files modified:** test/src/networkregistry/network_membership_filter_test.cpp
- **Verification:** 3/3 standalone green (8.6s each) + full suite green.
- **Committed in:** `d1ffbc415`

**2. [Rule 3 - Blocking] clang-format whole-file run produced repo-inconsistent churn; formatted added regions only**
- **Found during:** Task 1 (style gate)
- **Issue:** The plan's gate says "run clang-format on the changed file", but the checked-in file is NOT format-clean under any available clang-format (19.1.7 / 21.1.8 / 22.1.8 each produce ~196-238 changed lines in pre-existing code — sibling test files carry similar residuals), so a whole-file `-i` rewrote 100+ unrelated lines and polluted the atomic diff.
- **Fix:** Reverted the churn; hand-matched the file's established layout and applied clang-format's suggested shape to the added regions only (verified by diffing a formatted probe and confirming zero remaining deltas inside the added code, modulo tool-version disagreements that equally apply to the file's pre-existing constructs such as `{`-on-own-line lambdas). No added line exceeds the 120-column limit.
- **Files modified:** (same file, no extra diff)
- **Committed in:** `eafb89742`

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking/style)
**Impact on plan:** Both necessary for a green, reviewable deliverable. No scope creep; no production code touched.

## Issues Encountered

- The first standalone run revealed the teardown crash (Deviation #1); diagnosed via lldb backtrace and fixed with an established in-repo teardown ordering rather than weakening the topology (the both-direction dials required by the plan are retained — they are what makes leg (b) meaningful).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Gap UAT-1 in 15-HUMAN-UAT.md can be re-tested as "covered by network_membership_filter_test case (11)" instead of manual execution (orchestrator owns the UAT bookkeeping edit).
- The ordered-teardown pattern is now pinned in-code for any future fixture that dials cross-PSK GlobalDB pairs.

## Self-Check: PASSED

Both modified files exist on disk; SUMMARY.md created in the plan directory; both commits (`eafb89742`, `d1ffbc415`) present in git log; the new case name and the CMake link entry each grep-confirmed in their files.

