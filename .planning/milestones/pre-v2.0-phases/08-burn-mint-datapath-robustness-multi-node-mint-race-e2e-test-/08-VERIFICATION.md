---
phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
verified: 2026-07-17T00:00:00Z
status: gaps_found
score: 5/8 must-haves verified
overrides_applied: 0
gaps:
  - truth: "D-02: Recipient balance delta equals exactly one burn amount as observed from all 11 nodes, and stays stable across an additional watcher poll window"
    status: failed
    reason: "The 'stability window' guard is a no-op due to misuse of EXPECT_WAIT_FOR_CONDITION — it returns as soon as node 0 is READY (already true), completing in ~10ms instead of elapsing the documented multi-second window. A delayed double-mint on the watcher's next poll cycle is never observed, so the flagship exactly-once assertion would pass even if dedup were broken. Confirmed by 08-REVIEW.md CR-01."
    artifacts:
      - path: "test/src/bridge_race/bridge_race_single_burn_test.cpp"
        issue: "Lines 76-80: EXPECT_WAIT_FOR_CONDITION checks GetState()==READY (already true) instead of sleeping for kRaceStabilityWindow before the final balance re-check"
      - path: "test/src/bridge_race/bridge_race_batch_test.cpp"
        issue: "Same pattern at lines 113-117"
    missing:
      - "Replace the no-op wait with an actual std::this_thread::sleep_for(kRaceStabilityWindow) (or invert to wait-for-bad-condition-and-assert-timeout) before the final EXPECT_EQ balance check in both single_burn and batch tests"
  - truth: "D-11: A pubsub partition that isolates a subset of nodes, followed by a heal, results in CRDT convergence to exactly-once mint (partition genuinely exercised, not collapsed)"
    status: failed
    reason: "The pre-heal partition window has the identical EXPECT_WAIT_FOR_CONDITION no-op defect as CR-01 — node 0 is already READY, so the 12s kPrePartitionHealWindow collapses to ~10ms and AddPeers() heal runs almost immediately after ConfigureRpcEndpoint. The test degenerates into a momentary (<1s) disconnect indistinguishable from the plain single-burn test; true independent-mint-attempt-during-partition-then-reconcile semantics are not exercised, yet the test passes. Confirmed by 08-REVIEW.md CR-02."
    artifacts:
      - path: "test/src/bridge_race/bridge_race_fault_partition_test.cpp"
        issue: "Lines 101-105: same EXPECT_WAIT_FOR_CONDITION misuse as CR-01, collapsing kPrePartitionHealWindow to near-zero"
    missing:
      - "Replace with std::this_thread::sleep_for(kPrePartitionHealWindow) before the heal/AddPeers loop, ideally asserting each sub-group independently observed the mint pre-heal to positively prove divergence occurred"
  - truth: "MockRpcTransport handles all RPC methods issued by the validator/watcher datapath robustly under fault injection (no uncaught exceptions)"
    status: failed
    reason: "ExtractTxHash is called unconditionally at the top of MockRpcTransport::call() and throws std::out_of_range/std::invalid_argument for any request without a params array of a leading string (e.g. eth_blockNumber, eth_chainId, eth_getLogs filter objects) instead of returning nullopt as the API contract implies. This propagates uncaught exceptions into production validator/watcher code paths. Fault-injection tests currently pass only by accident of the validator issuing exclusively eth_getTransactionReceipt-shaped requests; any method-set drift is a live crash risk in the exact code the phase is meant to stress-test. Confirmed by 08-REVIEW.md CR-03."
    artifacts:
      - path: "test/src/mock/mock_rpc_transport.cpp"
        issue: "Lines 38-42 and 131-134: unconditional request.at(\"params\").as_array().at(0).as_string() with no defensive checks before dispatch on behavior type"
    missing:
      - "Guard ExtractTxHash with if_contains/is_array/is_string checks returning std::nullopt on any mismatch, and have call() handle nullopt gracefully for non-receipt request shapes"
