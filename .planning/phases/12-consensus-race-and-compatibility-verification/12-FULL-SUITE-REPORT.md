# Phase 12 Full-Suite Verification Report

## Disposition

**BLOCKED by the one required unfiltered repository run.** The fresh Phase 12
focused gate passed 7/7, and the mandatory isolated 11-node race passed with a
natural process exit. Dynamic discovery found 84 configured CTest entries.
The one unfiltered `-j2` invocation then completed with 70 passes and 14
failures, including two segfaults and one timeout. No repository-wide passing
claim is made, and no unrelated suite failure was fixed under this reporting
task.

Successful closure still requires a new unfiltered run with zero failures,
timeouts, crashes, `Not Run` entries, or unreviewed skips.

## Reproduction Identity

| Field | Value |
|---|---|
| Verification date | `2026-07-31` |
| Full-suite CTest window | `10:41-10:55 America/Sao_Paulo` (`13:41-13:55 UTC`) |
| Repository commit | `5fec4d6eee06280695bc57c42f76f9c9b56cc07d` |
| Branch | `gsd/phase-11-slot-owned-bridge-burn-reservations` |
| Worktree state | Dirty: pre-existing modified `ProofSystem` submodule and pre-existing logger-level changes in `src/account/GeniusNode.cpp`, plus pre-existing/generated untracked test artifacts and this report |
| Teardown repair commit | `5fec4d6e` (`fix(test): drain bridge race logging on shutdown`) |
| `ProofSystem` identity | `99593ca662d996869273f5e7414157e1d502ccf2` (dirty relative to superproject) |
| `evmrelay` identity | `4787e58204e4ca5590835779ec8a36ce02c59cb3` |
| Build directory | `build/OSX/Release` |
| Configuration / generator | `Release` / `Unix Makefiles` |
| CMake / CTest | `3.31.4` / `3.31.4` |
| Compiler | AppleClang C++ `16.0.0.16000026` (`XcodeDefault.xctoolchain/usr/bin/clang++`) |
| Toolchain | `build/apple.toolchain.cmake` |
| Platform | macOS `15.7.4` (`24G517`), arm64 host, `x86_64;arm64` build, deployment target `13.0` |

No credential, signing key, private key, account seed, RPC URL, or secret
environment-variable value is recorded in this report.

## Prerequisite Review

| Prerequisite | Presence/status only | Disposition |
|---|---|---|
| Host TCP/process permissions | Available | Used for all authoritative network runs |
| Anvil | Available | Started successfully in isolated and full-suite runs |
| Cast | Available | Used successfully by Anvil fixtures |
| Fork RPC | Available | Fork startup, readiness, account funding, and local burns succeeded; source value omitted |
| Local bridge contract/funding | Available | Account #0 funding and bridge burn setup succeeded |
| `RUN_E2E_BRIDGE` | Absent | Reviewed prerequisite-unavailable runtime skip for four positive `bridge_e2e_test` cases |
| `SIGNING_KEY` / `PRIVATE_KEY` | Absent | Confirms live signing cannot run; values were not printed |
| `RUN_E2E_RLPX` | Absent | Reviewed prerequisite-unavailable runtime skip for one RLPx case |
| Live Sepolia signing | Unavailable | `bridge_sepolia_e2e_test` has one `DISABLED_` body; recorded as suppressed prerequisite coverage, not an executed live test |

The runtime reviewed skips were:

- `BridgeE2ETest.BurnToMintPipeline`
- `BridgeE2ETest.SlotKeyCollisionResistance`
- `BridgeE2ETest.ReplayRejection`
- `BridgeE2ETest.MissingEndpointsFailClosed`
- `BridgeRlpxE2ETest.RlpxBurnStreamAutoMints`

`BridgeE2ENegativeTest.InvalidReceiptLogsRejected` still executed and passed.
The live `BridgeSepoliaE2ETest.DISABLED_BurnToMintPipeline` body was suppressed
by its `DISABLED_` name; the CTest wrapper returned pass in 1.05 seconds. Other
pre-existing disabled GoogleTest bodies are outside the 84-entry CTest count
and are not represented as executed coverage.

Skips emitted after a failed `SetUpTestSuite` in the Anvil catch-up and
bridge-race binaries are **not** reviewed prerequisite skips. Their
prerequisites were available, their suite setup failed, and CTest correctly
reported the entries as failures or timeout.

## Task 1 — Focused Phase 12 Gate

### Build

```sh
cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2
```

