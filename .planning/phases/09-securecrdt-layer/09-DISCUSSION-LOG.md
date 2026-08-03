# Phase 9: SecureCRDT Layer - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-23
**Phase:** 9-SecureCRDT Layer
**Areas discussed:** Pending signature storage, CRDT key layout, Local-write self-validation gap, Quorum finalization semantics

---

## Pending signature storage

| Option | Description | Selected |
|--------|-------------|----------|
| Store in CRDT | Each signer's signature is its own durable, replicated CRDT entry | ✓ |
| In-memory per-node, like ValidatorRegistry | Signatures accumulate in local process memory, simpler but not crash-safe/invisible to other peers | |

**User's choice:** Store in CRDT (Recommended)
**Notes:** Matches the milestone-level decision that CRDT itself carries all propose/sign/quorum messages, no new networking.

---

## CRDT key layout

| Option | Description | Selected |
|--------|-------------|----------|
| base_key/pending (value) + base_key/sig/<address> | Proposed value at a "pending" sub-key, signatures at sig/<address> sub-keys, promoted to final base_key on quorum | ✓ (later revised — see Quorum finalization below) |
| Single combined entry, no sub-keys | Value + all signatures in one blob, rewritten on each new signature | |

**User's choice:** base_key/pending + base_key/sig/<address> (Recommended)
**Notes:** This was later refined during the Quorum finalization discussion — the "pending" vs "final" distinction was dropped in favor of a single value key, since finalization is now a reader-side computation, not a write.

---

## Local-write self-validation gap

| Option | Description | Selected |
|--------|-------------|----------|
| SecureCRDT wrapper validates before every Put | All registered-key writes go through a wrapper that runs EvaluateQuorum/VerifyPayloadSignature before Put — single enforcement point | ✓ |
| Rely on filter symmetry / self-discipline | Trust callers to follow the correct flow by convention | |

**User's choice:** SecureCRDT wrapper validates before every Put (Recommended)
**Notes:** GlobalDB's filter callback only runs on remote-originated deltas (confirmed via research on crdt_datastore.cpp's `GetDeltaFromNode`), so local writes need their own enforcement point.

---

## Quorum finalization semantics

| Option | Description | Selected |
|--------|-------------|----------|
| Every node promotes independently on quorum detection | Each node's NewElementCallback re-evaluates quorum and writes the confirmed value to base_key itself once met | Initially considered |
| Only the original proposer promotes | Single-writer semantics, but proposer going offline post-quorum stalls the value | Rejected |
| **User's actual model (adopted):** No promotion write at all | A value is just data; each reader independently decides trust by checking signatures/quorum on read. Avoids single point of trust and bad-actor "fake final" writes. | ✓ |

**User's choice:** "Not sure it needs to write anything... some data is written to CRDT and all peers don't register as valid until the signatures are met. If they are, the node can consider that final, but that is an interpretation of every node. If we broadcast it on CRDT it just complicates things because each node can interpret on their own and have to deal with bad actor trying to validate something they didn't."
**Notes:** This became D-04 (revised) — no separate "final" CRDT write; validity is always re-derived per-reader from the value + sig/* entries + EvaluateQuorum. Followed up with a performance-cache clarification (D-05): a local-only RocksDB cache memoizes "quorum already verified for base_key at value X" purely to avoid rescanning sig/* on every read — never replicated, never trusted from other peers, invalidated on new sig/* entries.

---

## Claude's Discretion

- Exact registry API shape for declaring {topic/key pattern, signer-set source, quorum rule, ISignedCRDTData type} entries.
- Exact per-type serialization format (each ISignedCRDTData implementer owns its own payload codec).
- Location and key scheme of the local RocksDB performance cache (D-05).

## Deferred Ideas

None new this phase. Carried forward from Phase 8: raw-public-key signer identity, still deferred to Phase 10 discussion if genesis-time TrustedPeerRegistry seeding needs it.
