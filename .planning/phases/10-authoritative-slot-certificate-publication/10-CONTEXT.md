# Phase 10: Authoritative Slot Certificate Publication - Context

**Gathered:** 2026-08-21
**Status:** Ready for planning

<domain>
## Phase Boundary

Move certificate authority from subject-hash storage to the generic `/cert/<canonical-slot-id>` namespace. Preserve the existing deterministic consensus-round publisher rotation, ensure only that publisher writes the authoritative record, persist before any best-effort PubSub announcement, and migrate transaction-backed certificate lookup to the transaction-derived slot. This phase does not redesign certificate consumption or mint application.

</domain>

<decisions>
## Implementation Decisions

### Publisher selection and authority

- **D-01:** Preserve the existing proposal-derived, deterministic consensus-round aggregator rotation. Phase 10 introduces no new publisher-selection or timeout mechanism.
- **D-02:** Only the locally selected current-round aggregator may enter the authoritative certificate write path. Receiving a certificate through PubSub never grants authority and never writes the CRDT key.
- **D-03:** A non-selected validator that sees quorum retains the evidence but neither writes nor advertises. It waits until a later normal round makes it eligible.
- **D-04:** An identical certificate replay is harmless. If concurrent valid certificate encodings contend for one slot, every replica deterministically resolves them by certificate-hash ordering rather than local first-seen order; the result must converge and never use an overwrite race. Phase 9 still prevents distinct winning proposals from normally reaching this state.

### Persistence and advertisement

- **D-05:** The selected aggregator validates the certificate, persists `/cert/<slot>`, and only then sends the PubSub notification. A successful durable write result is sufficient; Phase 10 does not add a readback-before-advertise requirement.
- **D-06:** PubSub is best-effort cleanup acceleration, not finality. Publish the full certificate as today, but a failed publish is logged and not retried; normal CRDT replication/recovery is the fallback.
- **D-07:** The publisher has no special completion shortcut. After its write, it follows the same certificate receipt/recovery path as every other node.
- **D-08:** Add the smallest production PubSub publish-result/error contract needed to log an actual failed certificate notification. Do not retry the notification or change CRDT finality semantics.

### Failover

- **D-09:** Existing consensus-round rotation is the complete, protocol-visible failover rule. A later round's selected aggregator may publish only when no authoritative slot record exists.
- **D-10:** A successor requires the same fully validated quorum evidence for the exact winning proposal. It must fail closed and wait/retry if it cannot reliably determine whether the slot is already occupied.
- **D-11:** If publishers are unavailable for successive rounds, recovery continues through ordinary rotation with that same validated evidence. No new lease, timeout, or retry cap is introduced.

### Consumer lookup migration

- **D-12:** Transaction-backed consumers derive the authoritative certificate key directly from the transaction's `GetSlotID()`. No subject-hash-to-slot locator and no subject-hash certificate authority are introduced.
- **D-13:** A caller that retained only a transaction hash retrieves the transaction from CRDT, derives its slot, and then performs authoritative lookup. If it cannot retrieve the transaction, finality is unavailable and normal recovery retries; it never falls back to a subject-hash certificate key.
- **D-14:** Registry-batch identity semantics are not redesigned in this phase. Existing generic `GetSlotKey` behavior remains the integration point for non-transaction subjects using the slot-keyed namespace.

### the agent's Discretion

- The researcher and planner may choose the smallest safe code shape for slot-key creation, datastore collision detection, and migration of internal lookup APIs, provided every decision above and the Phase 10 boundary are preserved.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone contract

- `.planning/PROJECT.md` — Core finality safety boundary: generic slot authority, no receiver-side CRDT writes, no local delivery-source authorization.
- `.planning/REQUIREMENTS.md` — CERT-01 through CERT-04 and COMP-01 acceptance requirements and explicit out-of-scope boundaries.
- `.planning/ROADMAP.md` §Phase 10 — Phase goal, success criteria, and phase boundary.

### Decisions already implemented

- `.planning/phases/08-canonical-slot-certificate-binding/08-CONTEXT.md` — Canonical slot and exact certificate/proposal binding decisions that Phase 10 must preserve.
- `.planning/phases/09-durable-one-vote-finality/09-CONTEXT.md` — Durable vote-lock and safe certificate-finality release contract that Phase 10 must migrate from legacy lookup without weakening.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- `src/blockchain/Consensus.hpp` / `src/blockchain/Consensus.cpp`: `GetSlotKey`, `GetExpectedCertificateSlotKey`, certificate validation, ordered active-validator selection, and round-based `GetAggregatorRole` already provide the generic slot and publisher seams.
- `src/account/TransactionManager.cpp`: the `NONCE_SUBJECT_TYPE` slot handler deserializes the embedded transaction and returns `Transaction::GetSlotID()`.
- `src/crdt/globaldb/globaldb.hpp`: `GlobalDB::Put` and `GlobalDB::Get` are the existing CRDT-backed persistence faces.

### Established Patterns

- Existing `GetAggregatorRole` deterministically rotates across sorted active validators using the proposal and consensus round. Preserve this rather than adding publisher state or health detection.
- Certificate validation already checks structural and quorum validity; Phase 8 additionally established exact proposal/slot binding.
- CRDT certificate callbacks are pre-commit notifications. Phase 9 correctly marks work stalled and lets durable readback/recovery drive completion; Phase 10 must not reintroduce callback-time authority.

### Integration Points

- `ConsensusManager::SubmitCertificate` currently publishes before persisting `/cert/<subject-hash>` and is the primary publication migration point.
- `RegisterCertificateFilter`, `FilterCertificate`, `CertificateReceived`, and `RecoverPendingCertificateWork` form the certificate ingress/recovery boundary.
- `GetCertificateBySubjectHash` and `CheckCertificateForSubject`, plus callers in `Blockchain`, `TransactionManager`, and `GeniusInputValidator`, require slot-derived lookup migration.

</code_context>

<specifics>
## Specific Ideas

- “No need to fix things that aren't broken”: retain the existing round rotation for normal publisher selection and failover.
- PubSub exists to accelerate cleanup; CRDT is finality and the durable replication fallback.
- For mint transactions, slot derivation comes from the transaction itself through `GetSlotID()`.

</specifics>

<deferred>
## Deferred Ideas

- Redesigning registry-batch slot identity beyond its existing generic `GetSlotKey` integration is outside Phase 10.
- Convergent certificate consumption and exactly-once mint application remain Phase 11 work.

### Reviewed Todos (not folded)

- `bridge-startup-wiring-mock-rpc.md` — matched only weakly on “bridge” (score 0.2) and is unrelated to authoritative certificate publication.

</deferred>

---

*Phase: 10-authoritative-slot-certificate-publication*
*Context gathered: 2026-08-21*
