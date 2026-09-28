---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 23
subsystem: trust policy and payments
tags: [securecrdt, burn-config, passive-convergence, pay-escrow, multi-account]

requires:
  - phase: 13-20
    provides: production-owned deterministic initial burn and actionable refresh outcomes
  - phase: 13-21
    provides: signer-free passive policy activation and callback lifetime safety
  - phase: 13-22
    provides: fresh-linked production closure gate and structural no-bypass patterns
provides:
  - burn-ready controllers discover and attempt authoritative retained successors before publishing readiness
  - three-production-node proof that A proposes, B approves, and passive C durably consumes burn v2 without signing
  - real node-scoped PayEscrow proof across C account and TransactionManager replacement
  - below-quorum, stale-predecessor, and synchronous commit-failure economic non-publication controls
affects: [phase-13-verification, burn-policy, transaction-manager-lifetime, v1.1-closure]

tech-stack:
  added: []
  patterns: [authoritative discovery on every refresh, persist-before-publish, foreign-approval passive callback]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-23-SUMMARY.md
  modified:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - test/src/multiaccount/policy_lifetime_multi_account_test.cpp

key-decisions:
  - "Ready-state refresh always merges authoritative retained burn candidates with callback IDs and processes them in version/content-hash order before publishing ConfirmedReady."
  - "Burn callbacks enqueue only foreign approvals: passive receivers converge automatically while active operators retain their explicit LocalTrustAdmin activation path without a callback race."

patterns-established:
  - "Passive economic convergence: list current-predecessor candidates on every verified refresh, deduplicate, persist synchronously, reload durable state, then publish readiness."
  - "Multi-node trust tests derive configured peer identities through GeniusAccount, because raw Ethereum private-key addresses are not SGNS node addresses."

requirements-completed: [BURN-03, SCRDT-04, TEST-01]

duration: 29 min
completed: 2026-08-14
---

# Phase 13 Plan 23: Passive Burn Successor Convergence Summary

**Burn-ready nodes now discover and durably activate quorum-approved successors before readiness, with a three-node production test proving passive C's real PayEscrow burn changes from 100 to 250 across account-manager lifetime transitions.**

## Performance

- **Duration:** 29 min
- **Started:** 2026-08-14T13:50:09Z
- **Completed:** 2026-08-14T14:18:55Z
- **Tasks:** 1 TDD task
- **Files modified:** 3 source/test files plus this summary

## Accomplishments

- Removed the economically-ready early return and one-shot burn discovery so every verified refresh lists authoritative current-predecessor candidates before readiness publication.
- Preserved D-09/D-11: only BootstrapOnly burn v1 may be automatically signed, and only by a verified current member; burn v2 remains an explicit A-propose/B-approve decision.
- Added a production-topic A/B/C regression proving passive C independently commits burn v2, updates its node-scoped provider, and changes real `PayEscrow` output after account and `TransactionManager` replacement.
- Proved one-approval, stale-predecessor, and injected synchronous-commit failures leave C's durable burn head, provider, and economic output unchanged; actionable commit failure retains candidate identity and typed error.

## Task Commits

The TDD task was committed atomically as RED then GREEN:

1. **Task 1 RED: Add passive burn lifetime counterexample** - `c39b636b` (test)
2. **Task 1 GREEN: Process passive burn successors before readiness** - `0ea8ad4d` (fix)

## Verification Evidence

- RED build passed; the focused case then failed exactly at `passive C did not durably converge on burn v2` after A's proposal and B's approval (42.7 seconds).
- GREEN build: `cmake --build build/OSX/Release --target policy_lifetime_multi_account_test -j8` exited `0`.
- Focused GREEN: `PolicyLifetimeMultiAccountTest.PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin` passed, 1/1, in 19.8 seconds.
- Complete binary: `policy_lifetime_multi_account_test` passed, 1/1, in 20.9 seconds.
- Comment/string-aware structural guard reported `passive_burn_guard=PASS ready_order_guard=PASS` and found one exact A proposal, one exact B approval, no receiver action/nudge in the marked window, and durable plus `PayEscrow` observations.
- Declaration scan found one enabled exact case and no `DISABLED_` counterpart.

