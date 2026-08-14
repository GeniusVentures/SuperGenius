---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: trusted-peer genesis, quorum-policy, and production integration gaps
current_phase: 13
status: executing
stopped_at: Planned 13-27 through 13-29; ready to execute 13-27-PLAN.md
last_updated: "2026-08-14T20:21:55.358Z"
last_activity: 2026-08-14 -- Phase 13 planning complete
progress:
  total_phases: 17
  completed_phases: 5
  total_plans: 37
  completed_plans: 34
  percent: 29
---

# State: SuperGenius — Multi-Signature Secure CRDT Storage

**Last updated:** 2026-08-12
**Milestone:** v1.1 — Multi-Signature Secure CRDT Storage
**Current Phase:** 13

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-20)
**Core value:** A decoupled multi-signature component and secure CRDT storage layer let specific CRDT-backed values require quorum signatures to create/update — first applied to `TrustedPeerRegistry` and `BURN_BASIS_POINTS`.
**Current focus:** Phase 13 — Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps

## Current Position

Phase: 13 (Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps) — EXECUTING
Plan: 27 of 29
Status: Ready to execute
Last activity: 2026-08-14 -- Phase 13 planning complete

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 8 | MultiSig Primitive | complete | MSIG-01, MSIG-02, MSIG-03 |
| 9 | SecureCRDT Layer | complete | SCRDT-01, SCRDT-02, SCRDT-03, SCRDT-04 |
| 10 | TrustedPeerRegistry | complete | TPR-01, TPR-02, TPR-03 |
| 11 | BurnConfig Quorum Wiring | complete | BURN-01, BURN-02, BURN-03 |
| 12 | ValidatorRegistry Migration | complete | MIG-05, MIG-06 |
| 13 | Trusted-peer genesis, quorum policy, and production integration | ready to execute | BOOT-01..04, POLICY-01, VALID-01, TEST-01 and audited v1.1 closures |

## Key Decisions

- Reuse `ConsensusAuth` primitives directly (signing-bytes/SHA-256/`VerifySignature`), not `ConsensusManager`'s proposal/vote/certificate lifecycle — `ConsensusManager`'s voter/weight source is hardwired to a single `ValidatorRegistry` instance
- Propose/sign/quorum flow transported over CRDT itself (pending-value + signature entries via filter callbacks); no new networking/RPC
- `ISignedCRDTData` interface-based per-type classes (not a generic `SignedCRDTValue<T>` template) — matches `ValidatorRegistry`'s existing per-type style
- `TrustedPeerRegistry` is separate from `ValidatorRegistry`'s consensus voter set — different concerns (economic-parameter signers vs. consensus validators)
- `BURN_BASIS_POINTS` cached in `TransactionManager`, refreshed via CRDT-change callback — avoids a CRDT read on every `PayEscrow` call

## Notes

