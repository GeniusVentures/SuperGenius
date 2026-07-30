# Requirements: SuperGenius — Slot-Scoped Consensus Finality

**Defined:** 2026-07-22
**Core Value:** At most one valid certificate may finalize a canonical consensus slot.
**Protocol boundary:** v2.0 requires fresh certificate state; legacy transaction-hash-keyed certificate stores are not migrated or dual-read.

## v2.0 Requirements

### Canonical Slots

- [x] **SLOT-01**: Every consensus proposal resolves to one deterministic canonical slot key, and proposal arbitration, vote locking, certificate lookup, and finalized-state checks use that same key.
- [x] **SLOT-02**: A normal transaction's canonical slot remains its source address plus nonce, so competing transactions from the same account and nonce share one finality domain.
- [x] **SLOT-03**: A bridge mint's canonical slot is derived from the external burn identity (source chain plus burn transaction hash and canonical event/outpoint index) and is independent of the proposing validator's address and nonce.
- [x] **SLOT-04**: Two mint proposals referencing the same canonical burn resolve to the same slot even when their transaction hashes, proposers, nonces, amounts, destinations, or other candidate-controlled fields differ.

### Certificates

- [x] **CERT-01**: A certificate continues to cryptographically bind the complete winning proposal, including its subject, proposer/account identity, nonce, transaction hash, embedded transaction, registry, and votes.
- [x] **CERT-02**: The authoritative certificate is persisted under a stable hash of the canonical slot key, and a slot can expose at most one authoritative certificate.
- [x] **CERT-03**: A successful certificate write creates a transaction-hash-to-slot secondary index for the winning transaction without making the transaction hash the finality key.
- [x] **CERT-04**: `GetCertificateBySubjectHash(tx_hash)` resolves through the secondary index and verifies that the loaded slot certificate contains the requested transaction hash before returning it.
- [x] **CERT-05**: On first observing a valid certificate, a node atomically marks the slot finalized before clearing proposal candidates, pending votes, or temporary vote-lock state.
- [x] **CERT-06**: Pubsub certificate handling, local certificate submission, and CRDT certificate delivery are idempotent views of the same finalized slot and cannot apply the winning transaction more than once.
- [x] **CERT-07**: A different certificate received for an already-finalized slot is treated as a consensus-safety violation, is never applied as a second winner, and produces actionable diagnostics identifying both proposals and the slot.

### Validator Vote Safety

- [x] **VOTE-01**: Before publishing an approval signature, a validator durably records the canonical slot and proposal ID in a local vote journal.
- [x] **VOTE-02**: While a recorded signature can still contribute to a valid certificate, the validator may reproduce the same proposal's vote idempotently but cannot sign a different proposal in that slot.
- [x] **VOTE-03**: Validator restart restores outstanding slot vote locks before proposal processing or vote publication begins.
- [x] **VOTE-04**: Before its first signature, a validator may collect valid competing proposals for a bounded selection window and deterministically choose the best candidate using the existing proposal-ordering rule.
- [x] **VOTE-05**: Once the validator publishes its slot vote, a later better proposal may be tracked but cannot cause the validator to retract, replace, or publish another vote for that slot.
- [x] **VOTE-06**: A valid certificate finalizes its winner even when the validator locally voted for a different proposal, transitioning the local slot from voted to finalized without applying multiple winners.
- [x] **VOTE-07**: An uncertified vote lock may expire only after the recorded proposal and signature can no longer participate in a certificate accepted by current validation rules.

### Bridge Reservation Lifecycle

- [x] **BURN-01**: Bridge proposal validation remains side-effect-free; after a valid mint proposal is admitted to consensus, the node reserves its canonical burn under the slot identity.
- [x] **BURN-02**: Valid competing proposals for the same burn share the slot-owned reservation, so selecting a better proposal does not require releasing or reacquiring the burn.
- [x] **BURN-03**: Rejecting, failing, or cleaning up one losing proposal cannot release a burn reservation while another candidate or finalized certificate still owns the slot.
- [x] **BURN-04**: Observing the slot certificate transitions the burn reservation to consumed for the winning mint before proposal cleanup can admit another spender.
- [x] **BURN-05**: A reservation returns to ready only when the entire slot is abandoned, no candidate remains, no certificate exists, and every outstanding vote for the slot is cryptographically expired.

