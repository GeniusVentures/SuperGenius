# Phase 12: Multi-Node Finality Fault Proof - Research

**Researched:** 2026-08-25
**Domain:** C++17 / GoogleTest / CTest production-path finality fault regression
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

### Node topology and ingress

- **D-01:** Use a dedicated in-process harness with four real local peers: three validators and one passive non-validator recipient. Do not use the broad `GeniusNode` integration harness as Phase 12's primary test boundary.
- **D-02:** Each peer has an independent on-disk RocksDB directory. A restart recreates the peer from that same directory, so vote, certificate, transaction, UTXO, and bridge-marker durability are exercised.
- **D-03:** Proposals, votes, certificates, and transaction propagation must all travel through their normal PubSub/CRDT ingress routes. Test code may observe and synchronize those routes, but must not invoke local receive/author shortcuts.

### Fault control and observability

- **D-04:** Create propagation disorder through real local peer connectivity and lifecycle: deliberately start disconnected, connect/reconnect at named barriers, and stop/recreate peers. Do not substitute a mocked transport or direct delivery.
- **D-05:** A narrow test-only barrier may freeze the selected publisher after successful durable `/cert/<slot>` persistence and before the normal PubSub notification, proving the persistence-before-advertisement and failover boundary.
- **D-06:** Restart cases use barriers at actual durable boundaries: after vote persistence, after durable certificate acceptance, and between idempotent Mint effects and bridge-marker persistence. Then stop and recreate the affected peer.
- **D-07:** Add read-only instrumentation at existing production boundaries to count authoritative certificate-write attempts, vote publications, certificate notifications, and Mint effects. Combine those counters with durable-state assertions; observers must not alter control flow.

### Suite structure and proof

- **D-08:** Add a dedicated `multi_node_finality_fault_test` CTest binary with five named scenarios: same-burn contention; late contender plus passive-recipient behavior; restart recovery boundaries; publisher loss/failover; and a full production-route audit that demonstrates TEST-06 across the suite.
- **D-09:** Register the suite as a normal, non-disabled CTest target. It may take up to five minutes in total, but every wait must be bounded and condition-based.
- **D-10:** Each successful scenario proves per-node durable outcome: exactly one application of the exact winning Mint, no losing-Mint effect, and no duplicate UTXO or bridge-executed marker after recovery. Network-wide convergence alone is insufficient.

### the agent's Discretion

- Choose the smallest existing component-level harness shape, port allocation strategy, test-access seams, and scenario partitioning consistent with the locked real-transport and durable-boundary rules.
- Choose bounded per-scenario timeouts within the five-minute total suite budget. Reuse the project wait-condition utilities rather than sleeps.

### Deferred Ideas (OUT OF SCOPE)

### Reviewed Todos (not folded)
- `bridge-startup-wiring-mock-rpc.md` — matched only on generic “node” terms; startup/RPC wiring is unrelated to Phase 12's finality fault proof.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|---|---|---|
| TEST-01 | Differently sourced/proposed mints for one burn contend for one slot and produce one authoritative certificate with one winning proposal. | The dedicated four-peer harness should send both proposals through consensus PubSub while disconnected, then connect the validator mesh; assert the shared `GetSlotID()`, one `/cert/<slot>` value, exact winner, and per-peer durable Mint result. [VERIFIED: codebase inspection] |
| TEST-02 | A late contender after an earlier slot vote or certificate cannot obtain a second usable vote or certificate. | Introduce the losing proposal only after a condition proves the stored active vote or accepted certificate boundary, then assert no second vote publication/record and no second certificate. [VERIFIED: codebase inspection] |
| TEST-03 | PubSub recipients do not write the certificate key or time out synchronizing a CID they wrote themselves. | The passive recipient plus certificate-write/notification counters must prove receive-only handling and eventual durable readback; `CertificateReceived` currently marks journal work rather than writing. [VERIFIED: codebase inspection] |
| TEST-04 | Recovery before certificate arrival, after durable acceptance, and during Mint application keeps the vote and avoids duplicate Mint. | Recreate the affected peer from its unchanged RocksDB directory at the three existing durable seams, then replay only normal propagation/recovery and assert exact persisted outcomes. [VERIFIED: codebase inspection] |
| TEST-05 | Publisher loss proves persistence-before-advertisement and deterministic failover without conflicting slot records. | Freeze only the selected publisher after `PutConvergentImmutable` succeeds and before `Publish`, stop it, advance/observe normal round rotation, reconnect the successor, and assert one immutable record. [VERIFIED: codebase inspection] |
| TEST-06 | Tests exercise production PubSub, CRDT, persistence, and Mint ingress rather than direct local-author shortcuts. | The harness must call public proposal submission/transport startup paths and verify read-only boundary counters; no scenario may call `Handle*`, `CertificateReceived`, `OnConsensusCertificate`, direct `GlobalDB::Put`, or forced timer methods to create protocol data. [VERIFIED: codebase inspection] |
</phase_requirements>

