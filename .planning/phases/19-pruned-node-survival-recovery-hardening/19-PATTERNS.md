# Phase 19: Pruned-Node Survival & Recovery Hardening - Pattern Map

**Mapped:** 2026-10-23
**Files analyzed:** 12 (5 production files modified, 6 test files modified/created, 1 verified-no-change)
**Analogs found:** 12 / 12 (every change site has an in-file or same-family analog; one harness leg — real-boot READY wait — has no test-side precedent)

All line numbers verified against develop @ e0baa66bf this session. Cited ranges are non-overlapping read locations; expect a few lines of drift as edits land (RESEARCH A6).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/account/UTXOManager.hpp` + `.cpp` (D-01 preserve-clear API) | model (UTXO lifecycle owner) | CRUD + persistence (batch prefix-replace) | `UTXOManager::SetUTXOs` (UTXOManager.cpp:487-533) — the API being varied | exact |
| `src/transaction/TransactionManager.hpp` + `.cpp` — D-01 rebuild guard | service (boot assembly) | batch replay / transform | `InitializeUTXOs` mismatch branch itself (TransactionManager.cpp:3261-3280) + 18-landed bounded recovery (:3329-3334) | exact |
| `src/transaction/TransactionManager.hpp` + `.cpp` — own-replay recognition | service (tx parsing rule) | transform | `ParseTransferTransaction` double-spend check (TransactionManager.cpp:2634-2660) — the rule being extended | exact |
| `src/transaction/TransactionManager.hpp` + `.cpp` — D-02 typed error + READY-preserving site | service (send state machine) | request-response (tick-driven) | `TickOnce` error site (:566-593) + `Error` enum (hpp:85-88) | exact |
| `src/transaction/TransactionManager.hpp` + `.cpp` — D-02 bounded SYNCING exit | service (state machine) | request-response | `SyncNonce` (:3487-3550) + ForTest seam precedent (:3223-3233, hpp:777-780) | exact |
| `src/transaction/TransactionManager.hpp` + `.cpp` — D-03 tri-state lookup | service (tx lookup) | query / request-response | `FetchExactTransactionFromCRDT` (:3695-3731) + `PruneEligibility::PrunedLookup` (PruneEligibility.hpp:115-127) | exact |
| `src/account/GeniusNode.hpp` + `.cpp` (D-03 passthrough) | facade (presentation boundary) | request-response | `GeniusNode::GetTransactionStatus` (GeniusNode.hpp:962-967) | exact |
| `src/crdt/impl/crdt_datastore.cpp` (RetryDueFailedRoots guard) | service (retry scheduler) | event-driven (retry loop) | the 18-01 isResolved broadcast guard (crdt_datastore.cpp:720-741, same file) | exact |
| `example/node_test/NodeExample.cpp` (hash-arg query command) | CLI boundary (presentation) | request-response | `cmd_ds` (:395-402) + `COMMANDS` table (:513-529) | role-match (no hash-arg query exists; arg-parsing analog is `check_arg_count`/`args[1]` at :392) |
| `test/src/account/utxo_manager_test.cpp` (preserve-clear unit tests) | test (unit) | CRUD | `UTXOManagerTest` RocksDBFixture + access wrapper (utxo_manager_test.cpp:24-55) | exact |
| `test/src/account/tx_pruner_test.cpp` or new file in same target (D-01 suites + SC-1 restart matrix) | test (integration) | batch replay / restart | `TxPrunerTest` fixture (:122-156) + `ReopenStackOnSamePath` (:451-503) | exact (real-boot READY leg: no analog — see below) |
| `test/src/account/transaction_manager_pending_lifecycle_test.cpp` (D-02 suites) | test (integration unit) | request-response | `TransactionManagerPendingLifecycleTestAccess` seam owner (:34-75) | exact |
| `test/src/crdt/globaldb_erase_test.cpp` (D-04 3-node mesh + SURV-02 refusal/failover) | test (multi-node E2E) | event-driven pub-sub | `EraseNodeCollection` (:81-238) + `GlobalDBEraseRedeliveryTest::seedPrunedRoot` (:527-609) | exact |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` (SC-5 cert-consumer case) | test (integration) | event-driven | cert-fallback family; target under test `TransactionConsensusHandler::OnConsensusCertificate` (TransactionConsensusHandler.cpp:61-109) | exact |

