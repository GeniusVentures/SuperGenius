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

## Requirements (from spike 002)

- Do NOT add a gator/PSK to the libp2p fork — nothing exists upstream to port, and the
  SecurityAdaptor route requires zero libp2p changes. Author fork features only if
  protocol-fingerprint hiding becomes a hard requirement.
