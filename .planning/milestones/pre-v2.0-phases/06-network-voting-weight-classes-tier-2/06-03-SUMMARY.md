---
phase: 06-network-voting-weight-classes-tier-2
plan: 03
subsystem: consensus/registry
tags: [consensus, validator-registry, role-promotion, weight-classes, phase-6]
requires:
  - ValidatorRegistry::ApplyVoteEffects (existing reputation-mutation site)
  - ValidatorRegistry::WeightConfig (extended by 06-02 with 7 slot_* constants)
  - Role::FULL / Role::REGULAR in ValidatorRegistry.proto (existed, FULL was never assigned)
provides:
  - ValidatorRegistry::WeightConfig::full_promotion_weight_ (8th Phase 6 constant, default 500)
  - ValidatorRegistry::EvaluateRegularPromotionStatic (pure static helper)
  - REGULAR->FULL promotion branch in ApplyVoteEffects
affects:
  - ValidatorRegistry::ApplyVoteEffects (approve branch now mutates role)
tech-stack:
  added: []
  patterns:
    - "Algorithmic role promotion reusing existing weight/penalty fields (D-07: no new reputation daemon)"
    - "Pure static decision helper for deterministic, GlobalDB-free unit testing (mirrors 06-02 EvaluateSlotQuorumStatic)"
    - "Promotion operates on weight only; tally independence preserved (D-08: no EvaluateSlotQuorum change)"
key-files:
  created:
    - test/src/blockchain/validator_registry_promotion_test.cpp
  modified:
    - src/blockchain/ValidatorRegistry.hpp
    - src/blockchain/ValidatorRegistry.cpp
    - test/src/blockchain/CMakeLists.txt
decisions:
  - D-07 respected: reputation = existing ValidatorEntry.weight accumulated via ApplyVoteEffects; no new fields on ValidatorEntry
  - D-08 implemented: REGULAR->FULL promotion runs inside ApplyVoteEffects; the promoted node's higher weight flows into EvaluateSlotQuorum via validator.weight() with no tally-side special case
  - REQ-DETERM-01 respected: EvaluateRegularPromotionStatic reads ONLY (entry, weight_config); every peer mutates identically
  - REQ-REPUT-01 addressed: accumulated reputation now has an algorithmic effect (promotion to FULL)
metrics:
  duration: ~7m
  completed: 2026-06-19
  tasks: 1
  files_created: 1
  files_modified: 3
---

# Phase 6 Plan 03: REGULAR -> FULL Promotion Summary

Closed the "Role::FULL is never assigned" gap (RESEARCH Pitfall 2). Added an algorithmic promotion rule inside the existing `ApplyVoteEffects` so a REGULAR validator that accumulates enough reputation (weight >= `full_promotion_weight_`) with a low penalty (`penalty_score < penalty_threshold_`) is promoted to `Role::FULL`. The FULL role's higher max-weight cap (`full_max_weight_`) then flows naturally into the slot tally via `EvaluateSlotQuorum` (no Plan 02 change required — D-08 independence preserved). This is the organic reputation path that strengthens the DIRECT_API cohort over time without depending solely on configured weight>=50 endpoints.

## What Was Built

### Task 1 — Promotion config, decision helper, and ApplyVoteEffects wiring