**Verified NOT modified:** `src/transaction/TransactionConsensusHandler.cpp` — the CONTEXT's "?" resolves to no-change (RESEARCH audit: cert-embedded-subject fallback is already pruned-safe; test target only). Also untouched: `src/transaction/PruneEligibility.hpp`, `src/transaction/TxPruner.*`, `src/account/AccountMessenger.cpp`.

## Pattern Assignments

### `src/account/UTXOManager.hpp` + `.cpp` — consumed-preserving clear (D-01 substrate)

**Analog:** `SetUTXOs` (UTXOManager.cpp:487-533) — the new API is this function with a survivor predicate; copy its lock/rollback/persist shape verbatim.

**Core pattern — the clear being varied** (:487-533):
```cpp
outcome::result<void> UTXOManager::SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address )
{
    std::unique_lock lock( utxos_mutex_ );
    const auto       previous_utxo_outpoints    = utxo_outpoints_;
    const auto       previous_address_outpoints = address_outpoints_;
    const auto       previous_reservations      = local_reservations_;

    if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
    {
        for ( const auto &outpoint : address_it->second )
        {
            local_reservations_.erase( outpoint );
            utxo_outpoints_.erase( outpoint );       // <-- wipes CONSUMED entries too: the hazard
        }
        address_it->second.clear();
    }
    // ...rebuild entries as UTXO_READY, then:
    if ( auto store_result = StoreUTXOsLocked( address ); store_result.has_error() )
    {
        utxo_outpoints_     = previous_utxo_outpoints;   // rollback-on-store-failure shape to keep
        address_outpoints_  = previous_address_outpoints;
        local_reservations_ = previous_reservations;
        return outcome::failure( store_result.error() );
    }
    return outcome::success();
}
```
The preserve-clear keeps the same `unique_lock` + snapshot-rollback + `StoreUTXOsLocked` shape; the only change is erasing only non-CONSUMED entries and keeping reservations dropped. Persistence is free: `StoreUTXOsLocked` → `StoreUTXOSnapshot` rewrites the `/utxo/<address>/` prefix with whatever survives (the round-trip writes `spent_by_txid` at :1139-1144 — the field a TM-side save/restore would lose).

**Persistence pattern** — `StoreUTXOSnapshot` (:1086-1172): query existing prefix → `db->batch()` → remove old keys → serialize `SGTransaction::UTXOEntryRecord` per entry (state, epochs, `has_spent_by_txid`/`spent_by_txid` at :1139-1144) → `batch->commit()`. Do not add a second writer shape; the new clear persists ONLY through `StoreUTXOsLocked` (:1180-1184).

**Consumed-evidence accessors to reuse verbatim (do not shadow)** (:854-884):
```cpp
bool UTXOManager::IsOutPointGenuinelyConsumed( const base::Hash256 &utxo_id, uint32_t output_idx ) const
{
    std::shared_lock lock( utxos_mutex_ );
    auto             it = utxo_outpoints_.find( OutPoint{ utxo_id, output_idx } );
    return it != utxo_outpoints_.end() && it->second.state == UTXOState::UTXO_CONSUMED &&
           it->second.utxo.GetAmount() != 0;   // synthetic tombstones are amount-0 (Pitfall 3)
}

std::optional<base::Hash256> UTXOManager::GetOutPointSpender( const base::Hash256 &utxo_id,
                                                              uint32_t             output_idx ) const
```

**Resurrection blocker (already exists — no new mechanism)** — `PutUTXO` duplicate check (:166-172):
```cpp
std::unique_lock lock( utxos_mutex_ );
if ( auto existing = utxo_outpoints_.find( outpoint ); existing != utxo_outpoints_.end() )
{
    return false;   // any-state existing entry refuses re-creation; load-bearing once CONSUMED survives
}
```

---

### `src/transaction/TransactionManager.cpp` — D-01 rebuild guard (marker consult + crosscheck)

**Analog:** `InitializeUTXOs` itself (TransactionManager.cpp:3235-3338); the 18-landed bounded-recovery insert is the exact shape the marker consult extends.

**The mismatch branch being modified** (:3261-3280):
```cpp
else if ( checkpoint_result.value().has_value() )
{
    const auto local_root = account_m->GetUTXOManager().ComputeUTXOMerkleRoot( account_m->GetAddress() );
    if ( local_root != checkpoint_result.value()->utxo_merkle_root )
    {
        m_logger->warn( "Local UTXO root mismatch with checkpoint during init. Clearing local UTXOs and rebuilding" );
        auto clear_result = account_m->GetUTXOManager().SetUTXOs( std::vector<GeniusUTXO>{},
                                                                  account_m->GetAddress() );  // <-- swap for preserve-clear
```

