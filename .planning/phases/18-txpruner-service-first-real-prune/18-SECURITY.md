---
phase: 18-txpruner-service-first-real-prune
audited: 2026-10-26
auditor: gsd-security-auditor
asvs_level: 1
block_on: high
branch: gsd/phase-20-reclamation-proof-e2e-hardening
threats_found: 36
threats_closed: 36
threats_open: 0
threats_accepted: 3
status: secured
---

# Phase 18 Security Audit — TxPruner Service & First Real Prune

Audit of the implemented phase-18 surface (commits `706a142ad^..d2fc6644b`, 35
commits, including the review-fix series `08421f54a` CR-01, `4bda1178a` WR-01,
`b0fad55b6` WR-02, `56a73ab05` WR-03, and the gap-fix/docs commit `d2fc6644b`;
span verified an ancestor of the audited branch HEAD `b619e799c`). Every
mitigation was verified against the source on disk — documentation, SUMMARY
claims, and review-report "Status: FIXED" lines were not accepted as evidence;
each fix was re-located in the live tree.

**Surface audited:** the D-02 re-delivery guard at the broadcast intake, the
D-03 net-scoped dev-mint policy (predicate, validator, both evidence-bypass
legs), the D-01 owner-vended compose critical section + marker batch-join +
BLOCK_ALREADY_ERASED gate evidence + PruningConfig keys, the TxPruner service
cycle and its GeniusNode wiring, the MintFunds burn-UTXO cleanup, and the
18-06/18-07 proof suites (service behavior, crash fixed point, sanctioned-
caller containment gates).

**Live-tree caveat:** phases 19/20 stack on top of 18 in this branch. Line
numbers below are from the current tree; where a later phase evolved a shape
(e.g., `PruneStatus.bytes_reclaimed` now measured at Phase-20 compaction
instead of being absent), the phase-18 mitigation property was re-verified in
its evolved form.

## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| peer broadcast → HandleCIDBroadcast | untrusted network-delivered CIDs (lagging or malicious peers) |
| proposer → validator consensus path | untrusted proposal content (chain id, source ref, witness) — all proposer-controlled |
| node config → process net id / PruningConfig | operator-controlled net id (validated against the four known ids) and pruning JSON (fail-closed parse) |
| config → prune timer arm | operator consent for a destructive local service (default off) |
| evaluator snapshot → erase compose | committed CRDT state crossing into a destructive local operation (the WR-02 window) |
| consensus verdict → local UTXO state | failed consensus outcome triggers local bridge-mint cleanup |
| crash simulation → store durability | interrupt seams emulate power loss inside the destructive path |
| source-scan gates → production tree | gates read src/ at test runtime and pin the sanctioned caller set |

