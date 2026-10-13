---
title: SG node consumes privateNetworkId (network config, CRDT namespace, topics)
trigger_condition: When the private-network phase starts (after TokenContracts Phase 14.1 mint validation ships)
planted_date: 2026-08-25
---

# Seed: SG node consumes privateNetworkId

Once licenses mint with validated `privateNetworkId = uint256(Ed25519 pubkey)`
(TokenContracts Phase 14.1), wire it through the SG node:

- Config: `sgns_config.json` gains the network pubkey + PSK; retire/repurpose the reserved
  `subnet_id_` (`src/account/GeniusNode.hpp:876`).
- CRDT: per-network top-level branches `/chain/<privateNetworkId>/...` so mainnet and child
  chains coexist on one node (`src/crdt/hierarchical_key.hpp`).
- Pubsub/GossipSub topics and chain-ID strings (`GENIUS_CHAIN_ID`) derived from the
  networkId instead of hardcoded constants.
- Membership: network key signs TrustedPeerRegistry peer lists; PSK distribution/rotation
  signed by the same key (see notes/private-network-identity.md).

Depends on the libp2p PSK spike todo for the transport-enforcement decision.
