# Phase 15: Private Network Identity and libp2p Gating - Context

**Gathered:** 2026-08-31
**Status:** Ready for planning (implementation blocked by libp2p#10 and the trusted-peer production-closeout branch)

<domain>
## Phase Boundary

Deliver private-network identity, scoped job routing, and connection enforcement without conflating global trust with private-network governance. A private network is identified by the license NFT's public `privateNetworkId`, configured offline in each node's `network_config.json` with its secret pnet credential. SecureCRDT-backed data must use an explicit peer-registry authority selected per key: the global TrustedPeerRegistry is the root trust domain, while a new child NetworkRegistry owns membership for a particular private network.

Public and private jobs coexist. A public job uses scope `0` and the current public CRDT paths/topics; a private job uses exactly one nonzero `privateNetworkId`, and its task, escrow, result, payout, topic, and consensus artifacts must all use that same private scope. The phase also establishes separate ValidatorRegistry state/quorum for each private network while preserving the public ValidatorRegistry.

</domain>

<decisions>
## Implementation Decisions

### Network identity and offline provisioning
- **D-01:** `network_config.json`, not `sgns_config.json`, is the source of private-network configuration. The application owner provisions it offline before node startup with both `private_network_id` (the public Ed25519 identity from the license NFT) and `network_key` (the secret pnet credential).
- **D-02:** `private_network_id` and `network_key` are intentionally different values. The public ID drives identity, CRDT paths, chain IDs, and topic names; the secret key drives pnet. Nodes must not query the NFT or retrieve the pnet secret at runtime.
- **D-03:** The raw pnet key is never stored in a CRDT record. A registry may store only non-secret key metadata such as a version or fingerprint; provisioning and rotation of the raw secret remain offline operational work.

### Peer-registry hierarchy and SecureCRDT authorization
- **D-04:** Introduce an explicit `PeerRegistry` base abstraction that resolves the current authorized peer set and quorum. SecureCRDT policy entries associate each registered key/pattern with the appropriate `PeerRegistry` instance; SecureCrdt itself remains generic and must not assume TrustedPeerRegistry globally.
- **D-05:** `TrustedPeerRegistry` remains one global root trust domain. There are no per-private-network TrustedPeerRegistry instances, and ordinary private workers need not become global trusted peers.
- **D-06:** Add a SecureCRDT-backed `NetworkRegistry` child for each `privateNetworkId`. Its bootstrap record is signed by a majority of the global TrustedPeerRegistry. Once confirmed, the NetworkRegistry resolves its own cached current peer set and quorum, so later membership changes are signed by its current network peers; a single peer may never admit itself unilaterally.
- **D-07:** NetworkRegistry membership is the private-network allow-list consulted during connection upgrade in addition to pnet protection. Pnet proves possession of the private credential; NetworkRegistry determines whether that peer is authorized for the selected private network.

### Scoped work and consensus
- **D-08:** Job scope is explicit rather than inherited blindly from a node. Public jobs use scope `0` and retain current public routing. Private jobs use one `privateNetworkId`; every job-derived task, escrow, result, payout, CRDT key, and pubsub topic follows that same scope. Do not prefix unrelated global artifacts indiscriminately.
- **D-09:** ValidatorRegistry is separate for the public network and every private network. It supplies consensus only for its own scope and does not authorize NetworkRegistry updates; SecureCRDT authorization is handled by the selected PeerRegistry.

### Dependencies and enforcement surface
- **D-10:** Do not duplicate or replace the in-progress trusted-peer production work. Phase 15 implementation waits for `GeniusVentures/libp2p#10` (gater + pnet) and `gsd/phase-13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production` to land.
- **D-11:** Bind the same private-network enforcement to both host construction paths: the GossipPubSub host and the independent processing host. The processing host must not offer Plaintext security.

### the agent's Discretion
- Exact `PeerRegistry` interface shape, ownership/lifetime strategy, and whether SecureCrdtRegistry stores a shared or weak registry reference, provided the per-key association remains explicit and safe.
- Exact NetworkRegistry payload schema, key-pattern layout, cached-state implementation, and safe quorum-threshold configuration, provided bootstrap uses global TrustedPeerRegistry majority and later updates cannot be unilateral.
- Exact non-secret pnet metadata retained in NetworkRegistry and the offline rotation procedure.
- Exact job-scope field encoding and the migration strategy for existing public jobs, provided public scope remains compatible and private job artifacts never cross scopes.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Private-network design and issue history
- `.planning/notes/private-network-identity.md` — authoritative SuperGenius-side identity decision: `privateNetworkId` is a public Ed25519 key, `0` is public, and private connection gating protects all gossip topics.
- `.planning/seeds/sg-node-consumes-privateNetworkId.md` — original cross-cutting seed for config, CRDT namespace, topic, and chain-ID propagation.
- `.planning/spikes/001-libp2p-private-network-feasibility/README.md` — host attach points, two-host finding, and security-adaptor feasibility evidence.
- `.planning/spikes/002-upstream-feature-gap/README.md` — explains why gater/pnet is new fork work and why `GeniusVentures/libp2p#10` is a hard dependency.
- `https://github.com/GeniusVentures/SuperGenius/issues/367` — Phase 15 issue and acceptance criteria; read the issue comments for the registry-hierarchy clarification dated 2026-08-31.

### SecureCRDT and peer-registry precedent
- `src/securecrdt/SecureCrdtRegistry.hpp` — existing per-key `SignerSetSource` policy mechanism; evolve this to an explicit PeerRegistry association rather than hard-wiring TrustedPeerRegistry.
- `src/securecrdt/SecureCrdt.hpp` — generic SecureCRDT write/read/filter enforcement contract.
- `src/trustedpeer/TrustedPeerRegistry.hpp` and `src/trustedpeer/TrustedPeerRegistry.cpp` — root registry's cache, bootstrap-to-self-governance, SecureCRDT registration, and quorum precedent.
- `src/securecrdt/QuorumThresholdValidation.hpp` — existing safety-floor validation that must continue to protect quorum-backed registries.

### Networking, job routing, and consensus integration
- `src/account/GeniusNode.cpp:1213-1513` and `src/account/GeniusNode.hpp:951-1058` — `network_config.json` parsing and existing `network_key` pnet state; add the distinct public private-network identity here.
- `example/node_test/network_config.json` — test/example configuration to extend.
- `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pub_sub.cpp:175-189` — GossipPubSub host injector enforcement point.
- `src/processing/impl/processing_core_impl.cpp:96` — independent processing host that must receive equivalent gating and no Plaintext security.
- `src/crdt/hierarchical_key.hpp` — top-level key-namespace machinery for public/private job scope.
- `src/account/GeniusTransaction.hpp:36` and `src/account/TransactionManager.hpp:781` — hardcoded chain identifiers that must become scope-aware for private jobs while preserving public behavior.
- `src/blockchain/ValidatorRegistry.hpp` and `src/blockchain/ValidatorRegistry.cpp` — existing public validator-consensus implementation to isolate by network scope; do not confuse it with SecureCRDT authorization.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `SecureCrdtRegistry` already resolves authorization policy by registered key pattern; its callback-based signer-set source is the direct foundation for PeerRegistry-backed policies.
- `TrustedPeerRegistry` already owns a cached peer set, resolves a bootstrap signer source before confirmation, and resolves a self-governing peer set afterwards; NetworkRegistry should adapt this lifecycle rather than invent another CRDT protocol.
- `GeniusNode::LoadNetworkConfig` already parses network transport settings and retains `network_key_`; it is the natural configuration surface for `private_network_id`.

### Established Patterns
- SecureCRDT re-derives quorum from the current base value and signature children, instead of trusting a final marker. New NetworkRegistry data must preserve that behavior.
- Registry policy is attached to a CRDT key pattern, making per-key and per-network authorization possible without a single global signer source.
- Both the GossipPubSub and processing paths construct independent libp2p hosts; securing only one leaves a private-network bypass.

### Integration Points
- Extend the gater/pnet injector plumbing in `thirdparty/ipfs-pubsub` and bind the same policy in `processing_core_impl.cpp`.
- Associate scoped CRDT key patterns with the corresponding NetworkRegistry PeerRegistry instance; retain global TrustedPeerRegistry policy only where it is the actual authority.
- Carry a job's public/private scope through task publication, escrow, results, payout, chain-ID derivation, and ValidatorRegistry selection.

</code_context>

<specifics>
## Specific Ideas

The application owner configures private networks offline, so a node receives the correct public network identity and secret pnet credential before it starts. The network's first membership record is a child of global trusted peers: the global TrustedPeerRegistry authorizes its bootstrap, then the network's own peers authorize later changes. A private network is not a blanket namespace for all node state—only job-derived state belonging to that private job is scoped; public jobs remain public.

</specifics>

<deferred>
## Deferred Ideas

- Online discovery, on-chain lookup, or runtime retrieval of the license NFT identity/pnet secret — offline owner provisioning is the decided model for this phase.
- Raw pnet-secret distribution or storage through CRDT — prohibited; any secure delivery and rotation procedure remains an operational design outside the replicated data model.

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window.md` — keyword-only match; unrelated bridge test fixture, not part of private-network scope.
- `bridge-startup-wiring-mock-rpc.md` — keyword-only match; unrelated bridge startup/RPC work, not part of private-network scope.

</deferred>

---

*Phase: 15-Private Network Identity and libp2p Gating*
*Context gathered: 2026-08-31*
