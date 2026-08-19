# Phase 12 Full-Suite Verification Report

## Disposition

**BLOCKED by the unfiltered full-suite run at current HEAD.** This is the
third execution of Plan 12-05, re-run after the `evmrelay` submodule was
advanced from the stale worktree identity `62a9bbb1` to the
superproject-recorded pointer `4787e582` (`fix(eth): preserve bridge
destination byte order`). The fresh Phase 12 focused gate passed 7/7 and the
mandatory isolated 11-node race `bridge_race_single_burn_test` **PASSED** in
isolated `-j1` execution — the mint output destination now equals the burn
destination (SGNS address), resolving the deterministic blocker recorded by
the earlier 2026-08-19 run. No source or test code was modified under this
reporting task.

However, the single unfiltered full-suite invocation finished **91/98 passed,
7 failed (6 fixture setup failures + 1 timeout), zero `Not Run`**. Root cause
for the six fixture failures is an external prerequisite regression:
`sepolia.drpc.org` (the fork RPC endpoint configured in the six bridge/Anvil
test fixtures) now refuses Sepolia on its free plan, so Anvil exits during
fork initialization. The mandatory race additionally exceeded its configured
500-second timeout under full-suite `-j2` load after passing in isolation.
Per the plan ("a fixture/setup failure ... is not an acceptable skip"; "zero
failures, zero timeouts/crashes, zero `Not Run`, and zero unreviewed skips"
required for closure), **Phase 12 closure is not achieved**. The blockers are
recorded precisely below for a scoped follow-up decision; no failure is
represented as success.

## Reproduction Identity

