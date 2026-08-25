# Phase 12: Multi-Node Finality Fault Proof - Pattern Map

**Mapped:** 2026-08-25  
**Files analyzed:** 6 planned new/modified files  
**Analogs found:** 6 / 6

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `test/src/blockchain/multi_node_finality_fault_test.cpp` | test | event-driven / persistence-recovery | `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | role-match |
| `test/src/blockchain/CMakeLists.txt` | config | batch | same file, `consensus_pending_lifecycle_test` target | exact |
| `src/blockchain/Consensus.hpp` | service interface / test seam | event-driven / PubSub + CRDT | same file's existing friend seams | exact |
| `src/blockchain/Consensus.cpp` | service | event-driven / PubSub + durable CRDT | same file's active-vote and certificate publication paths | exact |
| `src/account/TransactionManager.hpp` | service interface / test seam | event-driven / durable Mint | same file's `CertificateFallbackTestAccess` seam | exact |
| `src/account/TransactionManager.cpp` | service | event-driven / durable Mint | same file's Mint confirmation path | exact |

`src/account/UTXOManager.{hpp,cpp}` is a **read-only supporting analog**, not a planned edit. Its hook fires before snapshot storage, whereas Phase 12 needs a post-durability barrier after `ParseTransaction()` and before the bridge marker. Keep that new barrier in `TransactionManager`.

## Pattern Assignments

### `test/src/blockchain/multi_node_finality_fault_test.cpp` (test, event-driven / persistence-recovery)

**Primary analog:** `test/src/blockchain/consensus_pending_lifecycle_test.cpp`

**Supporting analog:** `test/src/crdt/globaldb_integration.cpp`

**Mint-result assertion analog:** `test/src/account/transaction_manager_certificate_fallback_test.cpp`

**Imports and fixture conventions** — `consensus_pending_lifecycle_test.cpp:9-32`:

```cpp
#include "account/GeniusAccount.hpp"
#include "account/MintTransactionV2.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
```

Keep the new fixture on `test::CRDTFixture`, install `MemorySecureStorage` in `SetUp`, and add the existing account/transaction-manager headers needed to compose the consumer. The test owns every peer root path and must preserve that path across `StopPeer` / `RestartPeer`.

**Independent real peer construction** — `consensus_pending_lifecycle_test.cpp:623-747`:

```cpp
node.io = std::make_shared<boost::asio::io_context>();
auto keypair = sgns::crdt::KeyPairFileStorage(path + "/keypair").GetKeyPair();
node.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(keypair.value());
ASSERT_FALSE(node.pubsub->Start(port, {node.pubsub->GetLocalAddress()}).get());

auto db_result = sgns::crdt::GlobalDB::New(node.io, path + "/rocksdb", node.pubsub,
    sgns::crdt::CrdtOptions::DefaultOptions(), graphsync, scheduler, generator);
node.db = std::move(db_result.value());
node.db->Start();
node.account = sgns::GeniusAccount::NewFromPrivateKey(..., path + "/account", false);
```

Follow it for a four-entry `FinalityFaultNetwork::Peer` (three validator accounts and one passive recipient), but retain the scheduler, graphsync network, request-id generator, `Blockchain`, and `TransactionManager` as peer-owned runtime members so their dependent services outlive the relevant callbacks. `globaldb_integration.cpp:101-191` supplies the required IO-thread ownership and teardown sequence:

```cpp
std::thread t([io]() { io->run(); });
// Stop: io->stop(); join ioThread; pubsub->Stop(); then reset db/runtime objects.
```

Do **not** copy that fixture's `connectNodes()` sleep at `globaldb_integration.cpp:162-170`. Connect real peers with `AddPeers`, then wait for an observable predicate.

**Bounded asynchronous waits** — `test/testutil/wait_condition.hpp:64-102`:

```cpp
ASSERT_WAIT_FOR_CONDITION(
    [&] { return network.AllPeersHaveDurableWinner(slot, winner_hash); },
    std::chrono::seconds(10),
    "all peers durably accepted the winner",
    nullptr);