**The insertion site for the PRUNED consult** (:3329-3334):
```cpp
// Incomplete history for another account must not prevent this account from starting.
if ( !processed && address == account_m->GetAddress() )
{
    std::lock_guard missing_lock( missing_tx_mutex_ );
    missing_tx_hashes_.insert( tx_hash );    // <-- precede with LookupPrunedEntry==PRUNED => skip + INFO log
}
```
Guard style: take `missing_tx_mutex_` only for the insert (existing discipline); the skip path logs INFO naming the pruned ref and does not insert.

**Missing-set mutex/state shape** (hpp:762-767):
```cpp
mutable std::mutex              missing_tx_mutex_;
std::unordered_set<std::string> missing_tx_hashes_;
std::unordered_map<std::string, uint8_t> missing_tx_attempts_;   // GATE-05 bounded recovery
static std::atomic<uint8_t> missing_tx_max_attempts_for_test_;
```

---

### `src/transaction/TransactionManager.cpp` — own-replay recognition (Pitfall-1 fix)

**Analog + modification site in one:** the double-spend check (:2634-2660):
```cpp
auto &utxo_mgr = account_m->GetUTXOManager();
auto  inputs   = transfer_tx->GetInputInfos();
if ( std::any_of( inputs.begin(), inputs.end(),
                  [&utxo_mgr]( const InputUTXOInfo &input )
                  { return utxo_mgr.IsOutPointConsumed( input.txid_hash_, input.output_idx_ ); } ) )
{
    const auto outputs  = transfer_tx->GetProducedUTXOs();
    const bool replayed = outputs && !outputs->empty() &&
                   std::all_of( outputs->begin(), outputs->end(), /* every output outpoint has state */ );
    if ( !replayed )
    {
        m_logger->error( "Rejected double-spending transfer {}", transfer_tx->GetHash() );
        return std::errc::invalid_argument;    // <-- the reject the own-replay discriminator must bypass
    }
}
```
Extension shape: inside the consumed branch, before the reject, `GetOutPointSpender(input) == tx.GetHash()` for all consumed inputs ⇒ own replay ⇒ proceed (re-apply consume + produce). Note `transfer_txid` is already derived just below (:2667) via `base::Hash256::fromReadableString( transfer_tx->GetHash() )` — reuse that conversion idiom for the comparison.

---

### `src/transaction/TransactionManager.hpp` + `.cpp` — D-02 half 1: typed pre-broadcast error + READY-preserving site

**Error home to extend** (hpp:85-88):
```cpp
enum class Error : uint8_t
{
    TRUST_POLICY_NOT_READY = 1,
    // add PRE_BROADCAST_* here; extend the error-code category wiring beside it
};
```

**Preflight block whose returns become typed** (TransactionManager.cpp:2044-2085): the SIZE-01 loop returns `std::errc::message_size` (:2060) and `std::errc::invalid_argument` (:2071, :2082) BEFORE `crdt_transaction->Commit( topicSet )` (:2156). The identical codes fire post-Commit at :2175/:2187/:2205 — the ambiguity D-02 half 1 removes.

**The TickOnce site that classifies** (:566-593):
```cpp
auto send_result = SendTransactionItem( tx_queue_m.front() );
if ( send_result.has_error() )
{
    const auto err             = send_result.error();
    const bool retryable_error = ( err == ... timed_out ) || ( err == ... resource_unavailable_try_again );
    if ( retryable_error )
    {
        m_logger->info( "Send deferred/retryable ({}). Keeping transaction in queue", err.message() );
        break;
    }
    ChangeState( State::SYNCING );                     // <-- the wedge; pre-broadcast branch goes BEFORE this
    m_logger->error( "Error in SendTransactionItem: {}", err.message() );
    auto rollback_result = RollbackTransactions( tx_queue_m.front() );   // FAILED => ReleaseNonce at :5196
    if ( rollback_result.has_error() ) { ... break; }
    tx_queue_m.pop_front();
    break;
}
```
New branch shape: typed pre-broadcast error ⇒ WARN + `RollbackTransactions` + pop + `break` (STAY READY — no `ChangeState(SYNCING)`). The nonce release is already free: the FAILED transition calls `account_m->ReleaseNonce( tx->GetNonce() )` (:5196).

