# Milestones

## v1.1 trusted-peer genesis, quorum-policy, and production integration gaps (Shipped: 2026-10-05)

**Phases completed:** 8 phases, 52 plans, 66 tasks

**Stats:** 7 executed phases (08-13, 15; phase 14 discarded pre-execution), 55 plans / 54 summaries on disk (CLI parses 52 — 15-04 descoped into 15-13, phase-14 plans unexecuted) · git range `959d804b2 → HEAD` · 424 files changed, +63,734 / −8,477 (src/test/3rdparty) · 70 days (2026-07-27 → 2026-10-05)

### Known Gaps (accepted at close, 2026-10-05)

- **ELM-01..08 — dropped from this milestone.** The 2026-08-26 ELM-bridging extension never executed here: ELM-04..07 is cross-repo scope (GeniusVentures/SGProcessingManager); ELM-01..03/08 were assigned to a Phase 13 that instead closed the trusted-peer/production gaps. Track cross-repo.
- **Phase 14 (account-generation/retired-manager) — discarded.** 0/15 plans executed (owner decision 2026-08); its SelectAccount lifecycle safety residual (13-VERIFICATION D-13..D-16, CR-01..04) is permanently open.
- **MIG-05 — partial (behavior-neutral).** ValidatorRegistry genesis path reverted to direct `GeniusAccount::VerifySignature` by `d1f2a14ca`; the multisig delegate is a pure pass-through, so no behavioral divergence. Re-apply or amend REQUIREMENTS archive text.
- **15-04 — descoped** into 15-13's app-layer gating (D-07 delivered; verified).
- **Deferred items:** 18 acknowledged at close (see STATE.md ## Deferred Items).
- Full detail: `milestones/v1.1-MILESTONE-AUDIT.md`.

**Verification:** every delivered phase VERIFICATION.md passed or conditional-passed; phase 15 cycle 5 (11/11); milestone audit 21/30 requirements satisfied (22/22 core; 8 ELM dropped), 6/7 integration, no broken E2E flows.

**Key accomplishments:**

- Established `ISignedCRDTData` (per-type payload codec + Verify/Apply) and static `SecureCrdtRegistry` (regex-keyed policy resolution with injectable SignerSetSource), both header-only, zero GlobalDB/CRDT/node dependency.
- Genesis-seeded, quorum-updatable TrustedPeerRegistry built as the first real consumer of Phase 9's SecureCrdt/SecureCrdtRegistry/ISignedCRDTData machinery, wired into a standalone `trustedpeer` CMake library.
- Automated end-to-end proof of TrustedPeerRegistry's genesis-seeding (TPR-01) and N-of-M quorum-gated membership changes (TPR-02) via a real SecureCrdt-backed single-node fixture, plus the TPR-03 grep-inspection companion and a new parse-only `sgns_config.json` config surface for Phase 11.
- TransactionManager's hardcoded BURN_BASIS_POINTS constant replaced by a cached atomic refreshed via BurnConfig's quorum-signed CRDT value; GeniusNode now constructs SecureCrdt->TrustedPeerRegistry->BurnConfig before TransactionManager at startup, halting cleanly on any majority-floor construction failure.
- Domain-separated canonical genesis bytes now bind the network, bootstrapper, ordered peers, policy thresholds, and initial burn value into one reproducible SHA-256 trust fingerprint.
- A synchronous network-scoped RocksDB trust store now revalidates the complete genesis, policy, and burn chain before exposing any restart or transition authority.
- Each SecureCrdt now owns an isolated policy registry, so co-located nodes can govern identical CRDT keys with distinct signer sets without replacement or teardown collisions.
- Explicit candidate-core approvals now activate trusted-peer and burn successors only under the durable current policy, with deterministic burn genesis and commit-before-cache publication.
- Account-independent GlobalDB composition now owns the production GossipSub, graphsync, scheduler, datastore, topic, and shutdown lifecycle needed by one-shot trust tooling and first-boot E2E.
- GeniusNode now keeps production GlobalDB synchronization alive while trust is unconfirmed, enables economics only after durable TPR and BurnConfig v1 quorum, and preserves verified last-known-good authority across restart, tamper, rollback, and fork conditions.
- Account selection now preserves node-owned trust policy, and real PayEscrow output changes only after a current-policy quorum reaches durable BurnConfig activation.
- A review-gated `sgns-trust` command now submits canonical genesis through production GlobalDB/SecureCrdt, removes the ephemeral key only after durable confirmation, and exposes explicit local-only policy and burn approvals.
- Operators now have an exact reviewed-manifest ceremony and local approval runbook that preserves durable trust authority while spelling out key-erasure and whole-disk rollback limits.
- v1.1 metadata now reflects the recorded 25/25 production security gate while retaining the narrow ValidatorRegistry migration and accepted whole-disk rollback limit.
- Versioned policy bytes now bind every signer and threshold decision while exact integer floors and dual current-hash links prevent unsafe thresholds and proposed-set self-authorization.
- Verified burn authority now gates durable policy and later-burn advancement, with only an exact peer-quorum replacement of burn v1/value 100 able to unlock successor commits.
- Runtime and local-admin policy operations now remain locked until peer-confirmed burn readiness, while burn-v1 recovery stays usable and real activation failures are observable.
- Peer-quorum burn authority verified under its historical policy now remains economically ready after later policy threshold increases, while BootstrapOnly state remains unpublished.
- PayEscrow now calculates exact basis-point burns for every uint64_t escrow amount and rejects invalid policy values before escrow lookup or payout persistence.
- Legacy SecureCrdt signatures now require exact canonical current membership on both ingress paths, while retained children remain bounded to that same signer snapshot.
- All four blocker counterexamples, all nine HIGH-threat mitigations, the unchanged 25-test Phase 13 gate, and five additional account-lifetime repetitions passed in one fail-fast execution.
- Public restart verification now observes one complete old-or-new durable trust view while every commit shape retains synchronous post-commit verification without recursive-lock deadlock.
- Production startup now contributes and recovers deterministic burn-v1/value-100 approvals for eligible peers, passively activates retained quorum after restart, and exposes every actionable genesis or burn activation failure.
- Production receivers now durably converge on quorum-confirmed trusted-peer policies without signing or a third administrative action, while pending, commit-failure, and teardown outcomes remain explicit and safe.
- Five exact production/concurrency counterexamples, the unchanged 25-target Phase 13 gate, structural no-bypass proofs, and five additional policy-lifetime repetitions all passed from freshly linked release binaries.
- Burn-ready nodes now discover and durably activate quorum-approved successors before readiness, with a three-node production test proving passive C's real PayEscrow burn changes from 100 to 250 across account-manager lifetime transitions.
- A reconstructed startup controller now rediscovers retained current-policy quorum and durably advances the exact successor without another proposal, approval, CRDT write, admin call, signature, or activation nudge.
- Persisted-ready GeniusNode restart now constructs and starts exactly one TransactionManager while preserving historical transaction CIDs and generation-owned GlobalDB, account, blockchain, bridge, and burn-provider callbacks.
- Freshly linked Release binaries passed the exact 15-counterexample and explicit 25-target gates, five passive-lifetime repetitions, and all T13-G01..G25 HIGH-threat dispositions without weakening production-path or accounting requirements.
- Account selection now unpublishes and drains stale account-bound work before atomically publishing a coherent replacement generation, while trusted-peer approvals remain bound to one immutable node authority across every transaction-account switch.
- Trusted-peer refresh now retries only transient policy/burn discovery failures through an owner-independent serialized dispatcher, with exact bounded backoff, coalescing, typed events, and safe final-owner release from inside dispatched callbacks.
- Freshly linked Release binaries passed exact 22-case source/XML accounting, exact 25-target CTest/JUnit equality, and five passive-lifetime repetitions, giving every HIGH threat T13-G01..G32 named evidence while reporting unavailable sanitizer instrumentation only as NOT_RUN.

---

## v3.0 Canonical Burn Finality Rebuild (Shipped: 2026-09-03)

**Phases completed:** 5 phases (8-12), 34 plans, 48 tasks

**Stats:** 186 files changed, +72,648 / −1,029 lines · 21/21 requirements validated · 290 commits · 14 days (2026-08-20 → 2026-09-03) · git range `8d66d670a → 94b54905e`

**Key accomplishments:**

- Canonical burn-slot identity shared by every competing mint proposal, cryptographically bound to the exact winning proposal (Phase 8)
- Durable one-vote-per-slot lock persisted to RocksDB before broadcast, cleared only on matching durable finality (Phase 9)
- Deterministic verifiable publisher authority: persistence-before-advertisement, lowest-SHA-256 convergence for contested immutable records, safe failover with no bridge-specific finality side channel (Phase 10)
- Convergent certificate consumption with the CRDT work journal as the sole retry boundary; certificate-first Mint recovery with tightly validated embedded fallback; Mint V2 held VERIFYING until idempotent effects and bridge marker both persist (Phase 11)
- Real-socket four-peer production-path fault proof: one canonical slot, one authoritative certificate, one exact mint effect through contention, propagation disorder, publisher loss, and restart — exact-once held in every run ever recorded (Phase 12, TEST-01..06)
- Post-restart certificate recovery made reliable: surviving-replica retention before publisher restart + mesh-readiness-gated re-advertisement (RestartAtVote from ~50%/run to 8 consecutive full-suite greens)
- Real production defects found and fixed under evidence discipline: asio teardown-order SIGSEGV (propagated across all proof artifacts), stale test-fixture RocksDB reuse (run-unique + reap), SameBurn check-then-act wait race, silent no-quorum rejection made visible
- Publisher-observer meta-test apparatus removed by developer directive at close (801 lines) — the regression suite is the six finality scenarios, three-consecutive-serial-pass verified with zero crash reports

**Known deferred items at close:** 13 (see STATE.md Deferred Items — stale v1.0-era items, June quick-tasks from the TokenId subsystem, and debug sessions mooted by the apparatus removal). Plus standing deferred: thirdparty StopImpl hardening, MintRecoveryDiagnostics UAF, WR-02 notify-under-paired-mutex, CRDT equal-priority overwrite guard.

## v1.1 Multi-Signature Secure CRDT Storage (superseded — shipped 2026-10-05 with recorded gaps; see the Shipped entry above)

> **v1.1 extended, not closed.** Phases 10–12 (TrustedPeerRegistry, BurnConfig Quorum Wiring, ValidatorRegistry Migration — the items listed as "deferred" below) completed 2026-07-24/27. On **2026-08-26** the milestone was extended with the ELM Bridging phases as a **product-v1.0 pre-ship requirement** (owner directive; see `.planning/notes/ELM-bridging-gaps.md` and `INGEST-CONFLICTS.md`): Phase 13 — ELM Job Bridging (SuperGenius, issue #369), Phase 14 — ELM Runtime in SGProcessingManager (cross-repo, GeniusVentures/SGProcessingManager#17). The milestone now gates product v1.0. A final shipped entry replaces this section when Phases 13–14 close.

## v1.1 Multi-Signature Secure CRDT Storage (partial ship: 2026-07-29)

**Phases completed:** 2 phases (08-multisig-primitive, 09-securecrdt-layer), 4 plans

**Key accomplishments:**

- `MultiSig` library: canonical signing-bytes + N-of-M quorum evaluation (dedup + verify loop) reusing `ConsensusAuth`'s SHA-256/`VerifySignature` primitives, usable independently of CRDT (Phase 08-multisig-primitive — MSIG-01/02/03)
- `ISignedCRDTData` interface + `SecureCrdtRegistry`: static topic/key-pattern → {signer-set source, quorum rule, type} registry declared in code (Phase 09-securecrdt-layer — SCRDT-01/02)
- `SecureCrdt` wrapper: local-write gate rejecting unsigned/under-signed writes before apply, plus read-path quorum re-derivation; propose/sign/quorum flow transported entirely over CRDT put + filter-callback, no new networking/RPC (Phase 09-securecrdt-layer — SCRDT-03/04)
- Full build + CTest green, including an end-to-end propose/sign/quorum handoff-contract test

**Note:** This milestone's branch (`gsd/phase-09-securecrdt-layer`) numbered its own phases 8-9 independently of the `v2.0` roadmap's phases 8-9 (unrelated topics — burn/mint datapath robustness vs. multisig/SecureCRDT). Rebased onto `develop` on 2026-07-29 after `v2.0` phase 8 had already merged; `v2.0` was kept as the current milestone in `PROJECT.md`/`STATE.md` throughout.

**Deferred to a future milestone:** `TrustedPeerRegistry` (TPR-01/02/03), `BURN_BASIS_POINTS` as a quorum-signed CRDT value (BURN-01/02/03), and `ValidatorRegistry` migration onto `ISignedCRDTData` (MIG-05/06) — these were in the original v1.1 requirements but not implemented in phases 08-09.

---

## v1.0 GeniusNode Construction Refactor (Shipped: 2026-07-03)

**Phases completed:** 3 phases, 5 plans, 0 tasks

**Stats:** 39 files changed, +2956 / −341 lines · 12/12 v1 requirements validated · git range `2585e6a9 → HEAD`

**Key accomplishments:**

- Config-driven network settings: `auto_dht` + `port_seed` (renamed from `base_port`) read from `network_config.json` in `InitNetwork()` with config-wins precedence, safe defaults, and `pubsub_port` > `port_seed` priority Doxygen (Phase 1 — CFG-01, CFG-04)
- `NodeType` enum (Full/Light/Archive) + case-insensitive `NodeTypeFromString()` + `node_type` read in `LoadSgnsConfig()`; `is_full_node_` derived in a reordered private ctor that creates the account via `std::visit` **after** `LoadSgnsConfig` resolves the role (the init-order hinge fix) (Phase 2 — CFG-02, CFG-03)
- Canonical `New(dev_config, AccountSource)` variant factory (`AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>`, `FromPublicKey` promoted public); `nullptr`-on-failure preserved; old factories retained through Phase 2 then deleted (Phase 2 — INTF-01/02/03; Phase 3 — INTF-04)
- All ~25 `NewFromPrivateKey`/old-`New` call sites across 14 files migrated to `New(dev_config, FromPrivateKey{...})`; shared `WriteNetworkConfig`/`WriteSgnsConfig` helpers (truncate, create-dir, validate `node_type`); old factories + old private ctor deleted — `New(dev_config, AccountSource)` is the sole entry point (Phase 3 — MIG-01/02/03/04)
- Full build + CTest green; no behavior change for default or pre-existing config files (deployed configs without the new keys keep working via defaults)

**Known deferred items at close:** 0 (open-artifact audit clear). Future-milestone items noted in REQUIREMENTS v2: `NodeType` downstream propagation (PROP-01), distinct Archive-vs-Full behavior (PROP-02), `pubsub_port` numeric cleanup (HARD-01), config schema versioning (HARD-02).

---
