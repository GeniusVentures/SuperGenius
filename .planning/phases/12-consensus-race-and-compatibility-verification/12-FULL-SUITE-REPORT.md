# Phase 12 Full-Suite Verification Report

## Disposition

**BLOCKED by the mandatory isolated 11-node race at current HEAD.** This is a
fresh re-run after the teardown/CRDT repair commits (`f4c50540`, `289a1678`,
`18092111`, `af382b03`, and related). The fresh Phase 12 focused gate passed
7/7, but `bridge_race_single_burn_test` failed deterministically in two
consecutive isolated `-j1` executions at `af382b03`. Per the Plan 05 stop
gate ("if any focused or isolated test fails, stop and report the exact
blocker; do not proceed to characterize the full suite as passing"), the
unfiltered full-suite run was **not executed** in this window. No
repository-wide passing claim is made, and no source or test code was
modified under this reporting task.

Successful closure still requires a new unfiltered run with zero failures,
timeouts, crashes, `Not Run` entries, or unreviewed skips — and that run is
only meaningful after the mandatory race passes again in isolation.

## Reproduction Identity

| Field | Value |
|---|---|
| Verification date | `2026-08-19` |
| Focused gate window | `14:34:26-14:36:01 UTC` |
| Isolated race windows | `14:36:18-14:42:50 UTC` (run 1), `14:48:50-14:55:06 UTC` (run 2) |
| Repository commit | `af382b03f3488855ae499161c6f6374f3dd5a010` (`test: use portable CRDT temp paths`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` |
| Worktree state | Dirty: pre-existing modified `.planning/STATE.md` (phase bookkeeping), pre-existing modified `evmrelay` submodule pointer, one pre-existing untracked log artifact, plus this report |
| `evmrelay` recorded pointer | `4787e58204e4ca5590835779ec8a36ce02c59cb3` (superproject index) |
| `evmrelay` worktree identity | `62a9bbb101732a222466de19b80aca905af37e23` (`fix(09-06): check receipt ordinal narrowing`) — **differs from the recorded pointer and from the last passing run's identity** |
| `ProofSystem` identity | `a107566e745797f821d18d84994d4280b84f1cdc` (matches recorded pointer) |
| Build directory | `build/OSX/Release` (reconfigured with `cmake -S build/OSX -B build/OSX/Release` before building; generated Makefiles were stale for the focused targets) |
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
| Host TCP/process permissions | Available | Used for the focused gate and both isolated race runs |
| Anvil | Available | `/Users/henriqueklein/.foundry/bin/anvil`; started and stopped cleanly in both race runs |
| Cast | Available | `/Users/henriqueklein/.foundry/bin/cast`; used by the Anvil fixture to submit the sole burn |
| Fork RPC | Available | Fork startup, readiness, account funding, and the local burn succeeded in both race runs; source value omitted |
| Local bridge contract/funding | Available | Account #0 funding and bridge burn setup succeeded in both race runs |
| `RUN_E2E_BRIDGE` | Absent | Reviewed prerequisite-unavailable condition for positive `bridge_e2e_test` cases (not exercised this window) |
| `SIGNING_KEY` / `PRIVATE_KEY` | Absent | Confirms live signing cannot run; values were not printed |
| `RUN_E2E_RLPX` | Absent | Reviewed prerequisite-unavailable condition for the RLPx case (not exercised this window) |
| Live Sepolia signing | Unavailable | `bridge_sepolia_e2e_test` retains its `DISABLED_` body; suppressed prerequisite coverage, not an executed live test |

## Task 1 — Focused Phase 12 Gate

### Build

```sh
cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2
```

Result: **PASS**, exit `0`. A CMake reconfigure (`cmake -S build/OSX -B
build/OSX/Release`, exit `0`) was required first because the previously
generated Makefiles predated the rebase-era test registrations and had no
rules for the focused targets. All eight owning targets then built.

### Focused CTest

```sh
ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2
```

Result: **PASS** — 7/7 entries passed, zero failed, zero `Not Run`, total
real time 94.43 seconds (`14:34:26-14:36:01 UTC`).

| CTest entry | Result | Duration |
|---|---:|---:|
| `consensus_certificate_store_test` | PASS | 29.30s |
| `transaction_manager_pending_lifecycle_test` | PASS | 33.71s |
| `consensus_finalization_test` | PASS | 11.44s |
| `consensus_finality_race_test` | PASS | 5.07s |
| `consensus_vote_journal_test` | PASS | 34.48s |
| `certificate_compatibility_test` | PASS | 19.75s |
| `consensus_burn_reservation_test` | PASS | 44.19s |

### Mandatory isolated 11-node race

```sh
ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1
```

Result: **FAIL** in two consecutive isolated executions (CTest exit `8` both
times). Configured timeout unchanged at 500 seconds; neither run timed out.

| Run | Window (UTC) | CTest duration | GoogleTest body | Outcome |
|---|---|---|---|---|
| 1 | 14:36:18-14:42:50 | 391.99s | 243.445s | FAILED `BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn` |
| 2 | 14:48:50-14:55:06 | 376.28s | 243.658s | FAILED `BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn` |

**Failure assertion (both runs):**
`bridge_race_single_burn_test.cpp:201 — ASSERT_TRUE(application_converged)`
after the `kRaceNodeReadyTimeout` wait expired.

**What held (both runs, all 11 nodes):** exactly one canonical slot; 11
distinct local proposals; exactly one usable vote target per validator;
one convergent authority; the winner transaction `CONFIRMED` on every node;
no losing transaction `CONFIRMED` anywhere (each node's own losing proposal
remained `unconfirmed`, the rest `invalid`); `process_complete=1` on every
manager; all 11 explicit shutdowns and all 11 `GeniusNode` destructors
completed; Anvil stopped; the process exited naturally. The core TEST-01
consensus invariants (D-01..D-05) were satisfied.

**What failed (both runs, all 11 nodes, identical signature):** the winning
mint's applied output destination did not equal the burn destination.
`DeriveLightDestination(1)` is `s_nodes[1]->GetAddress()` (the node-1 SGNS
address). Every node instead recorded
`output_dest=bda716fdb55513ad` (abbrev), which is node 1's **validator
identity** as shown in that node's own trace events
(`stage=proposal validator=bda716fdb55513ad`), with `dest_match=0`,
`live_owner=bda716fdb55513ad`, `owner_match=0`, `owner_indexed=1`,
`output_amount=1`, `live_amount=1`, `live_state=0`. The destination balance
remained at its initial value (`0`) on every node, so
`GetBalance(destination) != initial + kMintAmount` and the convergence
predicate could never hold. Run 1 slot abbrev `1e8319d405715538`, winner
subject abbrev `1d00bf841255602e` (node 10's proposal); run 2 slot abbrev
`ad7f97c7d2627c8e`, winner subject abbrev `0dfc7dc6c60eb294`.

**Assessment:** deterministic, consensus-wide agreement on a mint output
whose destination is the destination node's validator identity rather than
its SGNS address. This is consistent with a destination-derivation or
burn-decoding regression in the rebase-era change set (the range
`5fec4d6e..af382b03` includes `Fix: Certificate lookup`,
`Fix: test mints were broken in phase 9`, `Fix: Removed aditional wrong
mint`, `Fix: votes when account switches`, and a worktree `evmrelay` at
`62a9bbb1` carrying receipt ordinal/position changes) — but that is an
inference, not a completed root-cause proof. The failure remains a real
blocker until a scoped follow-up fixes and verifies it. No fix was attempted
under this reporting task.

Raw evidence remains in
`build/OSX/Release/Testing/Temporary/LastTest.log` and the generated
`build/OSX/Release/xunit/` files for this worktree.

## Task 2 — Dynamic Inventory (execution gated off)

Command:

```sh
ctest --test-dir build/OSX/Release -N
```

Result: dynamic count **98** configured entries at `af382b03` (up from 84 at
the previous report; the rebase-era tree adds `account_signature_test`,
`burnconfig_test`, `genius_node_bootstrap_reconnect_test`,
`multisig_quorum_test`, `multisig_verify_test`, `remove_all_test`, the five
`securecrdt_*` entries, `transaction_manager_certificate_fallback_test`, and
the three `trustedpeerregistry_*` entries, and removes
`price_retrieval_test`).

Configured names, alphabetically:

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
node_startup_test, node_type_derivation_test, processing_datatypes_test, processing_nodes_test,
processing_result_durability_test, processing_schema_test,
processing_validate_result_data_test, prover_test,
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

Inventory caveat recorded for the follow-up full run: in this build tree the
configured entries `bridge_event_identity_test`,
`crdt_datastore_last_owner_test`, `public_chain_mint_validation_test`, and
`transaction_manager_certificate_fallback_test` currently have **no built
executable** (CTest reports "Could not find executable" during `-N`). Any
future unfiltered run must build the complete inventory first; a missing
executable surfaces as `Not Run`, which is a blocking state, never a skip.

### One unfiltered repository run

**Not executed in this window.** The Plan 05 Task 1 stop gate fired: the
mandatory isolated race failed, so no unfiltered
`ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2`
invocation was made and none is reported. The most recent unfiltered data
point remains the 2026-07-31 run at `5fec4d6e` (84 entries: 70 passed, 14
failed including two segfaults and one timeout), which was itself BLOCKED.
No success-only rerun, filter, or manual list is substituted for the missing
full-suite result.

## Requirement Matrix

| Requirement | Focused / isolated evidence | Full-suite evidence | Status |
|---|---|---|---|
| TEST-01 | Race consensus invariants held (one slot, 11 proposals, one vote per validator, one authority, winner-only confirmation, clean teardown) in both runs, but `application_converged` failed: mint output destination ≠ burn destination on all 11 nodes | Not run this window; prior run BLOCKED | **BLOCKED** |
| TEST-02 | `consensus_finality_race_test` passed in the fresh focused gate | Not run this window; prior run BLOCKED | **BLOCKED on full suite** |
| TEST-03 | Restart vote-lock regression passed in `consensus_vote_journal_test` | Not run this window; prior run BLOCKED | **BLOCKED on full suite** |
| TEST-04 | Before/after-deadline regression passed in `consensus_vote_journal_test` | Not run this window; prior run BLOCKED | **BLOCKED on full suite** |
| TEST-05 | Finality-race, certificate-store, burn-reservation, and compatibility entries passed | Not run this window; prior run BLOCKED | **BLOCKED on full suite** |
| TEST-06 | TransactionManager lifecycle and compatibility entries passed | Not run this window; prior run had multiple legacy mint/sync consumer failures | **BLOCKED** |

## Closure Decision

**Phase 12 Plan 05 is blocked.** The focused gate is green at `af382b03`,
but the mandatory D-14 isolated race fails deterministically with a
consensus-wide mint-destination mismatch, and the plan's stop gate forbids
characterizing the full suite under this condition. D-16/D-17 are unsatisfied
(no fresh unfiltered run exists), and D-14 is unsatisfied. The report is a
blocker artifact, not a completion summary.

**Recorded blockers for a scoped follow-up decision:**

1. `bridge_race_single_burn_test` — deterministic `application_converged`
   failure at `af382b03`: the winning mint's output destination is node 1's
   validator identity instead of its SGNS address (`dest_match=0`,
   `owner_match=0`, zero destination balance) on all 11 nodes, in two
   consecutive isolated runs. Suspected rebase-era destination/decoding
   regression; root cause not proven.
2. Full-suite state at `af382b03` is unknown; the last unfiltered run
   (2026-07-31, `5fec4d6e`) failed 14/84 including two segfaults and one
   timeout. A fresh unfiltered run is still owed once the race is green.
3. Four configured tests currently lack built executables in
   `build/OSX/Release` (see inventory caveat); any full run must build the
   complete inventory first to avoid `Not Run` entries.
4. The `evmrelay` worktree (`62a9bbb1`) differs from the superproject's
   recorded pointer (`4787e582`); the correct pinned identity must be
   decided and committed as part of the follow-up.
