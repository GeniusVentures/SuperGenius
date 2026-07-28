---
phase: 11-slot-owned-bridge-burn-reservations
status: complete
mapped: 2026-07-28
---

# Phase 11 Pattern Map: Slot-Owned Bridge Burn Reservations

## Architectural Through-Line

The closest existing architecture is Phase 10's split between replicated certificate authority and direct-RocksDB validator-private lifecycle state. Phase 11 should extend that split rather than create a third persistence model:

```text
approved mint subject
  -> canonical slot + canonical burn descriptor
  -> direct-RocksDB reservation create/join
  -> ephemeral candidate activation / possible durable vote
  -> replicated authoritative certificate
  -> direct-RocksDB FinalizedPendingApplication
  -> exact-winner handler
  -> one RocksDB batch: outputs + application record + bridge input + reservation Consumed
```

The authoritative winner remains the slot certificate. The reservation is node-local safety state and must never go through `GlobalDB::Put()` or a CRDT topic.

## File Map and Closest Existing Analogs

### `src/blockchain/impl/proto/ConsensusLocalState.proto` — modify

**Role:** Define the versioned private reservation record and reciprocal outpoint index. Likely states are `RESERVED`, `FINALIZED_PENDING_APPLICATION`, `CONSUMED`, and `SAFETY_ERROR`; the record needs canonical slot/outpoint identity, a random generation token, maximum admitted-candidate horizon, and exact certificate/winner identity after finality.

**Closest analog:** `DurableVoteRecord` and `CertificateProcessingRecord` in the same file.

```proto
message DurableVoteRecord {
  uint32 schema_version = 1;
  State state = 2;
  string slot_id = 3;
  string proposal_id = 4;
  uint64 generation = 11;
  uint64 created_at_ms = 12;
  uint64 acceptance_horizon_ms = 13;
}

message CertificateProcessingRecord {
  uint32 schema_version = 1;
  State state = 2;
  string slot_id = 3;
  string certificate_digest = 4;
  string proposal_id = 5;
  string winner_id = 6;
  uint64 lease_until_ms = 8;
  uint64 updated_at_ms = 9;
}
```

**Conventions:** Keep this schema private and node-local; retain explicit `schema_version`; never accept `STATE_UNSPECIFIED`; use canonical strings/bytes that can be recomputed and strictly checked. Do not put proposer, account, nonce, destination, or current-best identity in ownership fields. Model the reciprocal outpoint index as a typed strict protobuf too, not an unchecked string, so scan recovery can validate both directions.

### `src/blockchain/ConsensusStateStore.hpp` / `.cpp` — modify

**Role:** Own reservation and index key construction, strict reads/scans, create-or-join, finality, safety-error, consumed, and expected-generation deletion. Enforce node-wide slot/outpoint uniqueness through one store mutex and atomic two-key batches.

**Closest analogs:** the existing direct RocksDB vote/process APIs and conflict+safety two-record batch.

```cpp
enum class ConsensusStateStoreError : uint8_t {
    InvalidArgument,
    Integrity,
    Conflict,
    Storage,
};

outcome::result<std::optional<VoteRecord>> GetVote(
    const std::string &validator_id, const std::string &slot_id) const;
outcome::result<std::vector<VoteRecord>> ScanVotes() const;
outcome::result<void> PutActiveVote(const VoteRecord &record);
```

```cpp
template <typename Message>
bool ParseStrict(std::string_view bytes, Message &message) {
    if (bytes.empty() ||
        !message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) ||
        !message.GetReflection()->GetUnknownFields(message).empty()) {
        return false;
    }
    std::string canonical;
    return message.SerializeToString(&canonical) && canonical == bytes;
}
```

```cpp
std::lock_guard lock(mutex_);
auto batch = datastore_->batch();
BOOST_OUTCOME_TRY(batch->put(key, value));
auto committed = batch->commit();
if (committed.has_error())
    return outcome::failure(ConsensusStateStoreError::Storage);
```

**Data flow:** `ConsensusManager` supplies a validated descriptor and expected generation; the store re-reads slot and outpoint records while holding `mutex_`, validates reciprocal identity and legal transition, writes/removes both in one batch, then returns the resulting typed record/disposition.

