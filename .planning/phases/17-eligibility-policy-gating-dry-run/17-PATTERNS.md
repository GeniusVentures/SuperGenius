# Phase 17: Eligibility, Policy & Gating (Dry-Run) - Pattern Map

**Mapped:** 2026-10-16
**Files analyzed:** 11 (5 modified source pairs, 1 created source pair, 3 test targets, 2 build/doc wiring)
**Analogs found:** 11 / 11 (1 composite — the evaluator is a new role assembled from three existing precedents)

All file:line citations below were re-read in source this session against the current tree (branch `gsd/phase-16-erasure-primitives-bookkeeping-hardening`).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/account/UTXOManager.{hpp,cpp}` (mod) | service (UTXO lifecycle owner) | CRUD + raw-batch persist | `ConsumeUTXOs` / `RestoreConsumedUTXOs` / `StoreUTXOSnapshot` in the same file | exact (in-place extension) |
| `src/transaction/TransactionManager.{hpp,cpp}` (mod) | service (nonce-chain + tx-path owner) | request-response (GlobalDB reads) | `GetRegisteredOutgoingPreviousHash` itself + `CertificateMatchesTransaction` + replay handler cert fallback | exact (in-place relaxation) |
| `src/crdt/crdt_datastore.hpp` + `src/crdt/impl/crdt_datastore.cpp` (mod) | facade/service | read passthrough + delta-parse transform | `CrdtDatastore::GetHeadList` (`impl/crdt_datastore.cpp:2255-2258`); `GetLocalDeltaKeyValues` (`:2291-2312`) | exact |
| `src/crdt/globaldb/globaldb.{hpp,cpp}` (mod) | facade | read passthrough | `GlobalDB::GetCRDTHeadList` (`globaldb.cpp:892-900`); `GetLocalDeltaKeyValues` (`:993-1003`) | exact |
| `src/account/GeniusNode.{hpp,cpp}` (mod) | config + composition-root wiring | file-I/O parse + event-driven timer | `LoadSgnsConfig` node_type/quorum blocks (`GeniusNode.cpp:438-457, :514-519`); `gc_timer_` cycle (`:4700-4735`) | exact (config) / role-match (timer) |
| `src/transaction/PruneEligibility.{hpp,cpp}` (NEW, name planner's) | service (policy evaluator + marker contract) | batch scan → structured result (read-only) | composite: `PublicChainInputValidator` structured-evidence shape + `QueryTransactions` enumeration loop + `GlobalDB::Error` reason-enum | partial (new role — see No Analog) |
| `src/account/proto/SGTransaction.proto` (mod, if marker record is proto) | model (wire/persist schema) | record serialization | `message UTXOEntryRecord` (`SGTransaction.proto:54-64`) | exact |
| `test/src/account/prune_eligibility_test.cpp` (NEW, name planner's) | test (integration) | fixture-driven exact-set assertions | `CertificateFallbackTest : public ::test::CRDTFixture` (`transaction_manager_certificate_fallback_test.cpp:348-534`) | exact |
| `test/src/account/utxo_manager_test.cpp` (mod — B unit tests) | test (unit) | fresh temp DB + fault seams | `UTXOManagerTest : public test::RocksDBFixture` + `UTXOManagerTestAccess` (`utxo_manager_test.cpp:24-75`) | exact |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` (mod — GATE-04 tests, or new file in same target) | test (integration) | cert fixture + Phase-16 erase simulation | same file's fixture/helpers (`:421-533`) + `GlobalDBEraseTest` (`test/src/crdt/globaldb_erase_test.cpp:73-161`) | exact |
| `test/src/account/CMakeLists.txt` (mod) + `src/transaction/CMakeLists.txt` (mod) + `.planning/REQUIREMENTS.md` (mod) | build/config + docs | — | `addtest(utxo_manager_test …)` block (`CMakeLists.txt:13-33`); `add_library(genius_transaction_objs …)` (`src/transaction/CMakeLists.txt:10-12`) | exact |

---

## Pattern Assignments

### `src/account/UTXOManager.{hpp,cpp}` (service, CRUD + raw-batch persist)

