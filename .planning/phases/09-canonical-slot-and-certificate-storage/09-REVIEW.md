---
phase: 09-canonical-slot-and-certificate-storage
reviewed: 2026-07-24T15:45:00-03:00
depth: standard
files_reviewed: 47
files_reviewed_list:
  - evmrelay/include/eth/eth_receipt_source.hpp
  - evmrelay/include/eth/event_filter.hpp
  - evmrelay/src/eth/eth_receipt_source.cpp
  - evmrelay/src/eth/event_filter.cpp
  - src/account/BridgeRelayer.cpp
  - src/account/GeniusInputValidator.cpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/GeniusTransaction.cpp
  - src/account/GeniusTransaction.hpp
  - src/account/MintTransactionV2.cpp
  - src/account/MintTransactionV2.hpp
  - src/account/PublicChainInputValidator.cpp
  - src/account/PublicChainInputValidator.hpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/account/UTXOManager.cpp
  - src/account/UTXOManager.hpp
  - src/account/proto/SGTransaction.proto
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - src/blockchain/impl/Blockchain.cpp
  - src/crdt/crdt_data_filter.hpp
  - src/crdt/crdt_datastore.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/impl/crdt_data_filter.cpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/watcher/impl/bridge_catchup_watcher.cpp
  - src/watcher/impl/bridge_catchup_watcher.hpp
  - test/src/account/bridge_event_identity_test.cpp
  - test/src/account/bridge_relayer_test.cpp
  - test/src/account/public_chain_input_validator_slot_test.cpp
  - test/src/account/public_chain_mint_validation_test.cpp
  - test/src/account/transaction_manager_pending_lifecycle_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/certificate_compatibility_test.cpp
  - test/src/blockchain/consensus_certificate_store_test.cpp
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/blockchain/consensus_slot_key_test.cpp
  - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
  - test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp
  - test/src/crdt/CMakeLists.txt
  - test/src/crdt/crdt_datastore_last_owner_test.cpp
  - test/src/crdt/crdt_datastore_test.cpp
  - test/src/startup/startup_wiring_test.cpp
findings:
  critical: 0
  warning: 3
  info: 0
  total: 3
status: issues_found
---

# Phase 09: Code Review Report

## Summary

The three final gap closures are sound in their primary paths. Plan 09-14
serializes ordinary UTXO persistence and atomic mint application through one
gate, commits the winner's complete UTXO state and application record in one
batch, validates exact replay, and publishes `CONFIRMED` only afterward. Plan
09-15 scopes worker ownership to bounded turns and moves close/destruction to
the registered reaper. Plan 09-16 publishes endpoint/factory state as immutable
generations and retains one generation for each vote or receipt decision.

Three actionable lifetime/determinism issues remain. The receipt-source bridge
still has the previously reported raw-callback teardown problem. The validator
move constructor introduced to preserve test/helper compatibility transfers its
registration bookkeeping without transferring the registry's raw pointers.
Finally, the coherent vote snapshot selects its chain from
`unordered_map::begin()`, even though the shipped configuration installs eight
chains and public-slot quorum groups votes by the resulting hashes.

## Narrative Findings (AI reviewer)

### Warnings

#### WR-01 — Receipt source retains a callback to a destroyed bridge

**File/line:** `evmrelay/include/eth/eth_receipt_source.hpp:61-87`,
`evmrelay/src/eth/eth_receipt_source.cpp:39-49`,
`evmrelay/src/eth/eth_receipt_source.cpp:73-101`

**Issue:** `EthReceiptSourceBridge` installs a receipt handler capturing raw
`this`, but it still has no destructor or lifetime token. Its remaining source
filters and `EthWatchService` watches also remain registered unless every caller
manually calls `unwatch`. Both collaborators are references and can validly
outlive the bridge, so a later receipt batch invokes `process_receipt_batch` on
freed storage.

**Impact:** Normal teardown ordering can cause use-after-free and dispatch
callbacks for watches whose bridge owner no longer exists.

**Fix:** Add idempotent teardown that first prevents/clears source dispatch and
then removes every source filter and service watch. Define synchronization with
in-flight source callbacks, or capture a weak lifetime token rather than raw
`this`. Add a regression that destroys the bridge while source/service objects
remain alive, emits another batch, and verifies no callback or watch survives.