**Conventions:**

- Add keys under the existing `/consensus/local/v2/` namespace, for example `burn/slot/` and `burn/outpoint/`.
- Keep `Read*Unlocked` helpers for callers that already hold `mutex_`; public methods take the lock once.
- Map RocksDB `NOT_FOUND` to an empty optional only where absence is legal; every other read failure is `Storage`.
- Use `StrictScan` and validators that check protobuf canonical bytes, key/value identity, lowercase 64-hex slot IDs, canonical decimal chain ID, nonzero burn hash, recomputed outpoint key and mint slot, reciprocal index, state-specific required/forbidden fields, and exact certificate relationships.
- `create-or-join` is idempotent only for the same slot/outpoint/generation semantics. Same slot with another outpoint, or same outpoint with another slot, is `Conflict`/`Integrity`, never replacement.
- Expected-generation deletion must re-read under the store mutex and batch-remove both keys. Deleting by slot alone is forbidden.
- Use a cryptographically random 128-bit-or-greater token, preferably 256-bit lowercase hex to match existing hashes. Do not use the Phase 10 integer slot/vote generation as the durable burn generation.

### `src/blockchain/Consensus.hpp` / `.cpp` — modify

**Role:** Sequence post-validation admission, reservation recovery, pre-application finality, automatic abandonment, and races with candidate admission/finality under the slot lifecycle boundary.

**Closest admission seam:** `HandleProposal()` and `RetryPendingProposal()` currently call the subject handler and immediately activate the candidate:

```cpp
auto subject_result = subject_handler(proposal.subject());
// Reject/Stalled/Pending handling...
ContinueProposalAfterSubject(proposal, slot_result.value());
```

Insert a typed resource-admission hook after `Approve` and before every `ContinueProposalAfterSubject()` call. A persistence error must return without adding an active candidate and therefore before vote creation or publication. `Pending` validation never reserves.

**Closest slot sequencing analog:** `FinalizeSlot()` reserves `SlotState::Lifecycle::Finalizing`, increments a generation, drops `proposals_mutex_` for storage, then rechecks before publishing/applying. Follow this reserve/drop/recheck shape; do not hold `proposals_mutex_` over RocksDB, CRDT, or arbitrary transaction handlers.

```cpp
while (slot.lifecycle == SlotState::Lifecycle::SigningPublishing ||
       slot.lifecycle == SlotState::Lifecycle::PublishingReplay ||
       slot.lifecycle == SlotState::Lifecycle::Finalizing) {
    slot_cv_.wait(lock, ...);
}
reservation_generation = ++slot.generation;
slot.lifecycle = SlotState::Lifecycle::Finalizing;
```

**Closest finality/recovery analog:**

```cpp
if (!state_store_->PutPendingProcess(pending))
    return FinalizeResult::StorageFailure;
auto processed = ProcessFinalizedCertificate(normalized, slot_id, winner_id);
```

Add the reservation transition to `FinalizedPendingApplication` after the authoritative certificate pair is established but before process/handler/cleanup work. The certificate-only path must be able to create finalized protection directly from the certified mint descriptor. Failure to persist this transition returns `StorageFailure`; no handler or cleanup runs.

**Startup analog:** `ConsensusManager::New()` calls `RestoreLocalState()` before subscription, certificate filter registration, timer start, restored work recovery, or vote replay:

```cpp
if (!instance->RestoreLocalState()) return nullptr;
instance->EmitStartupEvent("subscribe");
instance->consensus_subs_future_ = instance->pubsub_->Subscribe(...);
```

Extend `RestoreLocalState()` to scan and reconcile reservations at the same fail-closed stage. Missing ephemeral candidates are normal. Malformed reciprocal records, slot recomputation failures, or contradictory certificate/vote bindings abort construction.

**Abandonment analog:** add reconciliation to the owned timer loop beside `ProcessCandidateDeadlines`, `ExpirePendingProposals`, and `RecoverPendingCertificateWork`; do not create detached per-slot threads. Release only at `now > max(candidate_horizon, active_vote_horizon)`, after an exact typed certificate `NotFound`, and through expected-generation deletion.

