# Phase 15: Private Network Identity and libp2p Gating - Research

**Researched:** 2026-08-31
**Domain:** libp2p private networks (pnet/gater) + SecureCRDT peer-registry hierarchy + job-scope routing (C++17, vendored cpp-libp2p fork)
**Confidence:** HIGH (vendored API verified against installed headers/build tree; codebase integration points read in source)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Network identity and offline provisioning**
- **D-01:** `network_config.json`, not `sgns_config.json`, is the source of private-network configuration. The application owner provisions it offline before node startup with both `private_network_id` (the public Ed25519 identity from the license NFT) and `network_key` (the secret pnet credential).
- **D-02:** `private_network_id` and `network_key` are intentionally different values. The public ID drives identity, CRDT paths, chain IDs, and topic names; the secret key drives pnet. Nodes must not query the NFT or retrieve the pnet secret at runtime.
- **D-03:** The raw pnet key is never stored in a CRDT record. A registry may store only non-secret key metadata such as a version or fingerprint; provisioning and rotation of the raw secret remain offline operational work.

**Peer-registry hierarchy and SecureCRDT authorization**
- **D-04:** Introduce an explicit `PeerRegistry` base abstraction that resolves the current authorized peer set and quorum. SecureCRDT policy entries associate each registered key/pattern with the appropriate `PeerRegistry` instance; SecureCrdt itself remains generic and must not assume TrustedPeerRegistry globally.
- **D-05:** `TrustedPeerRegistry` remains one global root trust domain. There are no per-private-network TrustedPeerRegistry instances, and ordinary private workers need not become global trusted peers.
- **D-06:** Add a SecureCRDT-backed `NetworkRegistry` child for each `privateNetworkId`. Its bootstrap record is signed by a majority of the global TrustedPeerRegistry. Once confirmed, the NetworkRegistry resolves its own cached current peer set and quorum, so later membership changes are signed by its current network peers; a single peer may never admit itself unilaterally.
- **D-07:** NetworkRegistry membership is the private-network allow-list consulted during connection upgrade in addition to pnet protection. Pnet proves possession of the private credential; NetworkRegistry determines whether that peer is authorized for the selected private network.

**Scoped work and consensus**
- **D-08:** Job scope is explicit rather than inherited blindly from a node. Public jobs use scope `0` and retain current public routing. Private jobs use one `privateNetworkId`; every job-derived task, escrow, result, payout, CRDT key, and pubsub topic follows that same scope. Do not prefix unrelated global artifacts indiscriminately.
- **D-09:** ValidatorRegistry is separate for the public network and every private network. It supplies consensus only for its own scope and does not authorize NetworkRegistry updates; SecureCRDT authorization is handled by the selected PeerRegistry.

**Dependencies and enforcement surface**
- **D-10:** Do not duplicate or replace the in-progress trusted-peer production work. Phase 15 implementation waits for `GeniusVentures/libp2p#10` (gater + pnet) and `gsd/phase-13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production` to land.
- **D-11:** Bind the same private-network enforcement to both host construction paths: the GossipPubSub host and the independent processing host. The processing host must not offer Plaintext security.

### Claude's Discretion
- Exact `PeerRegistry` interface shape, ownership/lifetime strategy, and whether SecureCrdtRegistry stores a shared or weak registry reference, provided the per-key association remains explicit and safe.
- Exact NetworkRegistry payload schema, key-pattern layout, cached-state implementation, and safe quorum-threshold configuration, provided bootstrap uses global TrustedPeerRegistry majority and later updates cannot be unilateral.
- Exact non-secret pnet metadata retained in NetworkRegistry and the offline rotation procedure.
- Exact job-scope field encoding and the migration strategy for existing public jobs, provided public scope remains compatible and private job artifacts never cross scopes.

