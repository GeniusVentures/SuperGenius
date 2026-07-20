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