**Cleanup correction:** `ExpirePendingProposals()` currently ends with `ClearProposalSlot(proposal)`, while `ClearProposalSlot()` can set `FinalizedPendingApplication` and fires slot cleanup callbacks. Split proposal-local expiry/removal from certified final-slot cleanup. Neither path may directly release a reservation; whole-slot reconciliation owns deletion.

**Conventions:** Keep one validator signature per slot governed by the existing durable vote acceptance horizon. Re-read `ConsensusStateStore::GetVote()` rather than inferring safety from transient `SlotState`. Certificate finality always defeats stale admission/release work. Preserve `processing_slots_` as the exact-winner handler lease and existing shutdown activity ownership.

### `src/blockchain/Blockchain.hpp` / `src/blockchain/impl/Blockchain.cpp` — modify

**Role:** Forward narrow subject-specific callbacks/queries between `TransactionManager` and `ConsensusManager`, analogous to current registration forwarding.

```cpp
bool RegisterSubjectHandler(std::string_view subject_type,
                            ConsensusManager::SubjectHandler handler);
bool RegisterCertificateHandler(std::string_view subject_type,
                                ConsensusManager::CertificateSubjectHandler handler);
bool RegisterProposalCleanupHandler(std::string_view subject_type,
                                    ConsensusManager::ProposalCleanupHandler handler);
```

**Conventions:** Use canonical subject type strings and hash dispatch exactly as existing handlers do. Registration should return success/failure where overwriting must be rejected, and callbacks should capture `weak_ptr` then lock it, matching `TransactionManager::New()`. Keep descriptor extraction/admission distinct from semantic validation and proposal cleanup.

### `src/account/TransactionManager.hpp` / `.cpp` — modify

**Role:** Extract the exact mint burn descriptor after validation, register consensus lifecycle hooks, stop proposal-owned bridge rollback, expose durable burn availability to relayer paths, and classify exact-winner application results.

**Closest identity analog:** `MintTransactionV2::GetSlotPreimage()` requires one input and builds the canonical identity from chain, burn hash, and receipt-local input index; `GeniusTransaction::GetSlotID()` hashes that preimage.

```cpp
static constexpr std::string_view kPrefix = "mint-v2:";
if (!canonical_chain || utxo_params_.first.size() != 1)
    return outcome::failure(std::errc::invalid_argument);
const auto &input = utxo_params_.first.front();
```

The descriptor extractor must deserialize the approved embedded mint, require the DAG uncle hash and sole input hash to agree, use `input.output_idx_` as receipt-log index, recompute `GetSlotID()`, and compare it to consensus' slot key. Return not-applicable for non-mint nonce subjects.

**Current anti-pattern to remove only for mint-v2:**

```cpp
account_m->GetUTXOManager().ReserveUTXOs(
    mint_inputs, transaction_hash, UTXOManager::UTXOType::UTXO_BRIDGE);
// catch: RollbackUTXOs(... transaction_hash, UTXO_BRIDGE)
```

`MintFunds`, `ChangeTransactionState(FAILED)`, mint revert, and `OnProposalTimeoutCleanup` must no longer own/release the bridge reservation. Ordinary transfer/escrow reservation behavior stays unchanged.

**Exact-winner application analog:** the current mint application prepares `UTXOManager::AtomicMintEffectRequest` and calls `ApplyMintEffectsAtomically()`. Preserve exact application idempotence, but return a typed disposition that distinguishes applied/already-applied, retryable storage/unavailability, and irreconcilable different-winner state. The last category drives durable `SafetyError`; it is not a generic `Pending` retry.

**Relayer query:** `GetBridgeBurnState()` currently checks application then transient UTXO state. It should query durable node-wide reservation/application authority. A reserved result blocks local relaying but must not make consensus reject another semantically valid contender for the same slot.

### `src/account/UTXOManager.hpp` / `.cpp` — modify

