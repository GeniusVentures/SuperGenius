---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: 24
subsystem: trusted-peer policy activation and restart recovery
tags: [securecrdt, trusted-peer, retained-quorum, reconstruction, tdd]

requires:
  - phase: 13-21
    provides: signer-free passive policy activation and callback-safe controller lifetime
  - phase: 13-23
    provides: authoritative-on-refresh retained successor processing pattern
provides:
  - authoritative retained policy-candidate replay on every verified controller refresh
  - deterministic callback/list merge with policy-specific pending and failure-suppression state
  - three-node no-new-write recovery proof across an injected commit failure and controller reconstruction
affects: [phase-13-verification, plan-13-26, boot-04, policy-01, tpr-02]

tech-stack:
  added: []
  patterns: [authoritative replay plus callback merge, deterministic policy queue, controller-local suppression]

key-files:
  created:
    - .planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-24-SUMMARY.md
  modified:
    - src/account/TrustStartupController.hpp
    - src/account/TrustStartupController.cpp
    - test/src/startup/trust_first_boot_e2e_test.cpp

key-decisions:
  - "Policy candidates use pending and failed containers distinct from burn candidates so their retry state and error context cannot cross domains."
  - "Every verified Refresh merges authoritative current-predecessor policy candidates with callback IDs, then sorts and deduplicates before activation."
  - "Actionable candidate suppression is scoped to one controller; reconstruction starts empty and therefore rediscovers retained authenticated records."

patterns-established:
  - "Retained replay: authoritative storage listing and callback hints merge into one deterministic, deduplicated work queue."
  - "Fault recovery: suppress actionable failures for the current owner lifetime while preserving durable records for reconstructed-owner retry."

requirements-completed: [BOOT-04, POLICY-01, TEST-01, TPR-02]

duration: 1h 18m
completed: 2026-08-14
---

# Phase 13 Plan 24: Retained Policy Quorum Replay Summary

**A reconstructed startup controller now rediscovers retained current-policy quorum and durably advances the exact successor without another proposal, approval, CRDT write, admin call, signature, or activation nudge.**

## Performance

- **Duration:** 1h 18m
- **Started:** 2026-08-14T16:10:00Z
- **Completed:** 2026-08-14T17:28:10Z
- **Tasks:** 1 TDD task
- **Files modified:** 3 source/test files plus this summary

## Accomplishments

- Added policy-specific pending and failed candidate containers instead of routing trusted-peer policy IDs through burn-named retry state.
- Made every verified `TrustStartupController::Refresh` call `TrustedPeerRegistry::ListPendingPolicyCandidates`, merge authoritative IDs with callback hints, filter controller-local actionable failures, and process a stable version/content-hash order with duplicates removed.
- Preserved typed semantics: authenticated below-quorum candidates remain pending, while actionable validation or commit errors emit `TRUST_ACTIVATION_FAILED`, preserve last-known-good authority, and avoid an in-process retry loop.
- Added the exact three-node reconstruction counterexample: passive C retains A/B policy-v2 approvals, observes one injected `COMMIT_FAILED`, keeps policy v1, destroys its controller/store, reopens the same durable state, and converges to the exact v2 hash without any new write or authority action.

## Task Commits

The TDD task was committed atomically as RED then GREEN:

1. **Task 1 RED: Add retained policy reconstruction counterexample** - `c5a882b0` (test)
2. **Task 1 GREEN: Replay retained policy quorum on refresh** - `55b947eb` (fix)

## Verification Evidence

- RED compiled and ran against the pre-GREEN controller. It observed the injected `COMMIT_FAILED`, preserved C's v1 durable hash, reconstructed C, then timed out because the retained v2 quorum was not rediscovered.
- `cmake --build build/OSX/Release --target trust_first_boot_e2e_test -j8` passed.
- The plan's exact automated two-case filter passed 2/2 at exit `0` in 16.339 seconds.
- The complete `trust_first_boot_e2e_test` binary passed all 4 enabled tests at exit `0` in 30.132 seconds.
- Structural checks found exactly one enabled reconstruction case and one enabled existing passive-policy case, with no disabled counterparts.
- The exact reconstruction markers span replacement construction through durable v2 readiness; their window contains no `Propose`, `Approve`, `SubmitCandidateApproval`, raw `Put`, callback registration/injection, direct `TryActivatePolicyCandidate`, or `LocalTrustAdmin` action.
- Source inspection confirmed policy-specific pending/failure state, authoritative listing on Refresh, activation outside the candidate mutex, stable version/content-hash sorting, and deduplication before each attempt.
- `git diff --check` passed and the RED commit precedes GREEN in repository history.

## Files Created/Modified

- `src/account/TrustStartupController.hpp` - Adds controller-local policy pending and actionable-failure suppression state separate from burn retry state.
- `src/account/TrustStartupController.cpp` - Merges retained and callback policy candidates deterministically, activates outside the queue mutex, and preserves typed pending/error behavior.
- `test/src/startup/trust_first_boot_e2e_test.cpp` - Adds commit-failure/reconstruction proof with retained approvals, exact no-write markers, unchanged approval count, zero passive signatures, and exact durable successor assertions.
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-24-SUMMARY.md` - Records implementation, TDD, threat, and acceptance evidence.

## Decisions Made

- Policy and burn retries maintain separate pending and failed containers because their authoritative discovery APIs and error contexts are distinct.
- Authoritative policy listing runs on every verified Refresh, including initial construction, so callback delivery is only a latency hint and never the recovery authority.
- Deterministic ordering uses candidate version first and content hash second; identical callback/list IDs collapse before activation.
- Actionable failure suppression belongs to the controller lifetime. A replacement controller intentionally starts with no suppression and re-evaluates the retained authenticated record against the same current-policy authorization and durable CAS checks.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Sandboxed test attempts could not bind local libp2p listeners. Every network-backed RED and GREEN gate was rerun with listener permission without weakening, skipping, or disabling coverage.
- Two early approval reviews for the combined listener command timed out before process launch. Both exact cases first passed separately, then the literal combined plan command and complete binary passed under listener permission.

## Known Stubs

None. The regression uses three real production GlobalDB compositions, retained SecureCrdt approvals, the durable `TrustStateStore::BatchCommitter` seam, controller destruction, and same-state reopening.

## Threat Flags

None. The change replays through the existing authenticated candidate verifier and durable policy CAS; it adds no endpoint, signer, admin path, file-access boundary, schema, package, topic, or transport.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- CR-09 is closed across transient durable commit failure and complete startup-controller reconstruction.
- BOOT-04 restart authority, POLICY-01 current-policy authorization, TEST-01 failure/restart evidence, and TPR-02 passive quorum convergence are explicit in the exact regression.
- Plans 13-23, 13-24, and 13-25 are complete; Plan 13-26 is ready for the exact fifteen-case, twenty-five-target, sanitizer-aware final closure gate.

## Self-Check: PASSED

- All three planned source/test files and this summary exist.
- RED commit `c5a882b0` and GREEN commit `55b947eb` exist in repository history in the required order.
- Every task acceptance criterion and both plan verification gates passed.
- No goal-blocking stub or unmodeled security surface was introduced.
- The two protected pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-14*