## Summary

Phase 12 is a focused integration-test phase, not a protocol or dependency phase. Build one `multi_node_finality_fault_test` GoogleTest binary around a reusable four-peer component fixture: three registry validators plus one non-validator consumer, each with its own real `GossipPubSub`, `GlobalDB`, account/transaction consumption wiring, and persistent RocksDB path. The nearest existing fixture already constructs independent local PubSub, `GlobalDB`, registries, accounts, and `ConsensusManager` instances; the CRDT integration fixture demonstrates real peer connection and per-node IO-thread cleanup. [VERIFIED: codebase inspection]

The proof must be event-driven. Existing `ASSERT_WAIT_FOR_CONDITION` checks a predicate every 10 ms with a bounded timeout, while the older multi-peer CRDT fixture still uses an unconditional sleep after `AddPeers`; reuse the former and do not copy the latter. [VERIFIED: codebase inspection] The test harness may add friend-scoped hooks only at the exact durable boundaries selected in context, and those hooks must wait/record rather than deliver, create, filter, alter, or retry protocol messages. [VERIFIED: codebase inspection]

**Primary recommendation:** Create a small persistent four-peer `FinalityFaultNetwork` fixture under `test/src/blockchain/`, add read-only test counters plus three narrow barriers in existing production boundary code, and drive every scenario through actual peer connection, PubSub, CRDT recovery, and restart.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|---|---|---|---|
| Proposal and vote propagation | API / Backend | CDN / Static — | `ConsensusManager` subscribes to and publishes consensus messages through `GossipPubSub`; test code only starts and connects peers. [VERIFIED: codebase inspection] |
| Authoritative certificate persistence and failover | Database / Storage | API / Backend | `/cert/<slot>` is written through `GlobalDB::PutConvergentImmutable`; consensus validates and selects the publisher before that persistence boundary. [VERIFIED: codebase inspection] |
| Certificate notification and durable recovery | API / Backend | Database / Storage | PubSub notification is best-effort after persistence; callback receipt marks work stalled and recovery rereads the durable record. [VERIFIED: codebase inspection] |
| Exact-Mint application and restart safety | API / Backend | Database / Storage | `TransactionManager` accepts a bound certificate, writes idempotent UTXOs, then persists the bridge marker. [VERIFIED: codebase inspection] |
| Fault scheduling and observations | Test harness | API / Backend / Database | Test-only barriers observe real boundaries; lifecycle operations manipulate real peer connectivity and persistent node recreation. [VERIFIED: codebase inspection] |

## Standard Stack

### Core

| Library / Component | Version | Purpose | Why Standard |
|---|---|---|---|
| Project C++17 test stack: GoogleTest + CTest | repository-configured | Five named test cases and normal CTest registration. | Existing blockchain targets use `addtest(...)`; the project has CTest 3.31.4 available. [VERIFIED: codebase inspection] |
| `GossipPubSub` | repository implementation | Real local proposal, vote, and certificate PubSub routes. | It is the production transport injected into `ConsensusManager`; existing multi-peer tests start peers and connect them with `AddPeers`. [VERIFIED: codebase inspection] |
| `crdt::GlobalDB` + RocksDB datastore | repository implementation | Per-peer durable CRDT certificate, transaction, UTXO, vote, and marker state. | The existing consensus fixture builds each node with `GlobalDB::New(..., path + "/rocksdb", ...)`; the context locks separate directories and reuse on restart. [VERIFIED: codebase inspection] |
| `ConsensusManager`, `TransactionManager`, `UTXOManager` | repository implementation | Production finality and Mint consumption under test. | They contain the persisted-vote, certificate, recovery, idempotent-UTXO, and bridge-marker boundaries required by TEST-01–05. [VERIFIED: codebase inspection] |

### Supporting