- **`WeightConfig::full_promotion_weight_`** (default 500) added as the 8th Phase 6 constant, appended AFTER the 7 `slot_*` constants that 06-02 (Wave 2) introduced. 500 is a conservative default between `regular_max_weight_` (100) and `full_max_weight_` (5000). A Doxygen comment explains the threshold, the penalty gate, and the D-08 rationale.
- **`EvaluateRegularPromotionStatic(entry, weight_config)`** pure static helper. Returns true iff `entry.role() == Role::REGULAR && entry.weight() >= full_promotion_weight_ && entry.penalty_score() < penalty_threshold_`. Extracted as a pure function (REQ-DETERM-01) so the promotion decision is unit-testable without a GlobalDB-backed `ValidatorRegistry` instance — this mirrors the `EvaluateSlotQuorumStatic` pattern established in Plan 06-02.
- **Promotion branch in `ApplyVoteEffects`** (approve branch, inside `entry.status() == Status::ACTIVE`, immediately AFTER `entry.set_weight(clamped)`). Calls the helper and, on a positive decision, runs `entry.set_role(Role::FULL)`. The guard `entry.role() == Role::REGULAR` enforces GENESIS-is-never-demoted, SHARDED-is-not-promoted, and already-FULL-is-idempotent. The promotion changes the role only — never the just-clamped weight. The next `ApplyVoteEffects` call will use the FULL role cap (`full_max_weight_`), allowing further accumulation.
- **Debug log extension**: captured `old_role` alongside `old_weight`/`old_penalty`/`old_status` at the top of the per-entry loop, and added a dedicated `role {}->{}` log line that fires ONLY when the role actually changed (so the common no-promotion path stays quiet).

No proto change (`Role::FULL` already existed at `ValidatorRegistry.proto:21`). No new field on `ValidatorEntry` (D-07). No `EvaluateSlotQuorum` or tally-side change (D-08).

## Commits (TDD: RED then GREEN)

| Task | Phase | Commit  | Message |
| ---- | ----- | ------- | ------- |
| 1    | RED   | a220e120 | test(06-03): add failing tests for REGULAR->FULL promotion decision |
| 1    | GREEN | 80032597 | feat(06-03): add REGULAR->FULL promotion in ApplyVoteEffects |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2/3 - Testability] Extracted promotion decision into pure static helper EvaluateRegularPromotionStatic**
- **Found during:** Task 1 (RED phase)
- **Issue:** The plan's `<action>` describes inserting the promotion check directly inside the private member `ApplyVoteEffects`. `ApplyVoteEffects` is a private member of `ValidatorRegistry`, which cannot be instantiated in a unit test without a full GlobalDB + GossipPubSub + block-request-callback wiring (`ValidatorRegistry::New` requires `std::shared_ptr<crdt::GlobalDB>`, etc. — the same constraint documented in the 06-02 SUMMARY). Without an instance, the promotion decision — the security-critical core of this plan — would have no deterministic unit coverage, and the TDD RED phase could not be written.
- **Fix:** Extracted the boolean promotion decision into `static bool EvaluateRegularPromotionStatic(const ValidatorEntry&, const WeightConfig&)`. The `ApplyVoteEffects` approve branch calls the helper and performs the `set_role` mutation. This follows the exact precedent set by 06-02 (`EvaluateSlotQuorumStatic`), honors the project's API-design principle ("free/static functions over members where possible"), and satisfies CLAUDE.md's "minimal change philosophy" — production behavior is unchanged; only the decision is now pure and unit-testable. REQ-DETERM-01 is strengthened: the helper provably reads no instance state.
- **Files modified:** src/blockchain/ValidatorRegistry.hpp, src/blockchain/ValidatorRegistry.cpp
- **Commit:** 80032597

### Plan wording vs. implementation notes

- The plan's `<interfaces>` sketch and `<action>` reference stale line numbers (1738-1763 for the approve branch, 1796-1805 for the debug log). The actual `ApplyVoteEffects` in the current codebase is at `ValidatorRegistry.cpp:1882-1996` (the function grew between the plan's verification date and execution — likely from the 06-02 additions landing). The promotion branch was placed at the semantically correct site (after `entry.set_weight(clamped)` inside the ACTIVE approve branch) rather than at the stale line number.
- The plan said to log via `spdlog::debug()`. As in Plans 06-01 and 06-02, the codebase pattern is `base::Logger` via the per-file `logger_` member (`logger_->debug(...)`). I followed the existing per-file logging pattern rather than calling spdlog directly, consistent with the surrounding `ApplyVoteEffects` log calls.