**Rollback helper as-is** (:2222-2230): `RollbackTransactions` flips every batch tx to `TransactionStatus::FAILED` — reuse unchanged.

---

### `src/transaction/TransactionManager.hpp` + `.cpp` — D-02 half 2: bounded SYNCING exit

**Analog A — the function to bound** (`SyncNonce`, :3487-3550): note the early `return` at :3497-3507 when neither network nor local confirmed nonce exists — the incident's infinite-spin hole. The bounded-exit check goes at the top, before `GetConfirmedNonce`.

**Analog B — the ForTest-seam precedent to copy exactly** (:3223-3233 + hpp:771-780):
```cpp
// hpp: static constexpr default + static atomic override + static accessors,
// with the "No production caller may invoke this" doc comment
static constexpr uint8_t k_init_tx_request_max_attempts{ 12 };
static void    SetMissingTxMaxAttemptsForTest( uint8_t max_attempts );
static uint8_t GetMissingTxMaxAttemptsForTest();
// cpp:
std::atomic<uint8_t> TransactionManager::missing_tx_max_attempts_for_test_{ k_init_tx_request_max_attempts };
```
`SetSyncingExitTimeoutForTest` mirrors this: `static constexpr` default window + `static std::atomic<...ms>` override + accessor pair.

**Collision-safety anchor (read-only dependency, no changes)** — `GeniusAccount` (GeniusAccount.cpp:913-941): `GetNextNonceLocked` skips `pending_nonces_` while walking; `ReserveNextNonce`/`ReleaseNonce` guard with `nonce_mutex_`. The bounded exit must call NONE of these mutation paths — anchor via `GetLocalConfirmedNonce()` only.

---

### `src/transaction/TransactionManager.hpp` + `.cpp` — D-03 tri-state lookup

**Composition sources (both read this session):**
- Tracked-map found-source — `GetTransactionByHash(NoLock)` (:3660-3678): `shared_lock<shared_mutex>( tx_mutex_m )` then linear scan of `tx_processed_m` by hash string.
- CRDT found-source — `FetchExactTransactionFromCRDT` (:3695-3731): iterate `GetMonitoredNetworkIDs()`, `FetchTransaction( *globaldb_m, GetTransactionPath( network_id, tx_hash ) )`; NOT_FOUND ⇒ continue to next network; hash+`CheckHash()` verified; hard errors returned via `outcome::failure`.

**Verdict vocabulary — reuse verbatim, do not re-enum** (PruneEligibility.hpp:115-127):
```cpp
enum class PrunedLookup : uint8_t
{
    PRUNED,                            // marker present AND value absent
    NOT_FOUND,                         // neither present
    VALUE_PRESENT_INVARIANT_VIOLATION, // both present: report loudly, never guess
};
static outcome::result<std::pair<PrunedLookup, std::optional<SGTransaction::PrunedTransactionRecord>>>
LookupPrunedEntry( crdt::GlobalDB &db, const std::string &tx_hash );   // :421-422
```
Ordering rule (Pitfall 8): probe the VALUE first (the two found-sources above), THEN the marker. Result struct shape per RESEARCH Pattern 5: `{ PrunedLookup verdict; shared_ptr<GeniusTransaction> tx; optional<PrunedTransactionRecord> record; }`.

---

### `src/account/GeniusNode.hpp` + `.cpp` — D-03 passthrough

**Analog:** `GetTransactionStatus` (GeniusNode.hpp:962-967):
```cpp
/**
 * @brief Returns a tracked transaction status by transaction hash.
 * @param[in] txId Transaction hash to look up.
 * @return Outgoing status when present, then incoming status, or INVALID when unknown/not ready.
 */
TransactionManager::TransactionStatus GetTransactionStatus( const std::string &txId ) const;
```
The new passthrough is the same one-line delegation shape returning the TM lookup result type; doc comment documents the tri-state. Presentation maps verdict→text; no domain logic here (invariant 3).

---

### `src/crdt/impl/crdt_datastore.cpp` — RetryDueFailedRoots isResolved guard

