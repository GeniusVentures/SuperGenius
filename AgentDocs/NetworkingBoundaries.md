# SuperGenius networking boundaries

The repositories under `../thirdparty` are separate for build and ownership
reasons. They are not general-purpose products. Their public APIs should expose
only the capabilities SuperGenius uses.

## Ownership

| Component | Responsibility | Must not own |
| --- | --- | --- |
| `GeniusNode` | Process composition root; Asio context, worker threads, libp2p scheduler, service startup and shutdown order | libp2p protocol implementation details |
| `ipfs-pubsub::P2pNode` | One libp2p host plus GossipSub, Kademlia DHT, identify, peer bootstrap and address monitoring | An Asio context, scheduler, or production worker thread |
| `libp2p` fork | Transports, connections, peer identity/repository, streams and concrete libp2p protocols | SuperGenius service composition or lifecycle policy |
| `ipfs-bitswap-cpp` | Block exchange over the host supplied by `P2pNode` | A second host or node runtime |
| `ipfs-lite-cpp` | IPLD/CID/Merkle-DAG data structures and the GraphSync code still used by the CRDT path | Process networking lifecycle or SuperGenius database policy |
| SuperGenius CRDT | Delta creation/merge, head tracking, topic selection and synchronization policy | libp2p host construction or RocksDB implementation details |
| RocksDB adapter | Local ordered key/value persistence implementing the datastore operations required by the CRDT/IPLD code | Replication, CRDT semantics or network activity |

`P2pNode` borrows the `io_context` and scheduler from `GeniusNode`. The same
context is injected into libp2p transports and resolvers; the same scheduler is
injected into libp2p protocols and GossipSub. `P2pNode::Stop()` stops its host and
protocols but does not stop the borrowed runtime.

## Data and network flow

```text
GeniusNode
  ├─ owns io_context, workers and scheduler
  ├─ owns P2pNode
  │    ├─ libp2p Host (connections and streams)
  │    ├─ GossipSub (announcements and CRDT messages)
  │    ├─ Kademlia DHT (peer/provider discovery)
  │    └─ identify
  ├─ owns Bitswap (uses the P2pNode host for content blocks)
  └─ owns GraphSync (uses the same host and scheduler for legacy CRDT DAG transfer)

CRDT operation
  -> IPLD-encoded delta and CID
  -> local datastore/RocksDB
  -> CID announcement through GossipSub
  -> missing blocks fetched through the current DAG transfer path
  -> CRDT merge and head update
```

IPLD is the data model and content-addressing layer. libp2p is the networking
toolkit. GossipSub, Kademlia, Bitswap and GraphSync are protocols running over a
libp2p host. “IPFS” is therefore not another layer or object in this codebase;
the project uses selected IPFS-compatible data formats and protocols.

## Phase 0 compatibility contract

Phases 0 and 1 change ownership and naming only. They must not change:

- peer identity/key persistence;
- libp2p protocol identifiers or stream payloads;
- CID, multihash, IPLD or DAG encoding;
- GossipSub topic construction or message serialization;
- CRDT delta semantics, datastore keys or RocksDB contents;
- bootstrap peer and network configuration formats.

The old `GossipPubSub` type name remains a source alias during migration. It is
not a second abstraction or implementation. New composition code uses
`P2pNode` because the object owns more than GossipSub.

## TCP port allocation in tests

Nodes bind `0.0.0.0`, not loopback, so every test process on a machine is
reachable from every other one. libp2p's TCP listener sets `SO_REUSEPORT`
unconditionally (`thirdparty/libp2p/src/transport/tcp/tcp_listener.cpp`), which
means binding a port another live process already holds **succeeds silently**;
the kernel then splits inbound connections between the two listeners by 4-tuple
hash. A port clash is therefore not a loud `EADDRINUSE` — it is intermittent,
confusing cross-talk. Topic names are not namespaced per test either
(`GetNetAndVersionAppendix()` is only version plus network ID), so two processes
that do end up connected will happily exchange CRDT traffic.

The default and correct choice is `port_seed = 0`, which asks the OS for an
ephemeral port. Prefer it. Note that `WriteNetworkConfig`'s second argument is a
*seed*, not a port: a nonzero seed `N` resolves through `GenerateRandomPort` into
`N .. N+300`, keyed by the account address, so a seed reserves a 301-port band and
two nodes with the same address derive the same port.

Only reach for a fixed port when a test must know the port *before* the node
exists. It must then sit **below 32768**, outside the kernel's ephemeral pool
(`/proc/sys/net/ipv4/ip_local_port_range`, typically `32768 60999`) — otherwise
it is drawn from the same pool as every `port_seed = 0` node on the box and the
clash is silent.

| Range | Owner |
| --- | --- |
| 18545-18644 | Anvil RPC, `bridge_race_*` band (`test/src/bridge_e2e/anvil_fixture.hpp`) |
| 18745-18844 | Anvil RPC, `bridge_anvil_e2e_test` band |
| 18945-19044 | Anvil RPC, `bridge_anvil_catchup_e2e_test` band |
| 20000-20300 | `port_seed = 20000` (`test/src/account/network_config_precedence_test.cpp`) |
| 21000 | `genius_node_bootstrap_reconnect_test` bootstrap full node |
| 32768-60999 | kernel ephemeral pool — every `port_seed = 0` node |
| 40001-40301 | production default `port_seed` fallback (`src/account/GeniusNode.cpp`) |

The last two rows overlap deliberately: the production default band sits inside
the ephemeral pool. That is a pre-existing wart, and it is why a fixed *test*
port in the 40000s is unsafe even though it looks reserved.

Add a row here when you allocate a fixed port.
