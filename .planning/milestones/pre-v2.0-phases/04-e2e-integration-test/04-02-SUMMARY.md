---
phase: 04-e2e-integration-test
plan: 02
subsystem: testing
tags: [e2e, bridge, negative-tests, dedup, fail-closed, receipt-verification, gtest]

requires:
  - phase: 04-e2e-integration-test
    plan: 01
    provides: "BridgeE2ETest fixture with 3-node cluster"
  - phase: 03-burn-dedup-cache
    provides: "Slot key collision fix, fail-closed RPC, log verification"

provides:
  - "ReplayRejection test: validates dedup cache rejects duplicate burn tx hash"
  - "MissingEndpointsFailClosed test: validates fail-closed on unknown chain"
  - "InvalidReceiptLogsRejected test: validates verify_receipt_log rejects mismatched data"

affects: [04-e2e-integration-test]

tech-stack:
  added: []
  patterns: ["standalone TEST() for env-var-independent negative tests", "mock ReceiptResult construction for receipt log verification"]

key-files:
  modified:
    - test/src/bridge_e2e/bridge_e2e_test.cpp

key-decisions:
  - "Used standalone TEST() for InvalidReceiptLogsRejected instead of TEST_F so it runs without RUN_E2E_BRIDGE/PRIVATE_KEY env vars"
  - "Used 6-arg MintTokens with timeout for ReplayRejection to ensure deterministic test flow"
  - "Used chain ID '999999' for MissingEndpointsFailClosed — guaranteed to have no configured RPC endpoints"

patterns-established:
  - "Mock receipt construction pattern: build eth::codec::Receipt with LogEntry, wrap in ReceiptResult, pair with BridgeEventClaim"
  - "Negative test without fixture: standalone TEST() in fixture file for tests that don't need node infrastructure"

requirements-completed: []

duration: 2min
completed: 2026-05-31
---

# Phase 4 Plan 02: Negative Test Cases Summary

**Three negative test cases validating Phase 3 security fixes: replay rejection via dedup cache, fail-closed on missing RPC endpoints, and invalid receipt log rejection via verify_receipt_log**

## Performance

- **Duration:** 2 min
- **Started:** 2026-05-31T23:54:39Z
- **Completed:** 2026-05-31T23:56:49Z
- **Tasks:** 3
- **Files modified:** 1

## Accomplishments
- ReplayRejection test: calls MintTokens twice with same burn tx hash, verifies second call is rejected by dedup cache
- MissingEndpointsFailClosed test: calls MintTokens for chain "999999" (no RPC endpoints), verifies fail-closed behavior
- InvalidReceiptLogsRejected test: directly calls verify_receipt_log with mock data, verifies matching/succeeds, wrong contract/kContractMismatch, wrong topic0/kTopic0Mismatch

## Task Commits

Each task was committed atomically:

1. **Tasks 1-3: Add negative test cases** - `8979e08e` (test)

## Files Created/Modified
- `test/src/bridge_e2e/bridge_e2e_test.cpp` - Added 3 TEST_F/TEST cases with evmrelay includes for receipt log verification

## Decisions Made
- InvalidReceiptLogsRejected uses standalone TEST() instead of TEST_F so it runs without env vars (RUN_E2E_BRIDGE, PRIVATE_KEY)
- ReplayRejection uses 6-arg MintTokens with 5s timeout for deterministic finalization waiting
- Chain ID "999999" used for MissingEndpointsFailClosed — no endpoints configured, guaranteed fail-closed path

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] InvalidReceiptLogsRejected uses TEST() instead of TEST_F**
- **Found during:** Task 3 implementation
- **Issue:** Plan specified TEST_F(BridgeE2ETest, ...) but the plan's verification criteria states "Negative tests pass without RUN_E2E_BRIDGE or PRIVATE_KEY". TEST_F with BridgeE2ETest fixture would skip without env vars.
- **Fix:** Used standalone TEST(BridgeE2ENegativeTest, InvalidReceiptLogsRejected) which doesn't depend on the fixture and runs without env vars
- **Files modified:** test/src/bridge_e2e/bridge_e2e_test.cpp
- **Verification:** Test passes without any env vars set
- **Committed in:** 8979e08e

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal — TEST() vs TEST_F is a mechanical difference that enables the stated verification criteria.

## Issues Encountered
None — build passed on first attempt, all tests recognized by gtest.

## User Setup Required
None — no external service configuration required. The tests require:
- ReplayRejection: `RUN_E2E_BRIDGE=1` + `PRIVATE_KEY` env vars (uses fixture)
- MissingEndpointsFailClosed: `RUN_E2E_BRIDGE=1` + `PRIVATE_KEY` env vars (uses fixture)
- InvalidReceiptLogsRejected: No env vars needed (standalone test)

## Next Phase Readiness
- Negative test coverage complete for Phase 3 security fixes
- InvalidReceiptLogsRejected can run in CI without secrets
- ReplayRejection and MissingEndpointsFailClosed ready for integration test runs with testnet credentials

## Self-Check: PASSED

- [x] test/src/bridge_e2e/bridge_e2e_test.cpp exists
- [x] .planning/phases/04-e2e-integration-test/04-02-SUMMARY.md exists
- [x] Commit 8979e08e exists in git log

---
*Phase: 04-e2e-integration-test*
*Completed: 2026-05-31*
