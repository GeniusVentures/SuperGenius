---
spike: 001
name: libp2p-private-network-feasibility
type: standard
validates: "Given the vendored C++ libp2p, when a host is built via the injector, then non-member peers can be denied during connection upgrade without forking vendored code"
verdict: VALIDATED
related: []
tags: [libp2p, security, private-network, connection-gating]
---

# Spike 001: libp2p Private-Network Feasibility (PSK / connection gating)

## What This Validates

1. Does the vendored C++ libp2p (`GeniusNetwork/thirdparty/libp2p`) support connection
   gating (deny connections before/during upgrade)?
2. Is there a PSK / raw-connection-verifier hook for private swarms?
3. Where does enforcement attach relative to `GlobalDB::New`?

Fact-type spike: answered by tracing the vendored source (file:line evidence below).
No compile probe was run — evidence is source-level, not build-verified.

## Research

Vendored libp2p is the Soramitsu C++ implementation (fork of soramitsu/libp2p; latest
commits "Reverted change in kademlia_injector.hpp", "Fixed teardown bugs in Windows").

| Approach | Mechanism in vendored source | Status |
|----------|------------------------------|--------|
| go-libp2p-style ConnectionGator | No gator exists — no `gator` files, no `ConnectionGator` symbols anywhere in `include/` or `src/` | ✗ Not available |
| go-libp2p-style PSK (pnet) | No PSK/`RawConnectionVerifier` anywhere | ✗ Not available |
| Custom `SecurityAdaptor` via DI injector | `include/libp2p/security/security_adaptor.hpp` — pluggable; injector exposes `useSecurityAdaptors<...>()` (`network_injector.hpp:201`); production host already binds Noise-only (`gossip_pub_sub.cpp:186`) | ✓ Available, no fork |

**Chosen approach:** custom `SecurityAdaptor` acting as the membership gate (see Results).

## Key Evidence

- **Security negotiation is protocol-name based.** Inbound upgrades run multistream-select
  over the registered security protocols, then dispatch to the matching adaptor:
  `src/transport/impl/upgrader_impl.cpp:68-90` — `protocol_muxer_->selectOneOf(security_protocols_, ...)`
  → `findAdaptor(...)` → `adaptor->secureInbound(...)`. A peer that doesn't speak our
  protocol fails negotiation and **never completes the handshake**.
- **The injector accepts custom security adaptors without forking.**
  `include/libp2p/injector/network_injector.hpp:86-95` documents exactly this:
  "struct NewSecurity : public SecurityAdaptor {...}; useSecurityAdaptors<NewSecurity>()".
- **Noise is already the only security protocol** on the gossip host
  (`thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pub_sub.cpp:186`), so all traffic is
  already encrypted and the remote peer's identity (static pubkey → PeerId) is established
  by the time `secureInbound` returns — the gate can check it.

## Investigation Trail

1. Initial scan of `ThirdParty/c-libp2p` (the C implementation) — dead end; SuperGenius
   uses the **C++** libp2p at `GeniusNetwork/thirdparty/libp2p` (include paths
   `libp2p/...` via the thirdparty build, confirmed by `globaldb.cpp:16-19`).
2. Grep for gator/PSK/verifier in the C++ libp2p: zero hits → no native gating.
3. Traced inbound upgrade path: `UpgraderSession::secureInbound`
   (`src/transport/impl/upgrader_session.cpp:42`) → `UpgraderImpl::upgradeToSecureInbound`
   → multiselect → adaptor. Confirmed adaptor-level rejection kills the connection.
4. Located the real host construction: **NOT `GlobalDB::New`**. `GlobalDB::New` receives an
   already-built `pubsub` and uses `m_pubsub->GetHost()` (`globaldb.cpp:326`). The host is
   built in `GossipPubSub::Init` → `MakeCustomHostInjector`
   (`gossip_pub_sub.cpp:175-189, 222`).
5. Found a **second, independent host**: `src/processing/impl/processing_core_impl.cpp:96`
   calls `makeHostInjector` directly with default (Plaintext+Noise) security — a gating
   implementation must cover this host too, or private-network processing nodes leak a
   public-capable listener.

## Results

| Sub-question | Verdict | Finding |
|---|---|---|
| Connection gating | ✓ VALIDATED | No native gator, but a custom `SecurityAdaptor` bound via `useSecurityAdaptors<>()` denies non-members during upgrade. Cleanest form: a gate adaptor exposing a network-specific protocol name (e.g. `/gnus-net/<networkId-hash>/1.0.0`) wrapping Noise — outsiders fail multistream-select before any payload; members additionally check remote PeerId against TrustedPeerRegistry after Noise identifies the peer. |
| PSK transport protection | ⚠ PARTIAL | A go-style PSK layer (encrypting even before security negotiation, hiding the libp2p fingerprint) does NOT exist and would require new transport-level code. However, the threat model is already covered: Noise-only security gives encrypted, peer-authenticated traffic; the missing piece was *membership*, which the gate adaptor provides. True PSK obfuscation only matters if protocol fingerprinting is a requirement — currently it isn't. |
| Attach point | ✓ VALIDATED (with correction) | Not `GlobalDB::New` — it consumes `pubsub->GetHost()`. The attach point is `GossipPubSub::Init`/`MakeCustomHostInjector` in **our own** `thirdparty/ipfs-pubsub` (`gossip_pub_sub.cpp:175`), which already forwards variadic DI args — needs a small change to thread a gate adaptor (or config) through `GossipPubSub`'s constructors. Plus `processing_core_impl.cpp:96` needs the same adaptor bound. |

**Surprises:**
- Two independent libp2p hosts exist per node (gossip + processing); both need gating.
- `MakeCustomHostInjector` already pins Noise-only — the project is one step from private
  networking already.

**Recommended enforcement layer:** security-adaptor gate (transport-level, no fork),
with TrustedPeerRegistry as the allow-list source distributed via SecureCrdt. PSK-style
symmetric secrets only if protocol fingerprint hiding becomes a requirement later.

## How to Run (follow-up verification when implementing)

Compile probe deferred to implementation: bind a stub `SecurityAdaptor` via
`useSecurityAdaptors<>()` in a test host and assert an unregistered peer's connection
fails upgrade. Add as the first task of the private-network phase.

## Signal for the Build

- Implement `PskGateAdaptor : public SecurityAdaptor` in SuperGenius (wraps Noise).
- Thread it through `GossipPubSub` config → `MakeCustomHostInjector` (modify
  `thirdparty/ipfs-pubsub`, which we own).
- Bind the same adaptor in `processing_core_impl.cpp:96`.
- Derive the gate protocol name and gossip topics from `privateNetworkId`.
- See `.planning/notes/private-network-identity.md` for the identity design.