| Library / Component | Version | Purpose | When to Use |
|---|---|---|---|
| `test/testutil/wait_condition.hpp` | repository implementation | Bounded condition waits. | Use for every network, barrier, recovery, and convergence wait; do not use sleeps for test synchronization. [VERIFIED: codebase inspection] |
| `MemorySecureStorage` | repository implementation | Isolated signing-account setup. | Reuse the consensus lifecycle fixture’s test setup so account keys do not couple peers to a shared persistent keystore. [VERIFIED: codebase inspection] |
| `KeyPairFileStorage` | repository implementation | Stable PubSub identity across a stopped/recreated peer. | Store each peer’s keypair below its peer directory and reopen that directory on restart. [VERIFIED: codebase inspection] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|---|---|---|
| Dedicated component-level four-peer fixture | Broad `GeniusNode` network fixture | The broad fixture already connects and operates full nodes, but it expands startup/RPC scope and conflicts with locked D-01 as the primary test boundary. [VERIFIED: codebase inspection] |
| Real local PubSub/CRDT transport | Mock transport or direct consensus handler calls | Direct calls would make ordering deterministic but violate D-03/D-04/TEST-06 and cannot prove ingress behavior. [VERIFIED: codebase inspection] |
| Condition barriers at durable boundaries | Fixed sleeps | Sleeps do not establish durable state and make delayed local networking flaky; bounded predicates provide actionable timeout failures. [VERIFIED: codebase inspection] |

**Installation:** None. This phase adds no external package. [VERIFIED: codebase inspection]

## Architecture Patterns

### System Architecture Diagram

```text
  competing Mint proposals                 late Mint proposal
             |                                      |
             v                                      v
  Validator A/B/C: real GossipPubSub <- start disconnected -> named AddPeers barrier
             |        |                                      |
             |        +---- real PubSub proposals/votes ------+
             v
  ConsensusManager: durable active vote -> quorum -> selected publisher
             |                                      |
             |                                      +--[barrier after durable write]-- stop publisher
             v
  GlobalDB / RocksDB: /cert/<canonical-slot>  <---- successor normal round/failover
             |                 |
             |                 +---- CRDT replication ----> passive recipient
             v
  certificate callback marks stalled -> durable readback/recovery -> TransactionManager
                                                               |
                                                               v
                                   idempotent UTXOs -> bridge-executed marker -> confirmed
                                                               |
                                                               v
                          restart peer from same RocksDB path and re-check durable result
```

The diagram describes existing production ownership plus the locked test barriers. It intentionally has no direct test-to-handler or test-to-datastore write edge. [VERIFIED: codebase inspection]

### Recommended Project Structure

```text
test/src/blockchain/
├── multi_node_finality_fault_test.cpp  # Four-peer fixture, five scenarios, read-only assertions
└── CMakeLists.txt                      # Normal addtest registration and existing link pattern

src/blockchain/Consensus.{hpp,cpp}      # Friend-scoped publisher/vote/certificate test observation/barriers
src/account/TransactionManager.{hpp,cpp}# Friend-scoped Mint-effect observation/barrier
src/account/UTXOManager.{hpp,cpp}       # Reuse existing durable UTXO boundary hook if sufficient
```

Keep the harness in `test/src/blockchain`: it is the existing home of the closest multi-validator lifecycle fixture and CMake target. [VERIFIED: codebase inspection]

### Pattern 1: Persistent component-level peer object

**What:** Give every peer an owned root path, PubSub identity, IO context/thread, PubSub instance, `GlobalDB`, account, registry/consensus wiring, and transaction-consumption wiring; make `StopPeer` destroy runtime objects without deleting that root, and make `RestartPeer` reconstruct them from the same root. [VERIFIED: codebase inspection]

**When to use:** Use for all five scenarios; it is the only way to prove a restart observes durable vote, certificate, UTXO, and bridge-marker state. [VERIFIED: codebase inspection]

**Example:**

```cpp
// Source: test/src/blockchain/consensus_pending_lifecycle_test.cpp
node.db = GlobalDB::New(node.io, path + "/rocksdb", node.pubsub,
                        CrdtOptions::DefaultOptions(), graphsync, scheduler, generator).value();
node.db->Start();
node.manager = ConsensusManager::New(node.registry, node.db, node.pubsub, signer, address);

// Phase 12 shape: StopPeer resets manager/transaction services/db/pubsub/io but preserves path.
// RestartPeer rebuilds the same composition then rejoins through AddPeers at a named barrier.
```

### Pattern 2: Two-layer proof — boundary counters plus durable-state readback

**What:** Count the real boundary once it is crossed, then independently assert stored `/cert/<slot>`, active-vote record, UTXOs, and `/bridge/executed/<chain>:<source>` after restart. [VERIFIED: codebase inspection]