**Role:** Preserve the physical application boundary and extend it so `Consumed` reservation state is committed in the same RocksDB batch as winning outputs, bridge application record, and bridge-input consumption. Materialize a missing synthetic bridge input deterministically from certified facts when safe.

**Closest analog:** `ApplyMintEffectsAtomically()` already takes locks in the required order and stages all UTXO changes before one commit:

```cpp
std::unique_lock persistence_lock(persistence_mutex_);
std::unique_lock state_lock(utxos_mutex_);
auto candidate_outpoints = utxo_outpoints_;
auto candidate_addresses = address_outpoints_;
auto batch = db->batch();
// batch UTXO records and application record
BOOST_OUTCOME_TRY(batch->commit());
utxo_outpoints_.swap(candidate_outpoints);
address_outpoints_.swap(candidate_addresses);
```

It also has exact replay protection:

```cpp
if (existing_application.has_value()) {
    if (existing_application.value().toString() != application_bytes)
        return outcome::failure(std::errc::state_not_recoverable);
    return AtomicMintEffectResult::AlreadyApplied;
}
```

**Conventions:** Maintain lock order `persistence_mutex_` then `utxos_mutex_`. All durable reservation writers participating in this batch must share the same underlying store gate; do not perform a separate reservation update after commit. Validate expected slot, outpoint, generation token, certificate digest, proposal ID, and winning transaction before batching `Consumed`. On duplicate delivery verify all four artifacts exactly. The in-memory `local_reservations_` remains only for ordinary transaction reservation and is not Phase 11 authority.

### `src/blockchain/impl/CMakeLists.txt` — likely unchanged or minimal modify

**Role:** Existing protobuf generation/link wiring already includes the local-state schema:

```cmake
add_proto_library(ConsensusLocalStateProto proto/ConsensusLocalState.proto)
target_link_libraries(blockchain_genesis PUBLIC ConsensusLocalStateProto)
```

Only update this file if a new proto/library is deliberately split out. Extending `ConsensusLocalState.proto` requires no new target.

### `test/src/blockchain/consensus_burn_reservation_test.cpp` — create

**Role:** Focused real-RocksDB harness for strict store behavior, admission ordering, restart reconciliation, horizons, and deterministic release/admission/finality races.

**Closest fixture:** `consensus_vote_journal_test.cpp` uses `BaseCRDTTest`, a friend-only access class, direct `ConsensusStateStore`, clock overrides, query failures, and startup event observation.

```cpp
class ConsensusVoteJournalTestAccess {
public:
    static void FailQueries(ConsensusStateStore &store) { store.query_ = ...; }
    static void SetClocks(std::chrono::steady_clock::time_point steady,
                          uint64_t system_ms) { ... }
    static void ObserveStartup(std::function<void(std::string_view)> observer) { ... }
};
```

**Conventions:** Use a Phase-11-specific friend access surface; reset every static hook in fixture teardown; use unique real RocksDB test directories through existing CRDT/storage fixtures; inject fixed system and steady clocks; assert exact strict errors and raw key presence/absence. Test equality boundary (`now == horizon` remains reserved) and strict passage (`now > horizon` releases).

### `test/src/blockchain/consensus_finalization_test.cpp` — modify

**Role:** Add certificate-before-reservation/application ordering, duplicate ingress, transient/safety-error behavior, and finality-vs-release/admission interleavings.

**Closest fixture:** `DeterministicBarrier` plus RAII `ScopedWorker` pauses production observers at exact stages and always releases/joins workers. Follow that pattern rather than sleeps:

```cpp
handler_barrier.WaitUntilArrived();
EXPECT_FALSE(close_returned.load());
handler_barrier.Release();
```

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — modify

**Role:** Prove pending/expired proposal removal is proposal-local, does not fabricate finality, and never releases a slot-owned burn. Confirm a remaining or later contender uses the same reservation generation until safe whole-slot abandonment.

**Closest analog:** existing pending TTL, scheduled retry, cleanup callback, and no-wall-clock fixture. Drive expiry through friend access/clock injection instead of sleeping.

### `test/src/blockchain/consensus_vote_journal_test.cpp` — modify only if shared restart assertions fit better here

