# Validator Admission & Cross-Certificate Equivocation Audit (TRUST-02)

**Requirement:** TRUST-02 — "admission growth bounds and cross-certificate equivocation detection are AUDITED, with findings documented" (assessment finding #14).
**Date:** 2026-10-21 · **Phase 21 Plan 07** · D-04: the audit's findings get teeth in-phase, not a follow-up.
**Line references:** current `gsd/phase-21-consensus-trust-mint-authorization` tree (verified 2026-10-21).

## Scope

The hardened verified-registry path shipped by 21-01 through 21-06:

- **Registry admission:** certified registry updates flow through `ValidatorRegistry::BuildRegistryFromCertificate` (`ValidatorRegistry.cpp:2210`, single-certificate path) and `BuildRegistryFromAggregatedVotes` (`:2300`, the `HandleBatchCertificate` path at `:1567`); both call `InsertNewValidators` (`:2344`) which inserts unregistered voters as REGULAR/ACTIVE entries under a per-update cap (`:2357-2361`). The cap is production-wired from `network_config.json` `max_new_validators_per_update` (21-03: `GeniusNode.cpp:860` applies `SetMaxNewValidatorsPerUpdate`; default `DefaultMaxNewValidatorsPerUpdate = 10`, `ValidatorRegistry.hpp:66`).
- **Certificate gossip:** `ConsensusManager::FilterCertificate` (`Consensus.cpp:3318`) is the consensus ingress for certificate deltas; the conflicting-certificate branch (`:3391-3409`) is the only place two certificates at one canonical slot meet.
- **Durable one-vote-per-slot ledger:** `ActiveVoteRecord` under the private local prefix `ACTIVE_VOTE_BASE_PATH_KEY` (`Consensus.hpp:741-742`), written/read via `BuildActiveVoteRecord`/`DecodeActiveVoteRecord` (`Consensus.cpp:1558-1607`) and recovered by `RecoverActiveVotes` (`:1694-1726`).

Assessment finding #14's own triage (security-assessment-2026-10-response.md): "New signed voters can be added as ACTIVE regular validators, up to ten per registry update … an arbitrary key cannot instantly grant itself quorum through normal admission. … Cross-certificate malicious equivocation detection and long-term validator admission policy still need auditing." This document is that audit; every finding carries a disposition.

## Findings

| ID | Finding | Evidence | Attacker authority | Disposition |
|----|---------|----------|--------------------|-------------|
| F1 | ACTIVE-addition growth is bounded per update only — unbounded across epochs | `ValidatorRegistry.cpp:2344-2384` (cap loop), call sites `:2240`, `:2308`; wiring `GeniusNode.cpp:860` | Certified registry-update author (batch proposer with cooperating quorum) | Mitigated in-phase (per-update cap) + accepted residual (cross-epoch aggregate) |
| F2 | Cross-certificate equivocation at one canonical slot was log-only | `Consensus.cpp:3391-3409` | Byzantine validator signing two conflicting certificates at one slot | Mitigated in-phase (ledger-backed equivocation guard) |
| F3 | Batch vote-summing nets yes-on-one against no-on-another | `ValidatorRegistry.cpp:1526-1563` | Byzantine validator balancing approvals and rejections inside one batch | Accepted with rationale (residual to Phase 25 spine) |
| F4 | Marker placement under the active-vote prefix would poison recovery | `Consensus.cpp:1694-1726` (prefix scan decodes every record) | n/a — defensive design finding from this audit's guard design | Mitigated in design (sibling private prefix) |

### F1 — Unbounded-across-epochs ACTIVE insertion (per-update cap only)

- **Evidence:** `InsertNewValidators` (`ValidatorRegistry.cpp:2344-2384`) sorts the unregistered voter ids and stops inserting once `added >= max_new_validators_per_update_` (`:2357-2361`, debug log "new validator cap reached"); each inserted entry is `Role::REGULAR`, `Status::ACTIVE`, weight `ComputeWeight(REGULAR)` (`:2366-2370`). The cap resets every update: `BuildRegistryFromCertificate` and `BuildRegistryFromAggregatedVotes` each bump the epoch (`:2238`, `:2304`) and run the capped insertion again.
- **Attack shape (attacker authority named):** a **certified registry-update author** — a batch proposer able to gather certified quorum for its updates (a collusion threshold, not a solo break) — stacks sympathetic keys at 10 per update, epoch after epoch. Each wave is individually legal; the aggregate growth is unbounded.
- **Disposition: MITIGATED in-phase (per-update bound) + ACCEPTED residual (cross-epoch aggregate).**
  - *Guard (21-07 Task 2, enforcement-tested):* the config-wired cap provably flows — `SetMaxNewValidatorsPerUpdate` (21-03 wiring at `GeniusNode.cpp:860`) into the `InsertNewValidators` cap loop. Tests prove: an over-cap certified update inserts exactly `cap` entries; a wired value (3) overrides the default 10; a sub-cap honest update inserts all candidates.
  - *Accepted residual:* aggregate growth **across** updates/epochs remains unbounded. Rationale: every insertion still requires a certified quorum over the *current* registry (verified since 21-01/21-04 — a forged/self-certified base registry is Rejected), so the remaining bound is governance/economic (the existing validators must repeatedly sign their own dilution), not a per-node correctness rule. Long-term admission policy (e.g. epoch-denominated growth ceilings, stake/identity requirements) is carried to the Phase 25 verification spine.
- **Compat consequence:** sub-cap honest updates insert all new validators (compat twin shipped); over-cap updates truncate deterministically — sorted id order decides which candidates land, so every peer computing the same update derives the same truncated set.

### F2 — Log-only equivocation response at the conflicting-certificate branch

- **Evidence:** `FilterCertificate` (`Consensus.cpp:3318`) reaches the conflict branch when the durable existing certificate and the candidate share the canonical slot but mint different verified v2 transaction hashes; the pre-21-07 code logged `critical` ("consensus equivocation", `:3395-3399`) and then fell through to lower-hash ordering (`:3402-3409`). No durable evidence was recorded and the equivocator paid nothing.
- **Attack shape (attacker authority named):** a **byzantine validator** signs votes for two conflicting certificates at the same canonical slot (e.g. two mint constructions competing for one burn identity). Pre-guard, it could pick the winner by hash ordering rather than having its conflict rejected, and its double commitment left no durable trace for slashing/audit.
- **Disposition: MITIGATED in-phase (21-07 Task 2 guard).**
  - *Guard:* `ProveCrossCertificateEquivocation` computes the signers common to both certificates and requires **both** signatures to verify over their respective proposals' vote signing bytes — same signer, two different proposals, one slot. The prior commitment is durable by construction: the existing certificate's bytes were read from the local store by the branch. On proof: a durable equivocation marker is written (see F4), the error log names the rule, the signer, and both proposals, and the candidate is returned `ElementFilterResult::Reject` instead of competing under lower-hash ordering.
  - *Guard-cost bound (D-04 / T-21-0704):* the guard runs **only** inside the conflict branch — one ledger read (the local `ActiveVoteRecord` consult) plus one ledger write (the marker). No background scanning, no quorum recomputation.
  - *Honest-conflict safety (T-21-0705):* disjoint-signer conflicts (honest view-change contention) prove no common signer and keep today's lower-hash ordering exactly; the compat twin pins this. The honest single-certificate accept path is untouched (the guard is inside the conflict branch only).
- **Compat consequence:** provable equivocation → Reject + durable marker; everything else in the branch → byte-identical lower-hash ordering with the historical critical log retained.

### F3 — Batch vote-summing netting (yes-on-one / no-on-another)

- **Evidence:** `HandleBatchCertificate` (`ValidatorRegistry.cpp:1422`) sums each validator's member-certificate votes into `registered_scores` / `unregistered_scores` with `+= approve ? 1 : -1` and collapses with `score >= 0` (`:1526-1563`); the resulting bool feeds `ApplyVoteEffects` (penalty/weight drift) and `InsertNewValidators` (penalty_score 0/1 for admissions).
- **Attack shape (attacker authority named):** a **byzantine validator** approves K member certificates and rejects K others inside one batch so its net score stays `>= 0` — its rejections never accumulate penalty.
- **Disposition: ACCEPTED with rationale.**
  - Votes on *distinct* member certificates are independent expressions on distinct subjects (distinct canonical slots); balanced mixed voting is not equivocation. The provable offense — one signer committing twice to one slot — is exactly what F2's guard now rejects and durably records; a same-slot double commitment can no longer hide inside a batch.
  - The netting's worst-case effect is ±1 penalty/weight attribution drift in reputation bookkeeping; it cannot change quorum outcomes (weights are read from the registry, not from these scores), authorization, or mint correctness.
  - Per T-21-0703's scope discipline, a per-member attribution rewrite of `HandleBatchCertificate` without a finding that demands it would be speculative — rejected.
  - *Residual:* per-member penalty attribution accuracy — named for the Phase 25 spine.
- **Compat consequence:** none — no behavior change ships for F3.

### F4 — (audit-added) Marker placement vs. the active-vote recovery scan

- **Evidence:** `RecoverActiveVotes` (`Consensus.cpp:1694-1726`) prefix-queries everything under `ACTIVE_VOTE_BASE_PATH_KEY` (`/consensus/vote/`) and decodes each record; anything not a valid local active vote logs `error` ("ignored invalid durable active vote") on every boot.
- **Disposition: MITIGATED in design.** Equivocation markers reuse the ledger's *storage conventions* — private local namespace, `GlobalDB::Buffer` put/get, one `ActiveVoteRecord`-shaped payload per event (the signer's verified prior vote) — under a **sibling** private prefix (`/consensus/equivocation/<slot>/<signer>`), so the recovery scan never trips over audit markers and boot logs stay clean. The flooding vector this closes (marker-count-driven boot error noise) never materializes.

## Conclusion

All three planned findings (F1, F2, F3) carry explicit dispositions and the audit-added F4 is closed by design:

- **Mitigated in-phase:** F1's per-update ACTIVE-addition cap (config-wired, enforcement-tested), F2's cross-certificate equivocation rejection with durable evidence, F4's marker placement.
- **Accepted residuals carried to the Phase 25 spine:** (1) cross-epoch aggregate admission growth (F1) — governance/economic bound, every wave still needs certified quorum over the current verified registry; (2) per-member batch penalty attribution (F3) — reputation-bookkeeping precision only.

TRUST-02's deliverable is complete with this document plus the enforcement/equivocation/compat proofs in `test/src/blockchain/validator_admission_equivocation_test.cpp`.
