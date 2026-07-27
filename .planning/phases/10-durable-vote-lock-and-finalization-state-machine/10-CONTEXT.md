# Phase 10: Durable Vote Lock and Finalization State Machine - Context

**Gathered:** 2026-07-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement a durable per-slot validator vote journal and a single idempotent finalization state machine. A validator may collect competing candidates during a bounded window, but must persist the exact signed vote before publication, restore and replay it after restart, and never produce a competing usable signature while the first remains certificate-valid. All certificate delivery paths converge on one finalization operation that establishes the authoritative slot certificate before transaction application or cleanup. Bridge burn reservation and consumption remain Phase 11, and end-to-end race verification remains Phase 12.

</domain>

<decisions>
## Implementation Decisions

### Vote-journal lifecycle
- **D-01:** Consensus initialization fails before subscriptions or vote publication if the persisted vote journal is unreadable or internally inconsistent. There is no observer-only or partial-slot recovery mode.
- **D-02:** Each journal entry preserves the canonical slot, proposal ID, complete exact signed vote bytes, validator identity, and validation/expiry metadata. Replay is byte-identical; votes are never reconstructed or re-signed.
- **D-03:** After a crash between durable write and publication, restart automatically republishes the exact stored vote if it remains valid. It never creates a replacement vote.
- **D-04:** An uncertified vote lock may retire only when the existing proposal/certificate validity rules establish a deterministic acceptance horizon beyond which that signature cannot contribute. Retirement must be durable before another vote can be recorded.

### Candidate-selection window
- **D-05:** The first valid proposal observed locally for an empty slot starts one bounded candidate-selection window.
- **D-06:** The window uses a fixed network-configured duration. Later candidates never reset or extend its deadline.
- **D-07:** At the deadline, candidate selection freezes atomically. A later better proposal may be retained for diagnostics and certificate acceptance, but cannot change the local vote.
- **D-08:** Candidate ranking uses the existing proposal comparator. Exact ordering ties resolve to the lexicographically smallest deterministic proposal ID; arrival order and local ownership are never tie-breakers.

### Finalization and replay
- **D-09:** The authoritative canonical slot certificate is the sole durable winner authority. A separate durable processing marker exists only to make transaction application and cleanup idempotent; it is not a competing finality record.
- **D-10:** Local certificate submission, pubsub delivery, and CRDT replay normalize their input and call the same `FinalizeSlot(certificate)` operation.
- **D-11:** If transaction application fails after certificate persistence, the slot remains finalized and the processing marker remains pending until the exact winning transaction succeeds. Local application failure never rolls back finality.
- **D-12:** Proposal candidates, pending votes, and temporary vote state remain available until the winning transaction is applied successfully and the durable processing marker is complete. Finalization is always established before cleanup.
- **D-13:** A valid certificate finalizes its proposal even when it differs from the validator's local vote; the local vote transitions to finalized without applying or voting for another winner.

### Conflicting certificates
- **D-14:** A different otherwise-valid certificate for a finalized slot is a safety violation. Preserve the original winner, reject the conflict, stop further consensus participation for that slot, and never overwrite or apply a second winner.
- **D-15:** Persist structured conflict evidence containing the slot ID, both proposal IDs, both certificate digests, delivery source, and first/last observation timestamps. Do not duplicate full certificate bytes in the conflict record.
- **D-16:** Repeated delivery of the same conflict is idempotent: deduplicate by a canonical pair of certificate digests and update only observation count and last-seen time.
- **D-17:** Never rebroadcast a conflicting certificate through the normal certificate channel. Emit a critical log and metric in addition to the durable local conflict record.

### the agent's Discretion
- Choose the concrete journal and processing-marker record encodings, persistence keys, and atomic-batch boundaries consistent with the decisions above and the existing GlobalDB/CRDT facilities.
- Choose the configuration name and safe default duration for the candidate-selection window, while ensuring all local timing transitions are race-safe.
- Choose the internal state types and synchronization primitives used to express candidate, voted, finalizing, finalized-pending-application, applied, retired, and safety-violation states.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone scope and requirements
- `.planning/ROADMAP.md` — Phase 10 goal, dependency, requirements, and success criteria; also fixes bridge reservation work in Phase 11 and full race verification in Phase 12.
- `.planning/REQUIREMENTS.md` — Normative definitions for CERT-05 through CERT-07 and VOTE-01 through VOTE-07.
- `.planning/PROJECT.md` — Observed double-certificate failure, root cause, protocol constraints, existing identities, compatibility obligations, and primary integration points.

### Existing finality contract
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-CONTEXT.md` — Canonical slot identity, authoritative slot-keyed certificate storage, transaction-hash index, idempotent identical-certificate replay, and conflict rejection established by Phase 9.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ConsensusManager::GetSlotKey` and Phase 9 slot-certificate APIs: use the established canonical slot identity and authoritative `/cert/v2/slot/<slot>` storage rather than introducing a second finality identity.
- Existing certificate work journal: extend or mirror its durable processing/recovery pattern for finalization application markers and startup replay.
- GlobalDB/CRDT atomic batch patterns: use the established persistence primitives for vote-before-publish and finality-before-cleanup ordering.
- Existing proposal comparator: preserve its ranking semantics and add only the deterministic proposal-ID tie-break required by D-08.

### Established Patterns
- Authoritative certificates are stored by canonical slot, with `/cert/v2/tx/<tx>` retained only as a verified compatibility index.
- Byte-identical certificate replay is idempotent, while a different certificate for an occupied slot is rejected without overwrite.
- Startup protocol-state validation fails closed rather than silently mixing corrupt or incompatible durable state.
- Certificate validity outranks local candidate preference; local selection governs only which proposal this validator signs.

### Integration Points
- `src/blockchain/Consensus.hpp`: extend `ProposalState`, `SlotState`, and manager state with explicit selection, durable-vote, finalization, processing, and conflict states.
- `src/blockchain/Consensus.cpp`: integrate the state machine with `SubmitProposal`, `SubmitVote`, `SubmitCertificate`, `HandleProposal`, `HandleVote`, `HandleCertificate`, `ClearProposalSlot`, and `ValidateCertificateBestProposal`.
- Consensus startup/shutdown: restore and validate vote locks before subscriptions, proposal handling, or vote publication, and safely coordinate candidate-window timers.
- Certificate storage and CRDT callbacks: funnel local, pubsub, and CRDT deliveries into the same normalized finalization primitive.

</code_context>

<specifics>
## Specific Ideas

- The certificate itself is finality; the processing marker answers only whether this node has finished applying that already-final winner.
- A validator's temporary vote lock is necessary only until a certificate finalizes the slot or deterministic validation rules prove the signature unusable forever.
- Safety diagnostics should be actionable without creating a second copy of certificate payloads: stable digests correlate the conflict record to authoritative and received certificates.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window.md` — kept deferred to Phase 12 race verification. Phase 10 supplies the vote-window and finalization semantics, but this todo's multi-node behavioral proof belongs with TEST-01 through TEST-06.

</deferred>

---

*Phase: 10-Durable Vote Lock and Finalization State Machine*
*Context gathered: 2026-07-27*
