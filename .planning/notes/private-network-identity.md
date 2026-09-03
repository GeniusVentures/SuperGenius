---
title: Private network identity — privateNetworkId = uint256(Ed25519 pubkey)
date: 2026-08-25
context: /gsd-explore session; settled jointly with TokenContracts/gnus-ai (commit 1f28d4c)
---

# Private network identity for SuperGenius

Implementation tracking: **GeniusVentures/SuperGenius#367** (this repo's private-networks
phase, assigned henriqueaklein, blocked by **GeniusVentures/libp2p#10** — gater + pnet,
assigned itsafuu).

Design settled 2026-08-25. Authoritative write-up lives in
`TokenContracts/gnus-ai/.planning/notes/network-pubkey-identity.md` — this note captures
the SG-side consequences.

## Decision

`privateNetworkId` (already in the `NFT` struct at slot +12, Phase 14 D-03 in
`GNUSNFTFactoryStorage.sol`) **is the network's Ed25519 public key**, stored as its raw
32-byte value as `uint256`. Self-certifying: the ID is the identity — SG nodes verify
network membership signatures directly against the configured ID. `0` = public network
(a real Ed25519 pubkey can never be all-zero). Key rotation = expire (`validUntil`) +
remint; never update the field in place.

Contract-side follow-up (mint validation, split-mint SKU) is tracked as Phase 14.1 in
gnus-ai: `.planning/todos/pending/phase-14-1-network-key-mint-validation.md`.

## SG-side state today (research findings, 2026-08-25)

- ChainID is a hardcoded string: `GENIUS_CHAIN_ID = "supergenius_chain"`
  (`src/account/GeniusTransaction.hpp:36`), `"supergenius"`
  (`src/account/TransactionManager.hpp:781`). No child-chain type exists.
- `subnet_id_` is a reserved, unused `uint16_t` from `sgns_config.json`
  (`src/account/GeniusNode.hpp:876`, `TransactionManager.hpp:553`). Obsoleted by this
  design — at most a log/topic fingerprint.
- CRDT foundation fits: `src/crdt/hierarchical_key.hpp` (`IsTopLevel()`/`ChildString()`),
  GlobalDB, signed/quorum layer in `src/securecrdt/`. Adding a `/chain/<id>/` top-level
  namespace is structurally trivial.
- Closest membership hook: `src/trustedpeer/TrustedPeerRegistry` — signed peer lists via
  SecureCrdt; not a libp2p connection gate.
- No PSK / connection-gating / peer-allowlist code anywhere in the libp2p host setup
  (`GlobalDB::New`, `src/crdt/globaldb/globaldb.cpp`). Private networking is greenfield.

## SG-side conventions

- CRDT: `/chain/<privateNetworkId>/...` top-level HierarchicalKey branches; child AI NFT
  token IDs underneath. Mainnet + child chains coexist as sibling branches on one node.
- Transport privacy: libp2p PSK-protected swarm (feasibility unverified — see spike todo).
- Membership management: extend TrustedPeerRegistry — network key signs peer lists / PSK
  rotation, distributed via SecureCrdt.
- Solidity never verifies Ed25519 against the ID — verification is off-chain in SG nodes.

## Pubsub privacy (decided 2026-08-26)

Gossipsub has no per-topic ACLs; channel privacy is inherited from connection-level
gating (owner: "Layer 1 is good enough"). On a private network — once libp2p#10
(gater/pnet) and #367 land — non-members cannot complete a handshake, so every topic,
including `ipfspubsub://results/<task>` ELM channels (AsyncIOManager#11), is
automatically members-only. No per-topic payload encryption is planned; gossipsub's
publisher signatures cover authenticity.
