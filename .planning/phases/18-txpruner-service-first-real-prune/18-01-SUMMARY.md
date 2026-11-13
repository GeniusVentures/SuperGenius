---
phase: 18-txpruner-service-first-real-prune
plan: 01
subsystem: crdt
tags: [re-delivery-guard, seal-dedup, d-02, pruned-roots, fetch-storm, pubsub, c++17]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening
    provides: GlobalDB::EraseEntry guarded compose (block erased, seal survives), GATE-03 seal retention
provides:
  - D-02 isResolved guard at the single broadcast intake (HandleCIDBroadcast) — re-delivered pruned roots are terminal
  - Test-observable fetch-attempt counter on EnqueueRootCID (Get/ResetRootFetchAttemptsForTest) for reuse by 18-06/18-07
  - SC-3 two-node proof: zero fetch attempts + zero re-created elements for both delivery modes (re-broadcast, restart head exchange) with a negative control
  - EraseNodeCollection two-node capabilities: address-advertised nodes, restartNodePendingStart (persisted-heads restart), connectNodes
affects: [18-06 sweep re-prune suites, 18-07 hardening suites, PRUNE-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Deaf-broadcaster peer: a node with broadcast topics but no listen topics keeps a retired-elsewhere root as its LIVE head forever — the deterministic lagging-peer shape for re-delivery tests"
    - "In-window non-vacuity tracer: a never-erased head on a topic the receiver joins mid-window; gossip's message cache delivers it on join and its fetch proves the intake is live while the pruned root contributes zero"

key-files:
  created: []
  modified:
    - src/crdt/impl/crdt_datastore.cpp
    - src/crdt/crdt_datastore.hpp
    - test/src/crdt/globaldb_erase_test.cpp

key-decisions:
  - "D-02 guard is exactly one insertion in HandleCIDBroadcast between the HasBlock-miss continue and IsCIDInCache; error branch logs-and-continues mirroring the HasBlock error shape; no other check reordered or replaced"
  - "Fetch-attempt counter is a private static std::atomic<uint64_t> member with file-scope definition (GlobalDB::erase_interrupt_hook_for_test_ seam shape) — clears clang-tidy global-variable findings the plan's raw file-scope static would raise"
  - "SC-3 topology: node A deaf (broadcast-only) so the pruned root stays A's live head; B receives, retires via its own put, erases; A's re-broadcast/restart head-exchange then re-delivers the pruned root through real pubsub into B's one guarded intake"

patterns-established:
  - "Deaf-broadcaster lagging-peer topology for re-delivery tests (head can never advance on the deaf node)"
  - "Fetch-attempt delta assertion: reset → delivery window → counter == expected-exactly (zero for pruned roots, one for the tracer) — the strict equality IS the guard proof"

requirements-completed: [PRUNE-05]

# Metrics
duration: 43min
completed: 2026-10-21
---

# Phase 18 Plan 01: D-02 Re-Delivery Guard Summary

**Pruned roots are terminal at the single broadcast intake: one isResolved insertion after the HasBlock miss makes re-delivered pruned roots skip with zero fetch attempts (both delivery modes, proven on real pubsub with a fetch-attempt counter), while unpruned behavior stays byte-identical.**

## Performance

- **Duration:** 43 min
- **Started:** 2026-10-21T17:31:03Z
- **Completed:** 2026-10-21T18:14:00Z
- **Tasks:** 2 (TDD: RED test commit + GREEN implementation commit, then the SC-3 suite)
- **Files modified:** 3

## Accomplishments

### Task 1: Fetch-attempt observable + the D-02 isResolved guard (TDD)

- **RED** (706a142ad): `RootFetchAttemptsCounterIsAResettableZeroObservable` — build failed on the missing static accessors (16-01 RED precedent).
- **GREEN** (e22faa05e):
  - `HandleCIDBroadcast`: one insertion — after the HasBlock-hit `continue`, `dagSyncer_->isResolved(cid)`; error → log + continue (HasBlock error shape); true → trace "Pruned root ... re-delivered - terminal, skipping" naming the CID → continue. Before `IsCIDInCache`. Nothing else in the function changed; `HasBlock` untouched (Pitfall 4: sealed-but-present blocks keep the fast path).
  - `EnqueueRootCID`: `root_fetch_attempts_for_test_.fetch_add(1, relaxed)` immediately before the success return (after `pendingRootQueue_.push`), return-false branch untouched.
  - `crdt_datastore.hpp`: public static `GetRootFetchAttemptsForTest()` / `ResetRootFetchAttemptsForTest()` with test-only Doxygen; private static atomic member.
  - Gates: build clean, clang-format clean, clang-tidy clean on changed lines, `crdt_test` + `globaldb_integration_test` green.

### Task 2: SC-3 two-node suite (5fbbcbbf4)

- `GlobalDBEraseRedeliveryTest` (filter `*Redelivery*`) on `EraseNodeCollection`, real GossipPubSub, two GlobalDB nodes, fresh per-test DB paths, `ASSERT_WAIT_FOR_CONDITION` waits, double-run green.
- **Case 1 (re-broadcast):** A's `BroadcastHeadsForTopics({prune, tracer})` re-delivers the pruned root on B's long-subscribed topic (guard skip); B then joins the tracer topic and gossip's message cache delivers the cached never-erased tracer head — its in-window fetch proves B's intake is live. Asserted fetch-attempt delta == exactly 1, erased keys still absent, seal still present.
- **Case 2 (restart head exchange):** A torn down and recreated on the same base path with `restartNodePendingStart` (db-first teardown, Phase-15 ordering), persisted heads re-asserted (pruned root still A's live head), reconnected, THEN `Start()` so the first `RebroadcastHeads` publishes into a live connection (interval is 60s — an immediate start broadcasts into an empty peer set). Same zero-fetch + zero-re-creation outcome, delta == exactly 1.
- **Case 3 (negative control):** an unpruned never-seen root through the same intake — counter advances (== 1) and the value lands with a seal. The guard does not suppress normal fetches.
- Fixture: `makeNode` advertises the loopback address (wildcard-only listeners make every `Broadcast` silently drop — `addrs_size() <= 0`, pubsub_broadcaster_ext.cpp:371); `connectNodes` dials one direction only (SYN-collision lesson from multi_node_finality_fault_test).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Counter moved from raw file-scope static to a private static class member**
- **Found during:** Task 1 tooling gate
- **Issue:** The plan's "file-scope static std::atomic" raised `readability-identifier-naming` (GlobalVariableCase UPPER_CASE) and `cppcoreguidelines-avoid-non-const-global-variables` findings.
- **Fix:** Declared as a private static member (definition at file scope in the .cpp) — the exact seam shape of the plan's own model (`GlobalDB::erase_interrupt_hook_for_test_`); plan's member name kept verbatim.
- **Files modified:** src/crdt/crdt_datastore.hpp, src/crdt/impl/crdt_datastore.cpp
- **Commit:** e22faa05e

**2. [Rule 3 - Blocking] SC-3 case-1 delivery window redesigned around gossip message-cache delivery-on-join**
- **Found during:** Task 2 first run (case 1 timed out: counter 0 while the tracer HAD been fetched)
- **Issue:** A's earlier publish of the tracer head reaches B the moment B subscribes to the topic (gossip message-cache relay) — subscribing before the measured window consumed the tracer's fetch before the counter reset, leaving the window with no observable arrival.
- **Fix:** Reordered: reset counter → explicit `BroadcastHeadsForTopics` (pruned root re-delivered on B's long-live topic) → B joins the tracer topic (cached tracer broadcast delivers in-window, fetch proves intake liveness) → assert delta == exactly 1. Dropped the `getPeerCount` readiness wait (no publish-after-subscribe race remains).
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Commit:** 5fbbcbbf4

**3. [Rule 2 - Missing functionality] Fixture nodes must advertise a dialable address**
- **Found during:** Task 2 design verification (pre-first-run)
- **Issue:** `EraseNodeCollection` started pubsub wildcard-only with no advertised address; `PubSubBroadcasterExt::Broadcast` drops every message with no addresses (`addrs_size() <= 0`), and receivers cannot graphsync-fetch from the broadcaster — the two-node suite could never deliver or fetch.
- **Fix:** `makeNode` starts pubsub with `{ "127.0.0.1" }` advertised (bind stays wildcard) — the multi_node_finality_fault_test "silently skip the broadcast" lesson applied to this fixture. Existing single-node tests unaffected (green).
- **Files modified:** test/src/crdt/globaldb_erase_test.cpp
- **Commit:** 5fbbcbbf4

## TDD Gate Compliance

- RED gate: `test(18-01)` commit 706a142ad (build failure on missing members, 16-01 precedent) — verified in execution log.
- GREEN gate: `feat(18-01)` commit e22faa05e after the test and both regression suites passed.
- REFACTOR gate: not needed (no post-green cleanup).

## Verification Evidence

- `cmake --build build/OSX/Release --target globaldb_integration_test` — clean, no warnings.
- `ctest --test-dir build/OSX/Release -R "crdt_test|globaldb_integration" --output-on-failure` — 2/2 passed (run after Task 1 and again after Task 2).
- `globaldb_integration_test --gtest_filter='*Redelivery*'` — 3/3 passed, twice consecutively (double-run).
- `/opt/homebrew/opt/llvm@22/bin/git-clang-format --diff HEAD` — "did not modify any files" after each task.
- `/opt/homebrew/opt/llvm@22/bin/clang-tidy -p <filtered-db> src/crdt/impl/crdt_datastore.cpp` — no findings on changed lines (remaining findings are pre-existing code outside the diff; test targets disable clang-tidy per the repo `addtest` macro).
- Guard placement check: exactly one `isResolved` occurrence in `HandleCIDBroadcast`, ordered `Already processed block` → `isResolved` → `IsCIDInCache`.

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0101 (DoS: re-broadcast storm of pruned CIDs reaching the DAG worker) | mitigated — guard skips before enqueue; SC-3 asserts zero fetch attempts, both modes |
| T-18-0102 (Tampering: re-delivery resurrecting pruned elements) | mitigated — guard skips before any processing; cases 1/2 assert zero re-created keys |
| T-18-0103 (DoS: guard suppressing legitimate fetches) | mitigated — case 3 negative control advances the counter; insertion order keeps fast paths unchanged |

## Self-Check: PASSED

- Files exist: src/crdt/crdt_datastore.hpp, src/crdt/impl/crdt_datastore.cpp, test/src/crdt/globaldb_erase_test.cpp, 18-01-SUMMARY.md — all FOUND.
- Commits exist on branch: 706a142ad (test RED), e22faa05e (feat GREEN), 5fbbcbbf4 (test SC-3) — all FOUND via `git log`.