```

Every connection, barrier-entry, recovery, and convergence wait uses this pattern; no `sleep_for`, unbounded `condition_variable::wait`, or test-order-dependent audit aggregate.

**Drive only normal production ingress.** Build the two competing public proposals with `CreateProposal`, then use the public route at `Consensus.hpp:463-475` and `Consensus.cpp:1936-1978`:

```cpp
auto proposal = peer.manager->CreateProposal(subject, peer.account->GetAddress(), registry_cid, epoch);
ASSERT_TRUE(proposal.has_value());
ASSERT_TRUE(peer.manager->SubmitProposal(proposal.value()).has_value());
```

`SubmitProposal` constructs the `ConsensusMessage`, publishes it, then executes its normal local self-vote path. The Phase 12 test must **not** copy `ConsensusPendingLifecycleTestAccess::{HandleProposal, HandleCertificate, CertificateReceived, WriteLiveCertificate, Force*}` from `consensus_pending_lifecycle_test.cpp:34-150`; those existing direct-driver helpers are explicitly invalid for TEST-06.

**Exact-winner and recovery assertions** — `transaction_manager_certificate_fallback_test.cpp:727-753, 904-964`:

```cpp
EXPECT_TRUE(TransactionManager::CertificateMatchesTransaction(loaded.value(), *winner));
EXPECT_FALSE(TransactionManager::CertificateMatchesTransaction(loaded.value(), *loser));
EXPECT_EQ(account_->GetUTXOManager().GetUTXOs(account_->GetAddress()).size(), 1u);
EXPECT_TRUE(db_->GetDataStore()->get(marker_key_buffer).has_value());
```

Build reusable `AssertSingleDurableMint(peer, winner, loser)` and invoke it before and after each affected peer is recreated from the same RocksDB root. It must assert: one canonical `/cert/<slot>` record, winner-only UTXO/outpoints, no loser output, exactly one Mint-effect counter, and exactly one `/bridge/executed/<chain>:<source>` marker.

**Scenario shape:** one independent fixture run per named GoogleTest case:

1. `SameBurnContentionUsesOneCanonicalSlotAndExactMint`
2. `LateContenderAndPassiveRecipientRemainReceiveOnly`
3. `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce`
4. `PublisherLossAfterPersistenceUsesDeterministicFailover`
5. `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress`

The fifth case is self-contained; it audits only counters from its own four-peer run.

---

### `test/src/blockchain/CMakeLists.txt` (config, batch)

**Analog:** `test/src/blockchain/CMakeLists.txt:36-46`

```cmake
set(CONSENSUS_PENDING_LIFECYCLE_SOURCE consensus_pending_lifecycle)
set(CONSENSUS_PENDING_LIFECYCLE_TARGET consensus_pending_lifecycle)
addtest(consensus_pending_lifecycle_test
    ${CONSENSUS_PENDING_LIFECYCLE_SOURCE}_test.cpp
)
target_link_libraries(${CONSENSUS_PENDING_LIFECYCLE_TARGET}_test
    blockchain_genesis
    genius_node_test
    rapidjson
    base_crdt_test
)
```

Add a normal, enabled `addtest(multi_node_finality_fault_test multi_node_finality_fault_test.cpp)` beside this target and mirror this link set unless compilation identifies a genuinely additional existing target. Add a CTest `TIMEOUT` property no greater than 300 seconds. Do not wrap it in `if`, prefix the target with `#`, or mark it disabled.

---

### `src/blockchain/Consensus.hpp` (service interface / test seam, event-driven)

**Analog:** existing friend-scoped test access at `Consensus.hpp:549-552, 973-995`.

```cpp
friend class ConsensusPendingLifecycleTestAccess;
friend class CertificateFallbackTestAccess;

bool fail_active_vote_persistence_for_test_ = false;
std::vector<std::string> active_vote_announcements_for_test_;
std::function<void()> timer_work_hook_for_test_;
```

Add one narrowly named Phase-12 friend accessor (for example `MultiNodeFinalityFaultTestAccess`) and private, resettable observer/barrier state guarded consistently with the owning path's mutex. Its callable surface is restricted to:

