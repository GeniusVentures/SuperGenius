# Phase 15: Private Network Identity and libp2p Gating - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-31
**Phase:** 15-private-networks-consume-privatenetworkid-identity-and-bind
**Areas discussed:** private-network identity, registry hierarchy, SecureCRDT authorization, public/private job scope, bootstrap and provisioning

---

## Private-network identity and configuration

| Option | Description | Selected |
|--------|-------------|----------|
| `sgns_config.json` identity | The original issue's config location | |
| `network_config.json` identity | Keep public identity beside existing pnet transport settings | ✓ |
| Runtime NFT lookup | Node reads the license NFT during startup | |

**User's choice:** `network_config.json` holds the private-network configuration, and the application owner provisions it offline with the correct values.

**Notes:** `private_network_id` is a public identity from the license NFT; `network_key` is a secret pnet credential. They cannot be the same value because the NFT identity is public. Nodes do not fetch either value at runtime.

---

## Global trust versus private membership

| Option | Description | Selected |
|--------|-------------|----------|
| Private TrustedPeerRegistry instances | Recreate trusted peers for every private network | |
| Global TrustedPeerRegistry only | Preserve one global root trust set | ✓ |
| No membership registry | Rely only on pnet possession | |

**User's choice:** TrustedPeerRegistry is global only. Introduce a separate SecureCRDT-backed NetworkRegistry for private-network membership.

**Notes:** The global trusted-peer majority signs each NetworkRegistry bootstrap record. After confirmation, the private network's current peers sign later registry updates by quorum. Private workers do not need to be global trusted peers, and no peer may admit itself alone.

---

## SecureCRDT authorization model

| Option | Description | Selected |
|--------|-------------|----------|
| Hard-wire TrustedPeerRegistry | Every SecureCRDT key uses the global trusted peers | |
| PeerRegistry per key/policy | SecureCRDT keys select their own PeerRegistry signer source | ✓ |
| ValidatorRegistry consensus | Use per-network validator consensus for SecureCRDT updates | |

**User's choice:** Introduce `PeerRegistry` as the common registry abstraction. SecureCRDT policy entries bind to the relevant registry instance per key.

**Notes:** TrustedPeerRegistry is the root PeerRegistry; NetworkRegistry is a child PeerRegistry. ValidatorRegistry is a separate consensus concern and does not authorize NetworkRegistry updates.

---

## Public and private job scope

| Option | Description | Selected |
|--------|-------------|----------|
| Namespace all node data | Apply a private prefix indiscriminately | |
| Job-derived scope | Public jobs retain public routing; private jobs route every derived artifact to one private scope | ✓ |
| Shared-only work plane | Publish all tasks/settlement globally | |

**User's choice:** Jobs can be public or private. Scope is attached to the job, and private job artifacts stay within that network; public jobs remain compatible with existing public behavior.

**Notes:** ValidatorRegistry is separate per public/private network. Private task, escrow, result, and payout flows use the private network's state and consensus scope.

---

## the agent's Discretion

- Concrete PeerRegistry API and registry-reference lifetime strategy.
- NetworkRegistry payload schema, safe threshold configuration, and key-pattern layout.
- Non-secret pnet metadata and the operational rotation protocol.
- Exact job-scope encoding and public-job migration details.

## Deferred Ideas

- Runtime/on-chain retrieval of private-network configuration.
- Replicating or distributing raw pnet secrets through CRDT.
- Unrelated bridge race and mock-RPC todos, reviewed as false-positive matches.