**Analog:** the same file — this is an in-place extension of the owning module (hard invariant 1: `UTXOManager` is the sole owner of the outpoint lifecycle and `/utxo/` records).

**Candidate B populate site — genuine path** (`UTXOManager.cpp:280-291`, inside `ConsumeUTXOs` :265-365):
```cpp
if ( auto canonical_it = utxo_outpoints_.find( outpoint ); canonical_it != utxo_outpoints_.end() )
{
    auto &entry              = canonical_it->second;
    stored_owner             = entry.utxo.GetOwnerAddress();
    const bool owner_matches = entry.type == UTXOType::UTXO_BRIDGE || stored_owner == address;
    if ( ( entry.state == UTXOState::UTXO_READY || entry.state == UTXOState::UTXO_RESERVED ) &&
         owner_matches && entry.type == type )
    {
        utxo_found  = true;
        entry.state = UTXOState::UTXO_CONSUMED;
        // NEW (D-02/B): entry.spent_by_txid = consuming_txid;
    }
}
```

**Synthesized-tombstone path — populate uniformly (research recommendation)** (`:302-311`):
```cpp
if ( !utxo_found )
{
    GeniusUTXO consumed_utxo( input_info.txid_hash_, input_info.output_idx_, 0, TokenID(), address );
    utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_CONSUMED,
                                           consumed_utxo,
                                           0,
                                           std::nullopt,   // spent_epoch stays dead (D-02 forbids wall-clock)
                                           std::nullopt,   // → carries consuming_txid if uniform population chosen
                                           type };
}
```

**Existing symmetric reset — DO NOT duplicate, this is already correct** (`RestoreConsumedUTXOs`, `:384-391`):
```cpp
auto          &entry = utxo_outpoints_.at( outpoint );
entry.state          = UTXOState::UTXO_READY;
entry.spent_epoch.reset();
entry.spent_by_txid.reset();
```

**Signature-extension pattern — overload forwarding to default-owner address** (`UTXOManager.hpp:194-219`): the 3-arg `ConsumeUTXOs(infos, address, type)` plus the header-inline `ConsumeUTXOs(infos, type)` forwarding to `address_`. Extend with the spender-hash parameter (research shape: `const std::optional<base::Hash256> &consuming_txid`) keeping every existing caller compiling; mirror the forwarding overload.

**Persistence round-trip already carries the fields — zero persistence work** (`StoreUTXOSnapshot`, `:1038-1043` serialize; `LoadUTXOs`, `:918-930` restore via `base::Hash256::fromSpan`). The `UTXOEntry` struct field: `UTXOManager.hpp:85` — `std::optional<base::Hash256> spent_by_txid;`.

**Call sites to update (spender in scope at all four — verified):**
- `TransactionManager.cpp:2637` — `utxo_mgr.ConsumeUTXOs( inputs, TransferInputOwner( *transfer_tx ) )` (tx object in scope)
- `TransactionManager.cpp:2657` — `account_m->GetUTXOManager().ConsumeUTXOs( params.first, migration_tx->GetSrcAddress() )`
- `TransactionManager.cpp:2702-2704` — `ConsumeUTXOs( inputs, mint_tx_v2->GetSrcAddress(), sgns::UTXOManager::UTXOType::UTXO_BRIDGE )`
- `TransactionManager.cpp:2871` — `ConsumeUTXOs( params.first, escrow_tx->GetSrcAddress() )`

**Error/rollback pattern to preserve** (`:345-362`): on store failure, memory maps are restored and already-stored owner buckets re-stored — B's populate must survive this path unchanged (it lives in `utxo_outpoints_`, which the rollback restores wholesale).

---

### `src/transaction/TransactionManager.{hpp,cpp}` (service, request-response)

**Analog:** `GetRegisteredOutgoingPreviousHash` itself (`TransactionManager.cpp:1688-1726`) — D-01 is a one-line relaxation in place, NOT a new function (research "Alternatives Considered": a parallel resolver would duplicate binding logic).

