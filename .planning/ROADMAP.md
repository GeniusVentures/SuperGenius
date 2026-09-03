# Roadmap: SuperGenius

## Milestones

- ✅ **v1.0 GeniusNode Construction Refactor** — Phases 1-3 (shipped 2026-07-03)
- 📋 **v3.0 Canonical Burn Finality Rebuild** — Phases 8-12 (planned)

## Overview

v3.0 turns bridge-mint finality into one canonical-slot protocol: verified burn facts identify the shared slot, a validator can durably issue only one active vote for it, one deterministic publisher persists the generic slot-keyed certificate before advertising it, every ingress path consumes the same durable certificate, and the winning mint applies once even through faults and restart. The earlier research proposal for a bridge-specific finality record is superseded: authority lives only at `/cert/<canonical-slot-id>`, while each certificate remains bound to its exact winning proposal.

## Phases

<details>
<summary>✅ v1.0 GeniusNode Construction Refactor (Phases 1-3) — SHIPPED 2026-07-03</summary>

- [x] **Phase 1: Config-Driven Settings Foundation** - Config-driven network defaults and port settings.
- [x] **Phase 2: Variant Factory + Constructor Reorder** - Canonical account factory and node-role initialization.
- [x] **Phase 3: Call-Site Migration + Verification** - Factory migration and full verification.

Full phase details: `.planning/milestones/v1.0-ROADMAP.md`

</details>

### 📋 v3.0 Canonical Burn Finality Rebuild (Planned)

**Milestone Goal:** One verified external burn can finalize through one authoritative generic certificate and produce one mint effect despite contention, reordered delivery, publisher loss, and restart.

- [x] **Phase 8: Canonical Slot & Certificate Binding** - Make every competing verified mint share its existing canonical slot and reject certificate/proposal mismatches. (completed 2026-08-20)
- [x] **Phase 9: Durable One-Vote Finality** - Lock each validator to one recoverable, non-overlapping vote per canonical slot. (completed 2026-08-20)
- [x] **Phase 10: Authoritative Slot Certificate Publication** - Persist and publish `/cert/<slot-id>` through deterministic, recoverable protocol authority. (completed 2026-08-21)
- [x] **Phase 11: Convergent Certificate Consumption & Mint Recovery** - Consume accepted slot certificates once and recover mint application safely. (completed 2026-08-24)
- [x] **Phase 12: Multi-Node Finality Fault Proof** - Prove the complete production path under contention, loss, delayed delivery, and restart. (completed 2026-08-25)

## Phase Details

### Phase 8: Canonical Slot & Certificate Binding

**Goal**: Validators and receivers recognize all competing proposals for one verified external burn as one finality domain while preserving the certificate's exact winning-proposal binding.
**Depends on**: Phase 3
**Requirements**: SLOT-01, SLOT-02, SLOT-03
**Success Criteria** (what must be TRUE):

  1. Competing `MintTransactionV2` proposals whose verified chain, token, source transaction, amount, and destination match resolve to the same canonical slot, while proposals for different verified burns do not.
  2. Changing a proposer account or proposal nonce cannot change a mint proposal's canonical slot.
  3. A certificate is accepted only when its canonical slot, storage key, payload, and embedded winning proposal agree; a slot/key/payload or proposal-binding mismatch is rejected before it can finalize or mint.

**Plans**: TBD

### Phase 9: Durable One-Vote Finality

**Goal**: A validator deterministically chooses and durably commits to at most one usable vote for a canonical slot throughout contention and restart recovery.
**Depends on**: Phase 8
**Requirements**: VOTE-01, VOTE-02, VOTE-03, VOTE-04
**Success Criteria** (what must be TRUE):

  1. Validators close a bounded contention window and select the same deterministic eligible winner for a slot without waiting indefinitely for a possible contender.
  2. Before a validator broadcasts its vote, a durable slot-keyed record contains the selected proposal, signed vote material, and acceptance deadline.
  3. After restart, a validator can recover or re-announce only the exact vote already recorded for that slot and cannot produce a different usable vote while its lock remains accepted.
  4. A vote lock clears only when the matching authoritative slot certificate is durably accepted; cleanup never enables an incompatible vote during the original vote's acceptance period.

**Plans**: TBD

### Phase 10: Authoritative Slot Certificate Publication