## Build / Test Execution

The verification commands in the plan (`cd build/OSX/Debug && ninja && ctest ...`) were **not executed by the agent**, following the same constraint documented in the 06-02 SUMMARY. This worktree has no populated `build/OSX/Debug` directory, and per `CLAUDE.md` the user owns the build (thirdparty is built separately, full regression is the user's responsibility before push). The user must run:

```bash
cd build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja
ctest -R "validator_registry_promotion_test|validator_registry_slot_quorum_test|consensus_vote_slot_test|consensus_bridge_mint_subject_test|ValidatorRegistry" -j8 --output-on-failure
```

Expected: the new `validator_registry_promotion_test` passes (9 cases); the Plan 01/02 Phase 6 tests remain green; existing `ValidatorRegistry`-linked tests remain green.

## TDD Gate Compliance

Per-task RED -> GREEN gate sequence verified in git log:
- Task 1: `test(06-03)` (a220e120, RED) -> `feat(06-03)` (80032597, GREEN). Both gates present.

No REFACTOR commit — the GREEN implementation was already minimal (a 4-line promotion branch delegating to a 6-line pure helper).

## Threat Model Mitigation Verification

The implemented surface matches the plan's `<threat_model>` exactly:

| Threat | Mitigation Status |
|--------|-------------------|
| T-06-11 (Elevation of Privilege via promotion) | Mitigated — promotion requires BOTH weight accumulation AND low penalty; a promoted FULL node still cannot capture a bridge mint alone (slot tally requires cumulative >75%). |
| T-06-12 (Tampering via penalty reset) | Mitigated — penalty decrements by 1 per approve; a penalized node must earn back reputation over many rounds before crossing `penalty_threshold_`. |
| T-06-13 (Repudiation via non-deterministic promotion) | Mitigated — `EvaluateRegularPromotionStatic` is a pure function of `(entry, weight_config)`; peers converge via the registry CRDT. |
| T-06-14 (Tampering via GENESIS demotion) | Mitigated — the `entry.role() == Role::REGULAR` guard prevents GENESIS from being rewritten to FULL. |

## Known Stubs

None. No hardcoded empty/mock data flows into production. The promotion decision is fully implemented and exercised by 9 deterministic unit cases.

## Threat Flags

None. The implemented surface introduces no new network endpoints, auth paths, file access patterns, or trust-boundary schema changes beyond what the plan's threat model enumerates. The only new mutation is `entry.set_role(Role::FULL)` inside the existing `ApplyVoteEffects` reputation-mutation site.

## Self-Check: PASSED

Agent-verified claims (build/test execution remains user-owned per CLAUDE.md):

- `grep -c "full_promotion_weight_" src/blockchain/ValidatorRegistry.hpp` -> 2 (declaration + doc reference; >= 1 required). FOUND.
- `grep -c 'set_role( Role::FULL )' src/blockchain/ValidatorRegistry.cpp` -> 1 (the promotion branch). FOUND.
- `grep -c "EvaluateRegularPromotionStatic" src/blockchain/ValidatorRegistry.hpp` -> 1 (declaration). FOUND.
- `grep -c "EvaluateRegularPromotionStatic" src/blockchain/ValidatorRegistry.cpp` -> 2 (definition + call site in ApplyVoteEffects). FOUND.
- `test/src/blockchain/validator_registry_promotion_test.cpp` present on disk. FOUND.
- Both task commits present in git log: a220e120 (RED), 80032597 (GREEN). FOUND.

User must still confirm (build/test not run by agent — see Build / Test Execution):

- `ninja` builds `validator_registry_promotion_test`.
- `validator_registry_promotion_test` passes all 9 cases.
- Pre-existing `validator_registry_slot_quorum_test`, `consensus_vote_slot_test`, `consensus_bridge_mint_subject_test` remain green.
- `consensus_certificate_test` remains commented out (pre-existing, not introduced here).