**The rung to relax — KEEP every binding check, drop only the type restriction** (`:1709-1725`):
```cpp
auto nonce_subject = ConsensusManager::DecodeNonceSubject(
    certificate_result.value().proposal().subject() );
if ( nonce_subject.has_error() ||
     nonce_subject.value().transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET ||
     nonce_subject.value().nonce() != ( nonce - 1 ) ||
     certificate_result.value().proposal().subject().account_id() != account_m->GetAddress() )
{
    return "";
}

auto embedded = DeSerializeEmbeddedTransaction( nonce_subject.value().transaction() );
if ( embedded.has_error() || !embedded.value() || embedded.value()->GetType() != "registration" )  // ← relax THIS
{
    return "";
}
return embedded.value()->GetHash();
```

**Ladder order preserved** (`GetOutgoingPreviousHash`, `:1660-1686`): tracked → persisted → cert-slot → CRDT scan. Rungs 1-2 answer first, so behavior with intact history is unchanged.

**Binding-discipline authority to keep identical** (`CertificateMatchesTransaction`, `:1790-1816`) — and the second consumer that ALREADY cert-resolves (proof the generalized rung returns the same hash; `TransactionConsensusHandler.cpp:666-688`):
```cpp
const std::string prev_slot = tx.GetSrcAddress() + ":" + std::to_string( tx.GetNonce() - 1 );
auto              prev_cert = owner_.blockchain_->GetCertificateBySlot( prev_slot );
if ( prev_cert.has_value() )
{
    auto prev_subject = ConsensusManager::DecodeNonceSubject( prev_cert.value().proposal().subject() );
    if ( !prev_subject.has_error() &&
         prev_subject.value().tx_hash() == previous_hash &&
         prev_cert.value().proposal().subject().account_id() == tx.GetSrcAddress() )
    { /* embedded deserialization + hash equality, then accept */ }
}
```

**Enumeration/prefix patterns the evaluator reuses (read-only):**
- `GetMonitoredNetworkIDs()` (`:2224-2233`) and `GetBlockChainBase(net)` (`:2235-2242`, `TRANSACTION_BASE_FORMAT = "/bc-%hu/"` at `hpp:433`)
- `QueryTransactions` loop shape (`:2473-2503`) — copy the loop, NOT its prefix: it scans `base + "tx"` **slashless** (`:2477`); the evaluator MUST use `base + "tx/"` (Phase-16 trailing-slash lesson; matches `…/txfoo` otherwise)
- Phase-16 landed contracts the evaluator/tests call: `GetTransactionCID` (`hpp:892`), `ResolveTransactionPathAndCID` (`hpp:864`), `EraseTransactionEntry` (`hpp:938`), `RemoveProcessedTransactionEntry` (`hpp:912`), `StoreTransactionCID` (`hpp:849`)

**NodeType plumbing precedent:** `TransactionManager::New` takes `NodeType node_type = NodeType::Light` (`hpp:155`) and stores `node_type_m` (`hpp:669`) — the evaluator constructor mirrors this.

---

### `src/crdt/crdt_datastore.hpp` + `src/crdt/impl/crdt_datastore.cpp` (facade, read passthrough + delta-parse)

**Analog A — member passthrough** (`impl/crdt_datastore.cpp:2255-2258`; the private member is `std::shared_ptr<CrdtSet> set_` at `crdt_datastore.hpp:496`):
```cpp
outcome::result<CrdtHeads::CRDTListResult> CrdtDatastore::GetHeadList()
{
    return heads_->GetList();
}
```
New: `CrdtDatastore::GetPriority(key)` → `return set_->GetPriority( aKey );` (target: `crdt_set.hpp:208`, `outcome::result<uint64_t> GetPriority( const std::string &aKey ) const`).