## Files Created/Modified

- `src/account/TrustStartupController.hpp` - Removes the stale process-lifetime one-shot discovery flag.
- `src/account/TrustStartupController.cpp` - Lists and deterministically processes passive burn successors on every verified refresh before readiness; ignores local approval callbacks to preserve explicit operator activation.
- `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` - Replaces same-node proposal coverage with a real three-production-node passive lifetime and PayEscrow counterexample plus negative controls.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-23-SUMMARY.md` - Records execution and verification evidence.

## Decisions Made

- Authoritative listing is performed on every refresh rather than cached by process lifetime; listed IDs merge with callback-retained IDs, excluding previously actionable failures.
- A successful activation reloads the coherent durable snapshot and ends that predecessor's pass; remaining IDs became stale when the predecessor changed and will be rediscovered against the new head.
- Local burn signatures do not enqueue the controller worker. This prevents the worker racing the operator's immediate `LocalTrustAdmin` activation while still letting passive receivers enqueue both foreign A/B approvals.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test fixture bug] Corrected production-node identity derivation and startup sequencing**
- **Found during:** Task 1 RED
- **Issue:** Two passive keys were 63 hex characters, and the fixture configured raw Ethereum-signer addresses rather than the derived SGNS `GeniusAccount` identities used by live nodes, preventing valid three-node startup.
- **Fix:** Used valid 64-character keys, pre-derived the three SGNS identities through `GeniusAccount`, registered node A's real authorized address immediately, and started all nodes before lifecycle waits.
- **Files modified:** `test/src/multiaccount/policy_lifetime_multi_account_test.cpp`
- **Verification:** All three production nodes reached durable burn-v1 readiness and RED failed only at passive burn-v2 convergence.
- **Committed in:** `c39b636b`

**2. [Rule 1 - Race] Preserved explicit operator activation against local callback races**
- **Found during:** Task 1 GREEN
- **Issue:** Enqueueing an operator's own approval let the controller worker commit between `ApproveBurnCandidate` and `LocalTrustAdmin`'s immediate activation, causing the explicit approve call to return a stale `invalid_argument` despite success.
- **Fix:** Mirrored trusted-peer callback ownership by enqueueing burn candidates only for foreign approvals; passive C still processes both A and B, while A/B use their explicit admin path.
- **Files modified:** `src/account/TrustStartupController.cpp`, `test/src/multiaccount/policy_lifetime_multi_account_test.cpp`
- **Verification:** Focused and complete production lifetime runs both passed.
- **Committed in:** `0ea8ad4d`

---

**Total deviations:** 2 auto-fixed bugs.
**Impact on plan:** Both fixes were required for deterministic production-path evidence and preserved the specified authority boundary; no package, schema, endpoint, topic, or transport was added.

## Issues Encountered

- The first sandboxed integration attempt could not open local libp2p listeners. The required focused and complete gates were rerun with listener permission, without weakening or skipping the test.

## Known Stubs

None. The changed production path and test use real retained candidates, synchronous durable commits, the live confirmed provider, and real `TransactionManager::PayEscrow` output.

## Threat Flags

None. This plan changed an existing trust-policy activation path and existing production-topic test; it introduced no new endpoint, authentication path, file-access boundary, schema, dependency, topic, or transport.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-08 is closed: economically-ready controllers cannot return before authoritative burn listing and deterministic activation attempts.
- The passive half of WR-07 is closed with real node-scoped provider and `PayEscrow` evidence across account/manager lifetime transition.
- Plan 13-26 can consume the exact named case and marked structural window for repeated final closure evidence.

## Self-Check: PASSED

- `src/account/TrustStartupController.hpp`, `src/account/TrustStartupController.cpp`, and `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` exist.
- RED commit `c39b636b` and GREEN commit `0ea8ad4d` exist in repository history in the required order.
- The summary contains no goal-blocking stub and records all acceptance gates and deviations.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
