# Phase 8: MultiSig Primitive - Context

**Gathered:** 2026-07-21
**Status:** Ready for planning

<domain>
## Phase Boundary

A standalone, node-independent C++ component that (1) computes canonical signing-bytes for an arbitrary payload and verifies signatures against those bytes, reusing `ConsensusAuth`'s SHA-256/`VerifySignature` primitives, and (2) evaluates N-of-M quorum for a given signer set and threshold. It must be constructible, exercisable, and unit-testable with no running node, no CRDT store, and no network dependency. This phase does NOT touch CRDT, `ISignedCRDTData`, `TrustedPeerRegistry`, `BURN_BASIS_POINTS`, or `ValidatorRegistry` — those are Phases 9-12.

</domain>

<decisions>
## Implementation Decisions

### Payload shape
- **D-01:** The primitive operates on raw, already-serialized bytes (opaque `std::vector<uint8_t>`/`std::string` payload), not a typed envelope like `ConsensusAuth`'s `ConsensusSubject` (account_id/subject_type_hash/payload/payload_hash). Callers (Phase 9's `ISignedCRDTData` implementers) own their own payload serialization/codec and hand the primitive already-canonical bytes to sign/verify.

### Signer identity
- **D-02:** Signers are identified by account address string, consistent with `GeniusAccount::Sign`/`GeniusAccount::VerifySignature` (`src/account/GeniusAccount.hpp:207,216`). No separate raw-public-key identity path was requested for this phase — deferred to Phase 10 if genesis-time seeding needs it (see Deferred Ideas).

### Quorum evaluation API shape
- **D-03:** Quorum evaluation is a pure, stateless function: `EvaluateQuorum(signer_set, threshold, collected_signatures) -> bool` (or equivalent result type), not a stateful accumulator object. Callers (Phase 9's CRDT filter callback) re-invoke it each time with the current signature set they've read from CRDT. This keeps the primitive trivially unit-testable per Phase 8's success criteria (no CRDT/network dependency) and pushes any state management (e.g. what signatures have been collected so far) to Phase 9, where it naturally belongs (CRDT already holds that state).

### Duplicate / invalid signature handling
- **D-04:** Quorum evaluation deduplicates by signer identity (keep at most one valid signature per signer), silently skips any signature that fails verification (does not reject the whole batch), and counts quorum against the remaining valid-unique signer count. Rationale: matches CRDT's eventually-consistent nature (the same signer's entry can legitimately appear more than once across replicas) and prevents a single malformed/malicious entry from blocking legitimate quorum.

### Claude's Discretion
- Exact C++ types/signatures for the primitive's public API (function names, whether quorum threshold is expressed as a raw count or a fraction, error/result type shape) — not discussed, left to planner/implementer following existing codebase conventions (`snake_case_`, `std::shared_ptr` factory pattern where applicable, Doxygen `@param` docs).
- Where in the source tree the new component lives (e.g. `src/multisig/` vs `src/blockchain/` vs `src/crdt/`) — not discussed.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Signing/verification primitives to reuse
- `src/blockchain/ConsensusAuth.hpp` — canonical signing-bytes builder pattern (`ProposalSigningBytes`, `VoteSigningBytes`, `VoteBundleSigningBytes`), deterministic id via SHA-256 (`ComputeProposalId`, using `crypto::sha2_256`), and signature validation via `GeniusAccount::VerifySignature`. This is the primitive Phase 8 must reuse directly (per milestone decision — see PROJECT.md Key Decisions), not `ConsensusManager`'s proposal/vote/certificate lifecycle.
- `src/account/GeniusAccount.hpp:207` (`Sign`) and `:216` (`static VerifySignature`) — underlying signing/verification primitive, impl in `GeniusAccount.cpp:790,845`.

### Precedent for the pattern this generalizes
- `src/blockchain/ValidatorRegistry.hpp` — `ComputeUpdateSigningBytes`/`VerifyUpdate` (lines ~470,477) show the same signing-bytes + quorum-verification pattern already applied to a registry update requiring an authorized signer set. Weight-based quorum machinery: `WeightConfig`, `QuorumThreshold`, `IsQuorum`, `EvaluateSlotQuorum` (lines ~68-103, 160-203). This is the component Phase 12 will migrate onto the new abstraction — Phase 8's primitive should be generalizable enough to eventually replace this logic without a redesign.

### Project-level context
- `.planning/PROJECT.md` — v1.1 milestone goal, Key Decisions table (esp. "Reuse ConsensusAuth primitives directly, not ConsensusManager" and "ISignedCRDTData interface-based per-type classes, not generic template").
- `.planning/REQUIREMENTS.md` §"Milestone v1.1" — MSIG-01, MSIG-02, MSIG-03 (this phase's requirements).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ConsensusAuth.hpp` signing-bytes/SHA-256/verify helpers — the core primitives this phase wraps/reuses.
- `GeniusAccount::Sign`/`VerifySignature` — underlying crypto operations, already proven and in use.

### Established Patterns
- C++17, `snake_case_` for private members, `std::shared_ptr` factory pattern, Doxygen `@param` docs on public API (`.planning/codebase/CONVENTIONS.md`).
- `ValidatorRegistry`'s quorum/weight evaluation (`IsQuorum`, `EvaluateSlotQuorum`) is the closest existing precedent for quorum-threshold logic shape.

### Integration Points
- None yet — this phase is foundational and standalone. Phase 9 (SecureCRDT) will be the first consumer.

</code_context>

<specifics>
## Specific Ideas

No specific implementation-detail preferences beyond the four decisions above — user deferred exact API shape to Claude's discretion.

</specifics>

<deferred>
## Deferred Ideas

- Raw-public-key signer identity (not just account-address string) — may be needed in Phase 10 if `TrustedPeerRegistry` genesis seeding needs to bootstrap peers before they have registered `GeniusAccount` addresses. Revisit during Phase 10 discussion if it becomes a blocker.

</deferred>

---

*Phase: 8-MultiSig Primitive*
*Context gathered: 2026-07-21*
