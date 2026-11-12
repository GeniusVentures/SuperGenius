---
phase: 20-reclamation-proof-e2e-hardening
audited: 2026-10-26
auditor: gsd-security-auditor
asvs_level: 1
block_on: high
branch: gsd/phase-20-reclamation-proof-e2e-hardening
threats_found: 25
threats_closed: 25
threats_open: 0
threats_accepted: 6
status: secured
---

# Phase 20 Security Audit — Reclamation Proof & E2E Hardening

Audit of the implemented phase-20 surface (commits `40d423594^..HEAD`: the
20-01..20-05 series plus the CR-01/WR-01/WR-02 review-fix commit `e31fbe6de`;
the three trailing docs commits are non-code). Every mitigation was verified
against the source on disk — plan text, SUMMARY "Threat Flags" claims, and the
review report's fix list were not accepted as evidence. All three review fixes
(CR-01 live-path cancel, WR-01 last_cycle stamp, WR-02 threshold wrap guard)
were re-located in the live tree; each is present in production code.

**Surface audited:** the owner-vended GlobalDB compaction + whole-DB
measurement surface (`CompactStorage`, `GetApproximateDiskUsageBytes`); the
`pruning.compact_threshold_mb` knob with degenerate-zero and wrap rejection;
the TxPruner post-cycle compaction gate (`MaybeCompactAfterCycle`) with the
honest-split `bytes_reclaimed` reporting and the member-scope cancellation
atomic; the GeniusNode live-path shutdown cancel; and the four test carriers
(reclamation gate target, conjunct matrix, post-compaction identity suite,
seam-held spend race, mesh retention).

**Method:** one-home greps re-run (`CompactRange(` in src/ → exactly
`globaldb.cpp:938`; in test/ → zero hits; `kForce` in globaldb.cpp → 1;
`INCLUDE_MEMTABLES` in src/ → 1), each mitigation pattern located at its cited
site, accept dispositions checked for both the risk documentation and the zero
registry-install premise (`git diff --stat 40d423594^..HEAD` over package
manifests → empty).

## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| sgns_config.json → PruningConfig::FromJSON | operator config crosses into compaction scheduling policy (the write-amp knob) |
| GlobalDB surface callers (TxPruner, tests) | internal callers must not re-state the compaction recipe or measurement sentinels |
| shutdown path → in-flight CompactRange | stop sequence racing an engine-internal whole-DB rewrite |
| CI full suite ← reclamation gate target | the milestone's headline claim is enforced here — a vacuous or blind gate repudiates it |
| concurrent spend mutation ↔ active prune cycle | the window-edge race SC-5 fault-tests |
| light node's local erase/compaction ↔ mesh peers | local RocksDB operations must never propagate as erasure to peers |

