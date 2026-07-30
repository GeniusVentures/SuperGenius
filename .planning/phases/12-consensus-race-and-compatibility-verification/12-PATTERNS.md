---
phase: 12-consensus-race-and-compatibility-verification
status: complete
mapped: 2026-07-30
---

# Phase 12 Pattern Map: Consensus Race and Compatibility Verification

## Architectural Through-Line

Phase 12 observes and tests the finality architecture already established by Phases 9-11:

```text
one external burn
  -> eleven local mint proposals
  -> one canonical slot
  -> at most one distinct published vote target per validator
  -> one authoritative slot certificate + verified winner index
  -> one finalized reservation/application
  -> one confirmed mint replicated to all nodes
```

Private observers expose facts; they do not choose candidates, persist votes, accept certificates, or apply transactions.

## File Map and Closest Existing Analogs

### `src/blockchain/Consensus.hpp` / `.cpp` — modify

**Role:** Define a private per-manager structured trace event/observer, emit proposal/vote/certificate facts, and add the deterministic `authority-established` finalization stage.

**Closest analogs:** Existing per-manager finalization/conflict observers and the vote-stage hook:

```cpp
std::function<void( const ConsensusStateStore::ConflictRecord &, bool )>
    certificate_conflict_observer_;
std::function<void( std::string_view )> finalization_stage_observer_;
static inline std::function<void( std::string_view, const std::string &, uint64_t )>
    vote_stage_observer_;
```

**Pattern:** Prefer a per-instance observer for an 11-manager process. The event must carry validator, slot, proposal, winner, and deterministic digest identifiers needed by the test. Invoke it after state transitions and without `proposals_mutex_`, handler-registry locks, or datastore gates held. Never let its return value influence production behavior.

**Authority-stage placement:** Follow the existing finalizer order:

```cpp
// authoritative pair is persisted or verified
{
    std::lock_guard lock( proposals_mutex_ );
    restored_final_slots_.insert( slot_id );
    slot.lifecycle = SlotState::Lifecycle::FinalizedPendingApplication;
}
// emit authority-established here, outside the lock
// then finalize burn reservation / write process / invoke application
```

### `src/blockchain/Blockchain.hpp` and `src/account/TransactionManager.hpp` — modify minimally

**Role:** Grant one named bridge-race test accessor private traversal to the existing manager graph.

**Closest pattern:** Existing narrowly named friends such as `TransactionManagerPendingLifecycleTestAccess`, `ConsensusFinalizationTestAccess`, and `NetworkConfigPrecedenceTestAccess`.

**Access route:** Use public `GeniusNode::GetTransactionManager()`, then friend-only reads of `TransactionManager::blockchain_` and `Blockchain::consensus_manager_`. Do not add public getters for consensus internals and do not modify `GeniusNode.cpp`.

### `test/src/bridge_race/bridge_race_fixture.hpp` — modify

**Role:** Install per-node observers after all managers are ready, collect thread-safe structured evidence, provide bounded wait predicates, and format failure snapshots.

**Closest patterns:** The fixture's existing all-node readiness barrier and deterministic node identity array:

```cpp
ASSERT_WAIT_FOR_CONDITION(
    [&]() {
        return std::all_of(s_nodes.begin(), s_nodes.end(),
            [](const auto &node) {
                return node && node->GetState() == GeniusNode::NodeState::READY;
            });
    },
    kRaceNodeReadyTimeout,
    "all 11 bridge-race nodes READY",
    nullptr);
```

**Pattern:** Store one evidence record per node/validator behind a mutex. Observer callbacks only append/copy lightweight facts. Predicate functions take snapshots under the mutex and release it before querying nodes. Diagnostic formatting is bounded and deterministic.

### `test/src/bridge_race/bridge_race_single_burn_test.cpp` — modify

**Role:** Prove TEST-01 through direct participation, vote, certificate, transaction, and balance assertions.

**Closest current setup:** Preserve one burn, three local-Anvil verification slots, checked endpoint configuration, and the all-node balance query. Replace the balance-only readiness predicate and unconditional `sleep_for`.

**Pattern:** Use ordered barriers: 11 local proposals → one slot → authoritative certificate on 11 nodes → winner confirmation on 11 nodes → bounded stable event counters. Group vote events by validator and compare distinct proposal targets/digests; exact duplicate bytes are allowed.

### `test/src/blockchain/consensus_finality_race_test.cpp` — create

**Role:** Dedicated focused integration target for TEST-02 and external-ingress portions of TEST-05.

**Closest fixture:** `consensus_finalization_test.cpp` provides `DeterministicBarrier`, RAII worker ownership, real `ConsensusManager`, real `BaseCRDTTest`, conflict inspection, lifecycle inspection, and finalization-stage observation.

```cpp
class DeterministicBarrier {
public:
    void ArriveAndWait();
    void WaitUntilArrived();
    void Release();
};
```