- arm/release/read a barrier entered **after** durable active-vote persistence and before normal vote publication;
- arm/release/read a barrier entered **after successful** immutable certificate persistence and before the normal PubSub notification;
- read/reset monotonic counters for actual vote publication, authoritative certificate-write attempts/successes, and certificate notification publication/receipt.

Do not expose `Handle*`, `OnConsensusMessage`, `CertificateReceived`, `FilterCertificate`, datastore-write, timer-force, or publisher-selection functions through this new accessor. The accessor is an observer/synchronizer, never a protocol-data creator or delivery mechanism.

---

### `src/blockchain/Consensus.cpp` (service, event-driven / PubSub + durable CRDT)

**Analogs:** `Consensus.cpp:1268-1315`, `2008-2079`, `2610-2622`, and `3623-3715`.

**Durable active vote and restart recovery** — `Consensus.cpp:1268-1315` writes the record to the datastore only after validating the exact record; `Consensus.cpp:1317-1374` reloads and locks the stored vote at manager construction. Add observation/barrier only after successful `datastore->put(...)` (and on the exact-existing successful path as appropriate), before the existing normal publication in `ProcessDueVoteWork`. The barrier must leave the stored vote and subsequent real PubSub route unchanged.

**Required certificate persistence-before-notification seam** — `Consensus.cpp:2057-2069`:

```cpp
auto cert_put = db_->PutConvergentImmutable(cert_key, cert_value, {consensus_datastore_topic_});
if (cert_put.has_error()) {
    return outcome::failure(cert_put.error());
}

ConsensusMessage message;
*message.mutable_certificate() = certificate;
auto result = Publish(message);
```

Increment an authoritative-write-attempt counter at the real write boundary; after `cert_put` succeeds, record durable success, enter the optional test barrier, then construct/publish the existing message. The failover test must be able to observe the durable `/cert/<slot>` while notification count remains zero, stop the real selected publisher, and let the normal round/authority rules proceed.

**Receiver behavior is readback, not authority.** `Consensus.cpp:2610-2622` deliberately only marks work seen/stalled in the pre-commit CRDT callback:

```cpp
certificate_work_journal_->MarkSeen(key);
certificate_work_journal_->MarkStalled(key, std::chrono::milliseconds(0));
timer_cv_.notify_all();
```

Keep it write-free. Count notification receipt there if needed, but never turn that counter/hook into a write or handler dispatch. `Consensus.cpp:3623-3715` is the later durable `db_->Get(...)` readback and `ProcessCommittedCertificate(...)` path; place the accepted-certificate restart barrier after successful readback/validation and before normal handler dispatch, so restart proves recovery rather than callback provenance.

---

### `src/account/TransactionManager.hpp` (service interface / test seam, event-driven durable Mint)

**Analog:** `TransactionManager.hpp:294-297, 560-588`.

```cpp
friend class CertificateFallbackTestAccess;
bool fail_bridge_executed_marker_write_for_test_ = false;
std::function<void()> fetch_and_process_before_state_change_hook_for_test_;

void SetBridgeExecutedMarkerWriteFailureForTest(bool fail);
void SetFetchAndProcessBeforeStateChangeHookForTest(std::function<void()> hook);
outcome::result<void> PersistBridgeExecutedMarker(const MintTransactionV2 &mint_tx);
```

Add the same narrowly scoped Phase-12 friend class plus a thread-safe Mint-effect counter and a resettable post-effects/pre-marker barrier. It may be armed/read/released by the test only; it must not invoke `OnConsensusCertificate`, fetch a transaction, mint, or mutate the marker itself.

---

### `src/account/TransactionManager.cpp` (service, event-driven durable Mint)

**Analogs:** `TransactionManager.cpp:109-155`, `1944-2012`, and `5406-5431`.

**Normal certificate-to-Mint ownership** — `TransactionManager.cpp:109-155` registers `OnConsensusCertificate` as a blockchain certificate handler. The network suite must rely on this registration reached from consensus durable readback; it must never call `OnConsensusCertificate` itself.

