# Roadmap: SuperGenius

## Milestones

- ✅ **v1.0 GeniusNode Construction Refactor** — Phases 1-3 (shipped 2026-07-03)
- ◆ **v2.0 Slot-Scoped Consensus Finality** — Phases 9-12 (roadmap approved)

Historical phase artifacts from work preceding v2.0 are preserved under `.planning/milestones/pre-v2.0-phases/`.

## v2.0 Phases

### Phase 9: Canonical Slot and Certificate Storage

**Goal:** Establish one canonical finality identity for every proposal and make slot-keyed certificates compatible with transaction-hash certificate consumers.

**Depends on:** Nothing

**Requirements:** SLOT-01, SLOT-02, SLOT-03, SLOT-04, CERT-01, CERT-02, CERT-03, CERT-04, COMP-01, COMP-02

**Success Criteria:**

1. Normal transactions with the same source and nonce share one slot, and every candidate for the same external burn shares one bridge slot regardless of proposer or candidate-controlled output fields.
2. A certificate remains bound to its exact proposal, is stored authoritatively by canonical slot, and exposes a verified transaction-hash secondary index.
3. Previous-nonce and producer-UTXO validation retrieve new-format certificates through `GetCertificateBySubjectHash()` without treating the transaction hash as the finality key.
4. A node presented with legacy transaction-keyed certificate state fails startup with a clear v2.0 clean-state error.

**Plans:** 16/16 plans complete

**Wave 7 gap closure** *(blocked on Wave 6 completion; serialized because worktrees are disabled)*:

- 09-10 — Durable catch-up publication outcomes and fail-closed bridge replay reads
- 09-11 — Typed certificate write-once preflight for local and replicated paths
- 09-12 — Mixed CRDT decision preservation and synchronized shutdown completion
- 09-13 — Endpoint-local receipt-status disagreement under weighted validation

**Wave 8 post-verification gap closure** *(serialized because worktrees are disabled)*:

- 09-14 — Atomic mint application and durable restart recovery
- 09-15 — Deferred CRDT destruction and callback-lifetime closure
- 09-16 — Immutable RPC configuration snapshots and concurrent publication

### Phase 10: Durable Vote Lock and Finalization State Machine

**Goal:** Ensure each validator publishes at most one usable signature per slot and transitions atomically from candidate selection to durable vote to certificate finality.

**Depends on:** Phase 9

**Requirements:** CERT-05, CERT-06, CERT-07, VOTE-01, VOTE-02, VOTE-03, VOTE-04, VOTE-05, VOTE-06, VOTE-07

**Success Criteria:**

1. A validator durably records its slot vote before publication, restores it after restart, and never signs a competing proposal while the first signature remains certificate-valid.
2. Valid candidates may replace the local best during a bounded pre-vote window, but no candidate can replace a vote after its signature is published.
3. `HandleCertificate()` marks the slot finalized before any proposal or vote state is cleared, including when the certificate winner differs from the validator's local vote.
4. Duplicate certificate paths are idempotent, while a conflicting certificate for the same finalized slot is not applied and emits actionable safety diagnostics.

**Plans:** 7/7 plans complete

### Phase 11: Slot-Owned Bridge Burn Reservations

**Goal:** Align bridge UTXO reservation and consumption with the canonical consensus slot so competing proposals cannot unlock or reuse a burn.

**Depends on:** Phase 10

**Requirements:** BURN-01, BURN-02, BURN-03, BURN-04, BURN-05

**Success Criteria:**

1. Bridge validation remains side-effect-free, and successful consensus admission establishes one reservation owned by the canonical burn slot.
2. Competing proposals for the same burn share the reservation and may change the pre-vote best candidate without releasing it.
3. Rejecting or cleaning up a losing proposal cannot release a reservation still protected by another candidate, vote, or certificate.
4. Certificate observation consumes the burn before proposal cleanup, while full slot abandonment releases it only after all usable votes expire.

**Plans:** 12/12 plans complete

### Phase 12: Consensus Race and Compatibility Verification

**Goal:** Prove the complete v2.0 finality path under the exact race, restart, candidate-ordering, indexing, and ordinary-transaction scenarios that define the safety boundary.

**Depends on:** Phases 9-11

**Requirements:** TEST-01, TEST-02, TEST-03, TEST-04, TEST-05, TEST-06

**Success Criteria:**

1. The 11-node single-burn race produces one canonical slot certificate and one confirmed mint, with no validator contributing usable signatures to competing certificates.
2. The original `HandleCertificate()`-before-CRDT interleaving cannot admit a second certificate, and duplicate delivery cannot apply the winner twice.
3. Restart and proposal-ordering tests prove durable non-equivocation and correct best-candidate behavior before versus after vote publication.
4. Slot/index corruption tests fail closed, and existing nonce-chain plus producer-UTXO certificate suites pass against the v2.0 store.

**Plans:** 5/5 plans executed — closure BLOCKED: 12-05 re-run recorded a deterministic `bridge_race_single_burn_test` failure (mint output destination mismatch on all 11 nodes); full-suite verification still owed

## Requirement Coverage

| Phase | Requirements | Count |
|-------|--------------|-------|
| 9 | SLOT-01..04, CERT-01..04, COMP-01..02 | 10 |
| 10 | CERT-05..07, VOTE-01..07 | 10 |
| 11 | BURN-01..05 | 5 |
| 12 | TEST-01..06 | 6 |

**Coverage:** 31/31 v2.0 requirements mapped exactly once ✓

---
*Roadmap created: 2026-07-22 for milestone v2.0*
