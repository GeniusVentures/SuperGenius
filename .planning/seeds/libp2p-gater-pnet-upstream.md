---
title: Author gater + pnet in GeniusVentures/libp2p as open-source upstream features
trigger_condition: When the private-network phase's fast path (SecurityAdaptor gate, spike 001) is proven, or when ecosystem/upstream contribution work is scheduled
planted_date: 2026-08-25
---

# Seed: gater + pnet for cpp-libp2p (authored, not ported)

Spike 002 established that no cpp-libp2p implementation has go-libp2p's connection
gating (`gater/`) or private networks (`pnet/`) — they would be genuinely new code.
Owner direction 2026-08-25: the `../thirdparty` repos are open source and others should
benefit, so these belong in `GeniusVentures/libp2p` as proper features, offered upstream
to `libp2p/cpp-libp2p`.

## Design inputs

- **gater**: mirror go-libp2p's `ConnectionGater` semantics — `InterceptPeerDial`,
  `InterceptAddrDial`, `InterceptAccept`, `InterceptSecured`, `InterceptUpgraded` —
  hooked into `tcp_listener.cpp` / `upgrader_impl.cpp` / dialer, bound via a
  `useConnectionGater<>()` injector helper (pattern already documented at
  `network_injector.hpp:86-95` for security adaptors).
- **pnet**: PSK-protected transport wrapper (go-libp2p spec: `ipfs/conn/pnet`), applied
  before security negotiation so the libp2p fingerprint is hidden; injector takes a PSK
  via `usePsk()`-style binding.
- SuperGenius consumes both through standard injector bindings in
  `thirdparty/ipfs-pubsub`'s `MakeCustomHostInjector` and
  `src/processing/impl/processing_core_impl.cpp:96` (which also needs its
  Plaintext-exposure fixed).
- Membership data source: TrustedPeerRegistry via SecureCrdt; network identity =
  `privateNetworkId` (see notes/private-network-identity.md).

## Sequencing

SecurityAdaptor gate first (fast path, zero fork changes) → gater/pnet in the fork →
upstream PRs → SuperGenius migrates to the standard bindings.
