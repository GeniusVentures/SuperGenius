# Spike Manifest

## Idea

Run SuperGenius private networks over libp2p, with network identity = the license NFT's
`privateNetworkId` (uint256 Ed25519 pubkey). Determine whether the vendored C++ libp2p
supports connection gating / PSK-style private swarms, and where enforcement attaches in
the node's host construction.

## Requirements

- No forking of vendored `thirdparty/libp2p` (SuperGenius-owned `thirdparty/ipfs-pubsub`
  may be modified — it is our code)
- Plaintext security already disabled (Noise-only) in the production host injector
- Enforcement must cover ALL libp2p hosts the node creates (gossip host AND processing host)

## Spikes

| # | Name | Type | Validates | Verdict | Tags |
|---|------|------|-----------|---------|------|
| 001 | libp2p-private-network-feasibility | standard | Given vendored C++ libp2p, when building a host via injector, then non-member peers can be denied at the security-negotiation layer without forking | ✓ VALIDATED | libp2p, security, networking |
| 002 | upstream-feature-gap | standard | Given upstream cpp-libp2p, when checked for gating/PSK features our fork lacks, then they can be ported | ✗ INVALIDATED | libp2p, upstream, private-network |

## Requirements (from spike 002, revised by owner 2026-08-25)

- The `thirdparty` repos are open source and should serve the broader ecosystem: a
  go-libp2p-style connection **gater** and **pnet** (PSK private networks) belong in
  `GeniusVentures/libp2p` as proper upstream-quality features, not SuperGenius-local hacks.
- SuperGenius should still consume them through the standard injector surface
  (`useConnectionGater` / `usePsk`-style bindings) once they exist.
- Sequencing: the SecurityAdaptor gate (spike 001) remains the fast path for the
  private-network phase; gater/pnet land in the fork as the durable, shareable
  implementation and SuperGenius migrates onto them.