## Threat Register Verification

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-18-0101 | DoS | re-broadcast storm of pruned CIDs reaching the DAG worker | mitigate | CLOSED | `src/crdt/impl/crdt_datastore.cpp:727-741` — `isResolved` guard as ONE insertion after the HasBlock-miss continue (:717) and before `IsCIDInCache` (:743); error branch logs-and-continues (:728-734, the HasBlock error shape); counter `root_fetch_attempts_for_test_` increments only on the EnqueueRootCID return-true branch (:2304-2313). SC-3: `test/src/crdt/globaldb_erase_test.cpp:631` (re-broadcast) and `:665` (restart head exchange) assert zero fetch attempts, both delivery modes |
| T-18-0102 | Tampering | re-delivery resurrecting pruned elements | mitigate | CLOSED | Guard skips before any cache/pending/enqueue consult (`crdt_datastore.cpp:720-741`); zero re-created keys asserted in SC-3 cases 1/2; sweep backstop (D-02b) landed: `test/src/account/tx_pruner_test.cpp:1855` `SweepReprunes_ResurrectedEntryIsReErasedNextCycle` |
| T-18-0103 | DoS | guard suppressing legitimate fetches | mitigate | CLOSED | Insertion order preserved — HasBlock fast path untouched (:707-718), guard between it and IsCIDInCache (:727→:743); negative control `RedeliveryOfUnprunedRootStillFetches` (`globaldb_erase_test.cpp:725-753`) proves the counter advances |
| T-18-0104 | Tampering | attacker erasing seals to force re-fetch | accept | CLOSED (accepted) | Documented in 18-01-PLAN threat_model: seal markers unreachable through the erase surface by construction (GATE-03) — the compose stages value/elem/block/map keys only and RE-ASSERTS the seal post-commit (`globaldb.cpp:894+`, `POST_COMMIT_ERASE_ANOMALY` on loss). No new surface this phase. Logged under Accepted Risks |
| T-18-0105 | Information disclosure | trace log naming pruned CIDs | accept | CLOSED (accepted) | Documented in 18-01-PLAN threat_model: trace-level only (`crdt_datastore.cpp:737`), CID strings are not secrets. Logged under Accepted Risks |
| T-18-0201 | Elevation | empty-chain-id mint certifying on main (the live global bypass) | mitigate | CLOSED | Both `GatherVerificationEvidence` bypass legs net-scoped in `src/account/PublicChainInputValidator.cpp` `LocalBypassEvidence`: empty-source leg `:64-70`, chain-id/supergenius leg `:82-88` — refusal returns default-invalid `RpcVerificationEvidence{}` (`valid = false` default, `PublicChainInputValidator.hpp:68`), mirroring the no-endpoints reject shape, logging the refused net. Tests: `dev_mint_policy_test.cpp:184/:204` (both legs × 963/369 refuse) and `:796` `RefusesOnNonDesignatedNetEmptyChainIdMintNeverCertifies` (the actually-reachable hole) |
| T-18-0202 | Elevation | policy keyed on proposer claims | mitigate | CLOSED | `src/base/DevMintPolicy.hpp:37-42` — `DevMintBurnlessPermitted()` is parameterless, body reads only `version::GetNetworkID()` and `kDevMintPermittedNets`; validator ignores witness content entirely (`DevMintInputValidator.cpp:32-45`). Test `:225` `DevValidatorWitnessVerdictFollowsOwnNetRegardlessOfWitnessContent`; containment gate bans proposer-field tokens in the predicate body (`crdt_erase_test.cpp:952-956`) |
| T-18-0203 | Elevation | config widening the designated set | mitigate | CLOSED | `DevMintPolicy.hpp:31` — `inline constexpr std::array` built from `version::STAGING_NET_ID`/`version::DEV_NET_ID` (zero raw net literals, grep-verified); header documents that only a code change widens it (:9-11). Test `:149` `PolicyPermitsExactlyTheDesignatedNets` |
| T-18-0204 | Spoofing | spoofing the designated net from a non-designated node | accept | CLOSED (accepted) | Documented in 18-02-PLAN threat_model: claiming net 333 means joining net 333's net-suffixed topics/keyspace — cross-net quorums cannot form; spoofing = actually running on the designated net, which is the policy's intent. Logged under Accepted Risks |
| T-18-0205 | Tampering | "dev" registry collision hijacking routing | mitigate | CLOSED | Insert-only `IInputValidator::Register("dev", &instance)` (`DevMintInputValidator.cpp:13-18`); link anchor `kDevMintValidatorRegistered` beside the genius anchor (`TransactionManager.cpp:78`); chainlist no-collision check (A6) recorded in 18-02-SUMMARY; routing test `:261` `DevChainMintRoutesToDevValidatorThroughRegistry` |
| T-18-0206 | Repudiation | dev mints indistinguishable from burned mints | mitigate | CLOSED | Chain id "dev" and absent witness are on-chain fields of the mint; the certificate path is unchanged — the mint certifies through the existing proposal/quorum/certificate machinery (test `:723` `DesignatedNetCertifiesDevChainBurnlessMintThroughRealQuorum` proves genuine quorum); zero consensus-core changes in the phase diff (routing via the existing registry only) |
| T-18-0301 | Tampering | spend revert interleaving between evaluation and erase (WR-02 planning window) | mitigate | CLOSED | `src/crdt/globaldb/globaldb.cpp:816` — `MakeComposeLock()` acquired as EraseEntry's FIRST act; guards (:823-864), staging (:869-890), and the single commit (:892) all inside the hold; header contract rewritten (`globaldb.hpp:217-234`: "CLOSED by construction"); mutex owner vends a scoped guard only (`crdt_set.cpp:739-745`). Non-interleaving proven: `tx_pruner_test.cpp:2881` `ComposeLock_ConcurrentMergeSerializesNeverTorn` (positive blocked-merge observation + legal-serialization assertion) |
| T-18-0302 | Tampering | partial erase exposing value-without-marker or marker-without-value | mitigate | CLOSED | Marker joins the ONE WriteBatch: `globaldb.cpp:888-891` stages `extra_entry` into the same batch, single commit `:892`; builder `PruneEligibility::BuildPruneMarkerEntry` (`PruneEligibility.cpp:255`); WR-02 review fix hardened it — marker built INSIDE `EraseTransactionEntryWithMarker` from the SAME resolved path as the erase (`TransactionManager.cpp:4729` resolve → `:4741-4743` build+stage), closing the pre/post-resolution divergence window; atomicity + seam-interrupt proofs in `prune_eligibility_test.cpp` (F2/F3 + `PrunedEraseDriver_MarksTheResolvedCrossNetworkPath`, `PruneMarker_CrossNetworkRecordCarriesResolvedPath`) |
| T-18-0303 | Tampering | BLOCK_ALREADY_ERASED erasing a tx whose block vanished non-sanctioned | mitigate | CLOSED | `PruneEligibility.cpp` `EvaluateDeltaBlockErasable` absent-branch: byte-presence discrimination via `HasLocalBlock` → `GraphsyncDAGSyncer::HasRawBlockBytes` (`graphsync_dagsyncer.cpp:481`, no parse — corrupt ≠ absent); eligible-candidate tie required (`sharing_tx_count > 0`); soundness comment in code (bytes vanish only via the sanctioned staged removal; seal survives, GATE-03); corrupt-but-present and non-eligible-tie stay fail-closed DESERIALIZE_FAILED; evidence code excluded from `HasFailureReason` (:44-53) |
| T-18-0304 | DoS | degenerate config (K=0 / settle=0) | mitigate | CLOSED | `PruneEligibility.cpp:97-108` — `max_erases_per_cycle`/`settle_seconds` parsed with HasMember + IsUint64 + `> 0`; zeros rejected at read time keeping safe defaults (hpp `:267` 100 / `:273` 60); config cases C7-C10 in `prune_eligibility_test.cpp` |
| T-18-0305 | Elevation | raw mutex exposure inviting lock misuse | mitigate | CLOSED | `crdt_set.hpp` — `mutex_` under `private:` (:386 region, member :402); the only public addition is `MakeComposeLock()` (:319) returning an owning scoped guard; bounded per-tx hold documented in the header contract |
| T-18-0306 | Repudiation | marker written without the erase (or vice versa) | mitigate | CLOSED | Single-batch join is the API contract (`globaldb.hpp` Doxygen + `globaldb.cpp:884-892`); tri-state lookup reports the invariant violation loudly — `PruneEligibility.hpp:122` `VALUE_PRESENT_INVARIANT_VIOLATION`, `LookupPrunedEntry` (:462) probes the value at the RESOLVED path (post-WR-02-fix, no vacuous probe); F1 byte-identity keeps one key home (`BuildPruneMarkerEntry` delegates to `BuildPruneMarkerKey`, single production call site) |
| T-18-0401 | Destruction | cycle erasing on non-Light / not-yet-synced node | mitigate | CLOSED | Belt-and-suspenders: config consent first statement (`GeniusNode.cpp:4915`, SC-5) + Light at wiring (:4922) AND at service factory (`TxPruner.cpp:72-77`, `operation_not_permitted`); tick gate `TickGateSettled` (`TxPruner.cpp:415-435`) = `State::READY && !HasMissingTxHashes()` + settle window with restart-on-reopen (:420-426 — cannot fire during initial sync); TickGate suites `tx_pruner_test.cpp:2092-2198` prove each conjunct refuses |
| T-18-0402 | Tampering | spend revert between evaluation and erase | mitigate | CLOSED | `TxPruner.cpp:201` `OutputsRemainGenuinelyConsumed` immediately before `:211` `EraseTransactionEntryPruned`; fresh per-outpoint `IsOutPointGenuinelyConsumed` (:437-444, each call atomic under the UTXO lock); reverted ⇒ WARN + skip, never abort; `SpendRace_RevertedSpendSkipsThisCycleAndNextCyclePicksItUp` (`tx_pruner_test.cpp:3088`) |
| T-18-0403 | DoS | K-budget stranding same-block siblings | mitigate | CLOSED | Group-or-defer is structural: `TxPruner.cpp:186-196` — groups keyed by CID, `group.size() > remaining_k` → deferred WHOLE (`++held_blocks`), never partial; only ERASABLE block reports form groups; BLOCK_ALREADY_ERASED completes stranded siblings (18-03); `SharedBlockBudget_K1DefersWholeThenK2TakesWhole` (:1943) + `MissingBlockSiblingCompletesViaEvidence` (:2015) |
| T-18-0404 | Tampering | real arm erasing while dry_run=true | mitigate | CLOSED | `GeniusNode.cpp:5115-5119` — RunPruneCycle's own FIRST gate refuses `dry_run==true` (error + return); distinct cycle functions with mode captured at timer arm (:4960-4981); dry-run body pinned token-clean by gate `crdt_erase_test.cpp:1049` `DryRunCycleBodyStaysTokenClean`; real-cycle source-order pin `:1085` `RealCycleIsPinnedToRealModeBeforeAnyErase` |
| T-18-0405 | Elevation | stale generation erase (account switch mid-cycle) | mitigate | CLOSED | `GeniusNode.cpp:5128-5140` — per-tick `SnapshotAccountServices`, null-guard skip-and-reschedule, generation-superseded check BEFORE the only `RunCycle` invocation (the sole erase path); TxPruner holds no CRDT/service references across ticks (deps passed per cycle); snapshot shared_ptrs held for the whole cycle |
| T-18-0406 | Repudiation | status overclaiming | mitigate | CLOSED | `TxPruner.cpp:257-310` `RecordCompletedCycle` — watermark = maxHeight advanced only on cycles that ran to completion without hard error, monotonic (:283-285), failed head read keeps previous (:268-269); interrupted cycles leave status untouched (:240); bytes never fabricated — at phase-18 the field was absent by design; in the current tree (Phase 20) `bytes_reclaimed` is set ONLY from measured compaction deltas (:394-405) and explicitly not reported when the size cannot be re-read; `PruneStatus_InterruptedCycleLeavesWatermarkUntouched` (:2723) + `ConcurrentReadDuringCycleIsCoherent` (:2786) |
| T-18-0407 | DoS | cycle fired from CRDT callbacks (pre-commit state) | mitigate | CLOSED | Timer-driven only: `RunPruneCycle` is invoked exclusively from the steady_timer callbacks (`GeniusNode.cpp:4978` initial arm, `:5185` reschedule) — grep over GeniusNode.{cpp,hpp} shows no other call site; zero CRDT-callback wiring reaches the cycle |
| T-18-0501 | DoS | dangling burn UTXO wedging boot (live incident) | mitigate | CLOSED | `UTXOManager::RemoveBurnUTXOs` (`UTXOManager.cpp:724+`) removes entry + reservation + address index and persists, under one scoped lock; wired on BOTH indicted paths: FAILED mint-v2 leg (`TransactionManager.cpp:5539`, id = pre-move `dag_st.uncle_hash()`) and the MintFunds catch path (`:1122`) using `burn_reservation_id` captured BEFORE the try (:1092) — the CR-01 use-after-move fix, regression `BurnCleanup_MintFundsCatchPathRemovesBurnUtxoAfterPostMoveThrow` (`certificate_fallback_test.cpp:2060`); reproduction on the real reject trigger `:1913`; boot-tolerance layer stays Phase 19 (GATE-05) |
| T-18-0502 | Tampering | over-broad rollback reverting genuine normal-tx state | mitigate | CLOSED | Discrimination intact and grep-visible: `TransactionManager.cpp:5523` (`GetSrcAddress()==local && HasUTXOParameters()`) ∧ `:5529` (`GetType() == "mint-v2"`); non-mint branch keeps release-only `RollbackUTXOs` (:5551); CONSUMED entries never removed (`UTXOManager.cpp:738-739`); negative control `BurnCleanup_NormalTxFailedLeavesUtxoEntriesUntouched` (:1995) |
| T-18-0503 | Tampering | double-rollback on repeated FAILED transitions | mitigate | CLOSED | FAILED→FAILED dedup (`TransactionManager.cpp:5507-5513`); `RemoveBurnUTXOs` is a no-op on absent/mismatched entries (idempotent re-fire); `BurnCleanup_SecondFailedTransitionIsIdempotent` (:1956) covers both the handler guard and the state-machine dedup |
| T-18-0504 | Repudiation | a fix landing without an understood mechanism | mitigate | CLOSED | Reconciliation-first held: 18-05-SUMMARY "Reconciliation Findings" records deltas (a)/(b) REFUTED and (c) CONFIRMED with file:line citations; the RED reproduction (commit `6af285871`) landed before the fix (commit `81d98c130`); the fix edits exactly the indicted expressions |
| T-18-0601 | Tampering | vacuous green (asserting presence instead of absence) | mitigate | CLOSED | `tx_pruner_test.cpp:1501` `RealPrune_ErasesEligibleAndLeavesEverythingElseByteIdentical` — three-part per-tx assertions (value/map/block gone + marker PRUNED via `LookupPrunedEntry`, used at :1319-2082), byte-identity snapshots of `/utxo/`, cert/seed, heads, seals, unspent-sibling negative control; double-run discipline recorded in 18-06-SUMMARY verification evidence |
| T-18-0602 | Repudiation | status tests trusting the log over the state | mitigate | CLOSED | `Observability_StatsAndStatusMatchFixtureActions` (:2217) and `PruneStatus_*` (:2723, :2786) bind status to store state — marker PRUNED iff value absent; watermark equals the live maxHeight; stats field-by-field equal to fixture actions |
| T-18-0603 | Tampering | race test passing by scheduling luck | mitigate | CLOSED | `ComposeLock_ConcurrentMergeSerializesNeverTorn` (:2881) — the compose window is held deterministically via the production seams (GlobalDB seam step 1), the blocked-merge observation is positive and bounded, and the assertion is the never-torn invariant (legal serialization), not a specific interleaving; `ComposeLock_HeadGuardRefusesAtEraseTimeNeverTorn` (:3007) proves guard re-check under the lock |
| T-18-0604 | DoS | seams/hooks leaking between cases | mitigate | CLOSED | Fixture destructor clears BOTH seams FIRST (`tx_pruner_test.cpp:234-235`, crash fixture `:3912-3913`); fresh per-test DB paths (UniqueFixturePath); full-binary double-run green recorded |
| T-18-0701 | Tampering | partial-erase state surviving a crash | mitigate | CLOSED | `TxPrunerCrashFixedPointTest` (`tx_pruner_test.cpp:3734-4035`) — 8 kill points (5 cycle-seam positions + 3 compose steps killed mid-progress on the second erase), each: reopen on the SAME db path, fixed-point assertions in both completeness directions (never value-without-block/map, never marker-plus-value), convergence to the uninterrupted end state, clean-node no-op; fork-based true-kill absent-WITH-NOTE (documented in the suite doc comment + 18-07-SUMMARY, plan-sanctioned default) |
| T-18-0702 | Elevation | unsanctioned production erase/marker caller appearing later | mitigate | CLOSED | `crdt_erase_test.cpp:986` `SanctionedCallerSetsAreExact` — exact-list caller sets (`EraseTransactionEntryPruned` == exactly TxPruner pair; `BuildPruneMarkerEntry` == exactly TransactionManager pair; `EraseEntry` outside GlobalDB == the 3 sanctioned files; `WritePrunedEntryForTest` == zero production callers); `PutRaw` in the base banned tokens (:857); tracked bodies cover every Phase-18 mutation surface incl. the dev-mint predicate (:952-956, proposer-field tokens banned); mutation-verified non-vacuous in both directions (18-07-SUMMARY Task 2) |
| T-18-0703 | Tampering | the real arm erasing in dry-run mode | mitigate | CLOSED | Dual pins: `:1049` dry-run full-body token cleanliness + `:1085` real-cycle `dry_run==false` mode gate preceding its erase path in source order (via `extractFullFunctionBody`, :730) |
| T-18-0704 | Repudiation | phase verification claims without runs | mitigate | CLOSED | Every SUMMARY (18-01..18-07) "Verification Evidence" section records run counts and filters; the phase gate records full ctest 154/154 (18-07-SUMMARY Task 3); this audit independently re-verified the load-bearing mitigations in source rather than trusting those records |

