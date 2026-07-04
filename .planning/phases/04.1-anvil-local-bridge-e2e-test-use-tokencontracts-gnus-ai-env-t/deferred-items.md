# Deferred Items — Phase 04.1

Pre-existing issues discovered during execution that are out of scope for the
current task's changes. Per the executor's scope-boundary rule, these are logged
for follow-up rather than fixed inline.

## 1. Pre-existing build break in `src/account/AccountMessenger.cpp` (build blocker)

- **Discovered during:** Plan 04.1-02, Task 2 build verification.
- **File (untouched by this plan):** `src/account/AccountMessenger.cpp`
- **Last modified (unrelated commit):** `57d6bafd` ("Feat: Adding hasher interface with void pointer and size to encapsulate casts")
- **Symptom:** Fresh compile of `libgenius_node_test` fails on macOS with the
  Xcode / MacOSX26.2.sdk toolchain:
  ```
  src/account/AccountMessenger.cpp:64:36: error: no viable overloaded '='
    instance->subs_acc_future_ = std::move( instance->pubsub_->Subscribe( ... ) );
  note: no known conversion from 'shared_future<shared_ptr<Subscription>>' to
        'const shared_future<shared_ptr<Subscription>>' for 1st argument
  ```
  The same error fires at lines 64 and 79 (the two `pubsub_->Subscribe(...)`
  assignments into `subs_acc_future_` / `subs_requests_future_`). The libc++ in
  MacOSX26.2.sdk rejects the `std::move(...)` assignment of a
  `shared_future<shared_ptr<Subscription>>` rvalue.
- **Why not fixed here:** This file is in the `genius_node_test` dependency
  chain, not in any file this plan touches (Task 1 creates
  `bridge_anvil_catchup_e2e_test.cpp`; Task 2 appends to
  `test/src/bridge_e2e/CMakeLists.txt`). Per executor scope-boundary rule, it is
  out of scope. The main checkout's `build/OSX/Debug` has a stale (working)
  `AccountMessenger.cpp.o` from an earlier build, so the break only surfaces on
  a clean rebuild.
- **Impact on this plan:** Blocks the full `ninja bridge_anvil_catchup_e2e_test`
  link/build verification and `--gtest_list_tests` runtime check. The new test
  source itself was validated via a `-fsyntax-only` compile using the existing
  `bridge_anvil_e2e_test` target's compile flags — it parses cleanly with zero
  warnings/errors after the `INITIALIZING` → `CREATING` enum fix.
- **Suggested follow-up:** A targeted fix to the two assignment sites (likely
  `subs_acc_future_ = instance->pubsub_->Subscribe(...).share();` or storing a
  `std::future` instead of `std::shared_future`, depending on the
  `GossipPubSub::Subscribe` return type). Should be its own bug-fix task — not
  bundled with an E2E test plan.

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