**When to use:** Use every time a test claims “exactly once,” “receiver did not write,” “persisted before notification,” or “no duplicate after crash.” A network-wide final value is not sufficient. [VERIFIED: codebase inspection]

**Example:**

```cpp
// Source boundaries: src/blockchain/Consensus.cpp and src/account/TransactionManager.cpp
ASSERT_WAIT_FOR_CONDITION([&] { return network.AllPeersHaveWinner(slot, winner_hash); },
                          std::chrono::seconds(10), "all peers durably accepted the winner", nullptr);
for (auto &peer : network.peers()) {
  peer.AssertSingleDurableMint(winner_hash);  // UTXO + bridge marker + no loser
  network.RestartPeer(peer.index());
  peer.AssertSingleDurableMint(winner_hash);  // persistent state, not just memory
}
```

### Pattern 3: Barrier blocks the caller after its real action

**What:** The production function executes the durable action first, increments a read-only counter, then optionally waits on a test-owned condition variable before continuing to its normal next production action. [VERIFIED: codebase inspection]

**When to use:** Only for (1) active-vote persistence before outbound vote publication, (2) accepted certificate durable-readback before consumer dispatch, (3) `PutConvergentImmutable` success before certificate notification, and (4) idempotent Mint UTXO effects before marker persistence. [VERIFIED: codebase inspection]

**Example:**

```cpp
// Source pattern: existing friend-only barrier hooks in UTXOManager and TransactionManager.
PersistCertificate(...);             // existing production operation
test_observer_.certificate_writes++; // observation only
test_barrier_.WaitIfArmed();          // test-only synchronization, no changed result
return Publish(message);              // existing production route continues unchanged
```

### Scenario partition (required five tests)

| Test name | Drive | Required assertions |
|---|---|---|
| `SameBurnContentionUsesOneCanonicalSlotAndExactMint` | Start validator peers disconnected; submit different real Mint proposals for one burn; connect the validator mesh. | All validator votes select one winner; one durable `/cert/<slot>`; all four peers have one exact winner Mint and no losing Mint. [VERIFIED: codebase inspection] |
| `LateContenderAndPassiveRecipientRemainReceiveOnly` | Admit the loser after the first durable vote, then again after certificate finality; connect passive recipient only through real peers. | No second usable vote/certificate; passive peer makes zero authoritative write attempts; it receives notification/recovery and completes without CID self-sync timeout. [VERIFIED: codebase inspection] |
| `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` | Run three subcases with stop/recreate at vote persistence, accepted certificate, and UTXO-before-marker barriers. | Restarted peer publishes only stored vote when applicable; certificate recovery converges; output/marker are each durable exactly once after replay. [VERIFIED: codebase inspection] |
| `PublisherLossAfterPersistenceUsesDeterministicFailover` | Freeze selected publisher after immutable write and before notification, stop it, let a later eligible real round continue, then reconnect. | Write counter precedes notification; successor uses normal authority and cannot create a conflicting value; all peers converge and Mint once. [VERIFIED: codebase inspection] |
| `ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress` | Trace one complete network run through public production entry points and all instrumentation. | Counters prove proposal/vote/certificate notification/Mint routes crossed; no forbidden local-author/receive or direct datastore helper is called; durable post-restart assertions still hold. [VERIFIED: codebase inspection] |

### Anti-Patterns to Avoid

