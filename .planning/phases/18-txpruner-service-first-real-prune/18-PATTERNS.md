# Phase 18: TxPruner Service & First Real Prune - Pattern Map

**Mapped:** 2026-10-21
**Files analyzed:** 15 (7 created, 8 modified)
**Analogs found:** 15 / 15 (13 strong, 2 partial — listed under No Analog Found)

All line numbers below were read in source this session (git HEAD `b163e0090`, branch `gsd/phase-17-eligibility-policy-gating-dry-run`). Symbol names are the stable reference if lines drift (RESEARCH A5).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/transaction/TxPruner.hpp/.cpp` (NEW) | service (cycle orchestrator) | timer-driven batch, per-tx atomic writes | `RunPruneDryRunCycle` in `src/account/GeniusNode.cpp:4913-5020` + `PruneEligibility::New` ctor shape (`PruneEligibility.hpp:283-287`) | exact (the arm being replaced) |
| `src/account/DevMintPolicy.hpp` (NEW, beside `sgns_version.hpp` net constants) | utility (policy predicate) | request-response (pure predicate) | `src/base/sgns_version.hpp:12-15` net constants + `sgns_version.cpp:56-59` | role-match |
| `src/account/DevMintInputValidator.hpp/.cpp` (NEW) | middleware/validator (consensus gate) | request-response | `test/testutil/TestMintInputValidator.hpp` (behavioral twin) + `GeniusInputValidator::Register` `src/account/GeniusInputValidator.cpp:113-120` (production registration) | exact |
| `src/transaction/PruneEligibility.hpp/.cpp` (MOD) — `PruningConfig` keys + `StagePrunedMarker` | model/contract (config + marker staging) | config parse / batch staging | `PruningConfig::FromJSON` `PruneEligibility.cpp:84-97` + `WritePrunedEntryForTest` `PruneEligibility.cpp:215-227` + batch staging in `globaldb.cpp:851-867` | exact (self-extension) |
| `src/transaction/TransactionManager.hpp/.cpp` (MOD) — missing-set accessor + FAILED cleanup | service (domain owner) | event-driven (state transition) | `GetState()` accessor `TransactionManager.hpp:330`; existing FAILED mint rollback `TransactionManager.cpp:5050-5066`; MintFunds exception path `TransactionManager.cpp:1016-1060` | exact (see Discrepancy below) |
| `src/account/PublicChainInputValidator.cpp` (MOD) — net-scope both bypass legs | validator | request-response | itself, `GatherVerificationEvidence` `PublicChainInputValidator.cpp:319-334` | exact (edit in place) |
| `src/account/GeniusNode.hpp/.cpp` (MOD) — real-cycle arm + `GetPruneStatus()` | controller (wiring only) | event-driven (asio timer) | `StartPruneDryRunTimerIfConfigured` `GeniusNode.cpp:4862-4911` (the `dry_run==false` WARN-return at 4879-4884 is the replaced branch) | exact |
| `src/crdt/crdt_set.hpp` + GlobalDB/CrdtDatastore exposure (MOD) — owner-vended compose lock | service (CRDT owner) | batch (critical section) | `PutElems` lock-across-build+commit `crdt_set.cpp:760-806`; `mutex_` recursive private `crdt_set.hpp:373-378` | exact (D-01 names this precedent) |
| `src/crdt/impl/crdt_datastore.cpp` (MOD) — D-02 guard | middleware (broadcast intake) | event-driven | itself, `HandleCIDBroadcast` dedup chain `crdt_datastore.cpp:703-763` | exact (one insertion) |
| `test/src/account/tx_pruner_test.cpp` (NEW) | test (integration) | fixture-driven | `test/src/account/prune_eligibility_test.cpp:61-90` (CRDTFixture stack) + `globaldb_erase_test.cpp` seam/crash discipline (`:136-152`) | exact |
| `test/src/account/dev_mint_policy_test.cpp` (NEW or folded) | test (multi-peer consensus) | request-response over real pubsub | `test/src/blockchain/multi_node_finality_fault_test.cpp:1097-1152, 1227-1249` | exact |
| SC-3 re-broadcast test (NEW, in `globaldb_integration_test` target) | test (two-node pubsub) | event-driven (broadcast/redelivery) | `EraseNodeCollection` `test/src/crdt/globaldb_erase_test.cpp:73-161` | exact |
| `test/src/crdt/crdt_erase_test.cpp` (MOD) — EraseSurfaceGate sanctioned-caller-set | test (gate) | source-scan | itself, targets vector `crdt_erase_test.cpp:780-790` | exact (edit in place) |
| `test/src/account/CMakeLists.txt` (+ `test/src/crdt/` if new file there) (MOD) | config (build) | — | `addtest(prune_eligibility_test ...)` block `test/src/account/CMakeLists.txt:254-275` | exact |

Also touched (docs only): `.planning/todos/pending/genius-chain-witness-bypass-hardening.md` (set `resolves_phase: 18`, close on landing) — no code analog needed.

## Pattern Assignments

### `src/transaction/TxPruner.{hpp,cpp}` (service, timer-driven batch)

**Analogs:** `GeniusNode.cpp:4913-5020` (cycle skeleton to move/extend), `PruneEligibility.hpp:264-320` (dependency-tuple + factory shape), `globaldb.cpp:797-884` (the compose it drives).

**Class/factory shape** — mirror `PruneEligibility::New` exactly (same dependency tuple, same GATE-01 fail-closed role check; `PruneEligibility.cpp:102-117`):

```cpp
// PruneEligibility.hpp:283-287 — the signature TxPruner mirrors
static outcome::result<std::shared_ptr<PruneEligibility>> New( crdt::GlobalDB      &globaldb,
                                                               TransactionManager  &transaction_manager,
                                                               UTXOManager         &utxo_manager,
                                                               NodeType             node_type,
                                                               const PruningConfig &config );
