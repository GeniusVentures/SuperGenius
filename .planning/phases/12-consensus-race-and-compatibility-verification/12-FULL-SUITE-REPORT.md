---
phase: 12-consensus-race-and-compatibility-verification
artifact: full-suite-report
run: 5
status: CLOSED — focused gate, isolated race, and unfiltered full suite all pass (98/98)
created: 2026-08-19
---

# Phase 12 Full-Suite Verification Report — Run 5

**Result: CLOSED.** Focused gate 7/7, isolated 11-node race PASS (271.20 s,
clean teardown), and one unfiltered full-suite run: **98/98 passed, 0 failed,
0 timeouts, 0 crashes, 0 Not Run, 0 skips** (1021.60 s wall, `-j2`).

Run 5 re-executes Plan 12-05 after the run-4 blocker fix (commit `84bce439`:
the exact-replay competitor proposal now uses a distinct proposer account, so
its `proposal_id` is deterministically distinct and no longer depends on
`CurrentTimeMs()` tick luck).

## Build Identity (reproducibility)

| Fact | Value |
|---|---|
| Repository HEAD | `101850cd` (`docs(12-05): reset blocked summary for re-run after exact-replay flake fix`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` (main working tree, sequential execution) |
| Working tree | clean except one pre-existing untracked scratch file (unrelated to this run) |
| Build directory | `build/OSX/Release` |
| Generator | Unix Makefiles |
| Build type | Release |
| Compiler | Apple clang 16.0.0 (clang-1600.0.26.6) |
| CMake / CTest | 3.31.4 |
| Platform | macOS (Darwin, arm64) |
| evmrelay submodule | `4787e582` (mint destination byte-order fix in place) |
| Race test properties | `bridge_race_single_burn_test`: `TIMEOUT 900`, `RUN_SERIAL TRUE` (verified in generated `build/OSX/Release/test/src/bridge_race/CTestTestfile.cmake`, commit `63645bbc`) |

No credentials, RPC endpoint URLs, seeds, or key material are recorded in this
report. External-service facts are stated as availability booleans only.

## Commands Executed (run 5)

1. Focused target build:
   `cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2`
   → **success** (`[100%] Built target bridge_race_single_burn_test`).
2. Focused gate:
   `ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2`
   → **100% tests passed, 0 failed of 7** (total 91.45 s).
3. Isolated race:
   `ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1`
   → **Passed, 271.20 s** (well within the configured 900 s timeout).

Timestamps (UTC): build+focused gate 2026-08-19T20:33:13Z → 20:35:16Z;
isolated race 20:35:27Z → 20:39:59Z.

## Focused Gate Results (run 5)

| Test | Result | Duration |
|---|---|---|
| consensus_finalization_test | Passed | 10.90 s |
| consensus_burn_reservation_test | Passed | 43.78 s |
| consensus_vote_journal_test | Passed | 33.84 s |
| consensus_certificate_store_test | Passed | 27.67 s |
| transaction_manager_pending_lifecycle_test | Passed | 33.03 s |
| consensus_finality_race_test | Passed | 4.34 s |
| certificate_compatibility_test | Passed | 19.02 s |

All 7 named Phase 12 regressions were discovered (`--no-tests=error` did not
trip) and passed. The run-4 flaky case
`ConsensusFinalizationHarness.StructuredTraceUsesStableIdentityForExactReplay`
passed deterministically under the `84bce439` fix (distinct competitor
proposer account ⇒ deterministically distinct `proposal_id`).

## Isolated Real Race (run 5)

`BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn` — **PASSED**, 271.20 s
(gtest body 139.7 s + bounded stability window + teardown), `-j1`, within the
900 s `TIMEOUT`.

| Fact | Value |
|---|---|
| Nodes ready | all 11 nodes READY before burn submission |
| Proposals | 11 (one per node) over one canonical burn slot |
| Validators | 11 (1 full authority + 10 additional genesis validators) |
| Outcome | exactly one winner certificate; `proposals=11 validators=11 stable_ms=16000` |
| Teardown | clean: per-node destroy-start/destroy-complete for nodes 0–10 (`teardown phase=nodes-complete`), then `teardown phase=anvil-complete` |
| Fixture | Anvil fork of Sepolia started and stopped cleanly; pre-burn baseline block recorded |

Full evidence: `build/OSX/Release/Testing/Temporary/LastTest.log` (run
section 93/98) and xunit output under `build/OSX/Release/xunit/`. Burn hash,
slot ID, and winner ID are recorded in the test log but not reproduced here.

## Dynamic Inventory and Full Suite

### Dynamic inventory (Task 2)

`ctest --test-dir build/OSX/Release -N` → **Total Tests: 98** (captured
2026-08-19T20:42:13Z, immediately before the full-suite run; count derived
from the current configured tree, not hardcoded).

Configured inventory (98 tests, alphabetical):

```
account_creation_test                      consensus_finality_race_test            processing_schema_test
blake2_test                                consensus_finalization_test             processing_validate_result_data_test
blob_test                                  consensus_pending_lifecycle_test        prover_test
blockchain_genesis_test                    consensus_slot_key_test                 public_chain_input_validator_slot_test
bridge_anvil_catchup_e2e_test              consensus_subject_test                  public_chain_mint_validation_test
bridge_anvil_e2e_test                      consensus_vote_journal_test             pubsub_counts_test
bridge_e2e_chainlist_test                  consensus_vote_slot_test                pubsub_graphsync_test
bridge_e2e_test                            crdt_datastore_last_owner_test          remove_all_test
bridge_race_batch_test                     crdt_test                               rocksdb_fs_test
bridge_race_fault_kill_test                full_node_test                          rocksdb_integration_test
bridge_race_fault_partition_test           genius_node_bootstrap_reconnect_test    scaled_integer_test
bridge_race_fault_rpc_test                 genius_proofs                           secure_storage_test
bridge_race_single_burn_test               globaldb_integration_test               securecrdt_interface_test
bridge_rlpx_e2e_test                       hasher_test                             securecrdt_propose_sign_quorum_test
bridge_sepolia_e2e_test                    hexutil_test                            securecrdt_quorum_contract_e2e_test
buffer_test                                json_migration_test                     securecrdt_quorum_gate_test
certificate_compatibility_test             keccak_test                             securecrdt_registry_test
chain_rpc_endpoint_provider_test           messaging_watcher_test                  sha256_test
child_tokens_test                          migration_sync_test                     startup_wiring_test
concurrency_cache_dir_test                 mock_rpc_test                           task_queue_test
concurrency_callback_test                  multi_account_test                      transaction_crash_test
concurrency_config_test                    multisig_quorum_test                    transaction_manager_certificate_fallback_test
concurrency_content_request_test           multisig_verify_test                    transaction_manager_pending_lifecycle_test
concurrency_get_block_test                 network_config_precedence_test          transaction_sync_test
concurrency_publish_test                   node_initialization_progress            trustedpeerregistry_genesis_test
concurrency_request_context_test           node_startup_test                       trustedpeerregistry_quorum_test
concurrency_stress_test                    node_type_derivation_test               trustedpeerregistry_threshold_floor_test
consensus_bridge_mint_subject_test         processing_datatypes_test               validator_registry_promotion_test
consensus_burn_reservation_test            processing_nodes_test                   validator_registry_slot_quorum_test
consensus_certificate_store_test           processing_result_durability_test
```

### External prerequisite classification

| Prerequisite | Status | Evidence |
|---|---|---|
| Live Sepolia fork RPC (fixture fork URL) | **available** | Anvil fixtures forked successfully during this run; `bridge_anvil_e2e_test`, `bridge_anvil_catchup_e2e_test`, and `bridge_race_*` executed against it |
| Foundry `anvil` / `cast` | **available** | `anvil` 1.7.1 on PATH; fixtures started/stopped Anvil cleanly |
| Bridge relayer contract environment | **available** | `bridge_relayer_test`, `bridge_e2e_test`, `bridge_e2e_chainlist_test`, `bridge_sepolia_e2e_test`, `bridge_rlpx_e2e_test` all executed and passed |
| Credential material | present | values intentionally not recorded (T-12-06) |

No prerequisite was unavailable in this run; therefore **no test required a
reviewed skip** (see Reviewed Skips below).

### Unfiltered full-suite run (the reported full-suite result)

Command (exactly one unfiltered invocation; no phase regex, no exclude regex,
no manual list, no success-only rerun):

`ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2`

| Metric | Value |
|---|---|
| Window (UTC) | 2026-08-19T20:42:24Z → 21:00:12Z |
| Tests executed | 98 of 98 (`--no-tests=error` did not trip) |
| **Passed** | **98** |
| Failed | 0 |
| Timeout | 0 |
| Crash | 0 |
| Not Run | 0 |
| Skipped | 0 |
| Wall time | 1021.60 s (~17 min) |
| Sum of test durations | 1580.9 s |
| Longest tests | bridge_race_single_burn_test 284.96 s (ran first, alone, under `RUN_SERIAL`); bridge_race_fault_kill_test 195.09 s |

Result line: `100% tests passed, 0 tests failed out of 98`.

The run-3 full-suite blockers are verified resolved in this unfiltered run:
the 6 bridge/Anvil fixture failures caused by the dead fork-RPC endpoint are
all green after the Aug-19 fixture rebuild, and
`bridge_race_single_burn_test` completed in 284.96 s under `-j2` — within the
`TIMEOUT 900` / `RUN_SERIAL TRUE` configuration from `63645bbc`.

Full raw output: ctest log of this run (retained out-of-tree); per-test
evidence in `build/OSX/Release/Testing/Temporary/LastTest.log`.

## Prerequisite Environment Facts

| Prerequisite | Status (run 5) |
|---|---|
| Bridge/Anvil fixture binaries rebuilt (Aug 19) with working fork endpoint | yes (fixtures embed a verified-working Sepolia fork RPC; the isolated race used it successfully) |
| External Sepolia fork RPC reachable from fixtures | available (used by the isolated race this run) |
| Foundry `anvil`/`cast` | available (`anvil` 1.7.1) |
| Credential material | present in environment; values intentionally not recorded |

## Requirement Matrix (TEST-01..06)

| Req | Focused evidence (run 5) | Isolated race | Full suite (unfiltered, -j2) | Status |
|---|---|---|---|---|
| TEST-01 | consensus_finalization_test, consensus_vote_journal_test Passed | bridge_race_single_burn_test Passed 271.20 s — exactly one certificate for one burn across 11 proposals / 11 validators | both + race Passed (race 284.96 s under RUN_SERIAL) | **SATISFIED** |
| TEST-02 | consensus_finality_race_test Passed (4.34 s) | — | consensus_finality_race_test Passed (4.35 s) | **SATISFIED** |
| TEST-03 | consensus_vote_journal_test Passed (33.84 s) | — | consensus_vote_journal_test Passed (33.84 s) | **SATISFIED** |
| TEST-04 | consensus_vote_journal_test Passed (33.84 s) | — | consensus_vote_journal_test Passed (33.84 s) | **SATISFIED** |
| TEST-05 | consensus_certificate_store_test, certificate_compatibility_test, consensus_finality_race_test Passed | — | all three Passed (27.55 s / 19.81 s / 4.35 s) | **SATISFIED** |
| TEST-06 | transaction_manager_pending_lifecycle_test Passed (33.03 s) | — | transaction_manager_pending_lifecycle_test Passed (32.86 s) | **SATISFIED** |

"SATISFIED" = named focused regression passed AND the full-suite leg passed
with zero failures, timeouts, crashes, Not-Run entries, or skips.

## Reviewed Skips

**None.** Every one of the 98 configured tests executed and passed in the
unfiltered run. All external prerequisites identified by research (live fork
RPC, Anvil, Cast, bridge contract environment) were available, so no skip
disposition was required. There are zero unreviewed skips and zero hidden
outcomes.

## Closure Statement

Phase 12 closure conditions from Plan 12-05 are met in run 5:

- Focused gate: 7/7 discovered and passed.
- Isolated mandatory race: passed alone (`-j1`) in 271.20 s with clean
  teardown, inside the 900 s configured timeout.
- Full suite: one unfiltered `ctest --output-on-failure --no-tests=error -j2`
  invocation executed all 98 configured tests; 98 passed, 0 failed, 0
  timeouts, 0 crashes, 0 Not Run, 0 skips.
- TEST-01 through TEST-06 each map to named focused evidence plus the
  full-suite result.
- No failure, timeout, crash, not-run test, or unreviewed skip is represented
  as successful closure.