- **Reusing `ConsensusPendingLifecycleTestAccess` to inject proposals, votes, certificates, or callback delivery:** Its helpers intentionally call private lifecycle and handler methods for component tests; using them to make Phase 12 protocol data violates D-03 and TEST-06. [VERIFIED: codebase inspection]
- **Writing `/cert/<slot>` from test code:** It would bypass selected-publisher authority, CRDT filtering, and persistence-before-notification proof. [VERIFIED: codebase inspection]
- **Mocking PubSub, manually invoking `CertificateReceived`, or calling `OnConsensusCertificate`:** Those routes appear in focused existing tests, but they bypass real network ingress and are invalid as Phase 12 driving mechanisms. [VERIFIED: codebase inspection]
- **Copying `connectNodes`' `sleep_for`:** That CRDT fixture’s one-second delay is not a durable condition; use the project wait utility with a specific connection/replication predicate. [VERIFIED: codebase inspection]
- **Deleting a peer’s directory while simulating restart:** That converts restart proof into a fresh-node test and loses the persistent-state contract. [VERIFIED: codebase inspection]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---|---|---|---|
| Local network simulation | Fake message bus or mocked CRDT transport | Existing `GossipPubSub` + `GlobalDB` construction and `AddPeers` topology | The requirement is specifically to prove production PubSub/CRDT ingress. [VERIFIED: codebase inspection] |
| Async waiting | Polling loops with sleeps | `ASSERT_WAIT_FOR_CONDITION` / `waitForCondition` | It bounds waits and reports the predicate that did not become true. [VERIFIED: codebase inspection] |
| Exactly-once Mint detector | Parallel shadow journal | Existing UTXO outpoint idempotence plus `/bridge/executed/...` marker inspection | The Phase 11 contract already defines UTXO-before-marker durability as the recovery proof. [VERIFIED: codebase inspection] |
| Certificate collision policy | Test-owned winner selection | Production `PutConvergentImmutable` and serialized-SHA-256 ordering | Test logic must observe the selected authority rather than reimplement it. [VERIFIED: codebase inspection] |
| Publisher failover mechanism | Test-selected replacement publisher | Existing round-derived `GetAggregatorRole` and normal `ProcessCertificates` loop | Context locks deterministic protocol-visible rotation as the failover rule. [VERIFIED: codebase inspection] |

**Key insight:** The harness should control only timing, membership, and lifecycle; all correctness decisions must remain in production consensus, CRDT, and Mint code. [VERIFIED: codebase inspection]

## Common Pitfalls

### Pitfall 1: A test “passes” using private handlers

**What goes wrong:** A direct call can create a valid final state without exercising subscriptions, callback timing, CRDT commit, or normal recovery. [VERIFIED: codebase inspection]

**Why it happens:** Existing focused fixtures expose friend helpers such as `HandleCertificate`, `CertificateReceived`, and direct datastore writes specifically to isolate unit behavior. [VERIFIED: codebase inspection]

**How to avoid:** Limit new accessors to arming/releasing barriers and reading counters/state; drive external protocol inputs through actual PubSub/CRDT/public transaction entry points only. [VERIFIED: codebase inspection]

**Warning signs:** The test body calls a `*TestAccess::Handle*`, `OnConsensusCertificate`, `CertificateReceived`, `FilterCertificate`, `GlobalDB::Put*`, or force-due method to establish its main scenario state. [VERIFIED: codebase inspection]

### Pitfall 2: Reconnect is assumed rather than observed

**What goes wrong:** `AddPeers` is asynchronous; work may run before peers have propagated subscriptions or CRDT state. [VERIFIED: codebase inspection]

**Why it happens:** The available integration helper calls `AddPeers` and then sleeps for one second instead of awaiting a condition. [VERIFIED: codebase inspection]

**How to avoid:** Start isolated peers, apply `AddPeers` only at named topology barriers, then wait for a harmless observable condition such as registry availability, a test-topic notification counter, or the target durable record—not elapsed time. [VERIFIED: codebase inspection]

**Warning signs:** A test contains `sleep_for`, accepts “eventually” without a predicate, or hardcodes successful connection timing. [VERIFIED: codebase inspection]

### Pitfall 3: Restart leaves live objects or port ownership behind

**What goes wrong:** Reconstructing a peer can retain a manager/timer, IO thread, PubSub host, or old port and produce duplicate callbacks or bind failures. [VERIFIED: codebase inspection]

**Why it happens:** `ConsensusManager` owns a round timer and PubSub/GlobalDB are asynchronous services; the CRDT fixture explicitly stops its IO thread and PubSub in teardown. [VERIFIED: codebase inspection]

**How to avoid:** Implement one idempotent `StopPeer` order, join the peer IO thread, release dependent managers before PubSub, preserve only the directory/keypair, and allocate each peer a deterministic unique port. [VERIFIED: codebase inspection]

**Warning signs:** Port conflicts, callbacks after a node is stopped, a second Mint counter increment before restart, or a test that passes only alone. [VERIFIED: codebase inspection]

### Pitfall 4: Persist-before-notification proof freezes the wrong point

**What goes wrong:** Pausing before persistence does not prove D-05; pausing after notification cannot prove the loss window. [VERIFIED: codebase inspection]

**Why it happens:** `SubmitCertificate` currently performs `PutConvergentImmutable` then constructs/publishes the PubSub message in the same method. [VERIFIED: codebase inspection]

**How to avoid:** Place the hook immediately after successful `PutConvergentImmutable` and before `Publish`; record write and notification counters on either side. [VERIFIED: codebase inspection]