- This milestone continues phase numbering from an undocumented prior body of work (`.planning/phases/01` through `07`, bridge-relayer/consensus-voting features). Phases 8-13 in this milestone are unrelated to those directories; do not reuse or renumber them.
- Precedent to build from: `ValidatorRegistry` (`src/blockchain/ValidatorRegistry.hpp`) already does signature+quorum-gated CRDT updates; `ConsensusAuth.hpp` has the reusable signing-bytes/SHA-256/verify primitives.
- Brownfield codebase map exists at `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS).
- Sequential dependency chain completed through Phase 12; Phase 13 is the audited production/security closure and depends on Phase 12.

## Operator Next Steps

- Execute the three additive Phase 13 gap-closure plans (`13-27` through `13-29`) using their three-wave dependency map in `.planning/ROADMAP.md`.
- Run `$gsd-execute-phase 13 --gaps-only` to close CR-11 through CR-14, rerun the exact 22-case/25-target gate, and perform five passive-lifetime repetitions.

## Session

**Last session:** 2026-08-14T18:05:08.629Z
**Stopped At:** Planned 13-27 through 13-29; ready to execute 13-27-PLAN.md
**Resume File:** None

## Accumulated Context

### Pending Todos

- 3 pending — see `.planning/todos/pending/`

### Roadmap Evolution

- Phase 13 added: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps

### v1.0 History

v1.0 (GeniusNode Construction Refactor) shipped 2026-07-03 — see `.planning/MILESTONES.md` and `.planning/milestones/v1.0-*` for full history. Between v1.0 and v1.1, a substantial body of bridge-relayer/consensus-voting work (`.planning/phases/01` through `07`) was executed outside formal GSD milestone tracking; it is unrelated to this milestone's scope.

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| Phase 13 P01 | 10min | 1 tasks | 7 files |
| Phase 13 P03 | 27min | 3 tasks | 10 files |
| Phase 13 P12 | 12 min | 1 tasks | 8 files |
| Phase 13 P02 | 11min | 2 tasks | 5 files |
| Phase 13 P04 | 22m | 3 tasks | 9 files |
| Phase 13 P05 | 23min | 3 tasks | 11 files |
| Phase 13 P06 | 8min | 1 tasks | 4 files |
| Phase 13 P09 | 22min | 2 tasks | 9 files |
| Phase 13 P07 | 12min | 3 tasks | 15 files |
| Phase 13 P10 | 7min | 1 tasks | 2 files |
| Phase 13 P11 | 4min | 1 tasks | 4 files |
| Phase 13 P13 | 10min | 1 tasks | 5 files |
| Phase 13 P16 | 6min | 1 tasks | 2 files |
| Phase 13 P17 | 13min | 2 tasks | 8 files |
| Phase 13 P14 | 13min | 1 tasks | 8 files |
| Phase 13 P15 | 4min | 1 tasks | 2 files |
| Phase 13 P18 | 7min | 1 tasks | 1 files |
| Phase 13 P19 | 5 min | 1 tasks | 3 files |
| Phase 13 P20 | 22 min | 2 tasks | 3 files |
| Phase 13 P21 | 25 min | 1 tasks | 2 files |
| Phase 13 P22 | 9 min | 1 tasks | 1 files |
| Phase 13 P23 | 29 min | 1 tasks | 3 files |
| Phase 13 P25 | 1h 31m | 1 tasks | 3 files |
| Phase 13 P24 | 1h 18m | 1 tasks | 3 files |
| Phase 13 P26 | 11min | 2 tasks | 1 files |

## Decisions

- [Phase 13]: Genesis identity uses SGNS_TRUST_GENESIS_V1 with fixed-width big-endian fields and sorted lowercase 64-byte keys. — This makes the reviewed trust root deterministic and independently verifiable.
- [Phase 13]: Initial genesis policy is version 1 with burn value 100 and majority/two-thirds safety floors. — Unsafe bootstrap thresholds or ambiguous economic readiness must fail before fingerprinting.
- [Phase 13]: SecureCrdt owns an isolated registry by default; duplicate same-node patterns fail closed instead of replacing the current owner. — This prevents co-located nodes and duplicate policy owners from replacing signer sources or unregistering one another.
- [Phase 13]: Quorum policy identity binds distinct predecessor and authorizer hashes in canonical bytes. — Both links must equal the confirmed current policy hash for a successor.
- [Phase 13]: Membership uses M/2+1 and BurnConfig uses M-M/3 with bounds checked first. — Exact integer formulas avoid percentage drift and overflow.
- [Phase 13]: Production TPR and BurnConfig select explicit policy-specific validators. — The burn path cannot silently inherit the weaker membership floor.
- [Phase 13]: Genesis derives version-1 policy and burn records from the canonical signed manifest. — This makes the bootstrap proof independently revalidatable after restart.
- [Phase 13]: Trust-store loads verify complete policy and burn predecessor chains. — Canonical hashes and quorum proofs, not JSON or arrival order, determine authority.
- [Phase 13]: Competing same-version trust candidates return STALE_HEAD. — Exact replay/decrease remains distinct while concurrent losers have a stable race outcome.
- [Phase 13]: Candidate approvals repeat exact canonical core bytes and bind version, content hash, and signer in the storage key. — Self-contained signed bytes and key binding prevent ambiguous reconstruction or cross-candidate approval reuse.
- [Phase 13]: One CandidateAuthorizationSource snapshot drives local submission and remote filtering. — Both ingress paths must enforce the same live network, predecessor, policy, signer, and resource constraints.
- [Phase 13]: Current-only candidate listing separates activation eligibility from bounded stale audit visibility. — Predecessor changes must immediately deactivate stale candidates without erasing authenticated audit records.
- [Phase 13]: Production TPR publishes peers only from durable TrustStateStore snapshots. — Genesis and successor candidates remain separate, and receive callbacks never sign.
- [Phase 13]: Trust records bind proofs to exact candidate-core authorization bytes with strict legacy decoding. — Restart verification must check the same context operators approved without rejecting prior Phase 13 records.
- [Phase 13]: Burn v1 uses a domain-separated genesis anchor and current burn quorum. — Economic readiness cannot derive from the bootstrap proof or unconfirmed peers.
- [Phase 13]: A node-scoped confirmed burn provider publishes only after durable activation. — TransactionManager replacements need one authoritative readiness and value source.
- [Phase 13]: GlobalDbNetworkComposition validates configuration in Create but defers network and datastore side effects to Start.
- [Phase 13]: The GlobalDB transport identity stays internal beneath the database path; callers provide no account or private key.
- [Phase 13]: GlobalDB listen and broadcast topics remain mandatory caller inputs so local trust tools reuse the production CRDT channel.
- [Phase 13]: sgns-trust consumes canonical GenesisManifest bytes and a caller-supplied existing production CRDT topic. — The reviewed, fingerprinted, signed, submitted, and verified bytes remain identical while the tool reuses the deployed topic.
- [Phase 13]: Private key material enters only through an owner-controlled 0600 no-symlink file or echo-disabled terminal input and never through argv or environment variables. — This limits secret exposure and permits deterministic cleanse plus success-only deletion.
- [Phase 13]: Local propose and approve operations attempt durable activation after the explicit signature while receive and list paths remain signer-free. — Explicit operator actions can complete quorum without adding an automatic signing surface.
- [Phase 13]: TrustStartupController advances only from verified durable records while networking remains live in restricted waiting states.
- [Phase 13]: Canonical peer ordering is diagnostic-equivalent on restart; actual trust-field conflicts alert while network mismatch remains fatal.
- [Phase 13]: First-boot verification connects independent production GlobalDB compositions through the reviewed GenesisCeremony path before protected-key deletion.
- [Phase 13]: Operator manifest construction mirrors the release codec exactly because sgns-trust accepts canonical binary GenesisManifest bytes, not JSON. — This keeps independently reviewed, fingerprinted, submitted, and durably verified bytes identical.
- [Phase 13]: Durable verified trust state is restart authority; software-only protection excludes restoration of the whole disk and all local anchors. — TPM or OS-keystore monotonic state, or authenticated off-host checkpoints, is required for that threat.
- [Phase 13]: Canonical v1.1 completion is derived from Plan 13-08's recorded exact 25/25 gate and five additional policy-lifetime passes.
- [Phase 13]: MIG-05 remains the approved signature-verification-only migration; full ValidatorRegistry ISignedCRDTData adoption remains retired.
- [Phase 13]: BOOT-04 covers software-visible rollback while whole-disk/all-anchor restoration remains an accepted unsolved boundary without external anchoring.
- [Phase 13]: Burn readiness is classified from the verified authorization path stored with the current burn head, never from proof count or the current policy threshold.
- [Phase 13]: While burn v1 is BootstrapOnly, both successor APIs reject before normal validation and writes; only an identical burn-v1/value-100 canonical candidate-core with current-policy burn quorum may replace its proof.
- [Phase 13]: PayEscrow uses validated quotient/remainder basis-point arithmetic so full-domain uint64_t financial outputs are exact without non-standard wide integers. — Validated bounds make each intermediate and the final sum fit uint64_t while preserving floor semantics.
- [Phase 13]: One validated signer-set snapshot governs legacy membership, retained-child pruning, the storage bound, and quorum evaluation for each operation.
- [Phase 13]: Remote legacy signatures must bind to the exact base/sig/canonical-address key before cryptographic verification or persistence.
- [Phase 13]: Policy successor authorization is available only in ConfirmedReady, and local policy signing preflights BurnConfig economic readiness. — Rejected policy operations must not write or sign before peer-confirmed burn readiness.
- [Phase 13]: Activation APIs return false only for authenticated below-quorum candidates; actionable validation and durable failures remain errors. — Callers can preserve ordinary pending operations without masking corruption, wrong-head, or commit failures.
- [Phase 13]: Startup activation failures emit TRUST_ACTIVATION_FAILED with candidate identity and typed error context. — Operators need structured evidence when asynchronous durable activation fails.
- [Phase 13]: BurnConfig restart publication consumes TrustStateStore's verified PeerQuorum classification and never reconstructs authority from proof cardinality or the current policy threshold.
- [Phase 13]: Phase 13 gap closure requires focused counterexamples, exact 25-test enumeration/JUnit accounting, and five post-gate lifetime repetitions in one fail-fast chain.
- [Phase 13]: Public LoadAndVerify holds transition_mutex_ across complete trust verification; lock-owning commit paths use LoadAndVerifyUnlocked. — This serializes coherent durable views without recursively locking commit precondition and post-commit verification.
- [Phase 13]: Automatic signing is limited to a verified current member and the exact deterministic BootstrapOnly burn-v1/value-100 candidate. — This closes initial-burn liveness without broadening manual approval or successor-signing authority.
- [Phase 13]: SecureCrdt callbacks queue serialized worker Refresh operations. — Activation writes must not reenter the GlobalDB callback thread.
- [Phase 13]: Failed candidate IDs are suppressed only for the current controller while authoritative CRDT records remain restart-discoverable. — This prevents tight in-process retries while preserving fault recovery after reconstruction.
- [Phase 13]: Only remotely authored trusted-peer callback records enter passive activation; explicit local admin paths retain their own activation result. — Prevents synchronous callback activation from racing the explicit administrative decision.
- [Phase 13]: Policy candidates use the serialized callback worker; teardown joins queued work before owner-safe callback removal. — Keeps GlobalDB writes off callback delivery and queued work within controller lifetime.
- [Phase 13]: Burn-ready refresh lists and deterministically processes retained successors before ConfirmedReady; only foreign burn approvals enqueue passive activation. — This closes passive divergence while preserving explicit LocalTrustAdmin activation and preventing local callback races.
- [Phase 13]: GeniusNode serializes complete transitions with a recursive mutex so existing same-thread nested transitions remain valid while concurrent duplicate work is excluded.
- [Phase 13]: Posted trust transitions carry their source state and lifecycle epoch; ConfirmedReady may initialize transactions only from the two trust waiting states.
- [Phase 13]: TransactionManager replacement stops and releases prior bridge, state, slot-hash, GlobalDB, and account callback ownership before TransactionManager::New installs a generation-owned replacement.
- [Phase 13]: Policy replay merges authoritative retained candidates with callback hints deterministically. — Callback delivery is not restart authority; controller-local suppression resets on reconstruction.
- [Phase 13]: Policy candidates use pending and failed containers distinct from burn candidates. — Their authoritative discovery APIs, retry state, and typed error contexts must not cross domains.
- [Phase 13]: Actionable retained-policy failure suppression is scoped to one controller lifetime. — This bounds in-process retries while reconstruction can rediscover and retry the retained authenticated record.
- [Phase 13]: Final Phase 13 closure requires exact source/XML/CTest/JUnit name equality and target-scoped sanitizer proof — Absent two-target instrumentation is NOT_RUN, never PASS.