**Analog B — block-parse gate source / the sibling to extend** (`impl/crdt_datastore.cpp:2291-2303`):
```cpp
outcome::result<std::vector<std::pair<std::string, base::Buffer>>> CrdtDatastore::GetLocalDeltaKeyValues(
    const std::string &cid_string )
{
    BOOST_OUTCOME_TRY( auto cid, CID::fromString( cid_string ) );
    BOOST_OUTCOME_TRY( auto node, dagSyncer_->GetNodeWithoutRequest( cid ) );
    //TODO - Check if filtering is needed here. Currently not filtering.
    BOOST_OUTCOME_TRY( auto filtered, GetDeltaFromNode( *node, true ) );
    const auto &delta = filtered.delta;
    //TODO - Maybe check tombstones, right now just grabbing elements.
    std::vector elements( delta.elements().begin(), delta.elements().end() );
```
The TODOs are documented gaps, not contracts — PRUNE-03 needs elements AND tombstones. Research recommends a SIBLING (e.g. `GetLocalDeltaKeys`) returning keys-only over both lists, leaving this function and its callers untouched.

**Fixture helper on the same file** — `AddHead(cid, topic, priority)` (`:2270-2273`) drives maxHeight deterministically in window tests.

---

### `src/crdt/globaldb/globaldb.{hpp,cpp}` (facade, read passthrough)