### Deferred Ideas (OUT OF SCOPE)
- Online discovery, on-chain lookup, or runtime retrieval of the license NFT identity/pnet secret — offline owner provisioning is the decided model for this phase.
- Raw pnet-secret distribution or storage through CRDT — prohibited; any secure delivery and rotation procedure remains an operational design outside the replicated data model.

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window.md` — keyword-only match; unrelated bridge test fixture, not part of private-network scope.
- `bridge-startup-wiring-mock-rpc.md` — keyword-only match; unrelated bridge startup/RPC work, not part of private-network scope.
</user_constraints>

<phase_requirements>
## Phase Requirements (derived — roadmap lists Requirements as TBD)

No requirement IDs exist for Phase 15. Coverage is derived from CONTEXT.md decisions D-01..D-11 and the acceptance criteria in GeniusVentures/SuperGenius#367.

| ID | Description | Research Support |
|----|-------------|------------------|
| PNET-CFG | `network_config.json` gains `private_network_id` (public, distinct from existing `network_key`); validated at load; offline provisioning model | `GeniusNode::LoadNetworkConfig` optional-key reader (`GeniusNode.cpp:1273-1277`) already reads `network_key`; add sibling key. Commit 0515def3 precedent |
| PNET-GATE | NetworkRegistry membership enforced as allow-list at connection upgrade, in addition to pnet PSK, on BOTH hosts | `ConnectionGater` interface + `useConnectionGater<T>()` verified in installed headers; ipfs-pubsub gap: `DenyListConnectionGater` is deny-only and not injectable — extension required |
| PNET-REG | `PeerRegistry` abstraction; SecureCrdtRegistry entries associate per-key registry; global TPR unchanged | `SecureCrdtRegistryEntry.signer_set_source` lambda mechanism (`SecureCrdtRegistry.hpp:50-59`); `BurnConfig` holds `shared_ptr<TrustedPeerRegistry>` as precedent |
| PNET-NETREG | SecureCRDT-backed `NetworkRegistry` per `privateNetworkId`; bootstrap signed by TPR majority; then self-governing cached peer set; non-secret pnet metadata only | `TrustedPeerRegistry` lifecycle (genesis cache → bootstrap signer → self-governance) is the direct template; `ValidateQuorumThreshold` floor (ceil(0.51*N)) must apply |
| PNET-SCOPE | Explicit job scope: `0` public (unchanged paths), private jobs carry one `privateNetworkId` through task/escrow/result/payout/CRDT key/topic/chain ID | `TaskKeys` builders, `PROCESSING_CHANNEL`/`GNUS_FULL_NODES_TOPIC` constants, `GENIUS_CHAIN_ID` x2, `SelectInputValidator(tx->GetChainId())` all located |
| PNET-VAL | ValidatorRegistry state/quorum isolated per private network; public registry preserved | `ValidatorRegistry::RegistryKey()/ValidatorTopic()` are static constants (`ValidatorRegistry.hpp:386-399`); single instance created in `Blockchain.cpp:122` — must become scope-parameterized |
| PNET-PROC | Processing host (`processing_core_impl.cpp:96`) gated identically and Noise-only (no Plaintext) | Current `makeHostInjector` uses default Plaintext+Noise (`network_injector.hpp:425`); needs `useSecurityAdaptors<Noise>()` + `usePrivateNetwork` + gater; key must be threaded through `ProcessingCoreImpl::New` (`GeniusNode.cpp:1641`) |
| PNET-TEST | Two same-network nodes connect+sync; unauthorized node fails upgrade; CRDT/validator isolation verified with wait-condition templates; >=80% coverage on new code | `pubsub_counts_test` (commit 0515def3) is the working two-node pnet test pattern; `test/testutil/wait_condition.hpp` exists |

Acceptance criteria source: [CITED: GeniusVentures/SuperGenius#367] (fetched via `gh` this session).
</phase_requirements>

## Summary

The libp2p-side hard dependency is materially complete on local disk. The GeniusVentures libp2p fork is checked out on branch `dev_pnets` at `/Users/henriqueklein/gnus/3rdparty/libp2p` with the full gater + pnet feature set (phases 02-03 committed: `Psk`, `PskHandle`, `PnetProtectedConnection` XSalsa20 boundary, `PnetUpgraderDecorator`, `ConnectionGater` + `PermissiveConnectionGater`, `usePrivateNetwork()`, `useConnectionGater<T>()`, `useAllowLoopbackDial()`, loopback-dial default-deny, public-bootstrap dial refusal, examples 05/06/07, tests). It is freshly installed at `3rdparty/build/OSX/Release` (built 2026-08-31 18:14, libs `libp2p_pnet.a`, `libp2p_pnet_upgrader.a`, `libp2p_connection_gater.a`). The GitHub issue GeniusVentures/libp2p#10 is still OPEN [VERIFIED: gh], so "D-10 wait" should be interpreted as "dev_pnets is the deliverable; confirm branch state before starting". The companion `3rdparty/ipfs-pubsub` (also `dev_pnets`) already exposes `GossipPubSub(keyPair, config, networkKey)` and a thread-safe `DenyListConnectionGater`, and SGNUS commit 0515def3 (on this branch) already consumes both: `network_key` parsing, pnet-mode `StartPubSub`, and `BlockPeer`/`UnblockPeer` plumbing with a passing-shape two-node PSK test (`test/src/pubsub_counts/pubsub_counts.cpp`).

The remaining phase work is therefore SGNUS-side plus one ipfs-pubsub extension. Nothing named `private_network_id` exists in SGNUS yet (grep is empty), so identity, registry hierarchy, and scope plumbing are greenfield. Three structural gaps: (1) the installed gater is **deny-list only and not injectable** through the public `GossipPubSub` API — D-07's NetworkRegistry **allow-list** requires extending our ipfs-pubsub fork with an injectable membership predicate (or custom-gater constructor); (2) `SecureCrdtRegistryEntry` carries a bare `SignerSetSource` lambda with no explicit registry identity — D-04 needs a `PeerRegistry` abstraction associated per key pattern (BurnConfig's `shared_ptr<TrustedPeerRegistry>` is the ownership precedent); (3) `ValidatorRegistry::RegistryKey()/ValidatorTopic()` are static constants with a single instance in `Blockchain::New` — D-09 requires scope-parameterized keys/topics.

**Primary recommendation:** Treat `3rdparty` dev_pnets (built today) as the libp2p#10 deliverable; reconfigure the SGNUS build against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` (existing build dirs are stale, configured Aug 24 against the old keyless `thirdparty`); extend ipfs-pubsub with an allow-list membership hook on the gater; then build SGNUS-side in this order — config identity -> PeerRegistry/NetworkRegistry -> gating binding on both hosts -> job-scope plumbing -> per-network ValidatorRegistry -> isolation tests.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| pnet transport protection (PSK, XSalsa20) | Vendored libp2p (`PnetUpgraderDecorator`) | — | Raw-connection encryption must sit below multiselect; already implemented and installed; never reimplement in app code |
| Connection allow/deny policy hooks | Vendored libp2p (`ConnectionGater` interface) | ipfs-pubsub (gater instance + DI binding) | Framework provides the pipeline hooks; policy object is app-level and must consult NetworkRegistry |
| Private-network membership state | SGNUS SecureCRDT layer (`NetworkRegistry`) | GlobalDB CRDT replication | Membership is quorum-governed replicated data; libp2p has no notion of it |
| Root trust / bootstrap authorization | SGNUS `TrustedPeerRegistry` (global, unchanged) | — | D-05: single root; signs NetworkRegistry bootstrap records |
| Public identity (`private_network_id`) consumption | `GeniusNode` config loading | Job/task scope carriers | Offline-provisioned config is the only identity source (D-01/D-02); no NFT queries |
| Job-scope routing (topics, CRDT keys, chain IDs) | SGNUS processing/account layers | — | App-level decision deriving every artifact path from the job's scope (D-08) |
| Consensus per scope | `ValidatorRegistry` instances | `Blockchain` wiring | D-09: one registry per network, keyed/topic'd per scope |
| Processing host security | `processing_core_impl.cpp` injector | GeniusNode config threading | D-11: independent host must be Noise-only + pnet + gater |

## Standard Stack

### Core (all vendored/first-party — verified against the installed build tree)

