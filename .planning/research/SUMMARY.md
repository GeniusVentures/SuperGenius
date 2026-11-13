# Project Research Summary — v1.3 Security Remediation & Network Readiness

**Project:** SuperGenius milestone v1.3 — all 25 findings from the October 2026 external security assessment, across SuperGenius + libp2p/ipfs-pubsub forks + GeniusSDK
**Domain:** Security remediation + supply-chain/network-readiness engineering on an existing C++17 CMake/CTest blockchain (pre-public-network gate)
**Researched:** 2026-10-19
**Confidence:** HIGH (all four researchers verified claims against source in this workspace with file:line citations; external patterns grounded in official specs — see per-area assessment)

## Executive Summary

This is a remediation milestone, not a new architecture: the 25 findings close authorization gaps at five existing boundaries (consensus/registry, mint/escrow, bridge ingestion, gossip/DHT wire, processing grid) and add exactly two new components — the SGNS→EVM certificate exporter (#16) and the adversarial regression suite that is the milestone's verification spine. Expert practice is uniform on every cluster: fail-closed authorization bound to a quorum-committed anchor (Tendermint's signed `NextValidatorSetHash` is the pattern for #1), source-chain finality before irreversible issuance (#10), attestation facts bound exactly to the effect they authorize (#5, the Wormhole/tBTC invariant), libp2p strict verify-then-forward (#7) and spec-mandated DHT record validation (#15) — with Hive/Bitcoin-functional-test-style adversarial suites as the proof mechanism. The stack conclusion is unusually clean: everything except one GitHub Action (`actions/attest-build-provenance@v4.2.2`) and optionally one Android benchmark library is already satisfiable in-tree (libFuzzer/ASan harnesses, `SANITIZE_CODE`, Foundry/anvil fixtures, the fork's own relay/hole-punch/autonat code, GTest/CTest everywhere).

The four researchers independently converged on the same build order modulo granularity: fix the delivery pipeline first (#19/#20 — releases currently publish *before* tests run, and thirdparty/zkLLVM artifacts download with no hash check), close the trust boundaries (#1–#5, #10/#11, #21), authenticate the grid (#7 → #13 → #6), and activate capabilities only on top of proven boundaries (#16 strictly last). The single hardest structural fact: **fork fixes (#7/#8/#15/#17) ride a four-stage release pipeline** (fork commit → thirdparty superproject bump + release build → SuperGenius CI download, today unverified → integration tests), so #19 must land with or before the first fork fix and every fork-touching phase must schedule the superproject bump as an explicit step. Three cross-cutting disciplines recur at every new authorization boundary and should be first-class roadmap deliverables, not side effects: the **Reject / Defer(bounded) / Accept verdict model** (missing CRDT evidence is an ordering problem, not an authorization failure — defer with TTL and a hard cap, never unbounded pending queues); the **adversarial+compat test pair** (every fix ships both the named-attacker rejection test and the pre-upgrade-state-survives compat test); and **fork-divergence discipline** (FORK-DELTA manifest, topic-scoped upstreamable commits, injection over edition, scheduled upstream-sync cost measurement).

The top risks are self-inflicted liveness/compat breakage, not the attackers: a naive fail-closed #1 strands historical certificate re-validation on restart/sync (needs an explicit, documented grandfather rule tied to pre-upgrade finality); a naive legacy-mint disable orphans historical UTXOs and breaks fresh-node sync (gate on *admission of new transactions*, never deserialization of history — the BIP-16/65 height-gated-activation precedent); flipping #21's `valid_proof = true` bypass on without staging tombstones legitimate `/tx/` state out of the CRDT (audit → quarantine → enforce); and enabling relay on the fork's 154-line stub is a public DoS service (limits-first or don't complete it). Three findings are genuinely hard and must be scoped honestly rather than over-promised: #6 (no deployed network verifies nondeterministic compute cheaply — land the deterministic/redundant tier only), #23 (software-only confidentiality against the executor is impossible without TEE — define the boundary, don't claim privacy), and #17 (DCUtR punches ~60% in the field; the relay backbone *is* the topology — publish the contract either way).

## Key Findings

### Reconciled Build Order (the three orderings, unified)

Features' 3 waves, Architecture's 4 waves, and Pitfalls' PA–PI converge on one spine; disagreements were sequencing granularity, not direction. Resolutions: (1) #8/#9 wire hardening moves behind PB (Pitfalls' order) rather than Architecture's Wave-1 — both orderings agree #19/#20 precede it, and PB-first lets the verdict model and suite standards land before the broad fork surface; (2) #21 follows Pitfalls into PF because its tombstone risk demands the staging plan, though its log-only audit mode may start earlier; (3) #2 lands minimally in PB *only if* the consensus-certificate path already yields a certified task-result record (validate in phase research) — the full entitlement binding completes in PF with #6/#13's authenticated results; (4) #12 (on-chain role audit) and #24 (SDK memory safety) are dependency-free — start them immediately as parallel tracks rather than serializing them into the last phases (Architecture Wave 1 / Features P1 placement adopted over Pitfalls' thematic grouping).

### Recommended Stack

**Two new externals maximum; everything else in-tree.** The repo's no-new-dependency rule holds for all 25 findings except #19's provenance action.

- **libFuzzer + ASan (in-tree)** — fuzz #8/#9 parse boundaries; `addfuzztarget` infra and three working harnesses exist — extend, don't adopt another engine (AFL++/cargo-fuzz explicitly rejected).
- **GTest + CTest (in-tree)** — the adversarial regression suite is written tests, not infrastructure; includes GeniusSDK's `test/` (currently CMakeLists-only) and the forks' own test trees.
- **`SANITIZE_CODE=address,undefined` (in-tree)** — SDK/ingestion memory-safety builds via existing `CommonCompilerOptions.cmake` option (#24/#9).
- **Foundry/anvil + `cast` (conditionally integrated)** — reorg/confirmation-depth tests (#10) and the on-chain role audit (#12); keep the `requires_foundry` skip-label pattern, and decide which CI leg installs Foundry.
- **`actions/attest-build-provenance@v4.2.2` (new, pinned by SHA)** — SLSA-style provenance for release tarballs; conservative choice over slsa-github-generator. Caveat: attestation on self-hosted runners proves source→artifact binding, not runner identity — document the residual.
- **Release-asset `digest` field via `gh api` + `sha256sum`** — zero-new-tooling hash verification of thirdparty/zkLLVM downloads; assets uploaded before June 2025 lack digests (backfill or committed manifest decision needed).
- **androidx.benchmark 1.5.0 (optional)** — only if #18/#25 budget proof needs library-grade metrics; the zero-dependency `dumpsys batterystats` + XCTest route is the acceptable first rung (requires a host-module placement decision — none exists today).
- **Fork's own relay/holepunch/autonat code (in-tree, disabled)** — #17 is wiring/completion of existing protocol code, not a new library; go-libp2p/rust-libp2p adoption explicitly rejected as a platform rewrite.

CI wiring points are pinned to current `cmake.yml` lines in STACK.md: digest check after the download steps (~318–347), `publish` job split with `needs: build` replacing the tag-before-tests ordering (539–564 vs 597), SHA-pinning of tag-pinned actions, and a clang-only fuzz matrix leg (MSVC has no libFuzzer).

### Expected Features

**Must have (the safety boundary — "launch with" per FEATURES.md):** adversarial suite apparatus + metadata convention; #1 registry-CID authorization chain (quorum-signed update chain from genesis, closing the historical-CID filtering bypass); #2 escrow entitlement; #3 fail-closed chain-id/source-ref paths; #4 legacy-mint disable-or-authorize; #5 receipt-fact→mint-output binding; #10 confirmation depth/finality per source chain; #11 content-corroborated multi-endpoint quorum; #8 length caps before allocation (all three fork readers) + depth limits; #9 error-results not `.value()` on network data; #7 verify-then-forward with signer-identity binding; #21 proof-bypass removal (enforce or honestly disable — never a claimed-but-dead control); #13 queue/payout authentication; #24 SDK length-checked copies + real test suite; #20 release-before-tests reorder.

**Should have (differentiators):** named-attacker-authority suite as machine-checked house style; registry authorization via quorum-signed update chain (Tendermint-equivalent without header changes); content-corroboration with per-operator policy; tiered result verification (deterministic sample → redundancy now, TEE/ZK later); fork-divergence discipline as an audit artifact; SLSA provenance after gating works.

**Scope honestly / defer beyond this milestone:** #6 limited to the deterministic/redundant tier (GCS Execution Integrity and ZK/TEE tiers are a separate change stream); #23 defines + enforces a named execution boundary with an honest threat model (confidentiality-vs-executor only via TEE later); #17 relay-backbone topology contract with best-effort DCUtR (no phone-to-phone mesh promise); contracts-repo remediation (multisig/timelock) recorded as operational findings only. Note: all 25 stay in v1.3 (PROJECT.md) — these are scoping decisions *within* the milestone, not deferrals.

**Anti-features (must not appear in REQ language):** trust-the-endpoint corroboration; any fail-open/security-by-flag default; verification theater (send-side signing without receive-path enforcement); full-ZK-before-redundant-tier; unconstrained mesh promises; confidentiality claims without TEE; always-rebuild-everything CI; legacy paths left enabled "for compatibility"; refactors bundled into security diffs.

### Architecture Approach

Five boundary closures + two new components, all mapped to owning modules with file:line evidence. Key patterns: (1) **authorization at the consuming domain boundary** — #1 verifies the RegistryUpdate certification chain inside `ValidatorRegistry::LoadRegistryByCid` (machinery already present at :1111/:1183/:1255/:1581), never inside `CrdtDatastore`'s generic raw-DAG API, and `ConsensusManager`'s cache (:2589) admits verified snapshots only; (2) **extend existing auth seams** — `gossip_auth.hpp`'s sealed envelope (private networks today) extends to public topics; #13 seals queue/result messages with the same machinery; no parallel signature scheme; (3) **#3/#5/#10/#11 land as one coherent rewrite** of `PublicChainInputValidator::GatherVerificationEvidence` (fail-closed branches, canonical receipt content digests into evidence slots, per-chain finality policy, decoded-fact→mint-output binding reusing Phase-8 canonical burn identity); (4) **the exporter is a new module** (`src/bridge_exporter/`) — a genuinely new domain (outbound attestation) beside `src/watcher/`'s ingress; reads certificates only via `ConsensusManager`'s public contract, submits via `evmrelay`.

**Hard-invariant compliance is pre-mapped:** no business rules move into transport (finality *policy* stays in the evidence-rule owner; the fork owns only wire-level verification); one comparison home for the burn-fact identity; envelope sealing is authentication, not logic.

### Critical Pitfalls (top 6 of 14 — full catalog in PITFALLS.md)

1. **Fail-closed vs. CRDT arrival order (Pitfall 1)** — #1/#2/#22 reject-when-evidence-absent converts an ordering problem into an authorization failure; payouts stall, multi-node suites flake. Avoid: the Reject/Defer(bounded)/Accept verdict model with TTL + hard cap; never `DeliverySource::Local` provenance; wait-predicate fault tests per fix.
2. **Historical certificate re-validation breakage (Pitfall 2)** — `ValidateCertificate` re-runs on restart/sync/merge; a naive fail-closed loader makes pre-upgrade finality state stop re-validating. Avoid: three loader outcomes (authorized / unauthorized / unverifiable-at-height with an explicit documented grandfather rule tied to certificate finality) + a compat gate: pre-fix certificates re-validate after upgrade and restart.
3. **Legacy-mint orphaning (Pitfall 3)** — disabling v1 reception at deserialization breaks boot, sync, and historical UTXO spendability. Avoid: gate on admission of new transactions only; paired adversarial (new self-signed v1 rejected) + compat (pre-cutover outputs spend, restart-to-READY, fresh-node sync) tests; authorized replacement for v1-seeded test genesis.
4. **#21's dead branch is a tombstone path (Pitfall 11)** — enforcement erases `/tx/` state for proofs that fail (format drift, zkLLVM skew, placeholder fixtures). Avoid: staged audit → quarantine → enforce, with a stored-proof/zkLLVM inventory first and per-element `VerifyFullProof` budget measured.
5. **Release-gate lockout and erosion (Pitfall 9)** — gating on a flaky mutable ref blocks hotfixes and breeds manual bypass; tags are currently delete-and-recreated. Avoid: release-branch escape valve with the same required checks, immutable tags, flaky-test budget from day one, check-name verification.
6. **Fork divergence + relay stub DoS (Pitfalls 10, 12)** — every fork fix deepens unmergeable divergence; the fork's relay hop handler logs-and-drops with auto-reservations and zero limits. Avoid: FORK-DELTA manifest + upstreamable patch discipline from the first fork change; relay completion is limits-first (ACL, per-peer/IP caps, duration/data Limits per circuit-relay-v2) or replaced by the explicit-topology decision.

Cross-cutting: Pitfall 13 (the suite must prove surviving legitimate paths, not just attacks) makes the adversarial+compat pair a plan-completeness gate, with Phase-12 fixture rules (fresh temp DBs, teardown invariants, negative controls).

## Implications for Roadmap

One recommended build order — nine phases numbered 21–29 (Pitfalls' PA–PI spine, corrected by the resolutions above) — plus two dependency-free parallel tracks. All 25 findings are placed: #19/#20/#12/#24-start → 21; #1–#5/#14 (+minimal #2 if validated) → 22; #8/#9 → 23; #7/#15 → 24; #10/#11 → 25; #21/#13/#22/#6 → 26; #17 → 27; #18/#23/#25 (+#24 completion) → 28; #16 → 29.

### Phase 21: Verification Spine & Release Gate (PA)
**Rationale:** every later fix ships through this pipeline and is proven by this suite; #19 must exist before the first fork artifact flows (four-stage fork release train) so its provenance is verifiable.
**Delivers:** adversarial-suite apparatus (named-attacker-authority metadata convention, adversarial+compat pair rule, Phase-12 fixture standards, mixed-version interop-matrix harness scaffold); `publish`-job reorder gating release on the full test matrix; hotfix release-branch valve + tag immutability (kills the delete-and-recreate smell); content-hash verification of thirdparty/zkLLVM downloads (digest API + manifest fallback); SHA-pinned actions; clang fuzz CI leg.
**Addresses:** FEATURES clusters A and M (#19, #20). **Avoids:** Pitfalls 9 (gate erosion), 13 (attack-only suite), 4 (matrix born here, reused by 23/24/27).
**Parallel tracks starting here:** #12 on-chain role audit (Foundry `cast` runbook, doc deliverable, findings feed the contracts-repo stream — zero dependencies) and #24 GeniusSDK memory safety (characterization tests → additive versioned API → Unity wrapper coordination — independent repo, own CI).

### Phase 22: Consensus & Mint Authorization (PB)
**Rationale:** highest-credibility critical findings; defines the verdict model and authorization identities every later policy references; #1 is the trust anchor for certificates, hence for #2/#6/#13/#16.
**Delivers:** #1 registry-CID authorization chain in `ValidatorRegistry`'s loader (all call sites inherit; verified-only cache) with the three-outcome grandfather rule; #14 admission/equivocation audit against the hardened path; #3 fail-closed type-authorized chain-id/source-ref paths; #4 legacy-v1 admission cutover (new-tx gating + authorized test-genesis replacement); #5 receipt-fact→mint-output binding (canonical decode shared with Phase 25); minimal #2 if the certified task-result record exists on the consensus-certificate path (validate during planning — else #2's binding slides to Phase 26); verdict-model primitive (bounded defer) as a reusable component.
**Addresses:** FEATURES clusters B and C. **Avoids:** Pitfalls 1, 2, 3 (grandfather + cutover rules live with their fixes — splitting them across phases guarantees an orphaning intermediate state).

### Phase 23: Wire & Ingestion Hardening (PC)
**Rationale:** the remote-crash class with no dependencies beyond the pipeline; the simpler fork delta with which to establish fork-divergence discipline before the larger gossip/relay changes.
**Delivers:** #8 per-protocol length caps before allocation in all three fork readers (uvarint, big-endian, Kademlia) with named constants and oversized-vs-malformed error distinction; depth/nesting limits; #9 error-result hash parsing (`TransferTransaction`/`EscrowTransaction`) + fuzz-corpus extension of the three existing harnesses; mixed-version interop matrix run (large-payload graphsync between old/new peers); FORK-DELTA.md + patch-series convention + first upstream-sync cost measurement; thirdparty superproject bump + release as explicit steps.
**Addresses:** FEATURES cluster E. **Avoids:** Pitfalls 4 (fragmentation), 10 (divergence discipline starts with the first delta).

### Phase 24: Gossip & DHT Authentication (PD)
**Rationale:** authentic transport underpins the grid's authenticated claims (#13/#6 ride on it); same fork release train as Phase 23.
**Delivers:** #7 fork API extension (signed envelope to the validator hook — the current `(from, data)` signature cannot express verification), verify-before-forward after msg-id dedup, per-topic authorization policy **as data** (topic → required identity class: validator/registered for consensus topics, authenticated worker/developer for processing, authentication-only + scoring for data), throughput benchmark before/after, per-peer rate limits, async validate-then-forward fallback; #15 real Kademlia record validators + storage quotas + request-bound `AccountMessenger` responses (nonce-poisoning test first).
**Addresses:** FEATURES clusters F and G. **Avoids:** Pitfalls 5 (hot-path CPU + the API-gap trap), 6 (authentication-without-authorization; "validly-signed unregistered key" is its own adversarial test).

### Phase 25: Bridge Ingestion Safety (PE)
**Rationale:** independent of Phase 24 (touches only the evidence validator + config) and can overlap it; must precede Phase 29 (#16 activation gates on ingestion safety).
**Delivers:** #10 confirmation-depth gate as a restart-durable bounded state machine (receipt-seen → depth-tracking → finalized | reorged-out | timeout-escalate) with per-chain finality-type config (never a C++ constant) and the anvil simulated-reorg harness in `bridge_e2e`/`bridge_race`; #11 content-corroboration — canonical content digests (block hash + decoded event, sharing Phase 22's #5 decode) across **distinct operators**, operator-identity in the endpoint config schema, operator-collapse fails config validation at startup, below-quorum → Defer never mint.
**Addresses:** FEATURES cluster D. **Avoids:** Pitfalls 7 (reorg stall), 8 (de-facto single provider — the fabricated operator-quorum adversarial test).

### Phase 26: Processing-Grid Integrity (PF) — the dependency sink
**Rationale:** #6 consumes #13 (authenticated claims), #22 (sufficient escrow), #2 (entitled settlement), and #21 (proof tier) — shipping #6 before them "verifies forgeries faithfully" (FEATURES' dependency note); this phase completes the cluster #2–#6–#13–#21–#22 that partially landed in 22.
**Delivers:** #13 signed/ownership-gated queue updates + payout addresses and developer split fixed at agreement time (sealed with the Phase 24 envelope machinery); #22 escrow-sufficiency check at worker acceptance (independently validated dimensions × price ≤ escrow; price feeds advisory, never authorizing); #21 staged enforcement (audit counts → quarantine → enforce) with the stored-proof/zkLLVM inventory and per-element verify budget decided and recorded; #6 the deterministic/redundant verification tier — validation redundancy enabled on the production split path (`GeniusNode.cpp:3193` currently passes `false`), determinism preconditions for re-execution checks, tiered-by-job-value policy — scoped to this tier, explicitly not ZK/TEE.
**Addresses:** FEATURES clusters H, I, J, K. **Avoids:** Pitfall 11 (tombstoning), 1 (deferred verdicts on payout paths), the over-promise anti-features.

### Phase 27: Network Topology (PG) — with the mid-phase decision gate
**Rationale:** relay creates its own attack surface only safe after gossip auth (24) and caps (23); the finding explicitly permits either outcome.
**Delivers:** the **decision gate**: complete circuit-relay v2 in the fork — limits-first (reservation ACL: full nodes relay, phones are clients; per-peer/IP/ASN caps; duration/data `Limit`s; go-libp2p's post-advisory `Resources` model) + observed-address race fix + CGNAT/symmetric-NAT emulation tests + DCUtR best-effort — **or** the explicit-topology contract (phones as clients of reachable full-node relays; direct mesh best-effort). The fork-divergence cost of relay completion (Pitfall 10's highest-risk delta) is the review criterion for the gate; the topology document ships either way.
**Addresses:** FEATURES cluster N. **Avoids:** Pitfall 12 (reservations-for-everyone relay DoS), the mesh anti-feature.

### Phase 28: SDK, Execution Isolation & Mobile (PH)
**Rationale:** mobile readiness is last in the dependency chain (#25 needs #17's connectivity and #18's runnable worker; #18's measurement half needs #25's budget apparatus); SDK safety (#24) may already be complete from its early start.
**Delivers:** #18 minimal worker profile (compile/link-time exclusion of full-node components; config preset over the existing `node_type`/`is_processor` surface); #25 adaptive budgets (platform-state-aware watermarks via existing `background_config.h` knobs) + demonstrated lifecycle recovery (OS kill → relaunch → rejoin) with iOS time-slicing budgeted honestly; #23 the named, enforced execution boundary with documented threat model (scoped credentials, encrypted transport, permissioned networks — the existing Phase-15 machinery) and an honest no-confidentiality-without-TEE statement; #24 completion (versioned API + Unity wrapper lockstep) if not already landed.
**Addresses:** FEATURES clusters L, O, P. **Avoids:** Pitfall 14 (integrator breakage), the confidentiality anti-feature.

### Phase 29: On-Chain Audit & Return-Leg Activation (PI) — strictly last
**Rationale:** activating the EVM return leg with an unhardened registry converts a native trust bug into an EVM mint bug — strictly worse; the exporter consumes the same certificate/finality facts Phases 22 and 25 made trustworthy.
**Delivers:** the new `src/bridge_exporter/` module (canonical certificate store → attestation set → EVM tx via `evmrelay` → confirmation watcher); GNUSBridgeAttestor activation with the full checklist (attestor-set rotation drill, replay protection via nonce/certificate-slot binding, rate/size limits, monitoring, verification of actually-deployed selectors on the claimed mainnet addresses); #12's audit findings folded into the activation risk record.
**Addresses:** FEATURES cluster R (+Q documentation). **Avoids:** the ordering constraint violated — #1, #3/#5/#10/#11 all closed and proven first.

### Phase Ordering Rationale

- **Pipeline before fixes (21 → everything):** the release gate is the shipping control and #19 must precede the first fork artifact (the four-stage fork release train makes unverified downloads the delivery path for #7/#8/#15/#17).
- **Trust boundaries before capabilities (22–25 → 26–29):** the assessment response's own rule; capabilities verify through boundaries that must already hold.
- **#16 strictly last (29):** hard-depends on #1 (certificate trust anchor), #3/#5/#10/#11 (burn-fact quality), and the exporter — activating earlier multiplies exposure.
- **#6 after its cluster (26, sink position):** consumes #13/#22/#2/#21; #13 after #7 (signed queue discipline is void over spoofable gossip transport).
- **Fork phases sequenced for discipline (23 → 24 → 27):** caps (simplest delta, discipline established) → gossip/DHT (API change + policy) → relay (highest-divergence-risk, gated decision).
- **22 and 25 can overlap 23–24:** ingestion hardening touches only the evidence validator + config (Architecture's explicit parallelism note).
- **Grouping follows one-owner-per-domain:** each phase's changes attribute to named owning modules, keeping one rule change inside one module.

### Research Flags

Phases likely needing deeper research during planning (`--research-phase`):
- **Phase 22 (PB):** the #2 minimal-path question (does a certified task-result record exist on the consensus-certificate path?); grandfather-rule design; verdict-model primitive placement — Architecture explicitly defers these to phase research.
- **Phase 24 (PD):** fork gossip API extension shape (envelope-to-validator signature), per-topic policy table schema, throughput budget/benchmark methodology.
- **Phase 26 (PF):** #6 determinism preconditions and redundancy cost model; #21's stored-proof/zkLLVM format inventory and per-element verify budget; payout-record schema fixed at agreement time.
- **Phase 27 (PG):** the complete-vs-topology decision inputs (circuit-relay-v2 spec conformance cost vs divergence cost; CGNAT emulation harness).
- **Phase 29 (PI):** exporter module design and the attestor activation checklist (deployed-contract state verification).

Phases with standard patterns (skip research-phase):
- **Phase 21 (PA):** workflow wiring points pinned to exact `cmake.yml` lines in STACK.md; suite conventions proven in Phase 12.
- **Phase 23 (PC):** mechanical per-reader caps with named constants; existing fuzz harnesses to extend.
- **Phase 25 (PE):** anvil reorg-harness patterns documented; state-machine + per-chain-config shapes standard.
- **Phase 28 (PH):** characterization-tests-first and additive API versioning are standard; measurement paths specified in STACK.md (dumpsys/XCT first rung).

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH (in-tree) / MEDIUM (2 externals) | Every in-tree capability verified with file:line citations; the two new externals verified against official release pages/changelogs; self-hosted-runner provenance caveat documented |
| Features | HIGH (ecosystem) / MEDIUM (project-fit) | Patterns verified against libp2p/Tendermint/SLSA/Ethereum finality/bridge practice; project-fit from the assessment-response triage and PROJECT.md, not fresh code reads |
| Architecture | HIGH (verified) / MEDIUM (capabilities) | Every vulnerability site located and read in develop or the sibling checkouts; #16/#17/#18/#23/#25 MEDIUM where they depend on unbuilt components |
| Pitfalls | HIGH (repo mechanics) / MEDIUM-HIGH (external) | All 14 pitfall mechanisms verified against current source; external patterns from official specs/docs, community items marked |

**Overall confidence:** HIGH — the four files cross-corroborate on every load-bearing claim (the four-stage fork pipeline, the #1-before-#16 constraint, the #6 dependency sink, the #21 tombstone path), and the three build orders converged without substantive conflict.

### Gaps to Address

- **#2 minimal-path validation (Phase 22 planning):** whether the consensus-certificate path yields a certified task-result record the escrow entitlement can consume — decides #2's split between Phases 22 and 26.
- **Release-asset digest backfill:** thirdparty/zkLLVM releases older than June 2025 lack `digest` fields — re-cut releases or commit expected-hash manifests (upstream release-engineering decision Phase 21 inherits).
- **Android instrumentation host module:** no Gradle app module exists to host androidx.benchmark — minimal `test/android/` host vs zero-dep `dumpsys` scripting is a Phase 28 placement decision.
- **Foundry on CI:** `requires_foundry` tests skip everywhere today — decide which CI leg(s) own Foundry or #10 reorg tests never gate a release.
- **#6 determinism preconditions:** whether task re-execution is deterministic enough for the redundant tier — the honest scoping boundary Phase 26 must establish before promising the tier.
- **#16 deployed-contract state:** verification of actually-deployed selectors/attestor sets on the claimed mainnet addresses — Phase 29's activation checklist depends on it (and on #12's early audit results).
- **Mixed-version rollout policy:** for each fork wire change, decide fail-open-by-version-negotiation vs fail-closed-by-config during the skew window (the Phase 21 matrix makes either testable).
- **Gossip throughput budget:** no pre-change benchmark exists for the #7 hot path — the before/after pair must be built in Phase 24, not assumed.

## Sources

### Primary (HIGH confidence — verified in this workspace)
- Vulnerability/fix sites with file:line citations: `src/blockchain/ValidatorRegistry.cpp` (:785 loader; :1111/:1183/:1255/:1581 verification machinery), `src/blockchain/Consensus.cpp` (:3513; :2589 cache; re-validation sites :2712/:3244/:3347/:3390), `src/account/PublicChainInputValidator.cpp` (:307-490), `src/account/GeniusInputValidator.cpp` (:443-447), `src/transaction/TransactionManager.cpp` (:3834-3874 FilterProof), `src/transaction/TransactionConsensusHandler.cpp` (:545/:801), `src/account/{TransferTransaction,EscrowTransaction}.cpp` (:101/:106), `src/processing/processing_subtask_queue.cpp` (:24), `src/account/GeniusNode.cpp` (:1887, :3189-3194), `src/base/gossip_auth.hpp`
- Fork + SDK: `thirdparty/libp2p/src/protocol/gossip/impl/gossip_core.cpp` (:300-362), `.../kademlia/message.cpp` (:181), `.../impl/validator_default.cpp`, `.../relay/relay.cpp` (154-line stub), `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp` (:317-319), `GeniusSDK/src/GeniusSDK.cpp` (:204/:798), GeniusSDK `test/` (CMakeLists-only)
- CI/build: `.github/workflows/cmake.yml` (downloads :318-345; release-before-tests :539-564 vs :597; tag delete/recreate :545-551), `fuzz/CMakeLists.txt`, `cmake/functions.cmake` (:64), `build/CommonCompilerOptions.cmake` (:90-101), `test/src/bridge_e2e/` (anvil fixtures)
- Repo topology: `~/gnus/thirdparty/.gitmodules` vs SuperGenius `.gitmodules` (libp2p is a thirdparty-superproject submodule)
- Project basis: `.planning/PROJECT.md`, `.planning/notes/security-assessment-2026-10-response.md` (read in full by all researchers)

### Secondary (MEDIUM-HIGH confidence — official specs/docs, corroborated)
- libp2p pubsub spec (Strict/Lax signing), Kademlia spec (record validation), Circuit Relay v2 spec + go-libp2p relay `Resources` (MaxReservations 128) + relay DoS advisory; gossipsub v1.1 hardening (IPFS blog)
- Tendermint light-client docs (signed `NextValidatorSetHash`); Ethereum PoS finality (2-epoch FFG); Jump Crypto bridging/finality; Trail of Bits Engineer's Guide to Blockchain Finality; Arbitrum reorg docs
- SLSA build-provenance spec; GitHub release-asset digest changelog + artifact-attestations docs + `gh attestation verify` manual; GitHub protected-branches/required-checks docs
- BIP-16 / BIP-65 (height-gated consensus activation); protobuf CVE-2024-7254 / CVE-2026-6409 + Trail of Bits recursion guidance; 2026 gossipsub StrictSign and kad-dht CVEs (advisory records)
- Golem gWASM verification + payments; Akash deposit-backed leases; ethereum/hive adversarial harness; DCUtR ~60% punch-success field data (rust-libp2p/arXiv)
- `actions/attest-build-provenance` v4.2.2 and androidx.benchmark 1.5.0 release pages (the two externals — MEDIUM)

---
*Research completed: 2026-10-19*
*Ready for roadmap: yes — phase numbering continues at 21 (v1.2 executes on a separate branch)*
