---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
plan: 02
subsystem: build-system, fuzzing
tags: [libfuzzer, asan, cmake, security-testing]
dependency-graph:
  requires: []
  provides:
    - addfuzztarget() CMake function
    - SGNS_FUZZING build option
    - fuzz_decode_log_data harness
    - fuzz_parse_burn_event_values harness
    - fuzz_mint_transaction_deserialize harness
  affects:
    - cmake/functions.cmake
    - test/CMakeLists.txt
tech-stack:
  added: [libFuzzer, AddressSanitizer]
  patterns:
    - "addfuzztarget() mirrors addtest()'s structure (executable creation, output dir, disable_clang_tidy) but swaps GTest linkage for -fsanitize=fuzzer,address"
    - "SGNS_FUZZING option (default OFF) gates add_subdirectory(fuzz) entirely — zero-cost when unset"
key-files:
  created:
    - fuzz/CMakeLists.txt
    - fuzz/fuzz_decode_log_data.cpp
    - fuzz/fuzz_parse_burn_event_values.cpp
    - fuzz/fuzz_mint_transaction_deserialize.cpp
    - fuzz/corpus/decode_log_data/seed_0
    - fuzz/corpus/parse_burn_event_values/seed_0
    - fuzz/corpus/mint_transaction_deserialize/seed_0
  modified:
    - cmake/functions.cmake
    - test/CMakeLists.txt
decisions:
  - "Link target for all 3 harnesses is genius_node_test (confirmed via test/src/account/CMakeLists.txt's bridge_relayer_test, which already links BridgeRelayer.cpp's home library this way)"
  - "MintTransactionV2::DeSerializeByteVector confirmed to return nullptr (not throw) on malformed input via direct read of MintTransactionV2.cpp:93-100 — harness still wraps in try/catch defensively per plan instructions"
  - "mint_transaction_deserialize seed_0 is an empty (0-byte) file — protobuf ParseFromArray accepts empty bytes as a valid all-defaults message, which is the simplest true-positive seed"
metrics:
  duration: "~35 min"
  completed: "2026-07-17"
---

# Phase 08 Plan 02: libFuzzer+ASan Harness Tier Summary

Established the project's first opt-in `-DSGNS_FUZZING=ON` build tier with three libFuzzer+ASan harnesses covering the burn/mint datapath's only untrusted-byte-parsing entry points: `eth::abi::decode_log_data`, `BridgeRelayer::ParseBurnEventValues`, and `MintTransactionV2::DeSerializeByteVector`.

## What Was Built

**Task 1 — `addfuzztarget()` + `SGNS_FUZZING` wiring:**
- `cmake/functions.cmake`: new `addfuzztarget(target_name)` function, placed after `addtest_part()`. Creates the executable, applies `-fsanitize=fuzzer,address -g -O1` compile flags and matching link flags, sets `RUNTIME_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/fuzz_bin`, and calls `disable_clang_tidy()`. The function itself is unconditional (no `if(SGNS_FUZZING)` wrapper) — gating happens entirely at the call sites.
- `test/CMakeLists.txt`: added `option(SGNS_FUZZING "Build libFuzzer+ASan harnesses (Clang only)" OFF)` followed by a guarded `add_subdirectory(${CMAKE_SOURCE_DIR}/fuzz ${CMAKE_BINARY_DIR}/fuzz)`, appended after the existing `add_subdirectory(testutil)` line (no reordering). A `message(STATUS ...)` warns if the compiler isn't Clang-family when `SGNS_FUZZING=ON`.