## Threat Register Verification

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-20-0101 | DoS (V5) | PruningConfig::compact_threshold_mb | mitigate | CLOSED | `src/transaction/PruneEligibility.cpp:114-128` — degenerate-0 rejected (`GetUint64() > 0` :115), ill-typed rejected (`IsUint64` guard :114), absent keeps default (`hpp:282` `= 16`); WR-02 wrap guard rejects the ≥2^44 wrapping range (:118-127) so the derived threshold can never be 0. Tested: `test/src/account/prune_eligibility_test.cpp:775-807` (C11 absent→16, C12 explicit 8→8, C13 zero→16, C14 ill-typed→16). Resolved value INFO-logged: `src/account/GeniusNode.cpp:486-492` |
| T-20-0102 | Tampering (V8) | GlobalDB::CompactStorage recipe | mitigate | CLOSED | `src/crdt/globaldb/globaldb.cpp:928-938` — `kForce` (:929), `allow_write_stall=false` (:930), whole-DB `nullptr,nullptr` bounds (:938), all in the ONE method. One-home machine-checked: `grep -rn "CompactRange(" src/` → only globaldb.cpp:938; `grep -c kForce globaldb.cpp` → 1; test/ → zero raw CompactRange. Value-byte identity across the rewrite pinned by `CompactStorageSmokeTest.CompactsWholeDbAndPreservesValueBytes` (`test/src/crdt/globaldb_erase_test.cpp:1287-1324`: CRDT value + raw block hex + seal + head asserted equal after two compactions) |
| T-20-0103 | DoS (V7) | surface post-ShutdownNow | mitigate | CLOSED | `globaldb.cpp:913-918` (CompactStorage) and `:955-960` (GetApproximateDiskUsageBytes) — `ActiveDataStore()` is the first act of both; nullptr → `Error::GLOBALDB_NOT_STARTED`, no touched moved handle |
| T-20-0104 | Tampering | sentinel/flags knowledge drift | mitigate | CLOSED | `globaldb.cpp:968-977` — 16×0xFF sentinel as a named local outliving the borrowed Slice (:968-970), `INCLUDE_FILES \| INCLUDE_MEMTABLES` (:976-977); `grep -rn INCLUDE_MEMTABLES src/` → exactly one hit (globaldb.cpp:977) |
| T-20-01SC | Tampering | npm/pip/cargo installs | accept | CLOSED | Zero registry installs: no package manifest touched in `40d423594^..HEAD` (git diff --stat over package.json/Cargo.toml/requirements/pyproject → empty); internal C++ only. Accepted-risk entry below |
| T-20-0201 | DoS (V4/availability) | post-cycle compaction vs sync writes | mitigate | CLOSED | Hook point: `src/transaction/TxPruner.cpp:252-254` — `MaybeCompactAfterCycle` between `RecordCompletedCycle` and `return stats`, on the prune-timer path (`GeniusNode.cpp:5149` RunCycle; reschedule `:5185` only after it returns). `allow_write_stall=false` inside the surface (`globaldb.cpp:930`). Conjuncts in order: erased≥1 (`TxPruner.cpp:318`), !canceled (:322), watermark ≥ `compact_threshold_mb << 20` (:328-329, the single derivation site) |
| T-20-0202 | Repudiation (V7) | fabricated bytes_reclaimed | mitigate | CLOSED | `TxPruner.cpp:394-403` — stamped ONLY on compaction success from the real `GetApproximateSizes` delta under `status_mutex_`; the unmeasured-success path stamps nothing and says so (:376-388); erase-time INFO counts-only (:296-310, "Phase 20" clause retired — grep zero). Canary asserts has_value>0 only after a real compaction: `tx_pruner_test.cpp:2293-2353` |
| T-20-0203 | Tampering/DoS | shutdown race on in-flight CompactRange | mitigate | CLOSED | Member atomic outlives the call: `TxPruner.hpp:414` (`std::atomic<bool> compaction_canceled_{false}`), passed as `&compaction_canceled_` at `TxPruner.cpp:354`. **CR-01 live-path fix verified:** `GeniusNode.cpp:2571-2574` — `RequestCompactionCancel()` inside `ShutdownForDestruction` (def :2532), beside the prune-timer cancel (:2560-2564) and BEFORE `io_->stop()` (:2627-2629), so an in-flight kForce compaction aborts promptly. `RequestCompactionCancel` sets the member, never resets (`TxPruner.cpp:496-502`) |
| T-20-0204 | Tampering (accuracy) | erased-bytes estimate drift | mitigate→accept | CLOSED | Accepted risk documented ON the fields as required: `PruneEligibility.hpp:156-163` (`value_bytes` — "±20% drift is immaterial… NEVER a measured reclamation number") and `:197-209` (block-share estimate). Populated from reads evaluation already performs (`PruneEligibility.cpp:475` value Buffer size; `:447` carried-key bytes; `:397` share divisor) — no second fetch path. Accepted-risk entry below |
| T-20-02SC | Tampering | npm/pip/cargo installs | accept | CLOSED | Zero registry installs (internal C++ only; manifest diff empty). Accepted-risk entry below |
| T-20-0301 | Repudiation | memtable/empty-range-blind measurement | mitigate | CLOSED | Flags+sentinel encoded once in the production surface (evidence at T-20-0104). Gate pins: erased==200 asserted (`reclamation_gate_test.cpp:525`, `kGateTxCount=200` :480), every marker PRUNED pre-measurement (:530-537), anti-vacuous before-pin `ASSERT_GE(before, erased_logical_bytes*8/10)` (:552) |
| T-20-0302 | DoS (CI availability) | gate flake on variance | mitigate | CLOSED | Bounded range: floor `EXPECT_GE(reclaimed, erased_logical_bytes*60/100)` (:575, `kFloorPct=60` :481) + measured-calibrated ceiling (:591, 20x+2MB with in-code rationale); low-entropy repeated-byte payloads (:99-100, :211-212); `addtest` TIMEOUT 600 (`cmake/functions.cmake:21`, registration `test/src/account/CMakeLists.txt:474`); dedicated target outside the quick matrix |
| T-20-0303 | Repudiation | second (auto-trigger) compaction corrupting the delta | mitigate | CLOSED | Default 16MB threshold in the gate's local MakePruner + the asserted nullopt BEFORE the direct call: `EXPECT_FALSE(pruner->GetPruneStatus().bytes_reclaimed.has_value())` (`reclamation_gate_test.cpp:543`; smoke leg :437) — the ONE compaction is the direct `CompactStorage(nullptr)` |
| T-20-03SC | Tampering | npm/pip/cargo installs | accept | CLOSED | Zero registry installs (test-only plan; manifest diff empty). Accepted-risk entry below |
| T-20-0401 | Repudiation | D-C5 vacuity (snapshots never straddle a compaction) | mitigate | CLOSED | `tx_pruner_test.cpp:1635` `RealPrune_PostCompactionEverythingElseStaysByteIdentical` — the production `db_->CompactStorage(nullptr)` sits INSIDE the test at :1709 between the before-snapshots (~:1678-1690) and the after-assertions (~:1725-1745); mutation check `EXPECT_LT(post_compaction, pre_compaction)` :1715 proves the rewrite was real; all six snapshot families byte-identical after it |
| T-20-0402 | Tampering (race passing by luck) | seam-held interleaving | mitigate | CLOSED | Deterministic blocking hook at `kInterruptAfterEvaluation` with the 5s deadline that fails rather than hangs (suite :3087; deadline spin :3131-3132 abs ≈ :3131); zero ordering sleeps in the interleaving path; leg (a) asserted stats+state (erased==0, skipped==1, value present, marker NOT_FOUND — mid-hold AND post-release); leg (b) next-cycle pickup (erased==1, PRUNED, value gone); the checker-B3 two-step revert (`LoadUTXOs` reload) present in the hook. 8x sweep + not-run-with-reason debug leg recorded in 20-04-SUMMARY |
| T-20-0403 | DoS (test hang) | blocking hooks left armed | mitigate | CLOSED | Dtor-first seam-clear discipline (`tx_pruner_test.cpp:26-29` contract, :233 clear before teardown touches stores) + clear-on-every-exit pairs in each hook suite + the 5s deadline in every blocking hook (:2678, :2845, SpendRace hook) |
| T-20-0404 | Repudiation | trigger matrix gaps | mitigate | CLOSED | One TEST_F per D-01 conjunct: BelowThreshold :2356, ErasedZero :2391, CrossedThreshold+stamp-retention(both forms) :2428, CanceledBeforeStart+fresh-instance-retry :2496, restart honesty (FreshInstance) :2573, ConcurrentReadDuringCompactingCycleIsCoherent :2620 — all stats/status-driven, zero log capture |
| T-20-04SC | Tampering | npm/pip/cargo installs | accept | CLOSED | Zero registry installs (test-only plan; manifest diff empty). Accepted-risk entry below |
| T-20-0501 | Repudiation (vacuous retention) | peers that never held the history | mitigate | CLOSED | `tx_pruner_test.cpp:3400` MeshRetention — bounded per-peer polls (`ASSERT_WAIT_FOR_CONDITION` :3478 + `PeerHoldsSeededHistory` over both peers) prove 100% presence BEFORE the prune; baselines captured through each peer's own public surface with non-empty pins (:3491-3508 region); light-side mutation checks (erased keys absent, markers PRUNED, blocks gone) at CHECKPOINT 1 |
| T-20-0502 | DoS (flaky mesh green) | gossip delivery nondeterminism | mitigate | CLOSED | 19-07 recipe verbatim: one-direction dial (peer dials the light only — `live.pubsub->AddPeers({pubs_->GetInterfaceAddress()})` :377; the light never dials back), loopback-advertised listener (:333), bounded waits + per-instance observables (never delivery-timing EXPECTs), direct deterministic `CompactStorage` instead of timing the auto-trigger; 8x sweep in 20-05-SUMMARY |
| T-20-0503 | Tampering (V8) | compaction propagating as value change | mitigate | CLOSED | CHECKPOINT 2: ONE `db_->CompactStorage(nullptr)` on the light (:3567) with the size-drop mutation check (:3570), then both peers re-snapshotted and asserted byte-identical to the SAME pre-prune baselines (:3574-3580 region); round 2 repeats both checkpoints on union baselines (:3632, :3708-3711) |
| T-20-0504 | DoS (test interference) | second pubsub host lifecycle | mitigate | CLOSED | Peers are fixture-owned members (`mesh_peers_` :310, pushed before any failing wait :370) and torn down FIRST in the fixture destructor through `sgns::test::stopGlobalDBNode` (:243-247) ahead of the fixture's own teardown chain; full-binary double-run green in 20-05-SUMMARY |
| T-20-05SC | Tampering | npm/pip/cargo installs | accept | CLOSED | Zero registry installs (test-only plan; manifest diff empty). Accepted-risk entry below |