**Role:** Verify reservation reconciliation consults the exact durable vote record and its `acceptance_horizon_ms`, does not require restored candidates, and occurs before subscription/replay. Prefer keeping most new cases in the focused reservation target.

### `test/src/account/transaction_manager_pending_lifecycle_test.cpp` — modify

**Role:** Verify validation remains pure, same-burn contenders produce the same descriptor/slot, timeout/failed/revert paths do not release, and handler disposition distinguishes retryable versus irreconcilable application failure.

**Closest analog:** existing weak-pointer handler registration and temporary transaction tracking checks. Keep remote ephemeral tracking independent from durable reservation ownership.

### `test/src/account/utxo_manager_test.cpp` — modify

**Role:** Verify one-batch outputs/application/input/reservation transition, exact replay, conflicting winner, reconstructible missing bridge input, and injected pre-commit failure leaving every artifact unchanged.

**Closest fixture:** `UTXOManagerTestAccess` exposes private `FaultStage` callbacks. Extend the existing stage enum/friend seam instead of exposing production fault APIs. Use the current `AtomicMintBeforeBatchCommit` stage and real RocksDB reopen checks.

### `test/src/blockchain/CMakeLists.txt` — modify

**Role:** Register the new target using the repository's `addtest(...)` convention and the same links as Phase 10 consensus integration tests.

```cmake
addtest(consensus_burn_reservation_test
    consensus_burn_reservation_test.cpp
)
target_link_libraries(consensus_burn_reservation_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)
```

`test/src/account/CMakeLists.txt` needs no new target if account cases extend existing tests.

## Cross-Cutting Planner/Executor Conventions

1. **Pure validation:** never mutate reservations from `ValidateUTXOParametersForConsensus`, public-chain receipt verification, or a `Pending` subject result.
2. **Persist before visibility:** the admission hook must finish durable create/join before `ContinueProposalAfterSubject`; storage failure means no active candidate and no vote.
3. **One owner:** ownership is `(canonical slot, canonical chain/burn/receipt-log outpoint)`, not any candidate field. Same-burn contenders join the generation.
4. **Replicated authority, local protection:** certificates use current CRDT-backed storage; reservations use direct RocksDB only.
5. **Finality before application:** persist `FinalizedPendingApplication` before process handler, cleanup, or application. Certificate-only observation can create protection.
6. **Atomic physical effects:** `Consumed` must commit with outputs, application record, and input consumption in one batch.
7. **Typed failures:** absence, storage failure, retryable application, integrity contradiction, and certificate conflict must remain distinguishable.
8. **ABA-safe deletion:** every cleanup captures a random generation, re-reads it under the shared store mutex, and conditionally deletes both reciprocal records.
9. **Strict horizons:** release only when `now >` every candidate/vote certificate-acceptance horizon and typed certificate lookup is exact `NotFound`.
10. **Startup fail closed:** reservation scans/reconciliation precede all observable consensus activity. Missing ephemeral candidates are expected; malformed durable identity is fatal.
11. **Built-in slot resolver ordering:** canonical nonce/mint slot derivation must be available before `ConsensusManager::RestoreLocalState()`. Current registration in `TransactionManager::New()` is too late and same-process static-map tests can hide it.
12. **No detached work:** reuse the consensus timer, lifecycle condition variable, processing lease, and shutdown activity guard.
13. **Deterministic tests:** fixed clocks, fault callbacks, predicate barriers, RAII worker joining, real RocksDB restart, and nonzero GoogleTest filter guards; no wall-clock sleeps.
14. **Scope fence:** do not absorb mock-RPC/startup simulation or the complete 11-node race; Phase 12 owns that proof.

## Recommended Dependency Order

1. Focused test harness and strict reciprocal reservation store.
2. Built-in canonical mint-slot resolver and startup restoration/reconciliation.
3. Post-approve/pre-active admission hook and transaction descriptor integration.
4. Durable finality/safety-error transitions and typed exact-winner disposition.
5. Atomic UTXO/application/reservation consumption.
6. Deterministic abandonment and race closure.
7. Focused integration plus Phase 9/10 regression gate.