| Field | Value |
|---|---|
| Verification date | `2026-08-19` |
| Build window | `15:25:05-15:26:44 UTC` |
| Focused gate window | `15:26:57-15:28:28 UTC` |
| Isolated race window | `15:28:41-15:33:13 UTC` |
| Repository commit | `a25c2312ee9923d317cdd54403e0ab7194d7c125` (`docs(12-05): reset blocked summary for re-execution after evmrelay fix`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` |
| Worktree state | Pre-existing untracked log artifact (`2026-08-05T13:22:40.8911080Z Current run`), plus this report; no source modifications |
| `evmrelay` recorded pointer | `4787e58204e4ca5590835779ec8a36ce02c59cb3` (superproject index) |
| `evmrelay` worktree identity | `4787e58204e4ca5590835779ec8a36ce02c59cb3` (`fix(eth): preserve bridge destination byte order`) — **matches the recorded pointer** |
| `ProofSystem` identity | `a107566e745797f821d18d84994d4280b84f1cdc` (matches recorded pointer) |
| Build directory | `build/OSX/Release` (reconfigured with `cmake -S build/OSX -B build/OSX/Release` before building) |
| Configuration / generator | `Release` / `Unix Makefiles` |
| CMake / CTest | `3.31.4` / `3.31.4` |
| Compiler | AppleClang C++ `16.0.0.16000026` (`clang-1600.0.26.6`) |
| Toolchain | `build/apple.toolchain.cmake` |
| Platform | macOS `15.7.4` (`24G517`), arm64 host |

No credential, signing key, private key, account seed, RPC URL, or secret
environment-variable value is recorded in this report. Transaction hashes,
slot IDs, and validator identity abbreviations quoted below are public test
artifacts, not secrets.

## Prerequisite Review

| Prerequisite | Presence/status only | Disposition |
|---|---|---|
| Host TCP/process permissions | Available | Used for the focused gate and the isolated race run |
| Anvil | Available | `/Users/henriqueklein/.foundry/bin/anvil`; started and stopped cleanly in the race run |
| Cast | Available | `/Users/henriqueklein/.foundry/bin/cast`; used by the Anvil fixture to submit the sole burn |
| Fork RPC (race endpoint) | Available | Fork startup, readiness, account funding, and the local burn succeeded in the isolated race run at 15:28 UTC; source value omitted |
| Fork RPC (`sepolia.drpc.org`, configured in 6 bridge/Anvil fixtures) | **Unavailable** | Direct Anvil invocation: HTTP 400 "chain is not available on free plan, please upgrade to paid plan" (code 35); deterministic across serial re-runs |
| Local bridge contract/funding | Available | Account #0 funding and bridge burn setup succeeded in the race run |
| `RUN_E2E_BRIDGE` | Absent | Reviewed prerequisite-unavailable condition for positive `bridge_e2e_test` cases |
| `SIGNING_KEY` / `PRIVATE_KEY` | Absent | Confirms live signing cannot run; values were not printed |
| `RUN_E2E_RLPX` | Absent | Reviewed prerequisite-unavailable condition for the RLPx case |
| Live Sepolia signing | Unavailable | `bridge_sepolia_e2e_test` retains its `DISABLED_` body; suppressed prerequisite coverage, not an executed live test |

## Task 1 — Focused Phase 12 Gate

### Build

```sh
cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2
```

Result: **PASS**, exit `0` (`15:25:05-15:26:44 UTC`). A CMake reconfigure
(`cmake -S build/OSX -B build/OSX/Release`, exit `0`) ran first so the
updated `evmrelay` submodule code (`4787e582`) was compiled and linked into
all eight owning targets.

### Focused CTest

```sh
ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2
```

Result: **PASS** — 7/7 entries passed, zero failed, zero `Not Run`, total
real time 90.46 seconds (`15:26:57-15:28:28 UTC`).

| CTest entry | Result | Duration |
|---|---:|---:|
| `consensus_vote_journal_test` | PASS | 34.58s |
| `consensus_burn_reservation_test` | PASS | 45.23s |
| `transaction_manager_pending_lifecycle_test` | PASS | 33.97s |
| `consensus_certificate_store_test` | PASS | 28.38s |
| `consensus_finalization_test` | PASS | 11.73s |
| `certificate_compatibility_test` | PASS | 20.03s |
| `consensus_finality_race_test` | PASS | 5.10s |

### Mandatory isolated 11-node race

```sh
ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1
```

Result: **PASS** — CTest exit `0`, duration 271.33s (`15:28:41-15:33:13
UTC`); GoogleTest body `BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn`
OK in 139.650s. Configured timeout 500 seconds; no timeout.

Race summary from the run log (public test artifacts):

- One external burn (`5eb61c9afffea872` abbrev) on the Anvil fork;
  pre-burn baseline block `11522938`, bridge creation block `11522939`.
- All 11 nodes READY before RPC endpoint configuration; 11 validators
  registered (full authority plus 10 genesis validators).
- Exactly one canonical slot (`bb57eeb6700c56bf` abbrev); 11 distinct
  proposals; one winner (`02d30b680dd420bc` abbrev); 16-second stability
  window observed before convergence was declared.
- The winning mint's applied output destination equals the burn
  destination SGNS address — the `application_converged` predicate held
  (this is the exact assertion that failed deterministically in run 2
  before the `evmrelay` fix).
- Clean teardown: all 11 node shutdowns completed
  (`phase=shutdown-complete` for nodes 0-10, 0.3s-14.7s each), Anvil
  stopped (`exit_code=383` is the fixture's expected SIGTERM), and the
  process exited naturally.

**Assessment:** the `evmrelay` update from `62a9bbb1` to `4787e582`
(`fix(eth): preserve bridge destination byte order`) resolved the
deterministic mint-destination mismatch recorded in run 2. The D-14
mandatory isolated-race invariant is now satisfied.

Raw evidence remains in
`build/OSX/Release/Testing/Temporary/LastTest.log` and the generated
`build/OSX/Release/xunit/` files for this worktree.

## Task 2 — Dynamic Inventory and Full Suite

### Inventory preparation

Command:

```sh
ctest --test-dir build/OSX/Release -N
```

Result: dynamic count **98** configured entries at `a25c2312`. Four
configured entries (`bridge_event_identity_test`,
`crdt_datastore_last_owner_test`, `public_chain_mint_validation_test`,
`transaction_manager_certificate_fallback_test`) initially had no built
executable — a `Not Run` risk, never an acceptable skip — so all four
targets were built first
(`cmake --build build/OSX/Release --target bridge_event_identity_test public_chain_mint_validation_test transaction_manager_certificate_fallback_test crdt_datastore_last_owner_test -j2`,
exit `0`, `15:35:50-15:37:34 UTC`). A re-check showed **zero** missing
executables across all 98 entries before the full run.

Configured names, alphabetically (98):

```text
account_creation_test, account_management_test, account_signature_test,
blake2_test, blob_test, blockchain_genesis_test, bridge_anvil_catchup_e2e_test,
bridge_anvil_e2e_test, bridge_e2e_chainlist_test, bridge_e2e_test,
bridge_event_identity_test, bridge_race_batch_test, bridge_race_fault_kill_test,
bridge_race_fault_partition_test, bridge_race_fault_rpc_test,
bridge_race_single_burn_test, bridge_relayer_test, bridge_rlpx_e2e_test,
bridge_sepolia_e2e_test, buffer_test, burnconfig_test,
certificate_compatibility_test, chain_rpc_endpoint_provider_test,
child_tokens_test, concurrency_cache_dir_test, concurrency_callback_test,
concurrency_config_test, concurrency_content_request_test,
concurrency_get_block_test, concurrency_publish_test,
concurrency_request_context_test, concurrency_stress_test,
consensus_bridge_mint_subject_test, consensus_burn_reservation_test,
consensus_certificate_store_test, consensus_finality_race_test,
consensus_finalization_test, consensus_pending_lifecycle_test,
consensus_slot_key_test, consensus_subject_test, consensus_vote_journal_test,
consensus_vote_slot_test, crdt_datastore_last_owner_test, crdt_test,
full_node_test, genius_node_bootstrap_reconnect_test, genius_proofs,
globaldb_integration_test, hasher_test, hexutil_test, json_migration_test,
keccak_test, messaging_watcher_test, migration_sync_test, mock_rpc_test,
multi_account_test, multisig_quorum_test, multisig_verify_test,
network_config_precedence_test, node_initialization_progress,
node_startup_test, node_type_derivation_test, processing_datatypes_test,
processing_nodes_test, processing_result_durability_test,
processing_schema_test, processing_validate_result_data_test, prover_test,
public_chain_input_validator_slot_test, public_chain_mint_validation_test,
pubsub_counts_test, pubsub_graphsync_test, remove_all_test, result_gc_test,
rocksdb_fs_test, rocksdb_integration_test, scaled_integer_test,
secure_storage_test, securecrdt_interface_test,
securecrdt_propose_sign_quorum_test, securecrdt_quorum_contract_e2e_test,
securecrdt_quorum_gate_test, securecrdt_registry_test, sha256_test,
startup_wiring_test, task_queue_test, token_amount_test, token_id_test,
transaction_crash_test, transaction_manager_certificate_fallback_test,
transaction_manager_pending_lifecycle_test, transaction_sync_test,
trustedpeerregistry_genesis_test, trustedpeerregistry_quorum_test,
trustedpeerregistry_threshold_floor_test, utxo_manager_test,
validator_registry_promotion_test, validator_registry_slot_quorum_test
```

### One unfiltered repository run

```sh
ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2
```

Exactly one unfiltered invocation; no phase regex, exclude regex, manual test
list, or success-only rerun is reported as the full-suite result.

Result: **91/98 passed, 7 failed (6 `Failed` + 1 `Timeout`), zero `Not Run`,
zero crashes** — `93% tests passed, 7 tests failed out of 98`, total real
time 818.86 seconds (`15:37:56-15:51:35 UTC`).

| CTest entry | Result | Duration | Classification |
|---|---|---:|---|
| `bridge_anvil_e2e_test` | Failed | 16.10s | Fixture setup failure — external prerequisite regression (below) |
| `bridge_anvil_catchup_e2e_test` | Failed | 16.16s | Fixture setup failure — external prerequisite regression |
| `bridge_race_batch_test` | Failed | 16.15s | Fixture setup failure — external prerequisite regression |
| `bridge_race_fault_rpc_test` | Failed | 16.13s | Fixture setup failure — external prerequisite regression |
| `bridge_race_fault_kill_test` | Failed | 16.10s | Fixture setup failure — external prerequisite regression |
| `bridge_race_fault_partition_test` | Failed | 16.20s | Fixture setup failure — external prerequisite regression |
| `bridge_race_single_burn_test` | **Timeout** | 500.04s | Exceeded configured 500s timeout under full-suite `-j2` load |

All other 91 configured entries passed, including every Phase 12 focused
target (`consensus_finalization_test`, `consensus_finality_race_test`,
`consensus_vote_journal_test`, `consensus_burn_reservation_test`,
`consensus_certificate_store_test`, `certificate_compatibility_test`,
`transaction_manager_pending_lifecycle_test`).

### Failure root cause — six Anvil-fork tests

**Failure signature (all six, identical):** the fixture logs
`anvil_fixture: started anvil port=<p> fork_url=https://sepolia.drpc.org`,
then `anvil did not become ready at http://127.0.0.1:<p> within 15000ms`;
`SetUpTestSuite` fails (`s_anvil.WaitForReady()` false), the GTest bodies
report `SKIPPED`, and CTest records the suite as `Failed` (~16s each).

**Direct endpoint verification (no test code involved):**

- `anvil --fork-url https://sepolia.drpc.org` exits immediately with:
  `HTTP error 400 ... "chain is not available on free plan, please upgrade
  to paid plan" (code 35)`. The endpoint's free plan no longer serves
  Sepolia.
- `anvil --fork-url <working public Sepolia endpoint>` starts cleanly and
  answers `eth_chainId = 0xaa36a7` (Sepolia). Anvil itself, the host
  network, and the fixture mechanism are healthy — proven independently by
  the isolated `bridge_race_single_burn_test` pass at `15:28 UTC` the same
  hour, which uses a different fork endpoint.

**Serial reproduction:** after the full run, all six tests were re-run
alone (`ctest -R '<the six names>' -j1`, `16:00:13-16:01:44 UTC`): all six
failed identically in 15.1-15.2s with the same readiness timeout. The
failures are therefore **not** parallel-execution contention; they are a
deterministic external prerequisite regression — the drpc free tier
withdrew Sepolia after the fixtures' fork URLs were configured.

**Disposition per plan rules:** a fixture/setup failure is explicitly *not*
an acceptable reviewed skip, and this plan may not modify source code (the
fork URLs live in test sources/configs). These six failures are recorded as
a blocker for a scoped follow-up decision (e.g., repointing the fixtures to
a working endpoint), not patched here and not counted as skips.

### Timeout root cause — mandatory race inside the full suite

`bridge_race_single_burn_test` passed isolated `-j1` in 271.33s (see Task
1) but exceeded its configured 500s CTest timeout when executed inside the
`-j2` full suite, where it overlapped other tests (including Anvil-fork
tests competing for CPU and loopback ports — the log shows
`preferred port 18545 is occupied; using 18546` for the overlapping run).
The timeout is a blocking state under the plan; it is recorded, not
re-characterized. A scoped follow-up must decide between a larger timeout
budget for the 11-node race and CTest resource-lock serialization of the
Anvil-fixture tests.

### Reviewed prerequisite skips (acceptable, evidence-backed)

These configured tests executed and reported `Passed` at CTest level with
their positive E2E cases explicitly GTest-skipped behind documented
prerequisite gates; each was re-verified directly this window:

| Test | Gate | Direct evidence (this window) | Disposition |
|---|---|---|---|
| `bridge_e2e_test` | `RUN_E2E_BRIDGE` env | Binary prints `Set RUN_E2E_BRIDGE=1 to run E2E bridge tests`; all positive cases `[ SKIPPED ]` | Reviewed skip — prerequisite unavailable |
| `bridge_rlpx_e2e_test` | `RUN_E2E_RLPX` env | Binary prints `Set RUN_E2E_RLPX=1 to run RLPx E2E bridge tests`; case `[ SKIPPED ]` | Reviewed skip — prerequisite unavailable |
| `bridge_sepolia_e2e_test` | Live Sepolia signing | `0 tests from 0 test suites ran`; `YOU HAVE 1 DISABLED TEST` (`DISABLED_` body retained) | Reviewed skip — prerequisite unavailable |
| (GTest-level, inside the 6 failed fixtures) | Anvil readiness | Bodies `SKIPPED` only after `SetUpTestSuite` failed | **Not** acceptable — suite-level `Failed` recorded above |

## Requirement Matrix

| Requirement | Focused / isolated evidence | Full-suite evidence | Status |
|---|---|---|---|
| TEST-01 | `bridge_race_single_burn_test` **PASSED** isolated `-j1` (271.33s): one slot, 11 proposals, one winner, correct mint destination, clean 11-node teardown | **Timeout** (500.04s) under full-suite `-j2` load | **BLOCKED on full suite** |
| TEST-02 | `consensus_finality_race_test` passed in focused gate (5.10s) | Passed in full suite (4.33s) | **PASS** |
| TEST-03 | Restart vote-lock regression passed in `consensus_vote_journal_test` (34.58s) | Passed in full suite (33.63s) | **PASS** |
| TEST-04 | Before/after-deadline regression passed in `consensus_vote_journal_test` | Passed in full suite | **PASS** |
| TEST-05 | Finality-race, certificate-store, burn-reservation, and compatibility entries passed in focused gate | All passed in full suite | **PASS** |
| TEST-06 | TransactionManager lifecycle and compatibility entries passed in focused gate | `transaction_manager_pending_lifecycle_test` and `certificate_compatibility_test` passed in full suite; six bridge/Anvil suites failed on the drpc prerequisite regression | **BLOCKED on full suite** |

## Closure Decision

**Phase 12 Plan 05 is blocked; closure is not achieved.** The focused gate
(7/7) and the mandatory D-14 isolated race both pass at `a25c2312` with the
`evmrelay` fix — the run-2 blocker is resolved. However, the single
unfiltered full-suite invocation (D-16) recorded 7 failures out of 98
entries: six deterministic Anvil-fixture setup failures caused by the
`sepolia.drpc.org` free-plan Sepolia withdrawal (an external prerequisite
regression, reproduced serially and by direct Anvil invocation), and one
timeout of the mandatory race under `-j2` full-suite load. D-17's bar —
zero failures, zero timeouts, zero `Not Run`, zero unreviewed skips — is
not met, and fixture/setup failures are not acceptable skips under this
plan. The report is a blocker artifact, not a completion summary.

**Recorded blockers for a scoped follow-up decision:**

1. Six bridge/Anvil tests (`bridge_anvil_e2e_test`,
   `bridge_anvil_catchup_e2e_test`, `bridge_race_batch_test`,
   `bridge_race_fault_rpc_test`, `bridge_race_fault_kill_test`,
   `bridge_race_fault_partition_test`) fail deterministically because their
   configured fork RPC endpoint (`sepolia.drpc.org`) no longer serves
   Sepolia on the free plan (HTTP 400, code 35, verified directly).
   Follow-up: repoint the fixtures' fork URLs to a working endpoint (a
   source/config change outside this plan's scope), then re-run the
   unfiltered suite.
2. `bridge_race_single_burn_test` exceeded its configured 500s timeout
   under full-suite `-j2` load after passing isolated in 271.33s.
   Follow-up: raise the race's CTest timeout budget and/or serialize
   Anvil-fixture tests with CTest resource locks, then re-run the
   unfiltered suite.
3. The three gated E2E suites (`bridge_e2e_test`, `bridge_rlpx_e2e_test`,
   `bridge_sepolia_e2e_test`) remain reviewed prerequisite skips
   (`RUN_E2E_BRIDGE`, `RUN_E2E_RLPX`, live Sepolia signing unavailable);
   no action required unless those prerequisites are provisioned.