## Review Fixes Verified In Source

| Finding | Severity | Status | Evidence |
|---------|----------|--------|----------|
| CR-01 stop-path cancel dead code (`ReleaseRuntimeMembersAfterIoStopped` has no caller) | critical | FIXED (live path) | `src/account/GeniusNode.cpp:2571-2574` — `tx_pruner_->RequestCompactionCancel()` in `ShutdownForDestruction` (def :2532), beside the prune-timer cancel, BEFORE `io_->stop()` (:2627). Confirmed the dead function still has no caller (declaration `GeniusNode.hpp:1578` + definition only) — its cancel copy at :2466-2473 remains as pre-existing dead code, classified as a documented residual below |
| WR-01 `last_cycle` never carries compacted/duration | warning | FIXED (success path) | `src/transaction/TxPruner.cpp:394-403` — `stats.compacted = true` moved BEFORE the status block; `status_m.last_cycle = stats` re-copied under `status_mutex_` with the WR-01 rationale comment (:399-402). Residual scope note below |
| WR-02 `compact_threshold_mb << 20` wraps for ≥2^44 | warning | FIXED | `src/transaction/PruneEligibility.cpp:118-127` — `kMaxCompactThresholdMb = max >> 20`; the wrapping range is rejected at parse, member keeps the 16MB default. No test pin yet (residual note below) |