**Goal**: The network can discover one generic, slot-keyed authoritative certificate that a deterministic publisher persists before advertising and an eligible successor can recover safely.
**Depends on**: Phase 9
**Requirements**: CERT-01, CERT-02, CERT-03, CERT-04, COMP-01
**Success Criteria** (what must be TRUE):

  1. The only authoritative certificate namespace for a canonical slot is `/cert/<canonical-slot-id>`; no bridge-specific finality record or subject-hash certificate authority can finalize the mint.
  2. Only the deterministic protocol-selected publisher writes the authoritative certificate record, and a peer receiving the certificate through PubSub never writes that CRDT key.
  3. The selected publisher durably persists and verifies the exact slot certificate before it advertises that certificate on PubSub.
  4. If the selected publisher stalls, an eligible successor can satisfy a defined protocol-visible recovery condition and publish the same valid certificate without admitting competing contents.
  5. A consumer that begins with a subject hash resolves the corresponding canonical slot before authoritative lookup, or uses a hash-to-slot locator that cannot itself confer certificate authority.

**Plans**: 5 plans
Plans:
**Wave 1**

- [x] 10-01-PLAN.md — Add immutable CRDT record semantics for authoritative certificate collision safety.

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 10-02-PLAN.md — Publish and recover authoritative slot certificates through deterministic selected-publisher authority.

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 10-03-PLAN.md — Replace Consensus and Blockchain certificate lookup with validated canonical-slot authority.
- [x] 10-05-PLAN.md — Migrate registry batch certificate loading to generic slot authority without redesigning batch identity.

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 10-04-PLAN.md — Migrate transaction-backed and witness consumers to transaction-derived slot lookup.

### Phase 11: Convergent Certificate Consumption & Mint Recovery

**Goal**: Every node converges on one accepted slot certificate and applies its exact certified mint at most once across duplicate delivery and crash recovery.
**Depends on**: Phase 10
**Requirements**: CERT-05, MINT-01, MINT-02
**Success Criteria** (what must be TRUE):

  1. Local completion, PubSub, CRDT synchronization, and restart recovery all pass certificates through one idempotent acceptance path for the same canonical slot.
  2. A byte-identical certificate replay is harmless, while different certificate contents for an occupied slot fail closed and never overwrite the authority or unlock the slot.
  3. A durably accepted certificate causes its embedded winning mint transaction at most once per node, including after duplicate delivery or restart.
  4. Recovery durably distinguishes certified, applying, and applied mint work (or an equivalent atomic boundary), so a crash neither repeats the mint effect nor silently loses certified work.

**Plans**: TBD

### Phase 12: Multi-Node Finality Fault Proof

**Goal**: Operators have production-path regression proof that canonical slot finality remains safe and live through contention, propagation disorder, publisher loss, and restart.
**Depends on**: Phase 11
**Requirements**: TEST-01, TEST-02, TEST-03, TEST-04, TEST-05, TEST-06
**Success Criteria** (what must be TRUE):

  1. A multi-node production-path scenario with competing proposals for one burn produces one canonical slot, one authoritative certificate, and one exact winning proposal.
  2. A late contender cannot acquire a second usable vote or certificate for a slot, and PubSub recipients neither write the certificate key nor stall on a CID they wrote themselves.
  3. Restart scenarios before certificate arrival, after durable certificate acceptance, and during mint application preserve the original vote and produce no duplicate mint.
  4. Publisher-loss scenarios prove persistence-before-advertisement and deterministic failover without conflicting slot certificate records.
  5. The regression suite exercises production PubSub, CRDT, RocksDB persistence, and mint ingress rather than direct local-author shortcuts.

**Plans**: 20 plans
Plans:

**Wave 1**

- [x] 12-01-PLAN.md — Add safe production-boundary instrumentation and a persistent four-peer real-route audit harness.

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 12-02-PLAN.md — Prove same-burn contention, late-contender safety, and passive-recipient receive-only recovery.

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 12-03-PLAN.md — Prove restart-boundary recovery and persistence-before-advertisement publisher failover.

**Wave 4** *(gap closure)*

- [x] 12-04-PLAN.md — Replace full-mesh readiness with public connected-topology checks and prove durable exact-once restart/publisher-loss recovery.

**Wave 5** *(gap closure)*

- [x] 12-05-PLAN.md — Diagnose the same-root active-vote boundary and isolate the pre-finality restart proof from valid certificate cleanup.

**Wave 12** *(gap closure — UAT Test 1)*

- [x] 12-12-PLAN.md — Repair the same-burn FilterCertificate fixture to write the convergent-immutable slot key exactly once per direction through the production PutConvergentImmutable path, prove no regression across the lifecycle and multi-node suites, and record the CRDT hardening follow-ups as deferred items.

**Wave 13** *(gap closure — UAT round 2: stale-fixture db reuse, tests 1-2 precondition)*

