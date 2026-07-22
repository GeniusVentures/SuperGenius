# Phase 9: Canonical Slot and Certificate Storage - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-22
**Phase:** 09-canonical-slot-and-certificate-storage
**Areas discussed:** Bridge event identity, Canonical slot encoding, Certificate and index writes, Lookup API behavior

---

## Pending Todo Review

| Todo | Decision | Reason |
|------|----------|--------|
| bridge_race fixture — not all 11 nodes mint within the 90s race window | Not folded | Phase 8 timing and teardown concern |
| Bridge Startup Wiring + Mock RPC Endpoints | Not folded | Startup/RPC simulation is outside Phase 9 storage scope |

---

## Bridge Event Identity

| Question | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Can one transaction contain multiple burns? | One burn per source transaction; receipt-local ordinal; event-content hash | Receipt-local ordinal |
| Which ordinal is canonical? | Absolute receipt-log position; bridge-burn ordinal; bridge-contract ordinal | Absolute receipt-log position |
| How is the ordinal represented? | Synthetic UTXO output index; dedicated field; duplicate in both | Synthetic UTXO output index |
| What happens if the index is missing? | Reject; default zero; resolve through RPC | Reject |

**User's choice:** Support multiple burns per source transaction using the mandatory absolute position in the finalized receipt's `logs` array, carried as `output_idx_`.

**Notes:** The user questioned whether RPC `log_index` can change for the same burn. Code inspection confirmed `MatchedEvent::log_index` is block-wide and may change across re-inclusion. The user also confirmed that destination is dictated by the smart contract, so validators must bind candidate payload values to the exact selected log.

---

## Canonical Slot Encoding

| Question | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Slot preimage representation | Typed binary; canonical text; protobuf | Preserve canonical text |
| Bridge mint preimage fields | Burn resource only; retain token/amount/destination | Burn resource only |
| Operational slot ID size | Hash every slot; hash mint only; hash storage key only | Hash every slot |
| Input aliases | Reject noncanonical; accept and normalize; hash raw | Reject noncanonical |

**User's choice:** Use SHA-256 of each domain's canonical text preimage. The bridge preimage is `mint-v2:<source_chain_id>:<burn_tx_hash>:<receipt_log_index>`.

**Notes:** The user initially expected only the receipt index to be added to the current mint slot. Inspection showed the current validator accepts any matching bridge log without comparing its decoded values to the mint output. The user chose a resource-only slot plus exact semantic event validation as defense in depth.

---

## Certificate and Index Writes

| Question | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Publication consistency | Atomic pair; certificate then index; index then certificate | Atomic pair |
| Persistent paths | `/cert/v2/slot` + `/cert/v2/tx`; separate top-level paths; version all consensus | Versioned certificate namespace |
| Index value | Slot ID only; structured record; duplicate certificate | Slot ID only |
| Existing slot write | Write-once; deterministic replacement; CRDT resolution | Write-once |

**User's choice:** Atomically publish a write-once slot certificate and minimal winner index under the v2 certificate namespace.

**Notes:** Identical retries are idempotent. A different certificate at an occupied slot is a safety conflict and never replaces the first finalized value.

---

## Lookup API Behavior

| Question | Alternatives considered | Selected |
|----------|-------------------------|----------|
| Public API shape | Separate slot/hash APIs; overloaded API; transaction-only API | Separate APIs |
| Candidate finality check | Derive slot; candidate hash only; both | Derive slot |
| Broken index behavior | Typed errors; all not-found; scan to repair | Typed fail-closed errors |
| Losing candidate aliases | Winner only; index all candidates; create on demand | Winner only |

**User's choice:** Direct slot lookup is authoritative; transaction-hash lookup is a verified compatibility path for the winner only.

**Notes:** `CheckCertificateForSubject(subject)` must derive and check the canonical slot so a competing candidate sees the slot's existing winner. Missing indexes are ordinary absence, while dangling or mismatched indexes are integrity failures.

---

## the agent's Discretion

- Exact C++ type and error names.
- Internal binary versus boundary hexadecimal representation of the fixed SHA-256 slot ID.
- Logging, metrics, and code factoring that preserve the locked semantics.

## Deferred Ideas

- Archived Phase 8 bridge-race timing, teardown, node-kill, RPC-disagreement, and partition work.
- Broader bridge startup/mock-RPC wiring.
- Bridge parser/configuration fuzzing.