## Accepted Risks Log

| ID | Risk | Rationale | Documentation |
|----|------|-----------|---------------|
| AX-20-01 (T-20-01SC/02SC/03SC/04SC/05SC) | Registry-install supply-chain exposure | Zero npm/pip/cargo installs across all five plans (internal C++ + tests only); package-manifest diff over the phase span is empty | Per-plan threat_model blocks; Package Legitimacy Audit "not applicable" per 20-RESEARCH.md |
| AX-20-02 (T-20-0204) | Erased-bytes estimate drift (shared-block integer division, key-total block sizing) | Threshold-TRIGGER estimate at ~16MB scale — ±20% immaterial (research A5); the measured number is solely the compaction-time delta; never surfaced to operators as measured | Doxygen on `PruneEligibility.hpp:156-163` and `:197-209`; 20-02-SUMMARY Threat Flags |
| AX-20-03 (IN-01) | `operation_canceled` conflates pre-set cancel, mid-flight abort, and engine-side pause in the skip INFO wording | Behaviorally harmless (idempotent compaction, watermark kept); log-wording only | 20-REVIEW.md IN-01; wording at `TxPruner.cpp:360-361` unchanged by design |
| AX-20-04 (IN-02) | `EXPECT_GT(compaction_duration_ms, 0u)` can flake on sub-ms compactions | Two asserts in two suites; storage-speed sensitivity accepted | 20-REVIEW.md IN-02; asserts at `tx_pruner_test.cpp:2332`, `:2457` |
| AX-20-05 (IN-03) | `SumRegularFileBytes` swallows directory-iteration errors (du belt can pass vacuously) | The du belt is never the primary carrier (the >=60% floor is); accepted at info severity | 20-REVIEW.md IN-03; helper at `reclamation_gate_test.cpp:113-130` |
| AX-20-06 (IN-04) | The `CompactStorage` const-pointer contract is engine-dependent (vendored `DisableManualCompaction` may write the aliased atomic in other option combos) | The write path is unreachable with this recipe (requires `change_level`, never set); the pruner's member is non-const — no UB today | 20-REVIEW.md IN-04; contract wording `globaldb.hpp:289-292` |