- [x] 12-13-PLAN.md — Make CRDTFixture db paths run-unique (pid + counter) with construction-time reap, log the silent no-quorum certificate rejection, and prove stale-db immunity across all CRDTFixture suites.

**Wave 14** *(gap closure — UAT round 2: teardown SIGSEGV, tests 1+5 single event; blocked on 12-13 for serial build/port contention and clean evidence)*

- [x] 12-14-PLAN.md — Release the GlobalDB host co-owner before GossipPubSub::Stop in Peer::Stop, prove three consecutive serial full multi-node passes with zero new crash reports, and record the thirdparty StopImpl hardening and MintRecoveryDiagnostics UAF as deferred items.

**Wave 15** *(gap closure round 3 — VERIFICATION gaps 1+2: post-review-fix full-suite flakiness; developer option (a) on the escalation gate)*

- [x] 12-15-PLAN.md — Attribute every intermittent full-suite signature from the preserved evidence logs, close the SameBurn marker wait-predicate check-then-act gap, and fix the WR-02 notify-without-paired-mutex contract violation in TransactionManager::Stop and ConsensusManager::Close.

**Wave 16** *(gap closure round 3 — VERIFICATION gap 3: teardown-invariant propagation; serialized after 12-15 for build-tree and port contention)*

- [x] 12-16-PLAN.md — Mirror the 12-14 Peer::Stop teardown order into ComponentPeer::Stop (CR-01), ~CRDTFixture (CR-02), and both lifecycle multi-validator teardown loops (WR-01 same class), and prove every affected suite green with zero new crash reports.

**Wave 17** *(gap closure round 3 — evidence gate; depends on 12-15 and 12-16)*

- [x] 12-17-PLAN.md — Re-run the three-consecutive-serial-pass evidence gate on the final build with crash-report absence proof, the sibling suite matrix, and an honest STATE.md resolution of the 12-14 evidence-gap blocker. (STOPPED at its own gate-entry branch — plan-faithful; developer NO-GO 2026-09-03 routes round 4; the record stands and the gate re-attempt moves to 12-20)

**Wave 18** *(gap closure round 4 — developer NO-GO directive: blacklist-backoff seam + CR-03; depends on 12-17)*

- [x] 12-18-PLAN.md — Add the test-only GraphsyncDAGSyncer blacklist-backoff seam (production defaults unchanged, millisecond resolution), fix CR-03's node.registry reset in both lifecycle teardown loops with the STATE.md:129 correction, and run the boot-window masking hypothesis test (three focused RestartAtVote runs, durable round4-traces evidence, recorded verdict).

**Wave 19** *(gap closure round 4 — directive's conditional fallback; depends on 12-18)*

- [x] 12-19-PLAN.md — Resolve the directive's conditional tail: record the justified skip when the hypothesis is proven, or implement post-restart certificate re-publication / surviving-replica serving and focused-verify three consecutive RestartAtVote passes.

**Wave 20** *(gap closure round 4 — evidence gate re-attempt carrying the round-4 attributed fix; depends on 12-18 and 12-19)*

- [ ] 12-20-PLAN.md — Re-attempt the three-consecutive-serial-pass evidence gate with crash baseline, per-log four-case assertions, STOP-on-first-strike discipline, the sibling suite matrix, and the honest STATE.md resolution of the 12-14 evidence-gap blocker, all evidence at durable repo-relative round4-traces paths.

## Progress

**Execution Order:** Phases execute in numeric order: 8 → 9 → 10 → 11 → 12.

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Config-Driven Settings Foundation | v1.0 | 1/1 | Complete | 2026-07-03 |
| 2. Variant Factory + Constructor Reorder | v1.0 | 1/1 | Complete | 2026-07-03 |
| 3. Call-Site Migration + Verification | v1.0 | 3/3 | Complete | 2026-07-03 |
| 8. Canonical Slot & Certificate Binding | v3.0 | 1/1 | Complete   | 2026-08-20 |
| 9. Durable One-Vote Finality | v3.0 | 2/2 | Complete    | 2026-08-20 |
| 10. Authoritative Slot Certificate Publication | v3.0 | 7/5 | Complete    | 2026-08-21 |
| 11. Convergent Certificate Consumption & Mint Recovery | v3.0 | 5/4 | Complete    | 2026-08-24 |
| 12. Multi-Node Finality Fault Proof | v3.0 | 19/20 | In Progress|  |

---
*Roadmap last updated: 2026-08-20 after renumbering v3.0 to avoid retained historical phase directories*
