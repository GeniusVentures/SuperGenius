# Milestones

## v1.2 Light-node transaction pruning (Shipped: 2026-10-26)

**Phases completed:** 5 phases (16-20), 29 plans, 62 tasks
**Stats:** 49 files changed, +17,668 / −171 (src/test/example) · git range `2272673a1 → HEAD` · 11 days (2026-10-15 → 2026-10-26) · 18/18 requirements verified · 139/139 threats closed (five SECURITY.md audits) · live-validated on staging (dev-mint certified through real consensus; prune cycle observed erasing)

### Verification

Every phase VERIFICATION.md passed (16: 17/17 must-haves; 17: 17/17 with the gap fixed live; 18: closed on live staging validation; 19: 5/5 + live spot-check; 20: 10/10 machine-checked). All five phases threat-secure (threats_open: 0 across 16-20 SECURITY.md).

### Known Gaps

None in v1.2 scope. The 20 acknowledged open artifacts are bookkeeping or out-of-milestone (see STATE.md ## Deferred Items): 4 resolved-with-stale-label UATs, 11 stale June-2026 quick-task records, 4 todos (2× bridge Track-A P1s, the proven-pre-existing NodeConsensusTest flake with documented fix options, 1 superseded), 1 closed debug session (NOT OURS, ticket filed).

### Key accomplishments

- Two-level physical erasure — CRDT entries + raw CID blocks + the tx→CID map in one compose-locked atomic WriteBatch; seal markers never erased (the traversal terminator)
- Spend-recency eligibility (a creation-age window was identified as unsound and redesigned pre-implementation) with the exact-set evaluator, the multi-element block gate, and per-tx `/prune/` markers
- The TxPruner — idempotent crash-consistent cycles (8-kill-point crash matrix), zero-fetch re-delivery, and the net-scoped dev-mint policy (burn-less mints through FULL consensus on staging/dev; validators refuse everywhere else, closing the ctest allowance bleed)
- Pruned-node survival — real-StartCore restart matrix, no-resurrection checkpoint-rebuild guard (consumed-preserving clear + own-replay + tamper-firing crosscheck), "history pruned" tri-state + `tx` CLI, 4-node pruned-mesh bootstrap
- The reclamation proof — scheduled whole-DB kForce compaction (atomic-cancellable, ~16MB threshold) with the honest measured `bytes_reclaimed`; the dedicated CI gate proves 32.1MB → 391KB on a 200-tx fixture

### Incident record (found live, all fixed in-milestone)

Boot wedge ×3 (dead recovery loop → GATE-05), TM-SYNCING send wedge, MintFunds burn-UTXO leak, GENIUS_CHAIN_ID witness bypass (resolved-by-design via the net-scoped policy), Windows WinSock + OSX guard-race CI defects, use-after-move in the leak fix itself.


## v1.2 Light-node transaction pruning (Shipped: 2026-10-26)

**Phases completed:** 5 phases, 29 plans, 76 tasks

**Key accomplishments:**

- CrdtSet batch-staging erase primitive that removes all four CRDT key families inside a caller-owned RocksDB batch (no hooks, no self-commit), plus the two public CrdtDatastore exposures (IsRootCIDPendingOrActive guard, StageEraseElement forwarder) the 16-03 GlobalDB compose needs
- GraphsyncDAGSyncer::StageBlockRemoval (guarded CID::toBytes() key staging into a caller-owned batch, seal unaddressable, removeNode dormant) plus TransactionManager::RemoveProcessedTransactionEntry (revert-free tx_processed_m erase), proven by the D-C6 real-store fixture EraseBlockStagingTest on a fresh temp RocksDB shared by both facades
- GlobalDB::EraseEntry — the one guarded one-batch atomic two-level erase behind the facade (four fail-closed guards, three owner stagings, single sync commit, post-commit seal re-assert, test-only interrupt seam) — plus TransactionManager::EraseTransactionEntry completing the tx→CID map's deletion lifecycle (resolve CID, atomic erase, revert-free tracked-map cleanup) with zero production callers
- Three real-store suites (substrate atomicity, GlobalDB end-to-end + interruption crash injection, guard inputs) plus a ctest source-scan gate that turns the phase's seal/removeNode/tombstone "never" rules into CI-enforced facts — SC-1..SC-5 all green, full 151-test suite green (one unrelated foundry flake re-verified in isolation)
- spent_by_txid now records the consuming transaction's hash at every production spend (4 call sites), survives bucket rewrite + restart, and is readable per outpoint via the new UTXOManager::GetOutPointSpender accessor — the only spend-time signal the Phase-17 eligibility window can read
- A caller above the CRDT facades can now read a key's per-key CRDT priority (GlobalDB::GetKeyPriority, with the missing-key-reads-as-0 caveat documented at both levels) and enumerate a delta block's carried keys — elements AND tombstones — keys-only via GlobalDB::GetLocalDeltaKeys, without touching the network or the elements-only precedent
- The cert-slot previous_hash rung now resolves ANY certified predecessor, so physically erasing the node's own frontier transaction (driven end-to-end through Phase-16 EraseTransactionEntry on a real store) no longer breaks the cold-start nonce chain: the never-pruned /cert/<addr>:<nonce-1> record answers instead
- PruneEligibility evaluates the /bc-\<net\>/tx/ namespace read-only — every output genuinely consumed (D-C2) AND the MOST-RECENT spend beyond the finality window (D-02: maxHeight − max spender priority ≥ N, spender existence established BEFORE any priority read) — returning per-candidate reason codes, with Light-only construction, a single allowlist home, and the GATE-06 PruningConfig contract
- A delta block is now judged erasable only when every element AND tombstone key it carries (parsed from the block itself via GetLocalDeltaKeys) is allowlist-conformant and inside the eligible set, reported per distinct CID in block_reports; the /prune/tx/&lt;hash&gt; marker record round-trips through the node-local raw surface with a loud PRUNED/NOT_FOUND/invariant-violation tri-state; and a spender whose history was pruned counts as window-satisfied with an explicit evidence code — zero production writes anywhere
- The sgns_config.json `pruning` object now gates a read-only repeating timer in GeniusNode — default off with absent keys byte-identical to today (the whole read, WARN and INFO included, sits inside one HasMember guard), enabled-without-window fail-closed with the 17-04 WARN, Light-only at the wiring AND at evaluator construction, and a per-tick evaluator built from live members emitting the D-03 candidates+blocks INFO summary — no new service, no write of any kind
- Pruned roots are terminal at the single broadcast intake: one isResolved insertion after the HasBlock miss makes re-delivered pruned roots skip with zero fetch attempts (both delivery modes, proven on real pubsub with a fetch-attempt counter), while unpruned behavior stays byte-identical.
- Burn-less mints now certify through FULL consensus only on the designated nets (staging 333 + dev 144) — a compile-time constexpr policy gates the dev validator's verdict and both GatherVerificationEvidence bypass legs, closing the unconditional empty-chain-id hole on every other net and the witness-bypass-hardening todo by design.
- The Phase-16 erase substrate hardened into its final Phase-18 shape: the erase compose now runs inside one owner-vended CrdtSet critical section (WR-02 closed by construction), the /prune/tx marker joins the erase WriteBatch (value XOR marker in one commit), budget/settle config knobs exist fail-closed, an absent delta block is erase-COMPLETING evidence for an otherwise-eligible candidate (never a stranding), and TransactionManager exposes the tick-gate missing-set input plus the production pruned-erase driver with one resolution home.
- The first production eraser exists as a service: TxPruner runs the ordered cycle (tick gate with restart-on-reopen settle clock → PruneEligibility::Evaluate reused verbatim → block-grouped K budget taken whole or deferred whole → fresh per-outpoint spent-ness re-verify → atomic marker-joined erase per tx → status + one RECL-03 INFO line), wired into GeniusNode behind default-off config with the dry-run arm byte-identical, per-tick snapshot deps + generation check before any erase, and a queryable watermark/last-cycle status that never fabricates bytes.
- The consensus-reject -> FAILED path now removes the MintFunds-inserted burn UTXO_BRIDGE entry (memory + disk) and its reservation via the new UTXOManager::RemoveBurnUTXOs, after reconciliation proved the existing FAILED rollback already released the reservation — the leak was rollback SEMANTICS (entry never removed), not missing wiring; pinned by a reproduction test on the real reject_and_maybe_fail_local trigger plus idempotency and normal-tx negative controls.
- The TxPruner service is proven on real stores: one cycle erases exactly the spent-beyond-window set (three-part per-tx assertions; /utxo/, cert/seed records, DAG heads, and every seal byte-identical; the unspent sibling untouched), post-drain cycles are no-ops, resurrected add-wins entries are re-erased by the next cycle (the sweep), the K-budget defers shared-block groups whole and completes stranded siblings through BLOCK_ALREADY_ERASED evidence, the tick gate refuses per conjunct (non-Light at construction, non-READY, settle-not-elapsed), the RECL-03/RECL-04 fields match fixture actions with a monotonic watermark that interrupted cycles never advance and concurrent readers never deadlock — and the D-01 compose lock holds: a concurrent PutElems on the same key provably blocks for the entire held erase window and the final state is always a legal serialization, never torn.
- SC-2 is closed at every interrupt point of both seams — 8 kill cases (every cycle-level position and every compose step, killed mid-progress after a committed erase), each torn down and REOPENED on the same database path, converge to exactly the uninterrupted run's end state with the clean-node re-run a no-op — and the destructive surface is now enumeration-pinned (exact caller-file sets, zero PutRaw in the erase path, dry-run body token-clean, the real cycle gated by dry_run==false before its erase path, and all three tick-gate conjuncts source-pinned before any erase), with the full default suite 154/154 green (SC-4's zero-change arm).
- Owner-vended `ClearUnspentKeepConsumed` in UTXOManager keeps CONSUMED records (with `spent_by_txid`) alive in memory and on the raw `/utxo/` prefix while dropping READY/RESERVED entries and reservations — the D-01 substrate that makes `PutUTXO`'s duplicate check a by-construction resurrection blocker.
- Pre-broadcast send failures now return a typed `Error::PRE_BROADCAST_VALIDATION` and leave the TransactionManager READY (rollback + nonce release, no SYNCING), and a SYNCING state with no confirming nonce advance exits within a bounded 60s window to the last durable confirmed nonce — reservations untouched — killing the live-incident wedge class on both halves.
- RetryDueFailedRoots now treats a pruned/sealed root as terminal — the isResolved guard fires before the fetch-attempt counter site, clears the ladder entry, and burns zero attempts — while the three already-safe consumers (stalled deltas, cert-embedded fallback, AccountMessenger serving) gained pinning tests: zero-fetch local-only retry, Approve-from-certificate after a pruned subject, and an empty signed BlockResponse over real pubsub.
- The checkpoint-mismatch clear now preserves CONSUMED spend evidence (ClearUnspentKeepConsumed swap), the rebuild recognizes the spender's OWN replay over preserved evidence (GetOutPointSpender == own hash per consumed input) so legitimate change outputs survive, every rebuilt-READY outpoint is belt-checked against its durable /utxo/ record (the belt's WARN+revert branch proven firing via the mandatory tampered-record induction), and locally-pruned creating txs skip recovery with zero requests — GATE-05's rebuild half closes by construction, never by refusing to boot.
- TransactionManager now answers a local tx lookup tri-state — FOUND from either found-source (tracked map, then exact-hash CRDT probe), PRUNED with the durable PrunedTransactionRecord naming the erased path, NOT_FOUND for unknown hashes — probed value-first-then-marker so a present value always outranks a marker, with the value-plus-marker invariant violation carried loudly on the result while the found tx is still returned; surfaced through a one-line GeniusNode passthrough and a `tx <hash>` CLI command whose "history pruned" answer is distinct from not-found, proven by a five-case suite on a real CRDT stack.
- A pruned node (frontier nonces 1-2 erased with markers, the certified nonce-3 frontier alive) restarts on the same database path and boots through the REAL TransactionManager::StartCore — never ForceManagerReady — reaching READY in ~2.5s while making ZERO pruned-CID fetch attempts across the entire restart window (counter bracket + per-instance ladder count 0), with the missing set empty for the locally-pruned creating refs, spend evidence/live balance/seals/heads intact, and the full matrix proven twice consecutively on fresh paths: new-tip delivery processed, an incoming spend of a still-unspent output applied, and the next outgoing tx built with nonce 4 (durable chain) and previous_hash equal to the GATE-04 ladder's answer — the never-pruned cert-slot hash at <addr>:3, never a pruned tx's hash.
- A pruned server's clean refusal is proven CID-accounted and failover-ready through a hermetic ordered-route getNode walk (pruned light first, full node second -- the light is never blacklisted and still serves its fresh data), and a fresh light bootstraps a two-pruned-light mesh through the full node's serving-topic broadcast with exactly zero ladder rides and a zero-fetch mesh join.
- Owner-vended GlobalDB compaction + whole-DB size surface (the one recipe home) plus the pruning.compact_threshold_mb knob with WR-03 zero-rejection and the resolved-values INFO extension.
- D-01 post-cycle compaction gate with the honest-split reporting: threshold-crossed cycles compact through GlobalDB::CompactStorage on the pruner's own tail, PruneStatus.bytes_reclaimed stamps the measured delta at compaction only, and the shutdown path cancels promptly — proven end-to-end by a 1MB-threshold canary.
- RECL-02's dedicated D-03 gate: a fresh full-suite target seeds 200 known ~10KB spent transactions, prunes them through the real TxPruner with the auto-trigger provably silent, runs exactly ONE compaction through GlobalDB::CompactStorage, and fails CI unless the measured byte drop is at least 60% of the erased logical bytes — with the anti-vacuous before-pin, a measured-calibrated sanity ceiling, and a du belt.
- The proof layer on the landed service: every D-01 compaction conjunct and the D-02 bytes_reclaimed honesty contract machine-checked conjunct-by-conjunct, GATE-02's non-tx byte-identity proven ACROSS a measurably-real whole-DB compaction (D-C5), and SC-5's window-edge spend race fault-tested deterministically with both legs asserted — 40/40 tests green twice consecutively.
- Two GlobalDB-only peers (nominally full and archive) in the light node's real-pubsub mesh provably hold 100% of the transaction history — byte-identical across BOTH of the light's erase cycles AND both of its whole-DB compaction cycles — with non-vacuous pre-prune baselines and an 8x stability sweep; SC-4 and the phase's last success criterion close.

---

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