**Warning signs:** The test cannot demonstrate the durable key exists while the notification count is still zero. [VERIFIED: codebase inspection]

### Pitfall 5: “Exactly once” is checked only in memory

**What goes wrong:** An in-memory transaction status can look correct while a restart replays an effect or a missing bridge marker leaves work incomplete. [VERIFIED: codebase inspection]

**Why it happens:** Mint completion spans UTXO persistence first and bridge-marker persistence second, and Phase 11 intentionally relies on replay across that boundary. [VERIFIED: codebase inspection]

**How to avoid:** For every peer, assert the exact winner’s UTXO/outpoints, absence of losing-Mint outputs, exactly one Mint-effect counter, and exactly one durable bridge marker before and after reopening its directory. [VERIFIED: codebase inspection]

**Warning signs:** Assertions stop at certificate convergence or `CONFIRMED` state without reopening storage. [VERIFIED: codebase inspection]

## Code Examples

Verified patterns from the codebase:

### Bounded condition wait

```cpp
// Source: test/testutil/wait_condition.hpp
ASSERT_WAIT_FOR_CONDITION(
    [&] { return peer.HasDurableCertificate(slot) && peer.HasExactWinnerMint(winner_hash); },
    std::chrono::seconds(10),
    "peer accepted exact certificate and minted winner",
    nullptr);
```

### Real peer construction and connection

```cpp
// Source: test/src/blockchain/consensus_pending_lifecycle_test.cpp
node.pubsub = std::make_shared<GossipPubSub>(keypair.value());
ASSERT_FALSE(node.pubsub->Start(port, {node.pubsub->GetLocalAddress()}).get());
node.db = GlobalDB::New(node.io, path + "/rocksdb", node.pubsub,
                        CrdtOptions::DefaultOptions(), graphsync, scheduler, generator).value();
node.db->Start();

// Source: test/src/crdt/globaldb_integration.cpp
node_a.pubsub->AddPeers({node_b.pubsub->GetInterfaceAddress()});
```

### Production certificate write-before-notification boundary

```cpp
// Source: src/blockchain/Consensus.cpp
auto cert_put = db_->PutConvergentImmutable(cert_key, cert_value, {consensus_datastore_topic_});
if (cert_put.has_error()) return outcome::failure(cert_put.error());

// Phase 12 test-only observer/barrier belongs here.
ConsensusMessage message;
*message.mutable_certificate() = certificate;
return Publish(message);
```

### Mint durability boundary