### Compatibility and Verification

- [x] **COMP-01**: Previous-nonce validation and producer-UTXO validation continue retrieving new-format certificates by transaction hash through the verified secondary index.
- [x] **COMP-02**: Nodes fail startup with a clear protocol-state error when legacy certificate state is present, rather than silently mixing transaction-keyed and slot-keyed finality formats.
- [x] **TEST-01**: The 11-node single-burn race deterministically demonstrates that all competing mints use one canonical slot, at most one validator signature per validator is usable for that slot, exactly one certificate exists, and exactly one mint becomes confirmed.
- [x] **TEST-02**: A regression test reproduces the observed ordering where `HandleCertificate()` runs before CRDT certificate application and proves that a second proposal cannot collect a certificate in that gap.
- [x] **TEST-03**: A restart test proves that a validator which signed before shutdown restores its vote lock and does not sign a competing proposal after restart.
- [x] **TEST-04**: Proposal-ordering tests prove that a better candidate arriving before the vote window closes can win, while a better candidate arriving after vote publication cannot trigger a second signature.
- [x] **TEST-05**: Certificate-index tests cover slot lookup, transaction-hash lookup, mismatched index rejection, duplicate delivery idempotency, and conflicting-certificate diagnostics.
- [x] **TEST-06**: Existing normal-transaction nonce-chain and Genius UTXO producer-certificate tests pass against the new slot-keyed certificate store.

## Future Requirements

### Broader Bridge Robustness

- **FUTR-01**: Exercise slot finality during node termination at each burn-to-mint lifecycle boundary.
- **FUTR-02**: Exercise slot finality under disagreeing RPC endpoint quorums and pubsub partitions/healing.
- **FUTR-03**: Fuzz bridge event parsing, configuration parsing, and transaction deserialization.

## Out of Scope

| Feature | Reason |
|---------|--------|
| Legacy certificate migration or dual-read compatibility | v2.0 is an explicit clean state break selected for this milestone |
| Changing certificate signatures to cover only a slot ID | The certificate must continue proving the exact winning proposal |
| Validator quorum-weight or reputation redesign | Existing quorum policy remains; this milestone enforces non-equivocation within it |
| Rejecting a valid certificate because it differs from the local vote | Consensus finality overrides local candidate preference |
| Node-kill, RPC-disagreement, and pubsub-partition fault suites | Archived with the broader Phase 8 work and deferred to a later milestone |
| Bridge parser/configuration fuzzing | Archived and deferred; not needed to close the certificate-safety defect |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| SLOT-01 | Phase 9 | Complete |
| SLOT-02 | Phase 9 | Complete |
| SLOT-03 | Phase 9 | Complete |
| SLOT-04 | Phase 9 | Complete |
| CERT-01 | Phase 9 | Complete |
| CERT-02 | Phase 9 | Complete |
| CERT-03 | Phase 9 | Complete |
| CERT-04 | Phase 9 | Complete |
| CERT-05 | Phase 10 | Complete |
| CERT-06 | Phase 10 | Complete |
| CERT-07 | Phase 10 | Complete |
| VOTE-01 | Phase 10 | Complete |
| VOTE-02 | Phase 10 | Complete |
| VOTE-03 | Phase 10 | Complete |
| VOTE-04 | Phase 10 | Complete |
| VOTE-05 | Phase 10 | Complete |
| VOTE-06 | Phase 10 | Complete |
| VOTE-07 | Phase 10 | Complete |
| BURN-01 | Phase 11 | Complete |
| BURN-02 | Phase 11 | Complete |
| BURN-03 | Phase 11 | Complete |
| BURN-04 | Phase 11 | Complete |
| BURN-05 | Phase 11 | Complete |
| COMP-01 | Phase 9 | Complete |
| COMP-02 | Phase 9 | Complete |
| TEST-01 | Phase 12 | Complete |
| TEST-02 | Phase 12 | Complete |
| TEST-03 | Phase 12 | Complete |
| TEST-04 | Phase 12 | Complete |
| TEST-05 | Phase 12 | Complete |
| TEST-06 | Phase 12 | Complete |

**Coverage:**

- v2.0 requirements: 31 total
- Mapped to phases: 31
- Unmapped: 0 ✓

---
*Requirements defined: 2026-07-22*
*Last updated: 2026-07-22 after roadmap creation*