// PruneEligibility.cpp:108-114 — role gate to repeat
if ( node_type != NodeType::Light )
{
    return outcome::failure( std::errc::operation_not_permitted );
}
```

**Per-tick snapshot + generation discipline (D-C4)** — copy from `RunPruneDryRunCycle` (`GeniusNode.cpp:4931-4943`; the whole lifecycle-safety comment block 4915-4930 explains why):

```cpp
const auto snapshot = SnapshotAccountServices();   // GeniusNode.cpp:4931
if ( !tx_globaldb_ || !snapshot.account || !snapshot.manager ) { /* skip tick */ }
// generation check BEFORE any erase — GeniusNode.cpp:4982
const bool generation_superseded = ( SnapshotAccountServices().generation != snapshot.generation );
```

**Error split: permanent-stop vs transient-retry** (`GeniusNode.cpp:4953-4973`):

```cpp
if ( evaluation.error() == std::errc::invalid_argument )
{
    node_logger_->error( "... {} — dry-run cycle stopped", evaluation.error().message() );
    return;                       // permanent: do not re-arm
}
node_logger_->warn( "... {} — retrying next cycle", ... );  // transient: reschedule below still runs
```

**Timer arm/weak-self pattern** (kept in GeniusNode, not TxPruner — Pitfall 9): `GeniusNode.cpp:4894-4910` — `std::make_unique<boost::asio::steady_timer>( *io_ )`, `std::weak_ptr<GeniusNode> weakSelf = shared_from_this()`, `expires_from_now( minutes(1) )` first tick, re-arm at `:5007-5019` with `minutes(10)`.

**Per-tx erase driver** — already landed, call it (`TransactionManager.cpp:4277-4309`): `ResolveTransactionPathAndCID` → `CID::fromString` → `globaldb_m->EraseEntry( path, cid )` → `RemoveProcessedTransactionEntry( path )` (warn-only cleanup). The TxPruner adds the K-bound loop, block grouping, and the D-01 compose-lock variant around this call — do NOT fork the path/CID resolution.

**Status surface (RECL-04)** — mutex-guarded struct on TxPruner, thin getter delegation from GeniusNode (`PruneStatus` shape in RESEARCH Code Examples). Analog for the "service owns state, facade delegates" split: `PruneEligibility` owning eligibility while GeniusNode only wires the timer.

**Cycle-level interrupt seam** — copy the static-seam shape from `globaldb.cpp:790-795` / `globaldb.hpp:250-258`:

```cpp
std::function<bool( int )> GlobalDB::erase_interrupt_hook_for_test_ = nullptr;   // file-scope static, nullptr in production
void GlobalDB::SetEraseInterruptHookForTest( std::function<bool( int step )> hook ) { ... }
```
Destructor-clearing discipline for fixtures: `globaldb_erase_test.cpp:140-142` (`SetEraseInterruptHookForTest( nullptr )` before teardown in the harness dtor — "a hook left armed from a failed case must never leak further").

---

### `src/account/DevMintPolicy.hpp` (utility/policy, NEW)

**Analogs:** `src/base/sgns_version.hpp:12-15` (constant home beside the net-id domain owner), `sgns_version.cpp:56-59` (the predicate's only input).

```cpp
// sgns_version.hpp:12-15 — the constants the policy set is built from (keep them the ONLY net-id literals)
static constexpr std::uint16_t MAIN_NET_ID    = 369;
static constexpr std::uint16_t TEST_NET_ID    = 963;
static constexpr std::uint16_t DEV_NET_ID     = 144;
static constexpr std::uint16_t STAGING_NET_ID = 333;

