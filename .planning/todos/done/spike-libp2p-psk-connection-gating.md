---
title: "Spike: c-libp2p PSK / connection-gating feasibility"
date: 2026-08-25
priority: P1
source: /gsd-explore 2026-08-25 (see notes/private-network-identity.md)
---

# Spike: can the vendored c-libp2p do PSK-protected private networks?

Determine whether the vendored C++ libp2p
(`/Users/Shared/SSDevelopment/Development/ThirdParty/c-libp2p` checkout, included via
`libp2p/...` in `src/crdt/globaldb/globaldb.cpp`) supports the hooks needed for a
private-network swarm, and where they attach in `GlobalDB::New` host construction.

## Questions to answer

1. Does the injector (`libp2p/injector/host_injector.hpp`, `kademlia_injector.hpp`) accept
   a connection gator / `ConnectionGate`-style customization point?
2. Is there a PSK-protected upgrader / security-protocol pluggability (a `RawConnectionVerifier`
   equivalent) — i.e., can we reject/handshake-fail peers lacking the network PSK before
   any protocol traffic?
3. If neither exists: what is the minimal upstream addition, and is forking vendored
   c-libp2p acceptable vs. application-level gating via TrustedPeerRegistry?
4. Does GossipPubSub topic derivation need the networkId to prevent cross-network topic
   collisions?

## Outcome target

A short spike report: recommended enforcement layer (transport-level gate, application-level
TrustedPeerRegistry gate, or both) with evidence from the vendored source.