deferred: []
human_verification:
  - test: "Run the 5 bridge_race binaries (single_burn, batch, fault_rpc, fault_kill, fault_partition) end-to-end against a live Anvil/Foundry fork with the CR-01/CR-02/CR-03 fixes applied, observing runtime pass/fail and timing"
    expected: "All 5 binaries pass; exactly-once mint holds across the full stability/partition windows; no uncaught exceptions from the mock transport under any RPC method mix"
    why_human: "Requires Foundry/Anvil + network + multi-second/multi-minute runtime; not exercised by this verification pass (orchestrator confirmed compile/link success only, no execution)"
  - test: "Run each of the 3 fuzz binaries under -DSGNS_FUZZING=ON for a smoke duration (~60s) against the checked-in seed corpus, and separately for a longer local fuzzing session"
    expected: "No ASan/UBSan crashes; corpus replay completes within the 60s smoke-run budget"
    why_human: "Requires Clang toolchain + libFuzzer runtime execution; orchestrator confirmed only that targets are absent by default and present under -DSGNS_FUZZING=ON (cmake target-listing check), not that they run clean"
---

# Phase 8: Burn/Mint Datapath Robustness Verification Report

**Phase Goal:** Burn/mint datapath robustness — multi-node mint-race e2e test (one burn observed concurrently by all nodes' watchers, exactly-once mint across an 11-node cluster), fault injection (node kill mid-mint, RPC disagreement, pubsub partition + heal), and libFuzzer harnesses (ParseBurnEventValues, ABI decode, transaction deserialize) behind -DSGNS_FUZZING=ON.
**Verified:** 2026-07-17
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | D-01: Every one of 11 nodes' watchers independently discovers the same seeded burn without manual MintTokens() calls | ✓ VERIFIED | `bridge_race_fixture.hpp` builds 11-node cluster (1 Full + 10 Light); `bridge_race_single_burn_test.cpp`/`bridge_race_batch_test.cpp` seed burns then configure RPC endpoints on all nodes to release watchers together (D-03 pattern); no `MintTokens()` call present in any bridge_race test file (grep confirms zero manual calls) |
| 2 | D-02: Exactly-once balance delta observed from all 11 nodes, stable across an additional watcher poll window | ✗ FAILED | CR-01 (08-REVIEW.md): stability window is a no-op — `EXPECT_WAIT_FOR_CONDITION` returns in ~10ms because node 0 is already READY; the documented multi-second stability window never elapses. A delayed double-mint on the next poll cycle is never caught. |
| 3 | A 3-5 burn batch mints exactly once each with no cross-burn interference | ⚠️ PARTIAL | `bridge_race_batch_test.cpp` exists (148 lines) and seeds a batch of burns targeting distinct Light-node addresses, but shares the same CR-01 stability-window defect as truth #2 — the no-double-mint claim for the batch is unverified for the same reason |
| 4 | D-16: Tests run against Anvil fork of Sepolia, skip cleanly when Foundry/network unavailable | ✓ VERIFIED | `bridge_race_fixture.hpp` reuses `anvil_fixture.hpp`'s `AnvilProcess`/`FundAccount0WithGnus` patterns (per SUMMARY and PLAN evidence); GTEST_SKIP pattern preserved per established convention |
| 5 | New bridge_race ctest target isolated from bridge_e2e suite with own timeout | ✓ VERIFIED | `test/src/bridge_race/CMakeLists.txt` registers 5 distinct binaries via `addtest()` with `TIMEOUT 180`; `test/src/CMakeLists.txt` adds the subdirectory separately from `bridge_e2e` |
| 6 | D-09: RPC faults delivered via extended Phase-5 MockRpcTransport with per-slot divergence, correct quorum decision under disagreement | ⚠️ PARTIAL | `mock_rpc_config.hpp`/`mock_rpc_transport.cpp` extend the existing transport with `BuildDivergentSlotConfigs()` and `SetTransportFactory()` wiring (`bridge_race_fault_rpc_test.cpp`, 156 lines); however CR-03 shows the transport throws uncaught exceptions on any non-receipt RPC method, and WR-01 shows test 2 may pass via test-1 leftover state without exercising the PUBLIC-pair quorum path — the "genuinely exercised disagreement" claim is not fully substantiated |
| 7 | Node kill (object destruction, node.reset()) mid-mint does not prevent remaining 10 nodes converging on exactly-once mint | ✓ VERIFIED | `bridge_race_fault_kill_test.cpp` (110 lines) calls `node.reset()` mid-mint per D-10; no equivalent no-op defect identified for this test in 08-REVIEW.md |
| 8 | D-11: Pubsub partition + heal at libp2p layer results in CRDT convergence to exactly-once mint | ✗ FAILED | CR-02 (08-REVIEW.md): the pre-heal partition window has the identical no-op defect as CR-01 — `kPrePartitionHealWindow` (12s) collapses to ~10ms, so sub-groups never independently diverge before healing. The test degenerates to a momentary disconnect and does not exercise true partition/reconcile semantics, despite passing. |
| 9 | D-12/D-13/D-14: Three libFuzzer harnesses (ParseBurnEventValues, ABI decode, tx deserialize) built behind -DSGNS_FUZZING=ON, normal build unaffected | ✓ VERIFIED | `fuzz/fuzz_parse_burn_event_values.cpp` (146 lines), `fuzz/fuzz_decode_log_data.cpp` (34 lines), `fuzz/fuzz_mint_transaction_deserialize.cpp` (41 lines) all present with `LLVMFuzzerTestOneInput`; `cmake/functions.cmake` defines `addfuzztarget()`; `test/CMakeLists.txt` gates `add_subdirectory(fuzz)` behind `option(SGNS_FUZZING OFF)`; orchestrator-verified cmake target-listing shows fuzz targets absent by default, present under the flag |

**Score:** 5/9 truths fully verified (2 FAILED as no-ops that pass without exercising the invariant, 2 PARTIAL due to review-confirmed defects)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `test/src/bridge_race/bridge_race_fixture.hpp` | 11-node fixture | ✓ VERIFIED | 286 lines, well above 120-line minimum |
| `test/src/bridge_race/bridge_race_single_burn_test.cpp` | Single-burn race test | ⚠️ STUB-LIKE | 95 lines (exists, substantive), but core assertion is a no-op (CR-01) |
| `test/src/bridge_race/bridge_race_batch_test.cpp` | Batch race test | ⚠️ STUB-LIKE | 148 lines, same CR-01 defect |
| `test/src/bridge_race/CMakeLists.txt` | Test registration | ✓ VERIFIED | 5 `addtest()` calls, `TIMEOUT 180` set (WR-03: missing RESOURCE_LOCK, ports may collide under parallel ctest) |
| `test/src/mock/mock_rpc_config.hpp` | `BuildDivergentSlotConfigs()` | ✓ VERIFIED | 53 lines, function present |
| `test/src/bridge_race/bridge_race_fault_rpc_test.cpp` | RPC disagreement test | ⚠️ WEAKENED | 156 lines; WR-01 shows test 2 may not exercise the PUBLIC-pair quorum path it claims to test |
| `test/src/bridge_race/bridge_race_fault_kill_test.cpp` | Node-kill test | ✓ VERIFIED | 110 lines, no defect flagged |
| `test/src/bridge_race/bridge_race_fault_partition_test.cpp` | Partition+heal test | ⚠️ STUB-LIKE | 145 lines, CR-02 collapses the partition window to near-zero |
| `cmake/functions.cmake` | `addfuzztarget()` | ✓ VERIFIED | function defined, gated correctly |
| `fuzz/CMakeLists.txt` + 3 harness files | Fuzz targets | ✓ VERIFIED | All 3 harnesses present with correct sizes; SGNS_FUZZING gating correct (orchestrator-confirmed) |
| `test/src/mock/mock_rpc_transport.cpp` | Extended mock transport | ⚠️ FRAGILE | 298 lines; CR-03 uncaught-exception bug on non-receipt requests is a live robustness gap in the exact fault-injection mechanism the phase is meant to prove out |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `bridge_race_single_burn_test.cpp` | `bridge_race_fixture.hpp` | TEST_F inheritance | ✓ WIRED | Class inheritance confirmed present |
| `test/src/CMakeLists.txt` | `test/src/bridge_race/CMakeLists.txt` | add_subdirectory | ✓ WIRED | Confirmed in PLAN cross-reference |
| `test/CMakeLists.txt` | `fuzz/CMakeLists.txt` | SGNS_FUZZING-guarded add_subdirectory | ✓ WIRED | `test/CMakeLists.txt:9-12` — option + conditional add_subdirectory confirmed |
| `bridge_race_fault_rpc_test.cpp` | `PublicChainInputValidator.hpp` | SetTransportFactory | ✓ WIRED | Confirmed present per PLAN 08-03 evidence, though WR-01 undermines the test's stated purpose |
| `bridge_race_fault_partition_test.cpp` | libp2p Host::disconnect | AddPeers/disconnect | ✓ WIRED (mechanically) | Present, but effectively inert due to CR-02 window collapse |

### Behavioral Spot-Checks

Not run directly by this verifier — relying on orchestrator-provided evidence per task brief:
- Compile/link: PASS (all 5 bridge_race binaries + mock/relayer tests build in build/OSX/Release)
- `ctest -R "bridge_relayer_test|mock_rpc_test"`: PASS (regression gate, unrelated to new suite's runtime correctness)
- Fuzz target gating: PASS (absent by default, present with `-DSGNS_FUZZING=ON`)
- **Race/fault e2e test runtime execution: NOT RUN** (requires Foundry/Anvil + network) — this is the critical gap: the tests that compile and "pass" structurally contain the no-op defects (CR-01, CR-02) that would make them pass even when the underlying invariant is violated. Compilation and even a green ctest run would NOT catch these defects, since the assertions themselves never elapse the required windows.

### Probe Execution

No formal probe scripts found under `scripts/*/tests/probe-*.sh` for this phase; N/A.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| D-01 | 08-01 | Watcher-driven race trigger, zero manual MintTokens | ✓ SATISFIED | Fixture + tests confirmed, no manual mint calls found |
| D-02 | 08-01 | Exactly-once + stability window | ✗ BLOCKED | CR-01 no-op defect |
| D-03 | 08-01 | Seed-before-poll race window | ✓ SATISFIED | Pattern confirmed in PLAN/fixture design |
| D-04 | 08-01 | Single + batch race tests | ⚠️ WEAKENED | Both exist but share CR-01 defect |
| D-05/D-06 | 08-01 | 11-node topology, compile-time kNodeCount | ✓ SATISFIED | Fixture confirmed |
| D-07 | 08-01 | Burns target Light-node addresses | ✓ SATISFIED | Per PLAN/SUMMARY description |
| D-08 | 08-03/08-04 | Four fault scenarios in scope | ⚠️ WEAKENED | RPC (weakened, WR-01/CR-03), kill (ok), partition (failed, CR-02) |
| D-09 | 08-03 | Extended Mock RPC Transport, per-slot divergence | ⚠️ WEAKENED | Exists, but CR-03 exception-safety bug and WR-01 quorum-path gap |
| D-10 | 08-04 | Node kill via object destruction | ✓ SATISFIED | Confirmed |
| D-11 | 08-04 | Partition + heal, CRDT convergence | ✗ BLOCKED | CR-02 no-op defect collapses partition window |
| D-12 | 08-02 | 3 fuzz targets selected correctly | ✓ SATISFIED | All 3 harnesses present, matching selected targets |
| D-13 | 08-02 | CMake gating behind SGNS_FUZZING | ✓ SATISFIED | Confirmed; WR-05 notes fail-fast-on-non-Clang improvement opportunity (non-blocking) |
| D-14 | 08-02 | CI smoke cadence, seed corpus checked in | ? NEEDS HUMAN | Corpus files present per file listing in PLAN frontmatter; actual <60s smoke-run timing not executed by this verifier |
| D-15 | 08-01 | Isolated ctest target/timeout | ✓ SATISFIED | Confirmed, though WR-03 flags port-collision risk under parallel ctest |
| D-16 | 08-01 | Anvil fork, clean skip | ✓ SATISFIED | Confirmed per fixture reuse pattern |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `bridge_race_single_burn_test.cpp` | 76-80 | No-op wait masking as stability check | 🛑 Blocker | Flagship exactly-once invariant unverified for delayed double-mint |
| `bridge_race_batch_test.cpp` | 113-117 | Same no-op pattern | 🛑 Blocker | Same as above, batch scope |
| `bridge_race_fault_partition_test.cpp` | 101-105 | Same no-op pattern | 🛑 Blocker | D-11 partition divergence never exercised |
| `mock_rpc_transport.cpp` | 38-42, 131-134 | Uncaught exceptions on non-receipt requests | ⚠️ Warning | Fault-injection mechanism itself is fragile; currently masked by narrow request-shape coverage |
| `bridge_race_fault_rpc_test.cpp` | 126-146 | Shared suite-level state leaking prior test's transport config | ⚠️ Warning | Test 2 may not exercise its stated PUBLIC-pair quorum path (WR-01) |
| `bridge_race/CMakeLists.txt` | all | No RESOURCE_LOCK/RUN_SERIAL despite fixed ports | ⚠️ Warning | Flaky port collisions possible under `ctest -j N` (WR-03) |

No unreferenced TBD/FIXME/XXX debt markers found in the reviewed files (08-REVIEW.md did not flag any).

### Human Verification Required

### 1. Full e2e execution of the 5 bridge_race binaries against live Anvil

**Test:** Run all 5 bridge_race binaries end-to-end (after CR-01/CR-02/CR-03 fixes) against Foundry/Anvil with network access
**Expected:** All 5 pass with the stability/partition windows genuinely elapsing; exactly-once invariant holds under real multi-second concurrency
**Why human:** Requires Foundry/Anvil + network + multi-minute runtime; this verification pass only confirms compile/link and structural code review, not runtime behavior

### 2. Fuzz harness smoke + deep runs

**Test:** Build with `-DSGNS_FUZZING=ON` under Clang and run each of the 3 fuzz binaries for the ~60s smoke duration against the seed corpus, plus optionally a longer local deep run
**Expected:** No ASan crashes, corpus replay completes within budget
**Why human:** Requires Clang + libFuzzer execution environment not exercised by this static verification

## Gaps Summary

The phase delivered all required artifacts (fixture, 2 race tests, 3 fault-injection tests, 3 fuzz harnesses, CMake wiring) and they compile/link successfully. However, code review (08-REVIEW.md) identified 3 Critical defects that undermine two of the phase's核心 (core) success criteria at the assertion level, not merely as style nits:

1. **CR-01 (single_burn + batch tests):** The "stability window" meant to catch delayed double-mints is a no-op — it checks a condition that is already true, so it returns in ~10ms instead of elapsing the documented multi-second window. This means D-02 (the flagship exactly-once invariant, explicitly called out in the phase goal) is **not actually verified** by these tests; they would pass even with a broken/delayed-double-mint dedup path.

2. **CR-02 (partition test):** The identical no-op defect collapses the pre-heal partition window to near-zero, meaning D-11 (partition + heal → CRDT convergence, the "harder correctness case" per 08-RESEARCH.md) is **not genuinely exercised**. The test degenerates into a momentary disconnect indistinguishable from the plain single-burn scenario.

3. **CR-03 (mock transport):** Uncaught exceptions on any non-`eth_getTransactionReceipt`-shaped RPC request make the fault-injection mechanism itself fragile — a live crash risk in code paths this phase specifically exists to stress.

Two Warnings compound these gaps: WR-01 shows the RPC-disagreement test's second case may silently not exercise the PUBLIC-pair quorum path it claims to test, and WR-03 flags port-collision risk that could produce misleading CI flakiness unrelated to the code under test.

Since the phase goal explicitly names "exactly-once mint" and "pubsub partition + heal" as the core deliverables, and the tests built to prove these currently cannot fail even when the underlying invariant is broken, the phase goal is **not fully achieved** as implemented. The fixes are narrow (each is a documented one-line-to-few-line change in 08-REVIEW.md) but must be applied and the affected tests re-run against live Anvil before this phase can be considered complete.

This looks like an implementation gap rather than an intentional deviation — no override is suggested.

---

_Verified: 2026-07-17_
_Verifier: Claude (gsd-verifier)_

## Post-Fix Update — 2026-07-21

CR-01, CR-02, CR-03 (above) were fixed and rebuilt (commit `b14f0dc8`). Foundry was installed and
the bridge_race suite was run against a real Anvil/Sepolia fork for the first time — this surfaced
two further defects that static review/compile-only verification could not have caught, since the
prior `SetUpTestSuite` never actually completed in any environment before now:

1. **Genesis-authority address-derivation race (found and fixed, commit `87ae6c9f`):** the fixture's
   address precomputation didn't replicate `GeniusAccount::GenerateGeniusAddress()`'s sign+SHA-256
   transformation, so the address registered via `SetAuthorizedFullNodeAddress()` never matched any
   real node — `Blockchain::EnsureValidatorRegistry()`'s address check always failed, and the
   validator registry never initialized (`SetUpTestSuite` deadlocked at the 90s READY timeout for
   every prior run). Fixed by creating Light nodes first (real addresses registered), Full node
   last (registered immediately, no intervening node creation).

2. **Missing `bridge_chains_config.json` watcher wiring (found and fixed, same commit):** the
   fixture called `ConfigureRpcEndpoint()` (verification-quorum path) but never wrote the per-node
   config that points `BridgeCatchupWatcher` at the local Anvil fork — watchers fell back to the
   default production chain list and could never see a burn seeded only on the local fork. Fixed by
   adding the missing config write (mirrors `BridgeAnvilCatchupE2ETest`'s established pattern).

**Current status after both fixes:** `SetUpTestSuite` no longer deadlocks, and the watcher→mint
pipeline genuinely works when reached — 2 of 11 nodes independently scanned the correct block
range and minted the seeded burn. However, only 2 of 11 nodes reached the
`InitializeAndStartBridge` lifecycle stage within the 90s window; the test still fails overall.
This was root-caused via a `/gsd:debug` session (see below).

## Post-Debug Update — 2026-07-21 (later same day)

`/gsd:debug` session `2-of-11-nodes-start-bridge` (resolved, archived at
`.planning/debug/resolved/2-of-11-nodes-start-bridge.md`) found and fixed the actual root cause:

3. **`ValidatorRegistry` never retried genesis-registry discovery (fixed, commit `a133fced`):**
   `ValidatorRegistry::InitializeCache()` attempts discovery exactly once, synchronously, at
   construction. If the registry hasn't synced into a node's local CRDT store yet (the common case
   for most nodes in an 11-node concurrent bootstrap), it silently gives up — no retry, no
   `NotifyInitialized()` call. The only remaining path was a passive broadcast that isn't guaranteed
   to reach every node. Added `RetryInitializationIfNeeded()`, wired into `Blockchain::Start()`'s
   existing deferred-retry cadence (already firing every ~5-10s). **Verified: `InitializeAndStartBridge`
   now fires for 11/11 nodes**, confirmed across two independent runs.

4. **Stale per-node data directories surviving a crash (fixed, commit `fdfe98d5`):** while
   independently re-verifying fix #3, the orchestrator's own rerun **segfaulted**. Root cause (spotted
   by the user): `TearDownTestSuite`'s directory cleanup only runs on a clean exit; the earlier crash
   skipped it, leaving stale RocksDB/CRDT state that the next run bootstrapped against. Added a
   proactive `remove_all()` at the start of `SetUpTestSuite`. **Verified: no more segfault** — the
   fixed run completed its full gtest assertion cycle cleanly.

**Current status:** both the original startup-gap bug and the crash are resolved. A clean run now
shows 33 mint attempts (up from 2-6) — most nodes are genuinely racing — but not all 11 complete
within the 90s window, and teardown ran long enough to graze ctest's outer 180s limit. Tracked as
`.planning/todos/pending/bridge-race-not-all-11-mint-within-window.md` (`resolves_phase: 08`) for a
dedicated follow-up.

**Revised score:** significant, confirmed progress (3 real bugs found and fixed via actual
execution across this phase — genesis-authority race, missing watcher config, `ValidatorRegistry`
retry gap — plus a crash fixed as a byproduct). Phase is **not yet complete** — one narrower,
better-characterized timing gap remains. `gaps_found` stands, but the gap is now much smaller than
at any prior checkpoint.