## Accepted Risks

| Threat ID | Risk | Rationale | Bound |
|-----------|------|-----------|-------|
| T-18-0104 | Attacker erasing seal markers to force re-fetch of pruned roots | Seal keys are unreachable through the erase surface by construction (GATE-03, Phase 16): the compose stages value/elem/block/map keys only and re-asserts the seal post-commit, failing loudly (`POST_COMMIT_ERASE_ANOMALY` family) on loss | Phase-16 gate `SealKeyHasNoProductionHome` (still green in `crdt_erase_test.cpp:862`) — a production seal-key construction fails CI |
| T-18-0105 | Trace log names pruned CIDs | Trace-level logging only; CID strings are content hashes, not secrets | Guard log stays at `logger_->trace` (`crdt_datastore.cpp:737`) |
| T-18-0204 | A hostile operator "spoofing" the designated dev-mint net | Claiming net 333 requires joining net 333's topics/keyspace (net-suffixed); cross-net quorums cannot form — running on the designated net IS the policy's intent (designated nets are staging/dev only, never main) | Compile-time set cannot be widened by config (T-18-0203); predicate keys on the process's own validated net id only |

## Unregistered Flags

None. No SUMMARY in this phase carries a `## Threat Flags` section; each
summary's "Threat Mitigations Landed" table maps one-to-one onto registered
threat IDs from the plan-time register. No new attack surface appeared during
implementation without a threat mapping. (Notable in-scope surface ADDITIONS —
the fetch-attempt counter, the cycle interrupt seam, `SetMintFundsPostMoveExceptionHookForTest`,
the per-binary `TransactionManagerPendingLifecycleTestAccess` definition — are
all test-only observables outside `src/` production reach or static
nullptr-default seams, matching the registered test-seam threat shapes from
Phases 16/17.)

