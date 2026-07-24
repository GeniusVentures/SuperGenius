---
phase: 09-canonical-slot-and-certificate-storage
plan: 13
subsystem: bridge-witness-validation
tags: [evm-receipt, weighted-quorum, endpoint-isolation, bridge-mint]

requires:
  - phase: 09-05
    provides: Exact receipt transaction-hash and indexed bridge-log validation
  - phase: 09-09
    provides: Fail-closed durable bridge and certificate read semantics
provides:
  - Endpoint-local handling for missing and failed EVM receipt status
  - Ordered disagreement coverage proving later independent 50 plus 25 weight can reach quorum
  - Negative coverage proving a failed endpoint contributes no success weight
affects: [bridge-mint-validation, rpc-quorum, bridge-e2e]

tech-stack:
  added: []
  patterns:
    - Treat every endpoint disagreement as a zero-weight vote and continue until quorum or exhaustion
    - Script RPC responses by endpoint URL and assert query order in weighted-policy tests

key-files:
  created: []
  modified:
    - src/account/PublicChainInputValidator.cpp
    - test/src/account/public_chain_mint_validation_test.cpp
    - test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp

key-decisions:
  - "A missing or failed receipt status is local to that RPC endpoint and cannot veto later independent proof."
  - "Success weight is awarded only after exact receipt hash, successful status, indexed log, and decoded burn facts all match."
  - "Validator-facing source references remain canonical prefix-free hashes while JSON-RPC receipt fixtures add the transport-level 0x prefix."

patterns-established:
  - "Weighted receipt policy: parse and bind hash -> require successful status -> bind indexed event facts -> add endpoint weight."
  - "Ordered RPC tests: URL-keyed responses plus an exact call trace prove continuation and quorum composition."

requirements-completed:
  - SLOT-03
  - SLOT-04

duration: 6 min
completed: 2026-07-24
---

# Phase 09 Plan 13: Endpoint-Local Receipt Status Summary

**Failed or missing EVM receipt status now contributes zero endpoint weight while later exact receipts may independently satisfy the configured 75-weight mint quorum.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-07-24T15:44:57Z
- **Completed:** 2026-07-24T15:50:39Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments

- Replaced the global receipt-status veto with an endpoint-local failure path that records the status class, increments the attempted-endpoint count, and continues without adding weight.
- Added ordered URL-scripted regressions covering both `0x0` and omitted status before later valid endpoints totaling 50 plus 25 weight.
- Added a negative control proving failed 50 weight plus valid 25 weight remains below quorum.
- Restored the adjacent ChainList E2E fixtures to the Phase 9 prefix-free canonical source-reference contract while retaining JSON-RPC `0x` encoding.

## Task Commits

Each task was committed atomically:

1. **Task 1: Treat missing/failed receipt status as endpoint-local disagreement** - `bab51950` (fix)

## Files Created/Modified

- `src/account/PublicChainInputValidator.cpp` - Continues past missing or failed receipt status without contributing endpoint weight.
- `test/src/account/public_chain_mint_validation_test.cpp` - Adds configurable receipt status, URL-scripted transport state, exact call-order assertions, and positive/negative weighted-policy tests.
- `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp` - Uses canonical prefix-free validator inputs and adds `0x` only when encoding JSON-RPC receipts.

## Decisions Made

- Receipt status disagreement follows the same endpoint-local weighted policy as transport, parse, receipt-hash, and indexed-log disagreement.
- Failed endpoints are counted as tried for diagnostics but cannot increase `success_weight`.
- Test helpers distinguish canonical application hashes from JSON-RPC wire quantities.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Updated stale adjacent ChainList source-reference fixtures**
- **Found during:** Task 1 full regression verification.
- **Issue:** Five ChainList E2E cases still passed `0x`-prefixed source references even though Phase 9 canonical validation requires exactly 64 lowercase hexadecimal characters without the prefix.
- **Fix:** Kept validator inputs prefix-free and centralized `0x` addition inside the JSON-RPC receipt fixture builder.
- **Files modified:** `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp`
- **Verification:** Complete `bridge_e2e_chainlist_test` passes 12/12.
- **Committed in:** `bab51950`

---

**Total deviations:** 1 auto-fixed (1 blocking).
**Impact on plan:** The change is test-fixture-only and restores the mandated adjacent regression to the already-established canonical source-reference contract.

## Issues Encountered

- The initial adjacent regression run failed five E2E cases because their source-reference fixtures retained the obsolete `0x` prefix. The fixture boundary was corrected and the complete binary then passed.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Built `public_chain_mint_validation_test` and `bridge_e2e_chainlist_test` successfully.
- Exact nonzero list guard found both required fully qualified status tests.
- Exact filtered status regressions — PASS, 2/2.
- Complete `public_chain_mint_validation_test` — PASS, 12/12.
- Complete `bridge_e2e_chainlist_test` — PASS, 12/12.
- `git diff --check` — PASS.

## Next Phase Readiness

- All four findings from the second Phase 9 gap review now have implemented closure plans.
- Phase 09 is ready for code review and independent phase verification; no Plan 09-13 blockers remain.

## Self-Check: PASSED

- All three modified files exist.
- Task commit `bab51950` is present.
- Both exact status tests, the full validator suite, and the adjacent ChainList suite pass.
- Protected user-owned dirty and generated paths remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
