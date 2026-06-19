---
phase: 06
slug: network-voting-weight-classes-tier-2
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-06-19
---

# Phase 06 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---
## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (gtest) |
| **Config file** | CMakeLists.txt with `add_subdirectory()` + `addtest()` macro |
| **Quick run command** | `cd build/OSX/Debug && ninja consensus_two_tier_test && ctest -R "ConsensusTwoTier|RolePromotion" --output-on-failure` |
| **Full suite command** | `cd build/OSX/Debug && ninja && ctest -j8 --output-on-failure` |
| **Estimated runtime** | ~15 seconds (quick), ~120 seconds (full suite) |

---
## Sampling Rate

- **After every task commit:** Run quick run command
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green (CLAUDE.md shared-library rule applies)
- **Max feedback latency:** 30 seconds

---
## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Command | Status |
|---------|------|------|-------------|------------|-----------------|-----------|---------|--------|
| 06-01-01 | 01 | 1 | REQ-COHORT-01, REQ-DETERM-01 | T-06-01 / V4 | CohortOf returns DIRECT_API for paid/api-key validators; deterministic across peers | unit | `ctest -R CohortOfDirectApi` | ⬜ pending |
| 06-01-02 | 01 | 1 | REQ-COHORT-02 | T-06-02 / V4 | CohortOf returns PUBLIC_ONLY for public-only validators | unit | `ctest -R CohortOfPublicOnly` | ⬜ pending |
| 06-02-01 | 02 | 2 | REQ-QUORUM-01..03 | T-06-03 / V4,V5 | Bridge-mint certificates produced only when both cohorts independently meet thresholds | unit | `ctest -R TwoTierQuorum` | ⬜ pending |
| 06-02-02 | 02 | 2 | REQ-QUORUM-04 | T-06-04 / V4 | Non-bridge subjects use single-pool quorum unchanged | unit | `ctest -R TwoTierNonBridge` | ⬜ pending |
| 06-02-03 | 02 | 2 | REQ-QUORUM-05 | T-06-05 / V5 | TallyVotes and HandleVote agree on EvaluateQuorum output | unit | `ctest -R TwoTierIncremental` | ⬜ pending |
| 06-03-01 | 03 | 3 | REQ-REPUT-01 | T-06-08 / V4 | Role::FULL promotion via ApplyVoteEffects when weight ≥ threshold and penalty low | unit | `ctest -R RolePromotionToFull` | ⬜ pending |
| 06-04-01 | 04 | 4 | REQ-QUORUM-01..05 | T-06-09 / all | Bridge-mint two-tier quorum full-pipeline integration test (both tally sites) | integration | `ctest -R TwoTierPipeline` | ⬜ pending |
| 06-04-02 | 04 | 4 | REQ-REPUT-01, REQ-DETERM-01 | T-06-10 / V4 | Role::FULL promotion full-pipeline test + cohort determinism across registry snapshots | integration | `ctest -R PromotionFullPipeline` | ⬜ pending |
| 06-04-03 | 04 | 4 | all | — | Full regression gate: all consensus + account tests green | regression | `ninja && ctest -j8 --output-on-failure` | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---
## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_two_tier_test.cpp` — NEW test file (covers REQ-COHORT-01/02, REQ-QUORUM-01..05, REQ-DETERM-01)
- [ ] `test/src/blockchain/validator_registry_promotion_test.cpp` — NEW test file (covers REQ-REPUT-01)
- [ ] `test/src/blockchain/CMakeLists.txt` — add both test executables via `add_subdirectory()` / `addtest()`
- [ ] Extend `ConsensusManagerTestAccess` if `EvaluateQuorum` helper or cohort tally needs friendship for test access
- [ ] No framework install needed — gtest already in thirdparty/