| Library | Version/Commit | Purpose | Why Standard |
|---------|----------------|---------|--------------|
| GeniusVentures/libp2p (fork of soramitsu/cpp-libp2p) | branch `dev_pnets` @ `b28eed2` (2026-08-28); installed 2026-08-31 | pnet PSK boundary, ConnectionGater, injector modules | The decided gater+pnet implementation (libp2p#10); API below |
| GeniusVentures/ipfs-pubsub | branch `dev_pnets` @ `3294d41` (2026-08-27) | GossipPubSub host construction, deny-list gater | Owns `MakeCustomHostInjector` — the gossip host's only construction path |
| SGNUS SecureCRDT layer | in-repo (`src/securecrdt/`, phases 8-12) | Quorum-signed CRDT values, per-key policy registry | The only sanctioned mechanism for NetworkRegistry (TPR precedent) |
| SGNUS TrustedPeerRegistry | in-repo (`src/trustedpeer/`) | Global root trust domain | D-05: stays global; bootstraps NetworkRegistry |
| GTest + GMock | vendored GTest | Tests | Project standard (`addtest()` in `cmake/functions.cmake`) |

[VERIFIED: local inspection of `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/{libp2p,ipfs-pubsub}/include` and `lib/`; git logs of both checkouts]

### Exact installed API surface this phase consumes

From `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/libp2p/include/libp2p/` [VERIFIED: installed headers]:

- `network/connection_gater.hpp` — `struct ConnectionGater` with five pure-virtual hooks, each returning `outcome::result<void>` (failure = `ConnectionGaterError`):
  `interceptPeerDial(const PeerId&)`, `interceptAddrDial(const PeerId&, const Multiaddress&)`, `interceptAccept(const Multiaddress& local, const Multiaddress& remote)`, `interceptSecured(bool is_initiator, const PeerId& remote_peer, const Multiaddress& remote_addr)`, `interceptUpgraded(const std::shared_ptr<CapableConnection>&)`.
- `network/impl/permissive_connection_gater.hpp` — default null-object bound when no override given.
- `injector/network_injector.hpp`:
  - `useConnectionGater<GaterImpl>()` — binds `network::ConnectionGater` to the impl (once).
  - `usePrivateNetwork(std::string_view key_text)` — one-line pnet activation; parses swarm-key text, else base16, else base64; throws `PskValidationError` eagerly before any injector assembles. Overload `usePrivateNetwork(Psk validated_psk)` is exception-free.
  - `useAllowLoopbackDial(bool allow = true)` — opts a composition into live 127.0.0.0/8 / ::1 dialing. Default is **deny** (secure-by-default; enforced in `src/transport/tcp/tcp_transport.cpp:48`).
  - Default `SecurityAdaptor*[]` binding in `makeNetworkInjector` is **Plaintext + Noise** (line 425) — D-11 sites must override with `useSecurityAdaptors<Noise>()`.
- `security/pnet/psk.hpp` — `Psk`: move-only, exactly 32 bytes (`kPskSize`), zeroed on destruction/move, never logged; factories `fromSwarmKeyText` / `fromRawBytes` / `fromBase16String` / `fromBase64String`. `PskHandle`: copyable DI holder; null handle = public mode.
- `security/pnet/pnet_error.hpp` — `PnetError` incl. `PNET_PUBLIC_BOOTSTRAP_REFUSED` (private network refuses dialing public bootstrap addresses; enforced in `DialerImpl`, `src/network/impl/dialer_impl.cpp:81`).
- `transport/impl/pnet_upgrader_decorator.hpp` — `PnetUpgraderDecorator` wraps the concrete `UpgraderImpl` on both raw upgrade paths. **Documented limitation:** relay overloads and `upgradeToMuxed` pass through unwrapped (relay streams are not PSK-wrapped; GossipPubSub disables relay: `protocol_config.enable_relay = false`).
- `security/pnet/pnet_protected_connection.hpp` — XSalsa20 per-direction streams after an in-the-clear 24-byte nonce exchange (matches go-libp2p `psk_conn.go` semantics).
- Static libs: `libp2p_pnet.a`, `libp2p_pnet_upgrader.a`, `libp2p_connection_gater.a` [VERIFIED: lib dir].

From `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/ipfs-pubsub/include/ipfs_pubsub/` [VERIFIED: installed headers + `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp`]:

- `GossipPubSub(libp2p::crypto::KeyPair, gossip::Config, const std::string& networkKey)` — private-network constructor; validates eagerly (throws `PskValidationError` -> report as init failure, per the SGNUS `StartPubSub` pattern).
- `deny_list_connection_gater.hpp` — `DenyListConnectionGater final : ConnectionGater`, mutex-protected runtime deny list; `BlockPeer/BlockPeers/UnblockPeer/IsPeerBlocked/GetBlockedPeers/Clear`. `interceptAccept` cannot know the peer yet, so unknown-at-raw-stage connections are checked at `interceptSecured` — this ordering matters for allow-list design too.
- Internal: `GossipPubSub::Init` constructs `m_connection_gater` itself and `MakeCustomHostInjector(keyPair, gater, ...variadic)` binds `di::bind<network::ConnectionGater>().TEMPLATE_TO(gater)[override]` plus `useSecurityAdaptors<Noise>()` (Noise-only) and, in pnet mode, appends `usePrivateNetwork(*networkKey)` (`gossip_pubsub.cpp:173-189, 255-277`).
- The ipfs-pubsub CMake target transitively links `p2p::p2p_pnet_upgrader` [VERIFIED: `ipfs-pubsubTargets.cmake` INTERFACE_LINK_LIBRARIES].

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Extend ipfs-pubsub gater with allow-list predicate | Emulate allow-list via BlockPeer of all non-members | Impossible in practice: deny-list requires enumerating the universe of peers; a real membership predicate is required (D-07) |
| `usePrivateNetwork` on both hosts | Spike-001 custom SecurityAdaptor gate | Superseded: pnet is the decided model (issue #367); adaptor gate was the pre-libp2p#10 fast path |
| Scope via global `version::SetNetworkId` appendix | Per-job scope field + derived keys/topics | Global setter is process-wide state; a node serving public + private jobs simultaneously cannot use it per-network (see Pitfall 6) |

**Installation:** No new external packages. The phase consumes already-built vendored artifacts and extends first-party repos (`3rdparty/ipfs-pubsub`, SGNUS). Build reconfiguration required — see Environment Availability.

## Package Legitimacy Audit

No registry (npm/PyPI/crates) packages are introduced by this phase. All consumed code is first-party vendored (GeniusVentures forks, verified by git remote/branch inspection) or in-repo. slopcheck gate: not applicable (no registry installs; nothing to remove or flag).

## Architecture Patterns

### System Architecture Diagram

Connection-upgrade enforcement pipeline (per host; both GossipPubSub host and processing host):

```
                          OFFLINE (application owner, before node start)
                               network_config.json
                 { private_network_id (public Ed25519/uint256),
                   network_key (secret 32-byte pnet PSK) }
                               |  (read once at LoadNetworkConfig; never from NFT/chain)
                               v
  GeniusNode ──────────────────────────────────────────────────────
   ├─ StartPubSub ──> GossipPubSub(keyPair, config, network_key)
   │                     └─ MakeCustomHostInjector(...)
   │                          ├─ useSecurityAdaptors<Noise>()          (Noise-only)
   │                          ├─ bind ConnectionGater ──────────┐
   │                          └─ usePrivateNetwork(network_key) │
   │                                                             │
   └─ ProcessingCoreImpl ──> makeHostInjector                    |
        (per ProcessSubTask)   ├─ useSecurityAdaptors<Noise>()   |
                               ├─ usePrivateNetwork(network_key) |
                               └─ bind ConnectionGater ──────────┤
                                                                   v
  INBOUND/OUTBOUND RAW CONNECTION (TcpTransport; loopback dial denied by default)
        │
        v
  [PnetUpgraderDecorator]  ── wrap raw conn ──> PnetProtectedConnection
        │   24-byte nonce exchange (clear) then XSalsa20 both directions
        │   wrong-PSK peer => decrypts garbage => multiselect NEVER succeeds
        v
  multistream-select (Noise) ──> secure handshake establishes remote PeerId
        │
        v
  [ConnectionGater.interceptSecured/interceptUpgraded]   <── allow-list source:
        │   deny-list (BlockPeer) checked; NetworkRegistry                NetworkRegistry[privateNetworkId]
        │   membership checked: PeerId authorized?  <───────────────────── (SecureCRDT, cached set)
        v                                                                   ^
  muxer (Yamux) ──> CapableConnection ──> gossip topics / streams          │
                                                                            │
  Registry hierarchy (SecureCRDT authorization):                            │
                                                                            │
  PeerRegistry (new abstraction; resolves current peers + quorum)          │
   ├─ TrustedPeerRegistry ────────── global root, unchanged ───── majority-signs ──┐
   └─ NetworkRegistry(<privateNetworkId>) ── child trust domain <──────────────────┘
        bootstrap record: TPR majority signatures
        after confirm: own cached peer set + quorum governs updates
        stores ONLY non-secret pnet metadata (version/fingerprint), never raw key
```

Job-scope data flow (D-08):

```
Job creation ──> scope = 0 (public) | privateNetworkId (private, exactly one)
     ├── task CRDT keys:  TaskKeys::TaskKey/SubTaskKey/... under public prefix  OR  /chain/<id>/... branch
     ├── escrow path / result / payout artifacts follow the job's scope
     ├── pubsub topics: PROCESSING_CHANNEL / results channels / queue channel topics scoped per network
     ├── chain IDs: GENIUS_CHAIN_ID (public) vs derived private chain id (tx->GetChainId() already plumbed)
     └── ValidatorRegistry selection: public instance vs per-<id> instance (own key+topic+quorum)
```

### Recommended Project Structure (new SGNUS code)

```
src/
├── peerregistry/               # D-04 abstraction (suggested home)
│   ├── PeerRegistry.hpp        #    interface: current peer set + quorum resolution
│   └── (TrustedPeerRegistry/NetworkRegistry implement it)
├── networkregistry/            # D-06/D-07 child registry
│   ├── NetworkRegistry.hpp
│   └── NetworkRegistry.cpp     #    payload, bootstrap-from-TPR, cached self-governance
├── securecrdt/SecureCrdtRegistry.hpp   # evolve entry: explicit PeerRegistry association
└── (modified) account/GeniusNode.*, processing/impl/processing_core_impl.cpp,
    processing/impl/TaskKeys.hpp, account/GeniusTransaction.hpp, account/TransactionManager.hpp,
    blockchain/ValidatorRegistry.*, blockchain/impl/Blockchain.cpp
3rdparty/ipfs-pubsub/src/ipfs_pubsub/
├── deny_list_connection_gater.hpp (or new membership gater)   # allow-list predicate extension
└── gossip_pubsub.{hpp,cpp}                                    # surface to inject it
test/src/
├── networkregistry/            # registry quorum/isolation tests
├── gating/ (or extend pubsub_counts)                          # two-node private-network tests
```

(File placement of new dirs is convention-consistent: lowercase module dirs like `trustedpeer/`, `securecrdt/`.)

### Pattern 1: ConnectionGater policy object bound via DI

**What:** Implement a gater whose allow/deny decision consults live registry state; bind it through the injector so every upgrade stage consults it.
**When to use:** Both host construction paths (D-07/D-11).
**Example:**
```cpp
// Source: 3rdparty/build/OSX/Release/libp2p/include/libp2p/injector/network_injector.hpp (verified)
// + 3rdparty/libp2p/example/06-private-network-gater/private_network_gater_example.cpp:130-137
auto injector = libp2p::injector::makeHostInjector(
    libp2p::injector::usePrivateNetwork(kSwarmKeyText),      // pnet PSK boundary
    libp2p::injector::useConnectionGater<NetworkMembershipGater>());  // peer-level authorization
// Compose BOTH: PSK proves network membership; the gater proves peer-level authorization
// (independent, non-redundant checks — fork's example/06 README).
```

### Pattern 2: GossipPubSub pnet-mode construction (already consumed by SGNUS)

**What:** Non-empty `network_key` selects the pnet constructor; failures surface as init failure, never a half-configured host.
**When to use:** `StartPubSub` (done in 0515def3); replicate the shape for any new construction paths.
**Example:**
```cpp
// Source: src/account/GeniusNode.cpp StartPubSub (commit 0515def3, verified in working tree)
if ( settings.network_key.empty() ) {
    pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ), config );
} else {
    try {
        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ),
                                                               config, settings.network_key );
    } catch ( const std::exception &e ) { /* log + return false */ }
}
```

### Pattern 3: Registry lifecycle — bootstrap authority then self-governance

**What:** A registry resolves its signer set from an external bootstrap authority until its first value is quorum-confirmed, then from its own cached current set — never re-entering the CRDT quorum-read path from inside the signer-set resolution (re-entrancy hazard).
**When to use:** `NetworkRegistry` (D-06) must copy this lifecycle with TPR-majority as the bootstrap authority.
**Example:**
```cpp
// Source: src/trustedpeer/TrustedPeerRegistry.hpp:196-208 (doc comment, verified)
// ResolveSignerSet: the sole bootstrapper (threshold 1) before genesis confirmation,
// or the current cached peer set (at quorum_threshold_) afterwards.
// Reads ONLY cached_peers_/genesis_confirmed_ — NEVER re-enters the SecureCrdt
// quorum-read path (Pitfall 2). NetworkRegistry: bootstrapper set = TPR current peers,
// bootstrap threshold = TPR-majority; after confirm, its own cached network peers.
```

### Pattern 4: Per-key SecureCRDT policy association (evolve to PeerRegistry)

**What:** `SecureCrdtRegistryEntry` already maps a key pattern -> `{signer_set_source, quorum, factory}`; the lambda captures whoever constructed it. D-04 makes the association explicit by capturing a `PeerRegistry` reference instead of ad-hoc state.
**Example:**
```cpp
// Source: src/securecrdt/SecureCrdtRegistry.hpp:44-59 (verified)
using SignerSetSource =
    std::function<outcome::result<SignerSetSnapshot>( const std::string &base_key )>;
struct SecureCrdtRegistryEntry {
    std::string                                       key_pattern;
    SignerSetSource                                   signer_set_source;
    std::function<std::shared_ptr<ISignedCRDTData>()> make_instance;
    ...
};
// Ownership precedent: BurnConfig holds shared_ptr<TrustedPeerRegistry> (BurnConfig.hpp:94-98).
// Discretion: shared vs weak reference — shared matches BurnConfig; weak only if cycle risk
// is demonstrated (registry outliving its policy owner).
```

### Anti-Patterns to Avoid

- **Gating only the gossip host:** the processing host (`processing_core_impl.cpp:96`) builds an independent libp2p host per `ProcessSubTask` with default Plaintext+Noise — securing only PubSub leaves a private-network bypass (spike-001 finding, still true).
- **Storing or logging the raw PSK:** `Psk` is move-only and self-zeroing precisely so key bytes never leak; `network_key_` as `std::string` in GeniusNode is config-sourced state — never replicate it into CRDT (D-03) and never log it.
- **Assuming the deny-list gater is an allow-list:** `DenyListConnectionGater` semantics are deny-only; D-07 needs positive membership authorization against NetworkRegistry.
- **Scope-prefixing every artifact:** D-08 forbids blanket prefixing; only job-derived state follows the job's scope.
- **Per-network TrustedPeerRegistry instances:** D-05 forbids; ordinary private workers must not become globally trusted.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Transport-level encryption before multiselect | Custom RawConnection encryption | `usePrivateNetwork` / `PnetUpgraderDecorator` | XSalsa20 nonce/stream handling, go-libp2p-compatible framing, reentrancy-tested (fork phase 03) |
| Connection policy hooks | Custom upgrader patching | `ConnectionGater` + `useConnectionGater<T>()` | Interface already wired at all five pipeline stages in the installed fork |
| Quorum/signature machinery for NetworkRegistry | New CRDT protocol | `SecureCrdt` + `SecureCrdtRegistry` + `ISignedCRDTData` | Propose/sign/quorum via CRDT puts + filter callbacks; reader re-derives trust from base + sig children (no final marker) |
| Bootstrap signer lifecycle | Novel genesis logic | `TrustedPeerRegistry` lifecycle (Pattern 3) | Proven cache/bootstrap/self-governance split incl. re-entrancy pitfalls already solved |
| Quorum floor safety | Manual threshold checks | `ValidateQuorumThreshold` (ceil(0.51*N)) | Security-critical floor; prevents locally-lowered thresholds from trivially self-confirming |
| PSK parsing/validation | Ad-hoc hex/base64 parsing | `Psk::fromSwarmKeyText/fromBase16String/fromBase64String` | Explicit `PnetError` codes; no silent truncation; rejects `/bin/` codec framing |

**Key insight:** every layer this phase needs below "membership policy" already exists and is installed; the phase's genuine new code is policy (who is a member), plumbing (scope propagation), and isolation (per-network registries/topics).

## Runtime State Inventory

This phase adds registries and scope plumbing adjacent to live CRDT data — compatibility of stored state is a plan-level concern.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data (CRDT/GlobalDB) | Public artifacts: `TaskKeys` trees (`/processing_<v>/tasks|subtasks|claimable|task_results`, `/lock_` prefix), `gnus-validator-registry` key, `trusted-peer-registry` branch, BURN key — all written under public scope today | None for public scope (D-08 compatibility); private branches are new (`/chain/<id>/...`); public ValidatorRegistry key/topic must remain byte-identical |
| Live service config | None external — no n8n/Datadog-style services; node config files only | — |
| OS-registered state | None — no Task Scheduler/launchd/pm2 registrations reference private-network strings | — |
| Secrets/env vars | `network_key` in `network_config.json` (file on disk, gitignored runtime dirs; example files must carry placeholder/dummy keys only); SGNUS tests embed well-known test PSKs | Code edit only: add `private_network_id` sibling key; NEVER move the secret into CRDT or logs; example/test configs get non-production keys |
| Build artifacts / installed packages | SGNUS `build/OSX/{Release,Debug}` configured 2026-08-24 against OLD `/Users/henriqueklein/gnus/thirdparty` (pre-pnet: no `deny_list_connection_gater.hpp`, no pnet libs); fresh dev_pnets install at `3rdparty/build/OSX/Release` (2026-08-31 18:14) is unused by any SGNUS build dir yet | Reconfigure SGNUS with `THIRDPARTY_BUILD_DIR=/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` (or `THIRDPARTY_DIR=/Users/henriqueklein/gnus/3rdparty`) before compiling; existing `pubsub_counts_test` binaries in both trees are stale (Aug 24, predate the Aug 27 pnet commit) |

**Canonical question answered:** after all source edits, the runtime systems still holding old state are (a) on-disk `network_config.json` files lacking `private_network_id` (benign — absent key = public node, preserving today's behavior), and (b) the two stale SGNUS build trees, which will not see the new headers until reconfigured.

## Common Pitfalls

### Pitfall 1: Loopback dialing is now denied by default
**What goes wrong:** Two-node local tests/examples that bootstrap via `/ip4/127.0.0.1/...` multiaddrs silently fail to dial on the dev_pnets libp2p — `TcpTransport` rejects 127.0.0.0/8 and ::1 destinations unless `useAllowLoopbackDial()` is bound (`tcp_transport.cpp:36-48`).
**Why it happens:** Secure-by-default hardening added with the gater work; ipfs-pubsub's `MakeCustomHostInjector` does not pass it.
**How to avoid:** Use interface addresses for local test bootstrapping (`GetInterfaceAddress()`, as `pubsub_counts.cpp:107` does), or extend ipfs-pubsub with a test-only loopback opt-in mirroring `useAllowLoopbackDial`.
**Warning signs:** Dial timeouts with no handshake in logs while tests pass on machines with LAN interfaces.

### Pitfall 2: The installed gater is deny-only and not injectable
**What goes wrong:** Assuming `DenyListConnectionGater` can express D-07's allow-list, or that a custom gater can be handed to `GossipPubSub` — it is constructed internally in `Init()` and the only public surface is Block/Unblock.
**Why it happens:** The dev_pnets ipfs-pubsub commit predates the NetworkRegistry requirement.
**How to avoid:** Extend ipfs-pubsub (ours): add an injectable membership predicate (e.g., `std::function<bool(const PeerId&)>` allow-list source consulted at `interceptPeerDial`/`interceptSecured`/`interceptUpgraded`) or a gater-accepting construction path. Deny-list stays for `BlockPeer`.
**Warning signs:** Any plan task that "configures the allow list" without an ipfs-pubsub change.

### Pitfall 3: PSK/identity confusion
**What goes wrong:** Using `network_key` for identity/CRDT paths, or deriving the PSK from the public ID (they are intentionally distinct — D-02); or copying the raw key into a NetworkRegistry record (D-03 violation).
**How to avoid:** Two config fields with different consumers; registry stores at most version/fingerprint metadata. `Psk`'s move-only self-zeroing design signals the intended hygiene.
**Warning signs:** Any `network_key`-derived string appearing in a CRDT key, topic name, or log line.

### Pitfall 4: Eager-throw constructor vs. outcome-based init
**What goes wrong:** `usePrivateNetwork(text)` and the pnet `GossipPubSub` ctor throw `PskValidationError` before anything assembles; code that expects `outcome::failure` leaks an uncaught exception through node construction.
**How to avoid:** Wrap pnet-mode construction in try/catch and convert to init failure — the exact pattern `StartPubSub` already uses [VERIFIED: GeniusNode.cpp, commit 0515def3].
**Warning signs:** New construction paths (processing host) without the try/catch.

### Pitfall 5: Two chain-ID constants, one selection point
**What goes wrong:** Editing only `GeniusTransaction::GENIUS_CHAIN_ID` ("supergenius_chain") or only `TransactionManager::GENIUS_CHAIN_ID` ("supergenius") — they are distinct constants with different values; `SelectInputValidator` falls back across both.
**How to avoid:** Derive private chain IDs at job creation and rely on the existing `tx->GetChainId()` override path (`TransactionManager.cpp:1326-1355`); keep both public constants byte-stable.
**Warning signs:** Diffs touching exactly one of the two constants.

### Pitfall 6: Process-global network appendix vs per-job scope
**What goes wrong:** Reusing `sgns::version::SetNetworkId/GetNetAndVersionAppendix()` (global MAIN/TEST/DEV state, used for queue-channel topic suffixes) to scope private topics — a node serving public and private jobs concurrently would corrupt the suffix for everyone.
**How to avoid:** Scope is a property of the job (D-08): thread it explicitly into topic derivation at call sites (`ProcessingSubTaskQueueChannelPubSub` builds `channelId + GetNetAndVersionAppendix()` — keep the version appendix, add the network component from job scope).
**Warning signs:** Any call to `SetNetworkId` in new code.

### Pitfall 7: ValidatorRegistry isolation is structural, not just a second instance
**What goes wrong:** Instantiating a second `ValidatorRegistry` while `RegistryKey()`/`ValidatorTopic()` remain static constants — both instances would read/write the same CRDT key and gossip topic, merging the networks' consensus.
**How to avoid:** Parameterize key/topic by scope (constructor arg or per-scope accessor) while the public instance keeps the exact current strings; `Blockchain::New` (single creation site, `Blockchain.cpp:122`) becomes scope-aware.
**Warning signs:** Any private registry instance constructed with the default key.

### Pitfall 8: The processing host is created per subtask
**What goes wrong:** Gating "the processing host" once at node start — `ProcessSubTask` builds a fresh injector per subtask (`processing_core_impl.cpp:96`), so per-subtask constructions each need the bindings, and the network key/gater policy must be threaded into `ProcessingCoreImpl` (its `New` currently takes only task queue, count, TokenID — `GeniusNode.cpp:1641`).
**How to avoid:** Extend `ProcessingCoreImpl` construction to carry the node's private-network context (key + membership source); apply `useSecurityAdaptors<Noise>()` + `usePrivateNetwork` + gater in its injector.
**Warning signs:** Processing tests passing while `addProtocols`/handshakes still offer `/plaintext`.

### Pitfall 9: Signer-set re-entrancy in NetworkRegistry
**What goes wrong:** NetworkRegistry's signer-set resolution reading the CRDT quorum state it is currently verifying — deadlock/re-entrancy; the exact pitfall TrustedPeerRegistry's cached-state design documents.
**How to avoid:** Copy Pattern 3: resolve from cached peers + confirmed flag only; refresh the cache from confirmed quorum reads on change callbacks.
**Warning signs:** Any `ResolveSignerSet` implementation calling `ReadIfQuorum`.

### Pitfall 10: Building against the stale thirdparty tree
**What goes wrong:** Compiling in the existing `build/OSX/*` dirs — they resolve to `/Users/henriqueklein/gnus/thirdparty` (old checkout, no pnet headers), producing "header not found" or linking the keyless libp2p.
**Why it happens:** `CommonCompilerOptions.cmake:158` defaults `THIRDPARTY_DIR` to the sibling `thirdparty`; both checkouts exist side by side.
**How to avoid:** Reconfigure with an explicit `-DTHIRDPARTY_BUILD_DIR=/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` (or `-DTHIRDPARTY_DIR=/Users/henriqueklein/gnus/3rdparty`); verify by grepping the new cache.
**Warning signs:** CMake configure messages; missing `libp2p/security/pnet/psk.hpp`.

### Pitfall 11: Public bootstrap dialing in private mode
**What goes wrong:** A pnet node keeping public bootstrap addresses dials them; the fork's `DialerImpl` now refuses (returns `PNET_PUBLIC_BOOTSTRAP_REFUSED`) — handle that error path, and provision private networks with their own bootstrap peers.
**Warning signs:** Bootstrap failures surfacing as generic dial errors in private mode.

## Code Examples

### Validating and activating pnet on a host injector
```cpp
// Source: 3rdparty/build/OSX/Release/libp2p/include/libp2p/injector/network_injector.hpp:280-349 (verified)
// Eager failure: invalid key material throws PskValidationError BEFORE di::make_injector
// assembles anything — a half-configured private/public Host can never exist.
auto injector = libp2p::injector::makeHostInjector(
    libp2p::injector::usePrivateNetwork( swarm_key_text ),   // or (validated_psk) overload
    libp2p::injector::useConnectionGater<MyGater>() );
```

### Gater implementation skeleton (NetworkRegistry-backed)
```cpp
// Interface source: 3rdparty/build/OSX/Release/libp2p/include/libp2p/network/connection_gater.hpp (verified)
// Deny-list reference: 3rdparty/build/OSX/Release/ipfs-pubsub/include/ipfs_pubsub/deny_list_connection_gater.hpp
class NetworkMembershipGater final : public libp2p::network::ConnectionGater {
public:
    // membership_source: thread-safe predicate over NetworkRegistry's cached set,
    // e.g. std::function<bool(const libp2p::peer::PeerId&)>; null/empty-registry = fail-open
    // only for public mode (decide explicitly and test both).
    libp2p::outcome::result<void> interceptSecured(
        bool is_initiator, const libp2p::peer::PeerId &remote_peer,
        const libp2p::multi::Multiaddress &remote_addr ) override {
        if ( denied_.IsPeerBlocked( remote_peer ) )
            return libp2p::network::ConnectionGaterError::GATER_REJECTED_SECURED;
        if ( private_mode_ && !membership_( remote_peer ) )
            return libp2p::network::ConnectionGaterError::GATER_REJECTED_SECURED;
        return libp2p::outcome::success();
    }
    // interceptAccept: peer unknown at raw stage — allow through, check post-handshake
    // (same ordering as DenyListConnectionGater).
    ...
};
```

### PeerRegistry association sketch (D-04 — shape is Claude's discretion; this is a recommendation)
```cpp
// Precedents: src/securecrdt/SecureCrdtRegistry.hpp:44-59 (SignerSetSource),
//             src/account/BurnConfig.hpp:94-98 (shared_ptr<TrustedPeerRegistry> ownership)
class PeerRegistry {           // suggested minimal surface
public:
    virtual ~PeerRegistry() = default;
    virtual securecrdt::SignerSetSnapshot CurrentSignerSet() const = 0;  // cached-only (Pitfall 9)
    virtual crdt::HierarchicalKey BaseKey() const = 0;
};
// SecureCrdtRegistryEntry captures the registry (shared_ptr per BurnConfig precedent) and its
// signer_set_source adapts: entry.signer_set_source = [registry]( const std::string &base_key ) {
//     return registry->CurrentSignerSet();   // outcome-wrapped
// };
```

### Two-node private-network test (working pattern already in-tree)
```cpp
// Source: test/src/pubsub_counts/pubsub_counts.cpp (commit 0515def3, verified)
// SWARM_KEY_PNET shared by the two inside nodes; SWARM_KEY_OUTSIDE differs.
// Start(0, {}) then AddPeers({ other->GetInterfaceAddress() })  // interface addr, NOT loopback
// outside node (different PSK) must fail the handshake; blocked-peer node (same PSK,
// gater-denied) must fail too. Extend with: same PSK but NOT in NetworkRegistry => rejected
// (the D-07 layer this phase adds).
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| No gating in cpp-libp2p (upstream master has no gator/PSK — spike 002 verified `git ls-tree upstream/master` zero hits) | GeniusVentures fork `dev_pnets`: go-libp2p-style `gater/` + `pnet/` semantics authored natively | Aug 2026 (fork phases 02-03, commits through b28eed2 2026-08-28) | The decided surface; PRs to libp2p/cpp-libp2p upstream are follow-up citizenship, not a dependency |
| Spike-001 SecurityAdaptor gate as fast path | `usePrivateNetwork` + `useConnectionGater` | libp2p#10 implementation landed | Do not implement the adaptor gate; consume the injector modules |
| Ad-hoc `network_key` handling (commit 0515def3) | Distinct public `private_network_id` + secret `network_key` | This phase (D-01/D-02) | Config, identity, CRDT paths, topics all become scope-aware |

**Deprecated/outdated:**
- Spike-001's `PskGateAdaptor : SecurityAdaptor` recommendation — superseded by pnet (kept as design history).
- `subnet_id_` (`GeniusNode.hpp:936`, read from `sgns_config.json` at `GeniusNode.cpp:403`, forwarded to TransactionManager): reserved/unused; retire or repurpose only where appropriate (issue #367 scope item 2) — at most a log/topic fingerprint per the identity note. [CITED: notes/private-network-identity.md]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Branch `dev_pnets` in `/Users/henriqueklein/gnus/3rdparty/{libp2p,ipfs-pubsub}` (built 2026-08-31) constitutes the libp2p#10 deliverable and may be consumed despite the GitHub issue still being OPEN | Summary, Environment | If the team intends a different integration point (merge to main / new tag), the build path changes; verify with owner at plan time |
| A2 | The ipfs-pubsub allow-list extension should be authored in the `3rdparty/ipfs-pubsub` `dev_pnets` checkout (same repo/branch as the existing pnet work) | Patterns, Pitfall 2 | If a separate branch/fork flow is required, an extra coordination step appears |
| A3 | `private_network_id` config value format is the 32-byte Ed25519 pubkey as uint256 (decimal or 0x-hex string in JSON) per the identity note | Requirements map | Exact parse/validation rules differ; confirm encoding with owner (TokenContracts gnus-ai Phase 14.1 is the mint-side counterpart) |
| A4 | `/chain/<privateNetworkId>/...` top-level HierarchicalKey branch is the private CRDT namespace layout | Scope flow | Alternative layouts still satisfy D-08; layout choice affects TaskKeys evolution |
| A5 | Processing-host gating threads the key through `ProcessingCoreImpl` construction rather than a global accessor | Pitfall 8 | A global/context-singleton design would also work; constructor threading matches existing DI style |
| A6 | SGNUS build reconfiguration to the 3rdparty tree is intended and persistent for this phase (user-directed: "inspect their headers/CMake config there") | Environment | If a different build location is canonical, re-derive the THIRDPARTY override |

**Note:** A1-A6 are assumptions needing owner confirmation before becoming locked; everything else cites verified local sources or the tracked issue.

## Open Questions (RESOLVED 2026-09-01)

> **Resolution record (planner, 2026-09-01):** every open question now has a decision owner and vehicle — none remain open for executors.

1. **libp2p#10 completion semantics — RESOLVED via 15-01 Task 2 (blocking D-10 checkpoint).** The gate is the owner's explicit choice among proceed-current-base (recommended: dev_pnets @ b28eed2 installed and tested) / merge-closeout-first / wait. Recorded verbatim in 15-01-SUMMARY before Wave 2 executes.
2. **`private_network_id` encoding — RESOLVED via 15-01 Task 2 (same checkpoint), recommended 0x-hex-32B.** 15-01 Task 3's validation is written SUBORDINATE to the checkpoint decision: hex regex and golden literals apply for 0x-hex-32B; a decimal-uint256 selection swaps only the regex and all-zero literal in the same commit. Q3/Q4 below were already planner discretion / plan-decided (15-03 literal per-network base key; 15-05 fail-closed), and both plans encode those choices.


1. **libp2p#10 completion semantics**
   - What we know: GitHub issue OPEN; dev_pnets fully implemented, tested, examples included, freshly installed locally.
   - What's unclear: whether the team treats branch state or a merged PR as the D-10 gate.
   - Recommendation: confirm at plan kickoff ("dev_pnets @ b28eed2 is the deliverable — proceed?"); the CONTEXT.md already says implementation is blocked only by that and the phase-13-closeout branch.
2. **`private_network_id` wire/JSON encoding**
   - What we know: identity = 32-byte Ed25519 pubkey as uint256; `0` = public.
   - What's unclear: JSON representation (hex vs decimal) and validation cutoffs.
   - Recommendation: accept 0x-prefixed hex of exactly 32 bytes, reject all-zero, default absent = public; confirm with owner (A3).
3. **NetworkRegistry key-pattern layout**
   - What we know: per-key SecureCRDT policy exists; TPR uses base key `trusted-peer-registry`.
   - What's unclear: pattern for per-network branches (e.g., `network-registry/<id>` literal vs regex family).
   - Recommendation (discretion): literal per-network base key with the existing `(/sig/[^/]+)?` compile shape; one registration per active network.
4. **Gater fail-open vs fail-closed before NetworkRegistry bootstrap confirm**
   - What we know: pnet alone already excludes non-members; registry allow-list is the second layer.
   - What's unclear: whether an un-bootstrapped (unconfirmed) registry should accept registry-unknown peers (pnet-only) or reject all.
   - Recommendation: fail-closed for private mode (defense in depth), with the bootstrap record provisioning membership before first sync; decide explicitly and test both branches.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| cmake | build | ✓ | 3.31.4 | — |
| ninja | build | ✓ | 1.13.0 | — |
| clang++ | build | ✓ | Apple clang 16.0.0 | — |
| ctest | test runner | ✓ | (bundled with cmake) | — |
| gh CLI | issue tracking refs | ✓ | — | — |
| dev_pnets libp2p install | pnet/gater API | ✓ | b28eed2, installed 2026-08-31 18:14 at `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/libp2p` | — |
| dev_pnets ipfs-pubsub install | GossipPubSub pnet ctor, gater | ✓ | 3294d41, installed 2026-08-31 18:14 at `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/ipfs-pubsub` | — |
| SGNUS build configured against dev_pnets | compiling phase code | ✗ | existing `build/OSX/{Release,Debug}` are stale (2026-08-24, old `thirdparty`) | reconfigure with explicit `THIRDPARTY_BUILD_DIR`/`THIRDPARTY_DIR` override |

**Missing dependencies with no fallback:** none.

**Missing dependencies with fallback:** SGNUS build-dir staleness — resolved by reconfiguration (one cmake invocation), not by installing anything.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test + Google Mock (vendored GTest) |
| Config file | `test/CMakeLists.txt` tree + `addtest()` helper (`cmake/functions.cmake`) |
| Quick run command | `ninja -C build/OSX/Release pubsub_counts_test && ./build/OSX/Release/test_bin/pubsub_counts_test --gtest_filter=<Case>` |
| Full suite command | `ctest --test-dir build/OSX/Release` (after `ninja -C build/OSX/Release`) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PNET-CFG | `private_network_id` parsed/validated; absent = public; distinct from `network_key` | unit | `./test_bin/network_config_test` (new; or extend an account test) | ❌ Wave 0 |
| PNET-GATE | same-PSK + in-registry peers connect; same-PSK but not-in-registry rejected; different-PSK rejected | integration (two-node) | `./test_bin/pubsub_counts_test --gtest_filter=*PrivateNetwork*` (extend) | ✅ base file exists; D-07 layer ❌ |
| PNET-REG | PeerRegistry association: SecureCrdtRegistry resolves per-key signer sets from the associated registry | unit | `./test_bin/peer_registry_test` (new) | ❌ Wave 0 |
| PNET-NETREG | bootstrap requires TPR-majority; post-confirm self-governance; no unilateral admission; no raw key in records | unit + integration | `./test_bin/network_registry_test` (new) | ❌ Wave 0 |
| PNET-SCOPE | private job artifacts land only under `/chain/<id>/...` + scoped topics; public unchanged | unit + integration | `./test_bin/job_scope_test` (new) | ❌ Wave 0 |
| PNET-VAL | per-network ValidatorRegistry key/topic isolation; public registry byte-stable | unit | `./test_bin/validator_registry_scope_test` (new) | ❌ Wave 0 |
| PNET-PROC | processing host offers no `/plaintext`; honors pnet | integration | extend processing-core test asserting negotiated protocols | ❌ Wave 0 |

**Acceptance-criteria constraints [CITED: SuperGenius#367]:** wait-condition templates, no `sleep_for` (`test/testutil/wait_condition.hpp` provides the macros); ≥80% coverage on new code.

### Sampling Rate
- **Per task commit:** targeted test binary for the touched component (commands above)
- **Per wave merge:** `ctest --test-dir build/OSX/Release`
- **Phase gate:** full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- `test/src/networkregistry/` (+CMakeLists, `addtest(network_registry_test ...)`) — covers PNET-NETREG
- `test/src/peerregistry/` or co-located securecrdt test — covers PNET-REG
- `test/src/gating/` or extension of `test/src/pubsub_counts/pubsub_counts.cpp` — covers PNET-GATE D-07 layer
- `test/src/account/` config-parse test extension — covers PNET-CFG
- Build reconfiguration (Environment Availability) precedes all test builds

## Security Domain

ASVS Level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | yes | Peer authentication via Noise handshake (PeerId from static pubkey); network authentication via pnet PSK possession — both from vendored libp2p, never hand-rolled |
| V3 Session Management | no | No user sessions in scope (P2P connections; connection lifetime owned by libp2p) |
| V4 Access Control | yes | NetworkRegistry allow-list enforced at `interceptSecured`/`interceptUpgraded`; bootstrap authorization = TPR majority (D-06); quorum floor via `ValidateQuorumThreshold` |
| V5 Input Validation | yes | `private_network_id`/`network_key` config validation (reject malformed/all-zero IDs); `Psk` parse factories return explicit `PnetError` — no silent truncation; `ISignedCRDTData::DeserializeFromBytes` returns false on malformed input |
| V6 Cryptography | yes | XSalsa20 (pnet), Ed25519 (identity/signatures), SHA-256 (signing bytes) — all via libp2p/ConsensusAuth providers; key hygiene: move-only self-zeroing `Psk`, no logging, no CRDT storage (D-03) |

### Known Threat Patterns for libp2p private networks + SecureCRDT

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Outsider joins private swarm | Elevation of Privilege | pnet PSK boundary: wrong key => multiselect never completes (PNET-03) |
| Valid-PSK but unauthorized peer connects | Elevation of Privilege | NetworkRegistry allow-list at gater (D-07) — independent, non-redundant layer [CITED: fork example/06 README] |
| Peer identity spoofing | Spoofing | Noise-only security (drop Plaintext, esp. processing host — D-11); PeerId established before gater checks |
| Single peer self-admits to NetworkRegistry | Elevation of Privilege | TPR-majority bootstrap + self-governance quorum + majority floor (ceil(0.51*N)) |
| PSK disclosure via replicated data or logs | Information Disclosure | D-03: never in CRDT (metadata only); `Psk` zeroing; no key bytes in error messages |
| Malicious advertised addr induces loopback dial | Tampering/SSRF | Loopback dial default-deny (`useAllowLoopbackDial` opt-in only for tests) |
| Private node leaks dials to public bootstrap | Information Disclosure | `PNET_PUBLIC_BOOTSTRAP_REFUSED` in DialerImpl when PSK present |
| Quorum threshold locally lowered to self-confirm | Elevation of Privilege | `ValidateQuorumThreshold` floor at construction (existing control, must cover NetworkRegistry) |
| Under-signed CRDT value applied | Tampering | `SecureCrdt::ReadIfQuorum` re-derives trust from base + sig children; no final marker |

## Sources

### Primary (HIGH confidence — local, verified this session)
- `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/libp2p/include/libp2p/` — connection_gater.hpp, permissive_connection_gater.hpp, psk.hpp, pnet_error.hpp, pnet_protected_connection.hpp, pnet_upgrader_decorator.hpp, injector/network_injector.hpp (exact installed API)
- `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release/ipfs-pubsub/include/ipfs_pubsub/` — gossip_pubsub.hpp, deny_list_connection_gater.hpp; `3rdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:142-277` (injector internals)
- `3rdparty/libp2p/src/network/impl/dialer_impl.cpp:81`, `src/transport/tcp/tcp_transport.cpp:36-48` — bootstrap refusal, loopback guard
- `3rdparty/libp2p/example/06-private-network-gater/README.md` + example sources 05/06/07 — complementary-layers rationale
- SGNUS source: `src/account/GeniusNode.{hpp,cpp}` (LoadNetworkConfig, StartPubSub, wiring order, subnet_id_, PROCESSING_CHANNEL), `src/securecrdt/{SecureCrdt,SecureCrdtRegistry,ISignedCRDTData,QuorumThresholdValidation}.hpp`, `src/trustedpeer/TrustedPeerRegistry.hpp`, `src/account/BurnConfig.hpp`, `src/blockchain/ValidatorRegistry.hpp` + `src/blockchain/impl/Blockchain.cpp:122`, `src/processing/impl/{processing_core_impl.cpp,TaskKeys.hpp}`, `src/account/{GeniusTransaction.hpp,TransactionManager.hpp,cpp}`, `src/crdt/hierarchical_key.hpp`, `src/base/sgns_version.hpp/cpp`, `test/src/pubsub_counts/pubsub_counts.cpp`
- Git: SGNUS commit 0515def3 (diff read); dev_pnums branch logs of both vendored repos; `3rdparty/build/OSX/Release` install timestamps

### Secondary (MEDIUM-HIGH — authoritative tracker)
- GeniusVentures/SuperGenius#367 (body + 2026-08-31 registry-hierarchy comment) — fetched via `gh` this session
- GeniusVentures/libp2p#10 — state/title fetched via `gh` (OPEN)
- `.planning/notes/private-network-identity.md`, `.planning/seeds/sg-node-consumes-privateNetworkId.md`, `.planning/spikes/001-libp2p-private-network-feasibility/README.md`, `.planning/spikes/002-upstream-feature-gap/README.md`, `.planning/REQUIREMENTS.md`, `.planning/ROADMAP.md`, `.planning/codebase/{TESTING,CONVENTIONS}.md`

### Tertiary (LOW confidence)
- None used for load-bearing claims.

## Metadata

**Confidence breakdown:**
- Vendored API surface (gater/pnet/GossipPubSub): HIGH — read directly from the installed headers and the vendored sources the user designated as authoritative
- SGNUS integration points: HIGH — every cited file:line read in the working tree this session
- Registry/scope design fit: HIGH for mechanisms (SecureCRDT/TPR precedents read), MEDIUM for the recommended PeerRegistry/NetworkRegistry shapes (discretion areas, sketched not implemented)
- Process assumptions (A1-A6): LOW-MEDIUM — flagged in the Assumptions Log for owner confirmation

**Research date:** 2026-08-31
**Valid until:** 2026-09-14 (vendored branches are moving — dev_pnets advanced as recently as 2026-08-28; re-verify branch heads and issue #10 state at plan time)