**Pattern:** A Phase-12-specific friend accessor may invoke `OnConsensusMessage()`/`HandleCertificate()`, CRDT filtering/callback recovery, slot lookup, and private stage installation. The core TEST-02 test pauses at `authority-established`, injects the competitor while lookup already returns the winner, and releases via RAII even when an assertion fails.

### `test/src/blockchain/consensus_vote_journal_test.cpp` — modify

**Role:** Make TEST-03/04 explicit.

**Closest patterns:** `RestartReplaysExactStoredEnvelopeWithoutSigning` for same-datastore reconstruction and `FixedDeadlineSelectsComparatorWinnerAndPersistsBeforeExactRawPublish` for controlled pre/post-deadline ordering.

```cpp
ConsensusVoteJournalTestAccess::SetClocks(steady, system_ms);
ConsensusVoteJournalTestAccess::Continue(manager, proposal);
ConsensusVoteJournalTestAccess::ProcessDeadline(manager, deadline);
```

**Pattern:** Extend restart with a different proposal sharing the restored slot; assert no new signer call and no distinct outbound envelope. Keep RAII reset for every static clock/publish override.

### `test/src/blockchain/consensus_certificate_store_test.cpp` — modify narrowly

**Role:** Preserve TEST-05 typed corruption and duplicate behavior without elapsed sleeps.

**Closest patterns:** Existing reader injection, callback counters, slot/index raw-state snapshots, and `ASSERT_WAIT_FOR_CONDITION`.

**Pattern:** Replace `sleep_for` absence checks with bounded negative/stability predicates tied to callbacks/publication counters or deterministic state inspection. Keep typed `NotFound`, `IntegrityError`, `StorageError`, and `Conflict` assertions and verify authority bytes remain unchanged.

### `test/src/blockchain/certificate_compatibility_test.cpp`, `test/src/account/transaction_manager_pending_lifecycle_test.cpp`, `test/src/account/utxo_manager_test.cpp` — verify/modify only if a concrete gap appears

**Role:** TEST-05/06 authoritative storage, actual previous-nonce consumer, actual producer-UTXO consumer, and atomic mint application gates.

**Closest real-consumer tests:**

- `PreviousNonceCertificateLookupPreservesConsumerSemantics`
- `ProducerUTXOCertificateLookupPreservesConsumerSemantics`
- `AtomicFinalizedMintFailureRetryAndRestartReplayAreExact`
- `ConsumedApplicationRejectsDifferentWinnerIdentityAndArtifacts`

**Pattern:** Do not replace these with direct getter calls. Retain not-found versus corruption/I/O distinctions and real consumer return semantics.

### `test/src/blockchain/CMakeLists.txt` — modify

**Role:** Register `consensus_finality_race_test`.

**Closest pattern:** Use the exact dependency set of `consensus_finalization_test` and `consensus_burn_reservation_test`:

```cmake
addtest(consensus_finality_race_test
    consensus_finality_race_test.cpp
)
target_link_libraries(consensus_finality_race_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)
```

### `test/src/bridge_race/CMakeLists.txt` — modify only from measured evidence

**Role:** Retain isolated explicit timeout for `bridge_race_single_burn_test`.

**Pattern:** Keep the current long-running standalone target. Change timeout only after startup/body/teardown measurements demonstrate a justified bound; timeout inflation is not a correctness task.

### `.planning/phases/12-consensus-race-and-compatibility-verification/12-FULL-SUITE-REPORT.md` — create in final plan

**Role:** Account for the dynamically enumerated complete CTest suite and reviewed external-prerequisite skips.

**Pattern:** Record build identity, `ctest -N` configured count, exact full command, passed/failed/not-run/skipped totals, and one row per skip with its emitted prerequisite. Do not hard-code the pre-phase count of 83.

## Cross-File Data Flow

```text
ConsensusManager event emission
  -> BridgeRaceTestAccess obtains each manager
  -> BridgeRaceE2ETest collector snapshots events
  -> single-burn assertions group by slot / validator / proposal / winner

HandleCertificate / CRDT / local submission / recovery
  -> FinalizeSlot authoritative pair + local finalized state
  -> authority-established barrier
  -> competing ingress assertions
  -> reservation/process/application
  -> once-only terminal evidence
```

## Conventions and Landmines

- Use `apply_patch` for all edits and preserve unrelated dirty files.
- Use friend-only access classes; no public production test APIs.
- Invoke observer callbacks without locks and never with mutable references to internal state.
- Use predicate barriers and controlled clocks; no detached threads and no correctness sleeps.
- Count distinct signed targets, not network publications.
- Always close managers and release barriers through RAII.
- Run the full suite without regex exclusions only after focused targets pass.
- Treat GTest skip as accountable only when it names a missing prerequisite.
- Do not touch startup wiring/mock RPC scope or the user's logging-level changes.