Result: **PASS**. All eight owning targets built; exit `0`.

### Focused CTest

```sh
ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2
```

Result: **PASS** — 7/7 entries passed, zero failed, zero `Not Run`, total
real time 85.80 seconds.

| CTest entry | Result | Duration |
|---|---:|---:|
| `consensus_vote_journal_test` | PASS | 34.13s |
| `consensus_finality_race_test` | PASS | 5.20s |
| `consensus_burn_reservation_test` | PASS | 46.39s |
| `certificate_compatibility_test` | PASS | 19.89s |
| `transaction_manager_pending_lifecycle_test` | PASS | 22.43s |
| `consensus_finalization_test` | PASS | 11.65s |
| `consensus_certificate_store_test` | PASS | 26.56s |

The owning binaries include the named Phase 12 regressions:

- `HandleCertificateBeforeCrdtApplicationBlocksCompetingCertificate`
- `IdenticalCertificateAllIngressPathsApplyAndCleanupOnce`
- `ConflictingCertificateAllIngressPathsPreserveWinnerAndDeduplicateEvidence`
- `RestartedVoteLockRejectsCompetingSameSlotProposalWithoutResigning`
- `BetterCandidateBeforeDeadlineWinsButAfterPublicationCannotResign`

### Mandatory isolated 11-node race

```sh
ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1
```

Result: **PASS** — 1/1, natural exit `0`, CTest duration 308.37 seconds
under the unchanged 500-second timeout.

- GoogleTest body: 140.762 seconds; suite total: 249.065 seconds.
- All 11 nodes reached `READY`.
- Structured summary: 11 proposals, 11 validators, one canonical slot, one
  winner, and a 16,000 ms stable window.
- All 11 explicit shutdowns completed.
- All 11 `GeniusNode` destructors completed.
- Anvil stopped, GoogleTest printed `[ PASSED ] 1 test`, and the process exited
  naturally. No thread was detached or force-killed as a passing mechanism.

## Task 2 — Dynamic Inventory

Command:

```sh
ctest --test-dir build/OSX/Release -N
```

Result: **PASS** — dynamic count `84`. CTest JSON discovery reported no labels.

Configured names, in discovery order:

```text
token_id_test, token_amount_test, utxo_manager_test, account_management_test,
result_gc_test, bridge_relayer_test, bridge_event_identity_test,
chain_rpc_endpoint_provider_test, public_chain_input_validator_slot_test,
public_chain_mint_validation_test, transaction_manager_pending_lifecycle_test,
network_config_precedence_test, node_type_derivation_test, account_creation_test,
buffer_test, hexutil_test, blob_test, scaled_integer_test,
concurrency_cache_dir_test, concurrency_config_test, concurrency_publish_test,
concurrency_get_block_test, concurrency_content_request_test,
concurrency_request_context_test, concurrency_callback_test,
concurrency_stress_test, mock_rpc_test, bridge_e2e_test,
bridge_e2e_chainlist_test, bridge_anvil_e2e_test,
bridge_anvil_catchup_e2e_test, bridge_sepolia_e2e_test, bridge_rlpx_e2e_test,
startup_wiring_test, blockchain_genesis_test, consensus_subject_test,
consensus_vote_slot_test, validator_registry_slot_quorum_test,
consensus_bridge_mint_subject_test, validator_registry_promotion_test,
consensus_slot_key_test, consensus_pending_lifecycle_test,
consensus_certificate_store_test, consensus_vote_journal_test,
consensus_finalization_test, consensus_finality_race_test,
consensus_burn_reservation_test, certificate_compatibility_test,
node_startup_test, crdt_test, crdt_datastore_last_owner_test,
globaldb_integration_test, blake2_test, keccak_test, hasher_test, sha256_test,
pubsub_graphsync_test, secure_storage_test, json_migration_test,
multi_account_test, node_initialization_progress, price_retrieval_test,
task_queue_test, processing_result_durability_test,
processing_validate_result_data_test, processing_datatypes_test,
processing_nodes_test, child_tokens_test, full_node_test,
processing_schema_test, prover_test, genius_proofs, pubsub_counts_test,
rocksdb_fs_test, rocksdb_integration_test, transaction_sync_test,
migration_sync_test, transaction_crash_test, messaging_watcher_test,
bridge_race_single_burn_test, bridge_race_batch_test,
bridge_race_fault_rpc_test, bridge_race_fault_kill_test,
bridge_race_fault_partition_test
```