// sgns_version.cpp:14, 56-59 — process-global net id, set once from config (GeniusNode.cpp:411-414)
static uint16_t network_id_ = sgns::version::DEV_NET_ID;   // default = local dev
uint16_t sgns::version::GetNetworkID() { return network_id_; }
```

RESEARCH's decided shape: `inline constexpr std::array<uint16_t, 2> kDevMintPermittedNets{ version::STAGING_NET_ID, version::DEV_NET_ID };` + a predicate reading ONLY `version::GetNetworkID()` — never a tx/proposal field (Pitfall 5). Doxygen file header required (M017); named constants, no magic numbers (M012).

---

### `src/account/DevMintInputValidator.{hpp,cpp}` (validator, NEW — production twin)

**Behavioral analog:** `test/testutil/TestMintInputValidator.hpp` (whole file, 51 lines). The twin keeps every return value, conditions `ValidateWitness`'s `kValid` on `DevMintPolicy`:

```cpp
// TestMintInputValidator.hpp:14-44 — the shape to twin
class TestMintInputValidator final : public IInputValidator
{
public:
    bool ValidateUTXOParameters( ... ) const override { return true; }
    IInputValidator::WitnessVerdict ValidateWitness( ... ) const override
    { return IInputValidator::WitnessVerdict::kValid; }   // twin: return kValid iff DevMintPolicy permits
    bool RequiresConsensusUTXOData() const override { return false; }
};
```

**Registration analog (production, NOT the test file-scope static):** `GeniusInputValidator.cpp:113-120`:

```cpp
bool GeniusInputValidator::Register()
{
    static GeniusInputValidator instance;
    IInputValidator::Register( "supergenius", &instance );
    IInputValidator::Register( "supergenius_chain", &instance );
    IInputValidator::Register( "", &instance );
    return true;
}
```
Call site / linker-anchor pattern: `TransactionManager.cpp:73-74` — `[[maybe_unused]] const bool kGeniusValidatorRegistered = GeniusInputValidator::Register();` at file scope, with the comment at `:66-72` explaining WHY (keeps validator object files in every validating binary; insert-only registration, separate variables). The dev validator registers the literal `"dev"` the same way. Registry contract: `InputValidators.hpp:99-118` — `Register` is insert-only, returns false if the chain id is already claimed.

**Routing (no change needed):** `TransactionManager::SelectInputValidator` (`TransactionManager.cpp:1942-1983`) — a registered chain id is found at step 2 (`IInputValidator::Get( chain_id )`, `:1967-1970`) before the genius/public fallthroughs. The interface: `InputValidators.hpp:47-95` (`WitnessVerdict` tri-state at `:54-60` — `kValid`/`kNotSynced`/`kInvalid`).

---

### `src/transaction/PruneEligibility.{hpp,cpp}` (MOD — config keys + `StagePrunedMarker`)

**Config-key analog — per-key type-guarded reads + fail-closed:** `PruneEligibility.cpp:84-97`:

```cpp
if ( pruning_object.HasMember( "finality_window" ) && pruning_object["finality_window"].IsUint64() &&
     pruning_object["finality_window"].GetUint64() > 0 )
{
    config.finality_window = pruning_object["finality_window"].GetUint64();
}
if ( config.enabled && !config.finality_window.has_value() )   // fail closed: disable + disabled_reason
{
    config.enabled         = false;
    config.disabled_reason = std::string( kWindowRequiredMessage );
}
```
Add `max_erases_per_cycle` (locked name; sane default ~100) and the settle knob with the same `HasMember && Is<Type>` guard discipline. GeniusNode's delegation block stays untouched-shape (`GeniusNode.cpp:478-489` — guard object-ness, delegate everything, log the summary line; extend the INFO fields for the new keys).

**Marker staging (`StagePrunedMarker`) analog:** `WritePrunedEntryForTest` `PruneEligibility.cpp:215-227` — same key build + record serialization, but `batch->put(...)` into the caller's `BufferBatch` instead of `db.PutRaw(...)`:

```cpp
// WritePrunedEntryForTest (test-only, direct PutRaw) — the production twin stages into the batch:
crdt::GlobalDB::Buffer key_buffer;
key_buffer.put( BuildPruneMarkerKey( tx_hash ) );            // PruneEligibility.cpp:135-140 — key home stays HERE
crdt::GlobalDB::Buffer value_buffer;
value_buffer.put( record.SerializeAsString() );
return db.PutRaw( key_buffer, value_buffer );                // twin: aDataStore->put( key_buffer, value_buffer )
```
Where the staging call sits in the batch: see the compose excerpt under Shared Patterns (EraseEntry compose, `globaldb.cpp:851-867`) — marker staging joins the SAME batch before `batch->commit()`. Key/record contracts (`BuildPruneMarkerKey`, `LookupPrunedEntry` tri-state, WR-04 path checks) already live here (`PruneEligibility.cpp:135-213`) — do not duplicate them in TxPruner.

---

### `src/transaction/TransactionManager.{hpp,cpp}` (MOD — accessor + FAILED cleanup)

**Accessor analog:** `GetState()` public accessor, `TransactionManager.hpp:330`. `missing_tx_hashes_` is private at `hpp:749` (`std::unordered_set<std::string>`); add the smallest public read (empty-check or copy) beside `GetState` with matching Doxygen style.

**FAILED-transition cleanup — DISCREPANCY, planner must reconcile.** RESEARCH E11 says "No reservation cleanup" at the FAILED transition, but source already contains a mint-v2 rollback there (`TransactionManager.cpp:5029-5076`):

```cpp
// TransactionManager.cpp:5050-5066 — EXISTS TODAY in the INVALID/FAILED case
else if ( tx->GetSrcAddress() == account_m->GetAddress() && tx->HasUTXOParameters() )
{
    // Local outgoing tx failed before confirmation: release locally reserved inputs.
    auto params_opt = tx->GetUTXOParametersOpt();
    if ( params_opt.has_value() )
    {
        if ( tx->GetType() == "mint-v2" )
        {
            account_m->GetUTXOManager().RollbackUTXOs( params_opt->first,
                                                       tx->dag_st.uncle_hash(),
                                                       UTXOManager::UTXOType::UTXO_BRIDGE );
        }
        ...
// TransactionManager.cpp:5070-5076 — the STUB (likely the todo's real delta: reservation release)
// Clear bridge mint reservation on failure
if ( tx->GetType() == "mint-v2" )
{
    auto mint_tx = std::dynamic_pointer_cast<MintTransactionV2>( tx );
    // UTXO consumed automatically via ParseMintTransactionV2's ConsumeUTXOs
    (void) mint_tx;
}
```
The live incident (todo dated 2026-10-20, burn UTXO stayed READY) postdates this code, so the todo's delta is NOT "add RollbackUTXOs" — verify what actually leaks: the reservation claim (`TryReserveOutpoint`, `TransactionManager.cpp:1016-1030`; reservation id = burn hash string, the D-18 contract), the `GetSrcAddress()==local` guard excluding some reject path (`reject_and_maybe_fail_local` fires `ChangeTransactionState(FAILED)` at `:2200`), idempotency on re-fire (`:5034-5040` already dedups FAILED→FAILED), or exception-path asymmetry (`:1050-1060`). Mirror the exception path exactly: `RollbackUTXOs( rollback_inputs, transaction_hash, UTXO_BRIDGE )`. Signature: `UTXOManager.hpp:345-347` — `void RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs, const std::string &reservation_id, UTXOType type = UTXOType::UTXO_NORMAL );`

---

### `src/account/PublicChainInputValidator.cpp` (MOD — net-scope both bypass legs)

**Analogs:** the two legs themselves, `PublicChainInputValidator.cpp:319-334`:

```cpp
if ( source_reference.empty() )                                    // LEG 1 (:319-326)
{
    logger->debug( "GatherVerificationEvidence skipped because source reference is empty" );
    evidence.valid = true;                                         // ← gains: && DevMintPolicy permits
    return evidence;
}
const auto chain_id = tx.GetChainId();
if ( chain_id.empty() || chain_id == "supergenius" )               // LEG 2 (:328-334)
{
    logger->debug( "GatherVerificationEvidence bypassed for local chain_id={}", chain_id );
    evidence.valid = true;                                         // ← gains the same conjunct
    return evidence;
}
```
Default-invalid evidence contract: `RpcVerificationEvidence.valid = false` by construction (struct default; the no-endpoints reject at `:336-341` returns it untouched) — on non-designated nets simply return the default `evidence` without setting `valid`, matching the existing reject shape. Both legs get the SAME `DevMintPolicy` conjunct (RESEARCH E8); keep the leg order and log lines (M002: change only what the task needs).

---

### `src/account/GeniusNode.{hpp,cpp}` (MOD — real-cycle arm + status delegation)

**Analogs:** `StartPruneDryRunTimerIfConfigured` `GeniusNode.cpp:4862-4911` — the replaced branch is exactly `:4879-4884`:

```cpp
if ( !pruning_config_.dry_run )
{
    node_logger_->warn( "Pruning enabled with dry_run=false: real erasure arrives with the Phase-18 "
                        "TxPruner; no cycle is started this phase" );
    return;
}
```
This WARN-return becomes the real-cycle construction/arm. Copy the surrounding guards verbatim: default-off consent gate `:4866-4869`, Light-only role gate `:4872-4878`, already-running guard `:4889-4892`. The timer member/decl pattern: `prune_dry_run_timer_` `GeniusNode.hpp:1169` + decl `:1278/:1291`; `pruning_config_` `:1179`. Wiring reaches NOTHING past timer arm/cancel, gate inputs, and `GetPruneStatus()` delegation — all cycle logic stays in TxPruner (Pitfall 9; containment-gate enforceable).

---

### `src/crdt/crdt_set.hpp` + GlobalDB/CrdtDatastore compose-lock exposure (MOD — D-01)

**Analogs:** the exact lock discipline D-01 names — `PutElems` `crdt_set.cpp:760-806`:

```cpp
{
    std::lock_guard lg( this->mutex_ );          // :761 — held across build AND commit
    auto batchDatastore = this->dataStore_->batch();
    ... // stage everything
    auto commitResult = batchDatastore->commit();   // :798 — commit INSIDE the lock
    ...
} // lg released here -- hooks below run with this->mutex_ NOT held.  (:806)
```

```cpp
// crdt_set.hpp:373-378 — the mutex being vended (recursive, PRIVATE — only the owner can expose it)
// Recursive: PutElems holds this lock across its synchronous putHookFunc_ callback, ...
std::recursive_mutex       mutex_;
```
Reentrancy fact that makes the owner-vended variant work: `StageEraseElement` takes `std::lock_guard lg( this->mutex_ )` at `crdt_set.cpp:690` — reentrant under an outer hold, so a lock-scoped `EraseEntry` variant simply wraps the existing guards + staging + commit (guards currently documented check-then-act at `globaldb.hpp:209-226`, the WR-02 window note — the comment MUST be updated when the window closes). Preferred API shape per RESEARCH Pattern 2: a locking `EraseEntry` variant (critical section impossible to forget) rather than exposing the mutex.

**Do not hold the lock across `Evaluate()` or across all K erases** — per-tx around guard→stage→commit only (Pitfall 3).

---

### `src/crdt/impl/crdt_datastore.cpp` (MOD — D-02 guard, one insertion)

**Analog:** the dedup chain itself, `crdt_datastore.cpp:703-763`. Insertion point is BETWEEN the `HasBlock` miss (`:712-717` `continue`) and `IsCIDInCache` (`:719`):

```cpp
if ( dagSyncerResult.value() )
{
    logger_->trace( "{}: Already processed block {}", __func__, bCastHeadCID.toString().value() );
    continue;                                   // :712-717 — known block, unchanged
}
// D-02 NEW: after HasBlock miss, before IsCIDInCache (:719)
// isResolved(cid) == true ⇒ processed-and-pruned ⇒ terminal, skip (seal survives deletion)

if ( dagSyncer_->IsCIDInCache( bCastHeadCID ) ) // :719 — unchanged
```
Call shape: `dagSyncer_->HasBlock( bCastHeadCID )` returns `outcome::result` (error-checked at `:707-711`); `isResolved` likewise (`globaldb.cpp:842` shows the BOOST_OUTCOME_TRY usage). One function, one insertion — anything more is wrong (Pitfall 4: sealed-but-present blocks must keep taking the HasBlock path; byte-identical behavior on unpruned nodes).

---

### `test/src/account/tx_pruner_test.cpp` (NEW — service-level suites)

**Analog:** `test/src/account/prune_eligibility_test.cpp` — the exact fixture stack (SC-1/SC-2/SC-4/SC-5 + status suites run on this, not on GeniusNode):

```cpp
// prune_eligibility_test.cpp:61-90 — CRDTFixture family: real GlobalDB + pubsub + temp dir, no GeniusNode
class PruneEligibilityTest : public ::test::CRDTFixture
{
public:
    PruneEligibilityTest() : ::test::CRDTFixture( "prune_eligibility_test" ) {}
    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( ... ) { return std::make_shared<MemorySecureStorage>( ... ); } );
        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        tm_ = TransactionManager::New( db_, io_, account_, blockchain_, NodeType::Light, ... );
        ...
```
Also copy from that file: seed through the REAL publish path on `kSeedTopic` ("CRDT.Datastore.TEST.Channel", `:53-54`) — never raw local puts (Phase-16 lesson, file header `:6-10`); include shape `:26-43`; `using namespace sgns;` `:45`. Crash-matrix + reopen-fixed-point discipline: the `GlobalDBEraseTest` suite (seam steps + destructor-cleared hook, `globaldb_erase_test.cpp:136-152`). Wait-condition template (no `sleep_for`): `ASSERT_WAIT_FOR_CONDITION` as used in `multi_node_finality_fault_test.cpp:1214-1225`.

**CMake registration analog** (`test/src/account/CMakeLists.txt:254-275`):

```cmake
addtest(tx_pruner_test
    tx_pruner_test.cpp
)
target_link_libraries(tx_pruner_test
    genius_node_test
    json_secure_storage
    base_crdt_test
)
# + the MSVC/APPLE/else whole-archive target_link_options block verbatim (:261-273)
```

---

### `test/src/account/dev_mint_policy_test.cpp` (NEW or folded — D-03 real-consensus suites)

**Analog:** `test/src/blockchain/multi_node_finality_fault_test.cpp` — the ctest-mint harness the policy replicates:

```cpp
// :1097-1112 — MintFor: fabricated burn hash, chain id, real signature
const auto burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'c' ) );
dag.set_uncle_hash( burn_hash.value().toReadableString() );
auto mint = std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
    42, "test", kToken, std::move( dag ), { { burn_hash.value(), 0, {} } }, peer.account->GetAddress() ) );
