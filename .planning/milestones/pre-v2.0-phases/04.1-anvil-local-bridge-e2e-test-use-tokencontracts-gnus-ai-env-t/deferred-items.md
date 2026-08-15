# Deferred Items — Phase 04.1

Pre-existing issues discovered during execution that are out of scope for the
current task's changes. Per the executor's scope-boundary rule, these are logged
for follow-up rather than fixed inline.

## 1. Pre-existing build break: `shared_future`/`Subscription` mismatch (build blocker)

- **Discovered during:** Plan 04.1-02, Task 2 build verification.
- **Root cause:** A systemic type mismatch between
  `GossipPubSub::Subscribe(...)`'s return type
  (`shared_future<shared_ptr<sgns::ipfs_pubsub::GossipPubSub::Subscription>>`)
  and the lvalue it is assigned into
  (`shared_future<shared_ptr<libp2p::protocol::Subscription>>` at some sites,
  plain `shared_future<shared_ptr<Subscription>>` assignment at others). The
  libc++ shipped with Xcode's MacOSX26.2.sdk rejects the conversion/assignment;
  older libc++ accepted it. This is NOT a regression from this plan — it
  reproduces on a clean rebuild of the EXISTING Plan 04.1-01 target too.
- **Files affected (none touched by this plan):**
  - `src/account/AccountMessenger.cpp` (lines 64, 79 — `subs_acc_future_` / `subs_requests_future_`)
  - `src/crdt/globaldb/pubsub_broadcaster_ext.cpp` (lines 108, 347 — via `unity_0_cxx.cxx`)
  - `src/blockchain/Consensus.cpp` (line 81)
- **Last unrelated commit touching `AccountMessenger.cpp`:** `57d6bafd`
  ("Feat: Adding hasher interface with void pointer and size to encapsulate casts").
- **Why not fixed here:** All three files are in the `genius_node_test` /
  `blockchain_genesis` / `crdt_globaldb` dependency chains — none are in scope
  for an E2E test plan that creates one `.cpp` and appends one `addtest` block.
  Per executor scope-boundary rule, these are out of scope. The main checkout's
  `build/OSX/Debug` has stale (working) `.o` files from earlier builds, so the
  break only surfaces on a clean rebuild (e.g. inside a fresh worktree).
- **Impact on this plan:** Blocks the full `ninja bridge_anvil_catchup_e2e_test`
  link and the `--gtest_list_tests` runtime check. Verification performed
  instead:
  1. `cmake .. -DBUILD_TESTING=ON` succeeds — the new CMake target configures.
  2. `ninja test/src/bridge_e2e/CMakeFiles/bridge_anvil_catchup_e2e_test.dir/bridge_anvil_catchup_e2e_test.cpp.o`
     succeeds (exit 0, zero warnings) — the new test source compiles cleanly
     against the project headers using the exact compile flags of the existing
     `bridge_anvil_e2e_test` target.
  3. `-fsyntax-only` compile of the new test source: clean (return 0, no
     warnings/errors) after the `INITIALIZING` → `CREATING` enum fix.
  4. All 16 static acceptance-criteria greps pass.
- **Suggested follow-up:** A single bug-fix task addressing the
  `GossipPubSub::Subscribe` return-type / `shared_future` assignment pattern
  across the three files. Likely either updating the lvalue types to match the
  `sgns::ipfs_pubsub::GossipPubSub::Subscription` return, or adding `.share()`
  / switching to `std::future`. Should be its own task — not bundled with an
  E2E test plan. This will unblock clean-rebuild verification for ALL bridge
  E2E tests (Plans 04.1-01 and 04.1-02).

## 2. Worktree submodules not initialized by default

- **Discovered during:** Plan 04.1-02, Task 2 build verification.
- **Symptom:** A freshly spawned Claude Code worktree under
  `.claude/worktrees/agent-*` does not have `evmrelay`, `SGProcessingManager`,
  `GeniusKDF`, `ProofSystem` submodules checked out. `cmake ..` fails with
  "does not contain a CMakeLists.txt file" for each.
- **Workaround applied:** `git submodule update --init --depth 1 evmrelay
  SGProcessingManager GeniusKDF ProofSystem` succeeded and unblocked cmake
  configure. This is an environment note for future worktree executors, not a
  code defect.
