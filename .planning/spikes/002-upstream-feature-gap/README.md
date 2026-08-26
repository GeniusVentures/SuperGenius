---
spike: 002
name: upstream-feature-gap
type: standard
validates: "Given upstream cpp-libp2p (libp2p/cpp-libp2p master), when checked for connection gating / PSK features our fork lacks, then we can port them instead of writing new code"
verdict: INVALIDATED
related: [001-libp2p-private-network-feasibility]
tags: [libp2p, upstream, private-network]
---

# Spike 002: Upstream Feature Gap — can we port gating/PSK instead of writing it?

## What This Validates

Premise (owner-stated): `../thirdparty` repos are ours, so anything "missing" is just a
feature other libp2p implementations have that we could port and ship.

## Investigation Trail

1. Confirmed `thirdparty/libp2p` is our fork: remote `git@github.com:GeniusVentures/libp2p.git`,
   detached at `1ae9b49` (fork point: `d2f2fd1` "Fix kademlia (#202)").
2. Added upstream `libp2p/cpp-libp2p` remote, fetched master (`87ae3d8`).
3. `git ls-tree -r upstream/master | grep -iE "gator|psk|raw_connection_verif"` → **zero hits**.
   Upstream cpp-libp2p master has **no connection gator and no PSK support either** —
   these exist only in go-libp2p (`gater/`, `pnet/`). The C++ implementation never got them.
4. Diffed fork vs upstream across `security/`, `transport/`, `injector/`: differences are
   formatting/refactor scale (upstream's big commits are clang-format + typo fixes);
   no gating-adjacent feature exists to pull.

## Results

**Verdict: INVALIDATED (the port premise) — but the "we can add and ship" half is
trivially true and already sufficient.**

- There is nothing to port: no cpp-libp2p implementation anywhere has a gator or PSK.
  Writing one in our fork would be genuinely new code (~a `gator/` directory, connection
  manager integration, injector plumbing) — significant work for capability we don't need.
- Spike 001's approach needs **zero changes to the libp2p fork**: the `SecurityAdaptor`
  DI hook already exists. The only repo needing changes is `thirdparty/ipfs-pubsub`
  (ours) to thread a gate adaptor through `GossipPubSub`.
- Optional later: contribute a gator upstream once the gate adaptor design is proven —
  good citizenship, not a dependency.

## Signal for the Build

Stick with the spike-001 plan. If protocol-fingerprint hiding (go-style PSK) ever becomes
a hard requirement, that's new code in `GeniusVentures/libp2p` (a PSK-protected
transport wrapper), not a port — estimate accordingly.

Note: added `upstream` remote to the local clone for this spike; harmless, but remove
with `git remote remove upstream` in `thirdparty/libp2p` if you want the clone pristine.