**Analog — shutdown-safe GlobalDB passthrough with null guard** (`globaldb.cpp:892-900`; error-logging variant at `:993-1003`):
```cpp
outcome::result<GlobalDB::CRDTHeadListResult> GlobalDB::GetCRDTHeadList()
{
    auto crdt_datastore = ActiveCRDTDataStore();
    if ( !crdt_datastore )
    {
        return outcome::failure( std::errc::operation_canceled );
    }
    return crdt_datastore->GetHeadList();
}
```
New: `GlobalDB::GetKeyPriority(key)` (or planner's name) — same 7-line shape, delegating to the new `CrdtDatastore::GetPriority`. Declaration goes in the public query block beside `QueryKeyValues` (`globaldb.hpp:148`) / `GetCRDTHeadList` (`:327`) / `GetLocalDeltaKeyValues` (`:349`).

**Marker vehicle (D-04) — node-local raw surface, already public:** `GetRaw` / `PutRaw` / `QueryRaw` (`globaldb.hpp:181-200`), doc-commented "Reads/Writes a node-local key straight from RocksDB, bypassing the CRDT layer".

---

### `src/account/GeniusNode.{hpp,cpp}` (config + composition-root wiring)

**Config-read convention** (`LoadSgnsConfig`, `GeniusNode.cpp:438-457` and `:514-519` — rapidjson `HasMember` + type-check + resolved-value INFO + safe default):
```cpp
if ( config_json.HasMember( "node_type" ) && config_json["node_type"].IsString() )
{
    const auto parsed = NodeTypeFromString( config_json["node_type"].GetString() );
    if ( parsed )
    {
        node_type_ = *parsed;
        node_logger_->info( "sgns_config.json: node_type={}", node_type_ );
    }
    else { node_type_ = NodeType::Light; node_logger_->warn( "... unrecognized, defaulting to Light", ... ); }
}
else { node_type_ = NodeType::Light; node_logger_->info( "... not set, defaulting to Light" ); }
```
New `pruning` object read follows the uint64 variant at `:514-519` (`trusted_peer_quorum_threshold`). Fail-closed add-on per GATE-06: `enabled` without `finality_window` ⇒ disable + WARN. Absent `pruning` member ⇒ NO construction, NO timer, NO log cycle (SC-5, Pitfall 8: NodeType-default-Light is a role gate, never consent).

**Periodic-cycle precedent for the production dry-run trigger** (`gc_timer_`, `GeniusNode.cpp:4700-4735`; member `GeniusNode.hpp:1166` `std::unique_ptr<boost::asio::steady_timer> gc_timer_`):
```cpp
gc_timer_                          = std::make_unique<boost::asio::steady_timer>( *io_ );
std::weak_ptr<GeniusNode> weakSelf = shared_from_this();
auto schedule = [this, weakSelf, intervalHours]()
{
    gc_timer_->expires_from_now( std::chrono::hours( intervalHours ) );
    gc_timer_->async_wait( [weakSelf]( const boost::system::error_code &ec ) {
        if ( ec ) { return; }
        if ( auto self = weakSelf.lock() ) { self->RunResultGC(); }
    } );
};
```
Cancellation discipline at shutdown: `GeniusNode.cpp:2429-2433, :2513-2516` (`ec`-ignoring `cancel()` then `reset()`). The evaluator's per-cycle INFO summary is emitted from this cycle's handler (D-03/D-06), never from a CRDT callback.

**Wiring point** — beside `TransactionManager::New` at `GeniusNode.cpp:1121-1130` (config-gated: default-off ⇒ the branch never runs).

---

### `src/transaction/PruneEligibility.{hpp,cpp}` (NEW — service, batch scan → structured result)

**No single analog — composite of three (see No Analog Found).** The pieces to copy:

**Enumeration loop** — `TransactionManager::QueryTransactions` (`TransactionManager.cpp:2473-2503`): per-network `QueryKeyValues` + `KeyToString` + per-entry processing, `continue`-on-error (never abort the scan):
```cpp
for ( auto network_id : GetMonitoredNetworkIDs() )
{
    const std::string query_path = GetBlockChainBase( network_id ) + "tx";  // ← evaluator uses "tx/" (trailing slash!)
    auto transaction_list = globaldb_m->QueryKeyValues( query_path );
    if ( transaction_list.has_error() ) { m_logger->error( "Unable to query transactions on {}", query_path ); continue; }
    for ( const auto &[key, value] : transaction_list.value() )
    {
        auto transaction_key = globaldb_m->KeyToString( key );
        if ( !transaction_key.has_value() ) { m_logger->error( "Unable to convert a key to string" ); continue; }
        ...
    }
}
```

**Structured-result + fail-closed shape** — `PublicChainInputValidator.hpp` (`src/account/PublicChainInputValidator.hpp:51-69`): a plain struct of named evidence fields ("recorded only after it passes *every* required check"), doc-commented per field, with a TestAccess friend. The evaluator's `PruneCandidateResult` (research shape: `{tx_path, cid, eligible, reasons}` with `enum class PruneReason`) follows this discipline: reasons are recorded, never silently dropped.

**Reason-enum precedent** — `GlobalDB::Error` (`globaldb.hpp:84-94`): scoped `enum class Error : uint8_t` with per-value doc comments, declared via `OUTCOME_HPP_DECLARE_ERROR_2` when carried in results.

**Output enumeration per candidate** — `GeniusTransaction::GetProducedUTXOs` (`GeniusTransaction.cpp:118-144`): returns `std::nullopt` for tx types without UTXO parameters — `nullopt` ⇒ INELIGIBLE with explicit reason (Pitfall 6, fail closed).

**Marker contract (D-04) — node-local prefix discipline** — `CRDTWorkJournal` (`src/crdt/globaldb/crdt_work_journal.hpp:117, :136`):
```cpp
static constexpr std::string_view NAMESPACE_PREFIX = "/crdt/work/"; ///< Storage key namespace.
std::string BuildStorageKey( const std::string &key ) const;  // namespaced storage key
```
New: `"/prune/tx/"` prefix, hash-keyed (`/prune/tx/<tx-hash>`), path travels in the value, written via `GlobalDB::PutRaw` (Phase 18 joins it to the erase WriteBatch). Record shape analog — `message UTXOEntryRecord` (`src/account/proto/SGTransaction.proto:54-64`): explicit `has_*` bool + payload field pairs for optional values.

**Gates at construction** — NodeType role gate mirrors `TransactionManager::New`'s `node_type` parameter (`hpp:155`); allowlist check is ONE function built from `GetBlockChainBase(net) + "tx/"` over `GetMonitoredNetworkIDs()` only (D-C7).

**Static test seam (only if the dry-run trigger needs one)** — Phase-16 precedent `GlobalDB::SetEraseInterruptHookForTest` (`globaldb.hpp:250-258`, static member `:413-415`): static, doc-commented "@note No production caller may invoke this method", cleared unconditionally in fixture teardown (`globaldb_erase_test.cpp:140-142`).

**Build wiring:** new `.cpp` joins `add_library(genius_transaction_objs OBJECT …)` (`src/transaction/CMakeLists.txt:10-12`).

---

### `test/src/account/prune_eligibility_test.cpp` (NEW — test, integration)

**Analog:** `CertificateFallbackTest : public ::test::CRDTFixture` (`transaction_manager_certificate_fallback_test.cpp:348-534`).

**Fixture skeleton** (`:348-385`) — real GlobalDB + gossip pubsub + temp dir, direct `TransactionManager::New( db_, io_, account_, blockchain_, NodeType::Light, … )` (no GeniusNode, no network sync), `LoadUTXOs( db_->GetDataStore() )` so ParseTransaction can store UTXOs.

**Teardown discipline — copy verbatim, it is load-bearing** (`:387-415`): `blockchain_->Stop()` FIRST (joins the ConsensusManager round timer), then `tm_.reset()` → `blockchain_.reset()` → `account_.reset()`, base `~CRDTFixture` stays the final host release. The comment documents the Linux CI segfault this prevents.

**Helper family to reuse** (`:421-533`): `PersistTransaction` (Put at `TransactionManager::GetTransactionPath`), `BuildSignedCertificate` (genesis registry → signing manager → `CreateNonceSubject` → proposal → vote → certificate), `PersistCertificateAtSlot` (Put at `"/cert/" + slot`), `FetchAndProcess`. SC-3 extends these with Phase-16 `EraseTransactionEntry` to simulate the pruned state.

**Second fixture analog for the block-gate / multi-node cases:** `GlobalDBEraseTest::EraseNodeCollection` (`test/src/crdt/globaldb_erase_test.cpp:73-161`) — per-test temp dir naming (`dbName + "_" + testName`), full GlobalDB::New stack with scheduler-on-pubsub-context, `db->Start()`, destructor `ShutdownNow()` → `pubsub->Stop()` and the always-clear-interrupt-seam rule.

**CRDTFixture base** (`test/testutil/storage/base_crdt_test.hpp:27-44`): provides `io_`, `pubs_`, `db_`, `db_path_`.

---

### `test/src/account/utxo_manager_test.cpp` (mod — Candidate B unit tests)

**Analog:** `UTOManagerTest : public test::RocksDBFixture` (`utxo_manager_test.cpp:54-75`) — fresh temp DB per test, `LoadUTXOs( db_ )`, plus the `UTXOManagerTestAccess` seam-forwarding class (`:24-36`) over `SetConsumeUTXOsBeforeStoreHookForTest` / `SetPutUTXOBeforeStoreHookForTest` (`UTXOManager.cpp:979-983`). B's tests assert: populate at `ConsumeUTXOs`, round-trip through bucket rewrite + `LoadUTXOs` restart, reset at `RestoreConsumedUTXOs`.

---

### `test/src/account/CMakeLists.txt` + `.planning/REQUIREMENTS.md` (build + docs)

**addtest registration pattern** (`CMakeLists.txt:13-33`; same shape at `:230-251` for the cert-fallback target):
```cmake
addtest(utxo_manager_test
    utxo_manager_test.cpp
)

target_link_libraries(utxo_manager_test
    genius_node_test
    json_secure_storage
    base_rocksdb_test
)

if(MSVC)
    target_link_options(utxo_manager_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(utxo_manager_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(utxo_manager_test PUBLIC
        "-Wl,--whole-archive" "$<TARGET_FILE:genius_node_test>" "-Wl,--no-whole-archive")
endif()
```
New `prune_eligibility_test` target must copy the `transaction_manager_certificate_fallback_test` block (`:230-251`) — its link set already covers the fixture family's needs.

**REQUIREMENTS.md:** narrow amendment of the out-of-scope row "Populating `spent_epoch`/`spent_by_txid`…" per D-02's conditional (A failed verification; B adopted): `spent_by_txid` becomes the spend-recency carrier; `spent_epoch` stays dead.

---

## Shared Patterns

### Error contract: `outcome::result<T>` everywhere
**Source:** every analog above (`ConsumeUTXOs`, `GetCertificateBySlot` path, `StoreUTXOSnapshot`, passthroughs). Fallible reads return results; `BOOST_OUTCOME_TRY` for propagation; `has_error()` + `continue` in scan loops. No exceptions, no swallowed failures. Scoped `enum class` error/reason codes with doc comments (`globaldb.hpp:84-94` shape).

### Prefix-scan enumeration (with the trailing-slash correction)
**Source:** `TransactionManager::QueryTransactions` (`TransactionManager.cpp:2473-2503`).
**Apply to:** evaluator candidate enumeration. Copy the per-network loop + `KeyToString` + continue-on-error; use `GetBlockChainBase(net) + "tx/"` (WITH slash) — the analog's own `:2477` slashless form is the documented anti-pattern to not copy.

### Config: default-off, type-checked, INFO-logged
**Source:** `GeniusNode::LoadSgnsConfig` (`GeniusNode.cpp:411-419, :438-457, :514-519`).
**Apply to:** the `pruning` object (enabled / finality_window / dry_run). Absent key ⇒ today's behavior byte-identical; enabled-without-window ⇒ disabled + WARN; resolved values INFO-logged once.

### Fail-closed posture
**Sources:** `IsOutPointGenuinelyConsumed` (`UTXOManager.cpp:777-783` — CONSUMED ∧ amount ≠ 0, `shared_lock` per call); `GetRegisteredOutgoingPreviousHash` returning `""` at every failed binding check; `EraseEntry` guard rejections (`globaldb.hpp:203-248`).
**Apply to:** evaluator construction (refuse unless `NodeType::Light`), per-candidate decisions (unparseable ⇒ ineligible-with-reason), block gate (parse failure ⇒ not erasable), cert rung (never a guessed hash — `""`).

### Locking discipline
**Sources:** `ConsumeUTXOs` takes `std::unique_lock( utxos_mutex_ )` with full-map rollback snapshots (`UTXOManager.cpp:270-273, :345-362`); `IsOutPointGenuinelyConsumed` takes `std::shared_lock` (`:779`); `GetTrackedOutgoingPreviousHash` takes `std::shared_lock( tx_mutex_m )` (`TransactionManager.cpp:1738`).
**Apply to:** any evaluator-adjacent state read; the evaluator itself reads committed state only (D-C6) so it takes shared locks or none (GlobalDB APIs lock internally).

### Logging
**Sources:** `m_logger->trace/debug/error` in TransactionManager scan loops; `node_logger_->info/warn` for resolved config; `logger_->error` + error return in `StoreUTXOSnapshot`.
**Apply to:** per-cycle INFO summary (dry-run production mode) derived from the structured result; WARN on config fail-closed; ERROR + reason on scan failures.

### Test fixtures: fresh temp DB + three-part assertions + teardown order
**Sources:** `CertificateFallbackTest` (`transaction_manager_certificate_fallback_test.cpp:348-534`), `GlobalDBEraseTest` (`globaldb_erase_test.cpp:73-161`), `UTXOManagerTest` (`utxo_manager_test.cpp:54-75`).
**Apply to:** all new suites. Seed through the real publish path or `PersistTransaction`/`PersistCertificateAtSlot` (never raw local seeds — Pitfall 11); drive the window via `AddHead` priority; include the negative control proving the exact-set assertion can fail.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `src/transaction/PruneEligibility.{hpp,cpp}` — the pure structured-result evaluator | service (policy) | batch scan → structured result | No existing policy evaluator over GlobalDB enumeration exists in the repo. Assemble from: `QueryTransactions` loop (enumeration), `PublicChainInputValidator` (structured-evidence struct discipline), `GlobalDB::Error` (reason enum). The RESEARCH.md Patterns 1-4 and Code Examples supply the design; treat those as the primary spec, the analogs as style. |

Everything else has an exact in-file or in-repo analog.

## Metadata

**Analog search scope:** `src/transaction/`, `src/account/` (incl. `proto/`, `NodeType.hpp`), `src/crdt/` (`globaldb/`, `impl/`, `crdt_set.hpp`), `test/src/account/`, `test/src/crdt/`, `test/testutil/storage/`, `cmake` via `test/src/*/CMakeLists.txt` and `src/transaction/CMakeLists.txt`
**Files scanned:** 18 read in targeted ranges; grep sweeps for `/utxo` writers, `spent_by_txid` writers, `steady_timer` precedents, erase-test fixtures
**Pattern extraction date:** 2026-10-16
