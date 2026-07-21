# Phase 8: MultiSig Primitive - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-21
**Phase:** 8-MultiSig Primitive
**Areas discussed:** Payload shape, Quorum evaluation API shape, Duplicate/invalid signature handling

---

## Payload shape

| Option | Description | Selected |
|--------|-------------|----------|
| Raw bytes only | Opaque bytes already-serialized by the caller; simplest, matches ConsensusAuth's signing-bytes-builder pattern | ✓ |
| Typed wrapper like ConsensusSubject | Mirror ConsensusAuth's ConsensusSubject shape (account_id + subject_type_hash + payload + payload_hash) | |

**User's choice:** Raw bytes only (Recommended)
**Notes:** Keeps Phase 9's ISignedCRDTData implementers in control of their own payload codec.

---

## Quorum evaluation API shape

| Option | Description | Selected |
|--------|-------------|----------|
| Stateless function | EvaluateQuorum(signer_set, threshold, collected_signatures) -> bool, easy to unit test in isolation | ✓ |
| Stateful accumulator object | Feed signatures in one at a time, object tracks quorum status internally | |

**User's choice:** Stateless function (Recommended)
**Notes:** Phase 9's CRDT filter callback re-invokes it each time with the current signature set read from CRDT; state lives in CRDT, not in the primitive.

---

## Duplicate/invalid signature handling

| Option | Description | Selected |
|--------|-------------|----------|
| Ignore duplicates, skip invalid, count valid-uniques | Dedup by signer identity, silently skip invalid signatures, count quorum against remaining valid-unique set | ✓ |
| Reject whole batch on any invalid signature | Any invalid signature fails the entire evaluation | |

**User's choice:** Ignore duplicates, skip invalid, count valid-uniques (Recommended)
**Notes:** Matches CRDT's eventually-consistent nature (same signer's entry may appear more than once); avoids a single malformed/malicious entry blocking legitimate quorum.

---

## Claude's Discretion

- Exact C++ types/signatures for the primitive's public API (function names, threshold expressed as count vs fraction, error/result type shape).
- Where in the source tree the new component lives (e.g. `src/multisig/` vs `src/blockchain/` vs `src/crdt/`).

## Deferred Ideas

- Raw-public-key signer identity (in addition to account-address string) — may be needed in Phase 10 for `TrustedPeerRegistry` genesis seeding of peers without registered `GeniusAccount` addresses yet. Revisit during Phase 10 discussion.