mint->MakeSignature( *peer.account );

// :1229-1238 — empty UTXOWitness subject → CreateProposal → SubmitProposal (real quorum)
auto subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(), mint->GetNonce(),
    mint->GetHash(), mint->SerializeToEmbeddedTransaction(), CommitmentFor( *mint ), sgns::UTXOWitness{} );
auto proposal = first.consensus->CreateProposal( subject.value(), ..., registryCid, registryEpoch );
ASSERT_TRUE( first.consensus->SubmitProposal( proposal.value() ).has_value() );

// :1244-1249 — certificate-through-CRDT assertions
ASSERT_WAIT_FOR_CONDITION( [&] { return first.consensus->CheckCertificateForSlot( canonical_slot ) && ...; },
                           std::chrono::seconds( 20 ), "one canonical certificate reached every peer through CRDT", nullptr );
```
`CommitmentFor` (params-only merkle commitment, `:1114-1152`) is reusable as-is. The refusal tests (non-designated net) assert the NEGATIVE: no quorum/certificate for the identical subject — same harness, inverted `ASSERT_WAIT_FOR_CONDITION`/timeout discipline. The dev validator registers for the literal `"dev"` (chain id in `MintFor`'s second arg changes from `"test"` to `"dev"`); registry update seeding via `StoreRegistryUpdate` at `:1217-1225`. NOTE: changing `version::SetNetworkId` is process-global and affects the whole test binary — check what the finality test binary's other cases assume before keying fixtures on net 333 vs 963; the `SetNetworkId` validation whitelist is `sgns_version.cpp:75-85`.

---

### SC-3 re-broadcast test (NEW file in `globaldb_integration_test` target)

**Analog:** `EraseNodeCollection` `test/src/crdt/globaldb_erase_test.cpp:73-161` — real pubsub + GlobalDB per node:

```cpp
// :93-133 addNode recipe (per-node basePath under the binary dir + testName; GossipPubSub on port 0;
//         SchedulerImpl on the pubsub AsioContext — see the threading comment at :108-110;
//         GlobalDB::New(...CrdtOptions::DefaultOptions()...) ; db->Start())
// :136-152 destructor: SetEraseInterruptHookForTest(nullptr) FIRST, then ShutdownNow/Stop per node
```
The test adds: put on A → erase on B → re-broadcast/restart-heads delivery → assert ZERO fetch attempts (needs the Wave-0 fetch-attempt observable — model it on the static-seam shape, `globaldb.hpp:250-258`) and zero re-created elements. Pre-pruned delivery still fetches = negative control. Register in `test/src/crdt/CMakeLists.txt` under the existing `globaldb_integration_test` addtest (line 50).

---

### `test/src/crdt/crdt_erase_test.cpp` (MOD — EraseSurfaceGate evolution)

**Analog:** the targets table itself, `crdt_erase_test.cpp:780-790`:

```cpp
const std::vector<FunctionTarget> targets = {
    { "crdt/globaldb/globaldb.cpp", "GlobalDB::EraseEntry", {} },
    { "transaction/TransactionManager.cpp",
      "TransactionManager::EraseTransactionEntry",
      { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
    ...
};
```
Extend with the new tracked bodies: the lock-scoped compose variant, `StagePrunedMarker` (its batch `put` sanctioned ONLY there), the TxPruner per-tx erase fn, and the dev-policy predicate. Invariant shift (RESEARCH Pattern 6): from "no production erase caller" to "exactly the sanctioned caller set" — grep-exact caller lists; the dry-run body token-cleanliness grep stays valid on `RunPruneDryRunCycle`; the real-cycle function needs its own `dry_run == false` gate. Scan machinery to reuse: `gateSourceRoot()` / `collectSourceFiles()` / `extractFunctionBody()` / `readWholeFile()` helpers and the `SUPERGNUS_ERASE_GATE_SRC` wiring (comment at `:702-705`).

## Shared Patterns

### outcome::result discipline (all new/modified C++)
`BOOST_OUTCOME_TRY` for propagation (`globaldb.cpp:814, :842, :852-867`); plain enum errors have no `.message()` (M004) — `std::error_code` returns do (`GeniusNode.cpp:4949`); never swallow (`INVALID_PARAMETERS` guard rejects, `POST_COMMIT_ERASE_ANOMALY` committed-erase reporting, `globaldb.cpp:869-881`). Fail-closed everywhere: role gate (`PruneEligibility.cpp:108-114`), config gate (`:90-97`), block gate.

### Guarded compose (one batch, owner stagings, one commit)
**Source:** `globaldb.cpp:849-867` — apply to StagePrunedMarker's join and the lock-scoped variant:
```cpp
auto batch = datastore->batch();
BOOST_OUTCOME_TRY( crdt_datastore->StageEraseElement( batch, key ) );      // owner 1: CRDT entries
BOOST_OUTCOME_TRY( m_dagSyncer->StageBlockRemoval( cid, batch ) );         // owner 2: block bytes
BOOST_OUTCOME_TRY( batch->remove( Buffer{}.put( key ) ) );                 // owner 3: raw map entry
// NEW owner 4: StagePrunedMarker( batch, ... ) — same batch, before commit
BOOST_OUTCOME_TRY( batch->commit() );                                      // single atomic WriteBatch
```
Key-layout knowledge stays in owners; the facade only composes (comment at `:849-850`).

### Logging
**Source:** `base::createLogger` — function-local static (`GeniusInputValidator.cpp:107-111`, `crdt_set.cpp` `CRDTSet()` accessor) or member (`PruneEligibility.hpp:448` `logger_ = base::createLogger( "PruneEligibility" )`). Never `std::cout` (M018). Structured per-cycle INFO summary shape: `GeniusNode.cpp:4990-4998`.

### Header/documentation conventions
Doxygen `@file/@brief/@details/@date` file header (`PruneEligibility.hpp:1-14`); `@param/@return` on every declaration (M009/M017 — carry forward verbatim when editing `globaldb.hpp`'s EraseEntry contract, which MUST be rewritten when D-01 closes WR-02). C++17 only, Allman braces, `constexpr` over macros, `enum class`, `static constexpr` members (note `sgns_version.hpp` uses `static constexpr` pre-C++17 style; `inline constexpr` is right for the NEW policy header).

### Test discipline
Wait-condition templates (`ASSERT_WAIT_FOR_CONDITION` with timeout + message, `multi_node_finality_fault_test.cpp:1214+`) — never `sleep_for`; three-part assertions + negative controls + double-run idempotence (prune_eligibility_test header, Groups A-E); seam cleared in fixture dtor (`globaldb_erase_test.cpp:140-142`); fresh per-test DB paths including test name (`:95-98`).

### File-scope registration anchors
`[[maybe_unused]] const bool k...Registered = ...Register();` with the linker rationale comment (`TransactionManager.cpp:66-74`) — the dev validator joins these two lines.

## No Analog Found

| File / Sub-feature | Role | Data Flow | Reason |
|------|------|-----------|--------|
| Block-grouped K budgeting (group-or-defer) inside TxPruner | service logic | batch | No existing budgeted-erase loop in the codebase; RESEARCH Pattern 1 + Pitfall 1 define it (group whole or defer whole; optional missing-block-as-complete-evidence reason code). Planner designs from the research spec. |
| Fetch-attempt observable (SC-3 counter seam) | test seam | event-driven | No existing counter seam on `EnqueueRootCID`; nearest shape is the static hook seam (`globaldb.hpp:250-258`) — planner picks counter vs hook per RESEARCH Wave-0. |

## Discrepancies for the Planner (verified this session)

1. **Folded-todo FAILED cleanup partially exists.** `TransactionManager.cpp:5050-5066` already rolls back mint-v2 burn UTXOs on the FAILED transition (gated on local src address), contradicting RESEARCH E11's "No reservation cleanup"; `:5070-5076` is a `(void) mint_tx;` stub for reservation clearing. Reconcile the todo's actual delta (likely reservation release via the `TryReserveOutpoint` claim at `:1016-1030`, idempotency, or a reject path excluded by the guards) before planning the fix — do not plan "add RollbackUTXOs to FAILED" as if absent.
2. **Timer cadence:** `RunPruneDryRunCycle`'s real arm must keep the dry-run function byte-token-clean (Pitfall 2) — two distinct functions, distinct gates; the `dry_run==false` WARN-return at `GeniusNode.cpp:4879-4884` is the sole wiring point to replace.
3. **Net-identity in tests is process-global** (`sgns_version.cpp:14`, set once): D-03 designated/refusal fixtures in one binary share it — plan fixture isolation accordingly.

## Metadata

**Analog search scope:** `src/transaction/`, `src/account/`, `src/crdt/` (+`globaldb/`, `impl/`), `src/base/`, `test/src/{account,crdt,blockchain}/`, `test/testutil/`, `test/src/*/CMakeLists.txt`
**Files scanned:** ~18 targeted reads across 16 source/test files (all excerpts read at cited lines this session)
**Pattern extraction date:** 2026-10-21