#### WR-02 — Moving a registered public-chain validator leaves dangling registry pointers

**File/line:** `src/account/PublicChainInputValidator.hpp:71-103`,
`src/account/PublicChainInputValidator.cpp:68-76`

**Issue:** The move constructor transfers `registered_chain_ids_` from
`other`, but `IInputValidator` registrations are raw pointers that still point
to `&other`. The moved-from destructor has an empty ID list and therefore does
not unregister them. The moved-to destructor tries
`UnregisterIf(chain_id, this)`, which cannot match the old pointer.

**Impact:** Moving after `RegisterForChain` permanently leaves the global
registry pointing at the moved-from object. Once that object is destroyed,
witness validation can dereference freed memory; the stale entry also prevents
a replacement validator from claiming the chain.

**Fix:** Either delete the move constructor and change helpers to avoid requiring
movement, or add an atomic registry `ReplaceIf(chain, &other, this)` operation
and rebind every transferred chain while both objects are alive. Add a test that
registers, moves, destroys the source, and proves lookup returns the destination
and later unregisters cleanly.

#### WR-03 — Multi-chain vote snapshots choose a non-canonical chain

**File/line:** `src/account/PublicChainInputValidator.cpp:384-411`,
`src/account/GeniusNode.cpp:706-739`

**Issue:** `GetVoteRpcSnapshot` obtains a coherent generation but selects
`snapshot->rpc_endpoints.begin()->first` from an `unordered_map`.
`GetFirstConfiguredChainId` repeats the same selection. There is no canonical
ordering or transaction/subject chain input. This is active in the shipped
configuration: provider initialization adds endpoints for eight chains, and
the slot-hash populator applies the selected chain's hashes to every signed
vote.

**Impact:** Nodes with the same endpoint set but different insertion, rehash, or
standard-library iteration order can sign different public slot hashes.
Public-slot quorum groups validators by exact hash, so this can split endpoint
agreement and prevent otherwise valid quorum. An empty endpoint vector that
sorts/iterates first can also turn a configured node into an abstainer.

**Fix:** Bind vote hashes to the consensus subject's canonical source chain and
pass that chain explicitly to the snapshot API. If the current populator cannot
receive subject context, define and enforce a deterministic configured-chain
policy (for example, one required configured chain or a sorted explicit primary
chain) and fail closed on ambiguity. Add a test with at least two chains
inserted in opposite orders and assert identical, intended vote hashes.

## Verification

- Reviewed committed `HEAD` against
  `16440b18baa1e6210d13c7f1b1bfca4de69b9ed3^`, plus every Phase 09 plan and
  summary for intent and claimed verification.
- Traced Plan 09-14 mint application through persistence ordering, application
  replay, restart, duplicate confirmation, and `AlreadyHandled` classification.
- Traced Plan 09-15 worker/callback promotions, final-owner custom deletion,
  reaper close arbitration, worker joining, and completion fulfillment.
- Traced Plan 09-16 writer serialization, immutable publication, vote snapshot
  construction, provider merge, and operation-scoped receipt verification.
- Focused binaries exited successfully: mint lifecycle filter, CRDT final-owner
  callback teardown, concurrent RPC generation publication, and the complete
  bridge event identity suite.
- `git diff --check` is not clean because committed phase changes contain
  trailing whitespace in `src/crdt/globaldb/globaldb.hpp` and an out-of-scope
  account CMake file; these were not elevated because they are non-material to
  correctness.
- The user-owned unstaged logger-level hunk in `src/account/GeniusNode.cpp`,
  `ProofSystem`, and generated/untracked test directories were excluded and
  left untouched.

## Self-Check

- Frontmatter count and `files_reviewed_list` contain all 47 requested paths.
- Severity counts sum to three and match the narrative findings.
- Every finding includes concrete evidence, impact, and an actionable fix.
- No source, test, or tracking file was modified, and no commit was created.

---
*Reviewed: 2026-07-24 | Depth: standard | Files: 47 | Findings: 0 critical, 3 warnings, 0 info*
