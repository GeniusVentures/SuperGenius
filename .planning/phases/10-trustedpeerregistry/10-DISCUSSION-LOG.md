# Phase 10: TrustedPeerRegistry - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-23
**Phase:** 10-TrustedPeerRegistry
**Areas discussed:** Genesis trust bootstrapping, Trust anchor location, Config location, Signer-set-source pattern, Membership change representation

---

## Genesis trust bootstrapping

The user's opening question: "How does zksnarks does it? I seem to remember a keypair in which you sign it and then delete the private key so it can never be hacked... If that is not the case, then we should have another member on sgns_config.json which is the bootstrapper_node and works like the authorized_full_node does for the Genesis stuff, but it's used in this one."

This correctly identified the zk-SNARK trusted-setup "toxic waste" ceremony pattern as the model: an ephemeral keypair signs the genesis value once, then the private key is destroyed so it can never sign again.

**Follow-up — how do other nodes know the address to verify against?**
User: "If the keypair is ephemeral and dynamically created, how would other nodes know which address to verify the signature? ... My idea would be that the bootstrapper node would be the pubkey part of that ephemeral keypair."
Confirmed: `bootstrapper_node` in config holds the ephemeral keypair's PUBLIC KEY (not a node reference), distributed out-of-band to every node ahead of time — same shape as `authorized_full_node`.

**Follow-up — is JSON config reliable enough, or should the pubkey live in the CRDT record itself?**
User raised a real concern: local JSON is editable. Considered "embed pubkey alongside the bootstrap record on CRDT" as an alternative — clarified this doesn't actually solve the bootstrapping problem (a self-describing record can't authenticate itself; something must be trusted out-of-band regardless). Two real anchor options were presented:

| Option | Description | Selected |
|--------|-------------|----------|
| Runtime JSON config, same as authorized_full_node | Consistent with existing precedent; tampering only affects the tampering node's own view | ✓ |
| Hardcoded in source code | Stronger tamper-resistance (Bitcoin-genesis-hash style), but new precedent, less deployment flexibility | |

**User's choice:** Runtime JSON config, same as authorized_full_node (Recommended)

**Final confirmation of the full genesis flow:**
User: "The trusted peers bootstrap value. The idea is to write the bootstrap on CRDT, signed by the private part of the bootstrap_node, correct?"
Confirmed: `ProposeValue` + one `AddSignature` call from the ephemeral private key, genesis `threshold=1`, `ReadIfQuorum` reports trusted via the normal quorum-of-1 case (no special-casing), then the private key is destroyed.

**User's choice:** Yes, exactly this (Recommended) — genesis becomes a real, CRDT-replicated, signature-verified record like any other TrustedPeerRegistry update.

An earlier framing ("just hardcode the list directly, no ceremony at all, matching ValidatorRegistry::CreateGenesisRegistry's unsigned pattern") was proposed and considered, but the user's tampering concern led to keeping the ceremony instead — this was a live back-and-forth, not a single-shot choice.

---

## Config location for genesis list

| Option | Description | Selected |
|--------|-------------|----------|
| sgns_config.json, new fields | Add trusted_peers + bootstrapper_node following existing bootstrap_fullnodes pattern | ✓ |
| New dedicated config file | Separates concern, one more file to manage | |

**User's choice:** sgns_config.json, new fields (Recommended)

---

## Signer-set-source pattern

| Option | Description | Selected |
|--------|-------------|----------|
| In-memory cache, mirrors ValidatorRegistry's cached_registry_ | Seeded from genesis, updated only after confirmed change | ✓ |
| Something else | | |

**User's choice:** Yes, in-memory cache (Recommended)
**Notes:** Avoids re-entrancy risk of calling ReadIfQuorum recursively inside the SignerSetSource callback itself.

---

## Membership change representation

| Option | Description | Selected |
|--------|-------------|----------|
| Whole new list each time | Simpler Verify/Apply and ReadIfQuorum semantics; all N-of-M signers must re-sign the full list per change | ✓ |
| Diff/delta operation | Smaller payloads, but harder Verify/Apply tracking and CRDT-merge concerns for concurrent diffs | |

**User's choice:** Whole new list each time (Recommended)

---

## Claude's Discretion

- Exact TrustedPeerRegistry public API shape (class name, method signatures).
- Exact ISignedCRDTData payload format for the trusted-peer list (protobuf vs simpler serialization).
- How the ephemeral keypair generation + one-time ceremony is actually invoked (tooling/operational detail, not core registry logic).

## Deferred Ideas

None new. Raw-public-key signer identity concern (deferred from Phase 8/9) was resolved during this phase's research: address IS the raw public key already, no separate identity type needed.