## Review-Cycle Verification (18-REVIEW.md / 18-REVIEW-FIX.md)

The review cycle's critical/warning findings are load-bearing for several
registered threats and were re-verified in source, not taken from the reports:

| Finding | Threat impact | Verified in code |
|---------|---------------|------------------|
| CR-01 use-after-move of `transaction_hash` (MintFunds catch rollback silent no-op) | Hardens T-18-0501's catch-path leg | `TransactionManager.cpp:1092` (`burn_reservation_id` captured before the try, with the failure-mode comment), `:1122-1132` (handler uses the copy); regression test at `certificate_fallback_test.cpp:2060` driving a post-move throw via the documented test seam |
| WR-01 silent repair-store swallow in `RemoveBurnUTXOs` rollback | Hardens T-18-0501/0503 (divergence visibility) | `UTXOManager.cpp:797` — each failed repair store logged at ERROR with owner + error; no-throw rollback contract unchanged |
| WR-02 marker named the local-network path for cross-network erases | Hardens T-18-0302/0306 (marker containment + non-vacuous invariant probe) | `PruneEligibility.cpp:255` `BuildPruneMarkerEntry(tx_hash, resolved_tx_path)`; construction moved inside `EraseTransactionEntryWithMarker` after `ResolveTransactionPathAndCID` (`TransactionManager.cpp:4729` → `:4741-4743`); sanctioned-caller gate comment updated to the 3-file compose set (`crdt_erase_test.cpp`, WR-02 note) |
| WR-03 wrong "first cycle in 1 min" operator log | Observability accuracy (RECL-03, adjacent to T-18-0406) | `GeniusNode.cpp:4988-4997` — real-arm log states the effective settle-window schedule (≈11 min at default cadence); dry-run arm untouched |

Info-tier findings IN-01..05 (doc contradictions, dead `generation_superseded`
field, facade `GetPruneStatus` with no production consumer yet, double INFO
lines, stale `reasons` doc) are documented residuals outside the fix scope
(`fix_scope: critical_warning`) — classified as informational deferrals, not
open threats. IN-04's consumer is scheduled as Phase-19 SURV-02 surface.

## Audit Trail

- Auditor: gsd-security-auditor (forced stance: every mitigation assumed absent until a source match proved otherwise)
- Branch/HEAD audited: `gsd/phase-20-reclamation-proof-e2e-hardening` @ `b619e799c` (clean tree); phase-18 span `706a142ad^..d2fc6644b` (35 commits) confirmed an ancestor of HEAD
- Method: plan threat_model blocks (7 plans, 36 threats) extracted; each `mitigate` disposition verified by locating the declared mechanism in the cited implementation file; each test-backed mitigation verified by locating the named suite/case; the 3 `accept` dispositions verified as documented in their plan threat_models and recorded above; 0 `transfer` dispositions
- Implementation files were read-only throughout this audit; the only file created is this SECURITY.md
- Result: 36/36 closed (33 mitigated with source evidence, 3 accepted and logged), 0 open — phase may ship