**Analog — the 18-01 insertion this mirrors, same file** (:720-741):
```cpp
// D-02 re-delivery guard: the block is absent (HasBlock miss above) but the
// never-erased seal marker may still record a root this node processed and
// later pruned. Such a root is terminal -- ... skip before any cache, pending,
// or enqueue consult.
auto resolved_result = dagSyncer_->isResolved( bCastHeadCID );
if ( resolved_result.has_failure() )
{
    logger_->error( "{}: error checking resolved state for CID {}", __func__, bCastHeadCID.toString().value() );
    continue;
}
if ( resolved_result.value() )
{
    logger_->trace( "{}: Pruned root {} re-delivered - terminal, skipping", __func__, ... );
    continue;
}
```
Copy this error/true/false three-way shape into the retry loop.

**Insertion point — the loop over due CIDs** (:1841-1850):
```cpp
for ( const auto &cid : due )
{
    // EnqueueRootCID flips a FAILED pending_jobs_ entry back to PENDING itself.
    (void) dagSyncer_->DeleteCIDBlock( cid );      // <-- guard goes BEFORE both this
    if ( EnqueueRootCID( cid ) )                    // <-- and this (counter ticks inside)
    {
        logger_->info( "RetryDueFailedRoots: retrying root CID {}", cid.toString().value() );
        dagWorkerCv_.notify_one();
    }
}
```
Pitfall 7: the check must precede `DeleteCIDBlock`/`EnqueueRootCID` so the fetch-attempt counter (SURV-01's observable) never moves for a pruned root. Retry bookkeeping context: `ScheduleFailedRootRetryLocked` (:1790-1808), `MAX_FAILED_ROOT_RETRIES = 8` (crdt_datastore.hpp:623).

---

### `example/node_test/NodeExample.cpp` — hash-arg query command

**Analog A — command shape** (`cmd_ds`, :395-402):
```cpp
static void cmd_ds( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 1, "ds" ) ) { return; }
    node->PrintDataStore();
}
```
The new `tx <hash>` command: same static function shape, `check_arg_count( args, 2, "tx" )`, take `args[1]` (idiom at :392 `args[1]`), call the GeniusNode passthrough, `logger->info` the verdict text ("history pruned" / not found / found).

**Analog B — registration** (`COMMANDS` map, :513-529): add one entry to the existing `std::map<std::string, CmdFunc>`; `cmd_help` body (from :531) lists commands — extend it.

---

### `test/src/account/utxo_manager_test.cpp` — preserve-clear unit tests

**Analog — fixture + access wrapper** (:24-55):
```cpp
namespace sgns
{
    class UTXOManagerTestAccess      // friend-seam wrapper: static methods forwarding to *ForTest APIs
    {
    public:
        static void SetConsumeUTXOsBeforeStoreHook( UTXOManager &manager, std::function<void()> hook );
        ...
    };
} // namespace sgns

class UTXOManagerTest : public test::RocksDBFixture   // real RocksDB, fresh per test
```
Constants/helpers idiom: `DUMMY_HASH`, `TOKEN_1`, `BalanceUTXOs()` factory (:40-52). New cases (`*KeepConsumed*` filter): assert CONSUMED entries + `spent_by_txid` survive in memory AND on disk (raw `/utxo/` read — the RocksDBFixture gives the handle), READY/RESERVED/reservations cleared, and `SetUTXOs` callers byte-identical (existing suites are the negative control).

---

### `test/src/account/tx_pruner_test.cpp` (or new 19 file in same target) — D-01 suites + SC-1 restart matrix

**Analog A — the stack fixture** (:122-156): `TxPrunerTest : public ::test::CRDTFixture( "tx_pruner_test" )` wiring `GeniusAccount::SetSecureStorageFactory(MemorySecureStorage)` → `GeniusAccount::New` → `LoadUTXOs( db_->GetDataStore() )` → `Blockchain::New` → `TransactionManager::New( ..., NodeType::Light, ... )`. Teardown clears interrupt seams BEFORE stores (destructor comment :160-162).

**Analog B — the restart primitive** (`ReopenStackOnSamePath`, :451-503): stop blockchain → reset tm/blockchain/account → `db_->ShutdownNow()` → rebuild GlobalDB on `db_path_` (scheduler + graphsync network + `GlobalDB::New`) → re-add topics → `db_->Start()` → rebuild account/blockchain/tm → `ForceManagerReady()`. The SC-1 real-boot leg replaces the final `ForceManagerReady()` with `tm_->StartCore()` (TransactionManager.hpp:167) + a bounded READY wait — the ONLY novel element (see No Analog Found).

**Analog C — deterministic state driving** (seam, :68-82): per-binary `TransactionManagerPendingLifecycleTestAccess` carrying only `ChangeState` — the D-C2 discipline: same class name owned per binary, minimal members.

**Pruned-state setup driver (all 19 tests):** `TransactionManager::EraseTransactionEntryPruned` (TransactionManager.cpp:4404-4410) — thin wrapper over `EraseTransactionEntryWithMarker( tx_hash, true )`; the marker pair joins the erase WriteBatch. Never hand-write markers.

---

### `test/src/account/transaction_manager_pending_lifecycle_test.cpp` — D-02 suites

**Analog — the seam's owner binary** (:34-75): `TransactionManagerPendingLifecycleTestAccess` with `ChangeState`, `Enqueue` (builds a `TransactionBatch`, `manager.EnqueueTransaction`), `TickOnce`, `RunInitializeUTXOs`, `RunInitTransactions`, `SetMissingTxMaxAttempts` (forwarding to the static seam), `ChangeTransactionState`. D-02 tests extend this class with the new seam forwarders (`SetSyncingExitTimeoutForTest`) and drive: preflight failure ⇒ assert state stays READY + nonce reusable; window elapsed ⇒ READY + WARN + pending reservation intact (next `ReserveNextNonce()` strictly greater — collision proof, Pitfall 4). File header carries the Doxygen block (@file/@brief/@details/@date) — keep that convention.

---

### `test/src/crdt/globaldb_erase_test.cpp` — D-04 3-node mesh + SURV-02 refusal/failover

**Analog A — the topology helper being extended** (`EraseNodeCollection`, :81-238): `addNode` (per-test temp dir under binary path + `removeAllWithRetry`), `makeNode` (KeyPairFileStorage → GossipPubSub advertising loopback `127.0.0.1` → scheduler on the pubsub io_context → graphsync Network → `GlobalDB::New`; optional `start`), `connectNodes` (ONE-direction dial + `ASSERT_WAIT_FOR_CONDITION` on connectedness, 10s), `restartNodePendingStart` (db-first teardown, recreate without Start). Destructor: clear interruption seam, `ShutdownNow` db, then `pubsub->Stop()` — preserve this exact order. All members are already N-node capable (`std::vector<EraseNode>`); D-04 adds a full-node role (more retained state / rebroadcast), not new wiring.

**Analog B — the suite** (`GlobalDBEraseRedeliveryTest : public GlobalDBEraseTest`, :527-609): `seedPrunedRoot` shows the working recipe — addNode x2, connect, broadcast/listen topic wiring, `ASSERT_WAIT_FOR_CONDITION` polls over raw seal-key/head-list predicates, `SeedEntryOn` via public `db.Put`, erase via `b_db.EraseEntry`. The 3-node mesh test follows the same skeleton with a third node and the full node's head (re)broadcast wired BEFORE the fetch window (Pitfall 6). Fetch-attempt observability: `Get/ResetRootFetchAttemptsForTest` (the 18-01 seam) for zero-fetch assertions.

---

### `test/src/account/transaction_manager_certificate_fallback_test.cpp` — SC-5 cert-consumer case

**Analog:** the existing cert-fallback family in the same file (fixture shares the CRDTFixture stack and `put_utxo_before_store_hook_for_test_` composition — see UTXOManager.cpp:184-195 comment naming this test binary). New `*PrunedSubject*` case: erase the subject tx via `EraseTransactionEntryPruned`, redeliver the certificate, assert approval through the cert-embedded fallback (`OnConsensusCertificate`, TransactionConsensusHandler.cpp:61-109) and zero re-fetch. Production code unchanged.

## Shared Patterns

### Evidence-first terminal skip (GATE-05 semantics)
**Source:** crdt_datastore.cpp:720-741 (the 18-01 isResolved guard)
**Apply to:** RetryDueFailedRoots guard; the PRUNED marker consult at the missing-set insert (TransactionManager.cpp:3329-3334); every skip logs (INFO/WARN) naming the pruned ref and touches no counters/queues. Three-way shape: lookup-error ⇒ log error + continue; resolved/pruned ⇒ log + skip; else ⇒ existing path.

### outcome::result discipline
**Source:** every cited analog (`PutUTXO`, `SetUTXOs`, `FetchExactTransactionFromCRDT`)
**Apply to:** all new fallible functions — `outcome::result<T>` returns; store-failure rollback (SetUTXOs :523-529 snapshot-restore shape); NOT_FOUND-as-continue vs hard-error-as-return distinction (FetchExactTransactionFromCRDT :3707-3713); never convert errors into success or silent empties.

### ForTest seam (static atomic + accessor pair + "no production caller" doc)
**Source:** TransactionManager.cpp:3223-3233 + hpp:766-780 (`missing_tx_max_attempts_for_test_`)
**Apply to:** `SetSyncingExitTimeoutForTest` (D-02 half 2); any request-attempt observable added for the marker-consult test. `static constexpr` default beside the override.

### Friend-class test access, one owner binary per definition
**Source:** tx_pruner_test.cpp:68-82 (minimal `ChangeState`-only copy) vs transaction_manager_pending_lifecycle_test.cpp:34-75 (full owner)
**Apply to:** any new 19 test file needing TM internals — define the class name locally in the new binary carrying only the members that binary uses.

### Lock discipline
**Source:** UTXOManager — `unique_lock` for mutation (:165, :491), `shared_lock` for reads (:868, :877); TransactionManager — `missing_tx_mutex_` scoped to the exact map touch (:3332), `tx_mutex_m` shared for lookups (:3662)
**Apply to:** the preserve-clear (unique, owner-vended); all crosscheck/spender reads go through the existing shared-locked accessors; no new raw map access from TM.

### Waiting in multi-node/integration tests
**Source:** globaldb_erase_test.cpp:152-156 (`ASSERT_WAIT_FOR_CONDITION( predicate, std::chrono::seconds(10), "message", nullptr )`)
**Apply to:** SURV-02/03 readiness, head-exchange, and failover polls; bounded windows with failure messages, never bare sleeps.

### Deterministic teardown order
**Source:** EraseNodeCollection destructor (globaldb_erase_test.cpp:213-229): clear interruption seams → `db->ShutdownNow()` → `db.reset()` → `pubsub->Stop()`; TxPrunerTest destructor (:158-162) same principle
**Apply to:** the 3-node fixture and the SC-1 matrix (ReopenStackOnSamePath already encodes it: :451-464).

## No Analog Found

| File / element | Role | Data Flow | Reason | Planner guidance |
|----------------|------|-----------|--------|------------------|
| SC-1 real-boot READY leg (`tm_->StartCore()` + bounded READY wait in tx_pruner_test family) | test (integration) | restart / request-response | No test in the tree calls `StartCore` (grep: comments only, tx_pruner_test.cpp:71, :1193 — the seam exists precisely because nobody drives the real boot) | `StartCore` is `TransactionManager::StartCore()` (hpp:167, cpp:464) — a TM method, NOT GeniusNode. Use RESEARCH Pitfall 5 options: prefer (b) pre-seeded local confirmed nonce (deterministic, peerless), fall back (a) two-node; marker-consult zero-fetch assertions don't need READY |
| NodeExample hash-arg query command | CLI boundary | request-response | Every existing query command is either argless (`ds`) or action-arg (`registerchild`); none prints a tri-state verdict | Copy `cmd_ds` shape + `check_arg_count(args, 2, ...)` + `args[1]` idiom (:392); verdict→text mapping is the only new logic |

## Metadata

**Analog search scope:** `src/account`, `src/transaction`, `src/crdt`, `example/node_test`, `test/src/account`, `test/src/crdt`
**Files read for excerpts:** 14 (UTXOManager.cpp, TransactionManager.cpp/.hpp, PruneEligibility.hpp, crdt_datastore.cpp, GeniusNode.hpp, GeniusAccount.cpp, NodeExample.cpp, tx_pruner_test.cpp, utxo_manager_test.cpp, transaction_manager_pending_lifecycle_test.cpp, globaldb_erase_test.cpp) + CMake target registrations verified (utxo_manager_test @ test/src/account/CMakeLists.txt:13, transaction_manager_pending_lifecycle_test @ :209, globaldb_integration_test @ test/src/crdt/CMakeLists.txt:50)
**Pattern extraction date:** 2026-10-23
**Key confirmations this session:** RetryDueFailedRoots lives in `src/crdt/impl/crdt_datastore.cpp` (:1816-1851) — NOT TransactionConsensusHandler; TransactionConsensusHandler.cpp needs no modification; the 18-01 isResolved guard is at crdt_datastore.cpp:720-741; `StartCore` is a TransactionManager method; `EraseTransactionEntryPruned` at TransactionManager.cpp:4404-4410.