## One Unfiltered Repository Run

Exactly one command was used as the reported complete run:

```sh
ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2
```

Result: **FAIL**, exit `8`, total real time 814.00 seconds.

| Count | Value |
|---|---:|
| Configured CTest entries | 84 |
| Passed CTest entries | 70 |
| Failed CTest entries | 14 |
| Timeout entries | 1 (included in failed) |
| Crash/segfault entries | 2 (included in failed) |
| CTest `Not Run` | 0 |
| CTest-level skipped entries | 0 |
| Reviewed prerequisite-unavailable GoogleTest runtime cases | 5 |
| Suppressed live-Sepolia GoogleTest body | 1 |

### Failed entries

| # | CTest entry | Classification | Primary evidence |
|---:|---|---|---|
| 4 | `account_management_test` | SEGFAULT | CTest `***Exception: SegFault` at 59.78s |
| 30 | `bridge_anvil_e2e_test` | Failed | Invalid-argument assertions plus unknown C++ exceptions after Anvil setup |
| 31 | `bridge_anvil_catchup_e2e_test` | Failed | Main and processor nodes did not reach `READY`; 60s predicate timeouts |
| 35 | `blockchain_genesis_test` | Failed | Authorized sync/process case did not obtain a mint result |
| 60 | `multi_account_test` | Failed | All three active cases failed to obtain required mint results |
| 67 | `processing_nodes_test` | Failed | Mint transaction failed or timed out |
| 68 | `child_tokens_test` | Failed | Four active cases failed their initial/grouped mint operations |
| 69 | `full_node_test` | Failed | Original-node mint failed |
| 76 | `transaction_sync_test` | Failed | All five active cases failed their prerequisite mint |
| 77 | `migration_sync_test` | SEGFAULT | CTest reported `SEGFAULT` |
| 81 | `bridge_race_batch_test` | Failed | All 11 nodes did not reach `READY`; 90s setup predicate timeout |
| 82 | `bridge_race_fault_rpc_test` | Failed | All 11 nodes did not reach `READY`; 90s setup predicate timeout |
| 83 | `bridge_race_fault_kill_test` | Failed | All 11 nodes did not reach `READY`; 90s setup predicate timeout |
| 84 | `bridge_race_fault_partition_test` | Timeout | Balance assertions failed after heal; teardown exceeded CTest's 180s timeout |

The three race setup failures overlapped other bridge-race binaries under the
required `-j2` run and used the same fixture directory names/network seeds.
That is evidence consistent with missing test resource isolation, but it is an
inference rather than a completed root-cause proof. The failures remain real
until a scoped follow-up fixes and verifies them. The other mint/sync failures
and segfaults are likewise recorded without speculative repair.

Detailed raw evidence remains in
`build/OSX/Release/Testing/Temporary/LastTest.log`,
`build/OSX/Release/Testing/Temporary/LastTestsFailed.log`, and the generated
`build/OSX/Release/xunit/` files for this worktree.

## Requirement Matrix

| Requirement | Focused / isolated evidence | Full-suite evidence | Status |
|---|---|---|---|
| TEST-01 | Mandatory 11-node race passed twice after repair; fresh gate had 11 proposals/validators, one winner, clean teardown | Single-burn entry passed in full run, but four other bridge-race entries failed/timeout | **BLOCKED** |
| TEST-02 | `consensus_finality_race_test` and ingress/finality regressions passed | Owning entry passed, repository gate failed | **BLOCKED on full suite** |
| TEST-03 | Restarted vote-lock regression passed in `consensus_vote_journal_test` | Owning entry passed, repository gate failed | **BLOCKED on full suite** |
| TEST-04 | Before/after-deadline regression passed in `consensus_vote_journal_test` | Owning entry passed, repository gate failed | **BLOCKED on full suite** |
| TEST-05 | Finality-race, certificate-store, burn-reservation, and compatibility entries passed | Owning entries passed, repository gate failed | **BLOCKED on full suite** |
| TEST-06 | TransactionManager lifecycle and compatibility entries passed | Owning entries passed; multiple legacy mint/sync consumers failed elsewhere | **BLOCKED** |

## Closure Decision

**Phase 12 Plan 05 is blocked.** Task 1 is green, inventory accounting is
complete, and the exact unfiltered command ran once, but its 14 failures
violate D-16/D-17 and the plan's zero-failure/zero-timeout/zero-crash closure
criteria. The report is a blocker artifact, not a completion summary.
