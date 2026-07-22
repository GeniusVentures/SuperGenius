---
phase: 8
slug: burn-mint-datapath-robustness-multi-node-mint-race-e2e-test
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-07-16
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (GTest/GMock) via CMake `addtest()`; libFuzzer targets via new `addfuzztarget()` |
| **Config file** | `cmake/functions.cmake`, per-suite `CMakeLists.txt` |
| **Quick run command** | targeted test binary from `build/.../test_bin/` (per plan) |
| **Full suite command** | `ctest` in the platform build dir |
| **Estimated runtime** | race suite is heavyweight (11 nodes + Anvil); quick runs use unit-level targets |

---

## Sampling Rate

- **After every task commit:** Run the targeted test binary for the touched suite
- **After every plan wave:** Run the affected suite binaries (race suite gated on Foundry availability)
- **Before `/gsd:verify-work`:** Full suite must be green; fuzz smoke (60s/target) must not crash
- **Max feedback latency:** minutes for the race suite; seconds for fuzz smoke and mock-transport units

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 08-01-T1 | 08-01 | 1 | D-01,D-02,D-03,D-05,D-06,D-07,D-15,D-16 | T-08-01 | 11-node fixture builds; node keys via `DeriveNodeKey()`, no hardcoded 11-key array | e2e (compile) | build of Task 2's test binary (includes this header) | ❌ Wave 0 |
| 08-01-T2 | 08-01 | 1 | D-01,D-02,D-03,D-04 | T-08-01 | Single contested burn + 4-burn batch: exactly-once mint via watcher race, zero manual mint calls | e2e | `ctest -R bridge_race_single_burn_test --output-on-failure`; `ctest -R bridge_race_batch_test --output-on-failure` | ❌ Wave 0 |
| 08-01-T3 | 08-01 | 1 | D-15 | T-08-02 | New ctest target isolated from `bridge_e2e`, own TIMEOUT | e2e (build/registration) | `ctest -N -R bridge_race` | ❌ Wave 0 |
| 08-02-T1 | 08-02 | 1 | D-13 | T-08-05 | `addfuzztarget()` + `SGNS_FUZZING` option; default build unaffected | build | `cmake --build . --target help \| grep -c '^fuzz_'` returns 0 (default) / 3 (`-DSGNS_FUZZING=ON`) | ❌ Wave 0 |
| 08-02-T2 | 08-02 | 1 | D-12,D-13 | T-08-03 | 3 libFuzzer harnesses build under Clang+ASan, survive short smoke run | fuzz (smoke) | `./fuzz_decode_log_data -runs=1000`; `./fuzz_parse_burn_event_values -runs=1000`; `./fuzz_mint_transaction_deserialize -runs=1000` | ❌ Wave 0 |
| 08-02-T3 | 08-02 | 1 | D-14 | T-08-03,T-08-05 | Seed corpus committed; 60s smoke ctest passes | fuzz (CI smoke) | `ctest -R fuzz_decode_log_data_smoke`; `ctest -R fuzz_parse_burn_event_values_smoke`; `ctest -R fuzz_mint_transaction_deserialize_smoke` | ❌ Wave 0 |
| 08-03-T1 | 08-03 | 2 | D-09 | T-08-06 | `BuildDivergentSlotConfigs()` returns 3 distinct-URL configs, zero modification to existing mock types | unit (compile) | build of Task 2's test binary (includes this header) | ❌ Wave 0 |
| 08-03-T2 | 08-03 | 2 | D-08,D-09 | T-08-06 | RPC disagreement (DIRECT-alone and PUBLIC-pair-alone) still reaches correct mint quorum | e2e | `ctest -R bridge_race_fault_rpc_test --output-on-failure` | ❌ Wave 0 |
| 08-03-T3 | 08-03 | 2 | D-15 | T-08-02 | RPC fault test registered with extended TIMEOUT | e2e (build/registration) | `ctest -N -R bridge_race_fault_rpc_test` | ❌ Wave 0 |
| 08-04-T1 | 08-04 | 3 | D-08,D-10 | T-08-07 | Remaining 10-node cluster converges to exactly-once mint after mid-mint `node.reset()` | e2e | `ctest -R bridge_race_fault_kill_test --output-on-failure` | ❌ Wave 0 |
| 08-04-T2 | 08-04 | 3 | D-08,D-11 | T-08-08 | CRDT converges to exactly-once mint across all 11 nodes after partition + heal | e2e | `ctest -R bridge_race_fault_partition_test --output-on-failure` | ❌ Wave 0 |
| 08-04-T3 | 08-04 | 3 | D-15 | T-08-02 | All 5 bridge_race binaries registered with extended TIMEOUT | e2e (build/registration) | `ctest -N -R bridge_race` | ❌ Wave 0 |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] New race-suite directory + CMake target compiles and links against existing testutil/anvil fixture helpers (08-01-T1/T3)
- [ ] `-DSGNS_FUZZING=ON` configure path succeeds with Clang (fuzz targets build empty-corpus) (08-02-T1)
- [ ] `test/src/mock/mock_rpc_config.hpp`/`.cpp` accept the additive `BuildDivergentSlotConfigs()` helper without touching existing mock types (08-03-T1)
- [ ] `bridge_race_fault_kill_test.cpp`/`bridge_race_fault_partition_test.cpp` compile against the 08-01 fixture (08-04-T1/T2)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Deep fuzz runs (>60s) | D-14 | Long-running, local-only per CONTEXT.md | Run each fuzzer locally with corpus dir for ≥30 min; triage any crashes |
| Race suite at scale (>11 nodes) | D-05 | Resource-dependent | Raise kNodeCount locally, observe stability |
| Deliberately-broken dedup sanity check | D-01/D-02 | Confirms the race test would actually fail if exactly-once dedup were broken (08-01 `<verification>`) | Temporarily bypass/disable the UTXO-reservation or CRDT-persistence check in `TransactionManager::MintFunds`, rerun `bridge_race_single_burn_test`, confirm it fails, then revert |
| Deliberately-uniform mock URL sanity check | D-09 | Confirms the RPC-disagreement test would fail to exercise real disagreement if all 3 slots shared one URL (08-03 `<verification>`) | Temporarily give all 3 `WeightedRpcEndpoint` slots the same mock URL, rerun `bridge_race_fault_rpc_test`, confirm it no longer exercises divergence, then revert |
| PeerId extraction API investigation | 08-RESEARCH.md Open Question 2 | Execution-time investigation, not automatable in advance | Before writing 08-04-T2's partition assertions, grep `GossipPubSub`/`libp2p::Host` for a dedicated remote-PeerId accessor; fall back to parsing the `/p2p/<peer_id>` suffix of `GetLocalAddress()` if none exists; document the chosen path in 08-04-SUMMARY.md |