**Idempotent durable sequence** — `TransactionManager.cpp:5408-5429`:

```cpp
if (it != tx_processed_m.end() && it->second.status == TransactionStatus::CONFIRMED) {
    return outcome::success();
}
tx_processed_m[key] = TrackedTx{tx, TransactionStatus::VERIFYING, tx->GetNonce()};

BOOST_OUTCOME_TRY(ParseTransaction(tx));
BOOST_OUTCOME_TRY(PersistBridgeExecutedMarker(*mint_tx));

tx_processed_m[key] = TrackedTx{tx, TransactionStatus::CONFIRMED, tx->GetNonce()};
```

The new counter increments only after `ParseTransaction(tx)` succeeds—`ParseMintTransaction` persists the UTXO effects through `UTXOManager::PutUTXO` at `TransactionManager.cpp:1969-1990`. Enter the optional Phase-12 barrier immediately after that successful Mint-effect observation and before `PersistBridgeExecutedMarker`. This preserves the UTXO-before-marker crash window exactly. On restart, recovery must finish the existing marker path without another UTXO effect.

`UTXOManager.cpp:156-195, 826-864, 938-950` confirms why no UTXO edit is needed: duplicate outpoints are durable-progress guarded and its existing hook is explicitly *before* store, not an acceptable post-durability Phase-12 barrier.

## Shared Patterns

### Real transport and persistence only

**Sources:** `Consensus.cpp:57-123`, `Consensus.cpp:1936-1978`, `globaldb_integration.cpp:124-191`

Create messages through public APIs, start real `GossipPubSub` / `GlobalDB`, and use real `AddPeers` plus stop/recreate lifecycle. Test code may inspect durable records and arm barriers, but may not call local receive/author paths or `GlobalDB::Put*` to establish a scenario.

### Barrier contract

**Sources:** `Consensus.cpp:2057-2069`; `TransactionManager.cpp:5423-5424`

Each hook is strictly post-action:

```text
production durable action → increment read-only counter → optional test barrier → existing production continuation
```

The allowed actions are durable vote persistence, accepted-certificate durable readback, immutable certificate persistence, and durable Mint effects. A barrier must be condition-based, releasable during teardown, and never filter, retry, deliver, synthesize, or select protocol data.

### Durable proof over in-memory convergence

**Sources:** `Consensus.cpp:1317-1374`; `transaction_manager_certificate_fallback_test.cpp:904-964`

Every claimed result is checked per peer before and after reopening its identical RocksDB root: active vote where applicable, canonical certificate, winner UTXO(s), absence of loser effect, and one bridge marker. Network convergence or a `CONFIRMED` in-memory status alone is insufficient.

### Error and cleanup behavior

**Sources:** `Consensus.cpp:2014-2074`; `TransactionManager.cpp:5408-5429`; `globaldb_integration.cpp:177-191`

Preserve existing `outcome::result` propagation. A failed persistence action must not enter/advance a barrier or increment its success counter. Fixture teardown stops/join IO and releases dependent runtime objects while retaining only peer root/key files for a deliberately restarted peer; final fixture teardown may remove test-owned roots through `CRDTFixture` cleanup.

### Bounded waiting

**Source:** `test/testutil/wait_condition.hpp:64-149`

Use `ASSERT_WAIT_FOR_CONDITION` for all asynchronous state observations. The suite has a normal CTest timeout of no more than 300 seconds, but each scenario's waits should use short named predicate timeouts within that budget.

## No Analog Found

No planned file lacks a usable analog. There is no existing single test that composes all of Phase 12's four peers, passive-recipient assertion, lifecycle disorder, and Mint consumer. Build that composition from the three test analogs above; do not broaden to the `GeniusNode` harness or reuse its sleep-driven timing.

## Metadata

**Analog search scope:** `test/src/blockchain`, `test/src/crdt`, `test/src/account`, `test/testutil`, `src/blockchain`, `src/account`  
**Files scanned:** 10  
**Pattern extraction date:** 2026-08-25