## Unregistered Flags

None. All five SUMMARY `## Threat Flags` sections report "None", each mapping
its content back to existing register IDs (T-20-0201..0204, T-20-0301..0303,
T-20-0401..0404, T-20-0501..0504). No new attack surface appeared during
implementation outside the register.

## Audit Trail Notes (non-blocking residuals)

- **Dead `ReleaseRuntimeMembersAfterIoStopped` cancel copy:** the original
  20-02 stop-path deliverable at `GeniusNode.cpp:2466-2473` still sits in a
  function with no caller (pre-existing dead code per 20-REVIEW.md CR-01);
  CR-01's live fix landed in `ShutdownForDestruction` (:2571-2574, verified).
  The stale comment at `GeniusNode.cpp:5014-5018` still names the dead
  function as `tx_globaldb_`'s release point. Cleanup of the dead function is
  a hygiene follow-up, not a threat gap — the live trigger is wired and the
  member atomic outlives the call either way.
- **WR-01 residual scope:** the last_cycle re-copy landed on the success path
  only. On the unmeasured-compaction path (`TxPruner.cpp:376-388`:
  compaction ran, post-size read failed) `stats.compacted=true` is set on the
  RETURNED stats but `status_m.last_cycle` is not refreshed, and the review's
  parenthetical test pin (`GetPruneStatus().last_cycle.compacted`) was not
  added. The honesty-critical contract (bytes_reclaimed stamped only from a
  real measured delta) is unaffected — that path stamps nothing. T-20-0202
  stays CLOSED.
- **WR-02 test pin:** the wrap-guard rejection has no dedicated FromJSON case
  (C11-C14 predate it; the fix commit touched production files only). The
  guard is verified present in code; a C15-style case would pin it against
  regression.

## Verdict

**SECURED** — 25/25 register threats closed (19 mitigate verified in code,
T-20-0204 accept-documented on the fields, 5 supply-chain accepts with the
zero-install premise verified). CR-01/WR-01/WR-02 all present in production
code. No open threats; no unregistered flags; documented residuals are
info-level deferrals recorded above.