**Task 2 — three harnesses + `fuzz/CMakeLists.txt`:**
- `fuzz_decode_log_data.cpp`: wraps raw fuzzer bytes as a `ByteBuffer` and calls `eth::abi::decode_log_data` against a fixed 5-param vector matching the v2 `bridgeOut` event's non-indexed layout (`tokenId(uint)`, `amount(uint)`, `destChainID(uint)`, `sgnsDestination(bytes32)`, `destinationYOdd(bool)`).
- `fuzz_parse_burn_event_values.cpp`: deterministically decodes fuzzer bytes into a 7-slot `std::vector<eth::abi::AbiValue>` via a documented cursor-based byte scheme (1 selector byte picks the variant `mod 6`, followed by that variant's fixed/length-prefixed payload), then calls `BridgeRelayer::ParseBurnEventValues`. 7 slots exercise both the `kExpectedMinParams=6` floor and the `kDestinationYOddIndex=6` v2 branch.
- `fuzz_mint_transaction_deserialize.cpp`: calls `MintTransactionV2::DeSerializeByteVector` on raw bytes, wrapped in try/catch (confirmed via source read that the implementation returns `nullptr` rather than throwing on malformed protobuf/hash input, but the harness stays defensive).
- `fuzz/CMakeLists.txt`: registers all three via `addfuzztarget()`, links `genius_node_test`, adds include dirs for `src/` and `evmrelay/include`, and registers three `_smoke` `add_test()` entries with `-max_total_time=60`.

**Task 3 — seed corpus:**
- `decode_log_data/seed_0`: 5×32-byte ABI head words (all fixed-size params, no dynamic types, so decode order == byte order).
- `parse_burn_event_values/seed_0`: 7-slot byte stream built per the harness's own documented scheme — slot1/slot2 are valid `uint256`, slot5 is a valid 64-byte v1 `ByteBuffer` destination, slot6 is `destinationYOdd=true`.
- `mint_transaction_deserialize/seed_0`: an empty (0-byte) file — a valid all-defaults `MintTxV2` protobuf message.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking issue] `evmrelay` git submodule was uninitialized**
- **Found during:** Task 2 (reading `evmrelay/include/eth/abi_decoder.hpp` per the plan's `<interfaces>` section — the file did not exist)
- **Issue:** `evmrelay/` was an empty directory; the submodule had never been checked out in this worktree, blocking any read of `AbiValue`, `AbiParamKind`, `decode_log_data`, or `ByteBuffer`/`Hash256` type definitions needed to write Task 2's harnesses.
- **Fix:** `git submodule update --init evmrelay` — checked out the exact commit already pinned in `.gitmodules`/the git index (`19e6daff54cf64b2e9027c684f55de3fe90b4775`), no new dependency or package introduced.
- **Files modified:** none (submodule checkout only; not part of any commit's diff)
- **Commit:** N/A — submodule checkout is a working-tree state fix, not a tracked-file change in this repo

## Known Limitations

**Full build/ctest verification not run.** This worktree sandbox has no configured CMake build directory and is missing `boost/outcome` (fetched via the project's normal dependency-resolution step, which was out of scope to run here). A standalone `clang++ -fsyntax-only` check confirmed the harness `.cpp` files are reachable up to the point of a missing vendored Boost.Outcome header, but a full `-DSGNS_FUZZING=ON` build + the three `_smoke` ctest invocations (Task 1's and Task 2's `<test-command>` acceptance criteria, and all of Task 3's) could not be executed in this environment. Code was written and cross-checked directly against:
- `src/account/BridgeRelayer.cpp`'s actual `ParseBurnEventValues` implementation (constants, branch conditions, variant checks)
- `evmrelay/include/eth/abi_decoder.hpp`'s actual `AbiValue`/`AbiParamKind`/`AbiParam`/`decode_log_data` signatures
- `src/account/MintTransactionV2.cpp`'s actual `DeSerializeByteVector` implementation (confirmed nullptr-on-failure, not throw)
- `test/src/account/CMakeLists.txt`'s existing `bridge_relayer_test` target (confirmed `genius_node_test` as the correct link target)

Recommend the orchestrator/CI run the full `-DSGNS_FUZZING=ON` build + `ctest -R fuzz_.*_smoke` on a Clang toolchain to close out Task 2/Task 3's `<test-command>` acceptance criteria before treating this plan as fully verified.

## Self-Check: PASSED

- FOUND: cmake/functions.cmake (contains `function(addfuzztarget`)
- FOUND: test/CMakeLists.txt (contains `option(SGNS_FUZZING` and `if(SGNS_FUZZING)`)
- FOUND: fuzz/CMakeLists.txt
- FOUND: fuzz/fuzz_decode_log_data.cpp
- FOUND: fuzz/fuzz_parse_burn_event_values.cpp
- FOUND: fuzz/fuzz_mint_transaction_deserialize.cpp
- FOUND: fuzz/corpus/decode_log_data/seed_0
- FOUND: fuzz/corpus/parse_burn_event_values/seed_0
- FOUND: fuzz/corpus/mint_transaction_deserialize/seed_0
- FOUND commit 16ce6a6c (Task 1)
- FOUND commit 36aa7d8b (Task 2)
- FOUND commit a97ac2ba (Task 3)