```cpp
// Source: Phase 11 verified behavior and src/account/TransactionManager.cpp
// Normal certificate ingress reaches TransactionManager; test code does not call it directly.
// The barrier is released only after idempotent UTXO effects and before marker persistence.
ASSERT_WAIT_FOR_CONDITION([&] { return peer.HasDurableWinnerUtxos(winner_hash); }, std::chrono::seconds(10),
                          "UTXO effects persisted", nullptr);
network.StopPeer(peer_index);
network.RestartPeer(peer_index);
ASSERT_TRUE(network.Peer(peer_index).HasExactlyOneBridgeMarker(winner));
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|---|---|---|---|
| Focused lifecycle tests directly invoke friend-access consensus ingress and datastore helpers. | Phase 12 must use those access seams only for observing/blocking while real PubSub/CRDT ingress drives protocol behavior. | Phase 12 locked context, 2026-08-24. [VERIFIED: codebase inspection] | The final suite can prove production routing rather than only unit semantics. |
| Receiver callback could be mistaken for committed authority. | Certificate callback marks work stalled; durable readback/recovery is the authorization/dispatch route. | Verified in Phase 11, 2026-08-24. [VERIFIED: codebase inspection] | Tests must wait for committed results, not infer finality from callback receipt. |
| Tests could model bridge Mint recovery with forced write failures. | Phase 12 uses stop/recreate at the same real durable boundaries, preserving production recovery behavior. | Phase 12 locked context, 2026-08-24. [VERIFIED: codebase inspection] | Fault proof covers actual persisted state rather than a mock-only failure mode. |

**Deprecated/outdated:**

- The sleep-based synchronization in `GlobalDBIntegrationTest::TestNodeCollection::connectNodes` is unsuitable for this suite; use bounded condition waits. [VERIFIED: codebase inspection]
- The broad `GeniusNode` integration fixture is not the Phase 12 primary harness by locked D-01. [VERIFIED: codebase inspection]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|---|---|---|
| A1 | `GossipPubSub` exposes no repository-visible public peer-disconnect method beyond stopping/recreating a peer; verify the external dependency API before adding an independent transient-disconnect helper. | Open Questions | A plan could choose an unavailable API or accidentally replace real lifecycle disconnection with a mock. |
| A2 | The smallest four-peer fixture can compose `TransactionManager`/`Blockchain` consumer wiring without using `GeniusNode`; confirm current constructor/wiring requirements while implementing the first harness task. | Architecture Patterns | The harness may need one additional existing component fixture or a narrow test factory. |

## Open Questions

1. **Which public real-transport API should create an in-place disconnection without destroying a peer?**
   - What we know: Repository usage exposes `AddPeers` for connection and `Stop` for lifecycle loss; no repository-visible `Disconnect`/`RemovePeer` call was found. [VERIFIED: codebase inspection]
   - What's unclear: The external `GossipPubSub`/libp2p dependency may expose a host-level disconnect API that is not vendored in this repository. [ASSUMED]
   - Recommendation: Inspect the installed dependency headers before implementation. If no supported API exists, model loss by actual `StopPeer` and model reconnection by `RestartPeer` plus `AddPeers`, which preserves D-04’s real lifecycle constraint; do not invent a mock. [VERIFIED: codebase inspection]

2. **Where should the cross-scenario TEST-06 audit evidence live?**
   - What we know: D-08 requires a fifth named audit scenario, while individual GoogleTest cases must remain independently runnable. [VERIFIED: codebase inspection]
   - What's unclear: A counter aggregate cannot safely depend on test execution order. [ASSUMED]
   - Recommendation: Make the fifth test self-contained and make every other scenario assert its own route counters; the fifth test is the explicit trace/audit rather than a post-suite accumulator. [ASSUMED]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|---|---|---:|---|---|
| CMake | Configure/build target | ✓ | 3.31.4 | — [VERIFIED: local environment] |
| CTest | Normal-suite registration/run | ✓ | 3.31.4 | — [VERIFIED: local environment] |
| Ninja | Existing build backend | ✓ | 1.13.0 | make is also available. [VERIFIED: local environment] |
| Apple Clang | C++17 test compilation | ✓ | 16.0.0 | — [VERIFIED: local environment] |
| Existing `consensus_pending_lifecycle_test` binary | Nearest focused regression baseline | ✓ | Present at `build/OSX/Release/consensus_pending_lifecycle_test` | Rebuild through CMake if stale. [VERIFIED: local environment] |
| New external package | Phase implementation | Not required | — | No install. [VERIFIED: codebase inspection] |

**Missing dependencies with no fallback:** None identified. [VERIFIED: local environment]

**Missing dependencies with fallback:** No external package/tool is required; the PubSub disconnect API remains an implementation-time header inspection, not an install dependency. [VERIFIED: codebase inspection]

## Validation Architecture

### Test Framework

| Property | Value |
|---|---|
| Framework | GoogleTest registered through the project `addtest` helper and CTest. [VERIFIED: codebase inspection] |
| Config file | CMake target definitions in `test/src/blockchain/CMakeLists.txt`; no separate GoogleTest config found. [VERIFIED: codebase inspection] |
| Quick run command | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` [ASSUMED] |
| Full suite command | `ctest --test-dir build/OSX/Release --output-on-failure` [VERIFIED: local environment] |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|---|---|---|---|---|
| TEST-01 | Same burn → one slot/certificate/exact winner across four peers | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |
| TEST-02 | Late contender cannot produce a second usable vote/certificate | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |
| TEST-03 | Passive PubSub recipient is receive-only and makes progress | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |
| TEST-04 | Vote/certificate/Mint-boundary restart recovery is duplicate-safe | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |
| TEST-05 | Persisted publisher loss has deterministic safe failover | integration | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |
| TEST-06 | All protocol data crosses production PubSub, CRDT, persistence, and Mint ingress | integration/audit | `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** Build the target, then run `ctest --test-dir build/OSX/Release -R '^multi_node_finality_fault_test$' --output-on-failure`. [ASSUMED]
- **Per wave merge:** Run the new target plus `consensus_pending_lifecycle_test` and `transaction_manager_certificate_fallback_test`. [VERIFIED: codebase inspection]
- **Phase gate:** Full CTest suite green before `$gsd-verify-work`. [VERIFIED: configuration inspection]

### Wave 0 Gaps

- [ ] `test/src/blockchain/multi_node_finality_fault_test.cpp` — reusable persistent four-peer fixture and five named scenarios covering TEST-01–TEST-06. [VERIFIED: codebase inspection]
- [ ] `test/src/blockchain/CMakeLists.txt` — normal `addtest(multi_node_finality_fault_test ...)`, link set mirroring `consensus_pending_lifecycle_test`, and CTest timeout no greater than 300 seconds. [VERIFIED: codebase inspection]
- [ ] Friend-scoped test observer/barrier declarations and implementations at selected consensus/transaction boundaries; reuse the existing UTXO hook where it matches the Mint gap. [VERIFIED: codebase inspection]

## Security Domain

Security enforcement is enabled in `.planning/config.json`; this phase must preserve the existing finality safety boundary while adding test-only observation. [VERIFIED: configuration and codebase inspection]

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---|---|---|
| V2 Authentication | No | The suite does not add user authentication; validator identity/signatures remain production behavior. [VERIFIED: codebase inspection] |
| V3 Session Management | No | No browser or user session state is added. [VERIFIED: codebase inspection] |
| V4 Access Control | Yes | Only friend-scoped test access may arm/read hooks; no public production API for fault control. [VERIFIED: codebase inspection] |
| V5 Input Validation | Yes | Production proposal, vote, certificate key/binding, and exact-Mint validation must remain on every real ingress path. [VERIFIED: codebase inspection] |
| V6 Cryptography | Yes | Reuse existing validator signing and serialized SHA-256 certificate ordering; do not implement cryptography in tests. [VERIFIED: codebase inspection] |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---|---|---|
| Test helper bypasses finality authorization | Elevation of Privilege / Tampering | Helpers only wait/count; public production routes create all protocol data. [VERIFIED: codebase inspection] |
| Receiver writes an authoritative certificate | Tampering | Assert zero receiver writes and rely on selected-publisher `SubmitCertificate` plus immutable CRDT storage. [VERIFIED: codebase inspection] |
| Crash duplicates Mint effect | Tampering / Denial of Service | Restart through UTXO-before-marker boundary and assert persisted idempotent outpoints plus one marker. [VERIFIED: codebase inspection] |
| Late contender forks finality | Tampering | Assert one persisted active vote, canonical-slot lock, exact certificate validation, and no losing Mint after real transport delivery. [VERIFIED: codebase inspection] |

## Sources

### Primary (HIGH confidence)

- `.planning/phases/12-multi-node-finality-fault-proof/12-CONTEXT.md` — locked topology, ingress, barrier, observability, suite, and timeout decisions.
- `.planning/REQUIREMENTS.md` — TEST-01 through TEST-06 acceptance requirements.
- `.planning/ROADMAP.md` — Phase 12 goal and success criteria.
- `.planning/phases/11-convergent-certificate-consumption-mint-recovery/11-VERIFICATION.md` — verified preceding-phase recovery boundaries and multi-node coverage gap.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — closest three-validator construction, RocksDB path, account/registry/consensus setup, and existing private test seams.
- `test/src/crdt/globaldb_integration.cpp` — actual local multi-peer PubSub/GlobalDB startup, peer connection, and teardown pattern.
- `test/testutil/wait_condition.hpp` — bounded condition waiting behavior.
- `src/blockchain/Consensus.cpp`, `src/blockchain/Consensus.hpp` — vote persistence, certificate immutable write, notification ordering, callback/recovery behavior, and round failover ownership.
- `src/account/TransactionManager.cpp`, `src/account/TransactionManager.hpp`, `src/account/UTXOManager.hpp` — Mint and bridge-marker durability boundaries plus existing friend-only barrier patterns.

### Secondary (MEDIUM confidence)

- Local environment probes — CMake/CTest/Ninja/compiler availability and existing target presence.

### Tertiary (LOW confidence)

- None. The two explicit implementation assumptions are listed in the Assumptions Log rather than treated as sources.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — the phase reuses repository C++ test, PubSub, CRDT, persistence, and finality components; no new library selection is proposed. [VERIFIED: codebase inspection]
- Architecture: HIGH — locked context directly specifies the topology and fault boundaries, and source inspection identifies the closest fixture and production edges. [VERIFIED: codebase inspection]
- Pitfalls: HIGH — all listed pitfalls are evidenced by existing focused-test shortcuts, asynchronous connection code, or the actual durable ordering. [VERIFIED: codebase inspection]

**Research date:** 2026-08-25
**Valid until:** 2026-09-24 — stable internal C++ codebase, unless Phase 12 predecessor code changes. [ASSUMED]
