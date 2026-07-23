---
phase: 09-canonical-slot-and-certificate-storage
plan: 02
subsystem: bridge
tags: [receipt-index, bridge-mint, evm-receipt, exact-log-validation]

requires:
  - phase: 09-01
    provides: Canonical bridge mint slots keyed by source chain, burn hash, and receipt-local index
provides:
  - Immutable receipt-local positions for live and catch-up bridge observations
  - Mandatory receipt index propagation through mint construction and bridge state
  - Exact indexed-log semantic validation against serialized mint-v2 claims
affects: [09-03-certificate-storage, 11-bridge-reservations, 12-race-verification]

tech-stack:
  added: []
  patterns:
    - Receipt-local event position is mandatory identity while block-wide log index remains observation metadata
    - External bridge claims fail closed unless one exact indexed log proves every serialized mint fact

key-files:
  created:
    - test/src/account/bridge_event_identity_test.cpp
    - test/src/account/public_chain_mint_validation_test.cpp
  modified:
    - evmrelay/include/eth/event_filter.hpp
    - evmrelay/src/eth/event_filter.cpp
    - src/watcher/impl/bridge_catchup_watcher.cpp
    - src/account/BridgeRelayer.cpp
    - src/account/GeniusNode.cpp
    - src/account/TransactionManager.cpp
    - src/account/PublicChainInputValidator.cpp
    - test/src/account/bridge_relayer_test.cpp

key-decisions:
  - "Use the absolute zero-based position in the finalized transaction receipt as the mandatory bridge event index; retain block-wide logIndex only as observation metadata."
  - "Persist and reserve bridge burns by canonical chain, transaction hash, and receipt-local index so same-transaction burns remain independent."
  - "Validate only the mint input's indexed receipt log and compare decoded chain, token, amount, and destination before endpoint weight contributes to quorum."

patterns-established:
  - "Bridge identity tuple: canonical source chain plus burn transaction hash plus receipt-local index."
  - "Exact-log witness validation: bounds-check, authenticate address/topic, decode, then compare all event-controlled facts."

requirements-completed:
  - SLOT-03
  - SLOT-04

duration: 35 min
completed: 2026-07-23
---

# Phase 09 Plan 02: Canonical Bridge Event Identity Summary

**Live and catch-up burns now carry one immutable receipt-local index through mint outpoints, persistence, reservation, and exact external-event validation.**

## Performance

- **Duration:** 35 min
- **Started:** 2026-07-23T13:12:01Z
- **Completed:** 2026-07-23T13:46:31Z
- **Tasks:** 3
- **Files modified:** 31

## Accomplishments

- Live receipt processing records absolute receipt-local ordinals without changing block-wide `log_index`, while catch-up resolves filtered logs against full finalized receipts, caches by transaction, and deduplicates `(tx_hash, receipt_log_index)`.
- The receipt index is required across relayer, node, and transaction-manager APIs and is used for synthetic bridge outpoints, reservations, rollback, consumed checks, and durable executed-event keys.
- Public-chain validation selects only `receipt.logs[input.output_idx_]`, authenticates its configured bridge address and topic, decodes v1/v2 burn data, and compares chain, token, amount, and destination to the serialized mint and output.
- Focused regression fixtures cover multiple burns in one receipt, missing or unresolvable positions, distinct persisted identities, wrong-index events, malformed candidate collisions, and weighted endpoint quorum behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Scaffold identity tests and expose immutable receipt-local event positions** - `dab3d75` (nested `evmrelay`) and `eb04d765` (feat)
2. **Task 2: Propagate the mandatory index through mint creation and bridge outpoint state** - `2b7d29b4` (feat)
3. **Task 3: Bind public-chain validation to the exact indexed burn log** - `9e30cfa4` (fix)

Verification follow-ups:

- `917310b8` - Updated the remaining standalone startup catch-up callback.
- `b9ded893` - Converted legacy chainlist witness fixtures to real indexed mint-v2 claims and v2 ABI event data.

## Files Created/Modified

- `evmrelay/include/eth/event_filter.hpp` - Adds optional receipt-local position to matched events while preserving block-wide index semantics.
- `evmrelay/src/eth/event_filter.cpp` - Assigns the absolute zero-based receipt position during full receipt iteration.
- `src/watcher/impl/bridge_catchup_watcher.hpp` - Requires receipt index in the burn callback contract.
- `src/watcher/impl/bridge_catchup_watcher.cpp` - Resolves block log indices against full receipts, caches receipts, and deduplicates canonical tuples.
- `src/account/BridgeRelayer.cpp` - Rejects missing live receipt positions and dispatches the exact index.
- `src/account/GeniusNode.hpp` / `src/account/GeniusNode.cpp` - Makes the receipt index required for both mint APIs and catch-up dispatch.
- `src/account/TransactionManager.hpp` / `src/account/TransactionManager.cpp` - Uses indexed outpoints and canonical executed-event keys throughout mint lifecycle state.
- `src/account/PublicChainInputValidator.cpp` - Decodes and compares only the exact indexed receipt log under the existing weighted quorum policy.
- `test/src/account/bridge_event_identity_test.cpp` - Covers immutable receipt identity, same-transaction burns, deduplication, and persistence keys.
- `test/src/account/public_chain_mint_validation_test.cpp` - Covers exact-log selection and all candidate-controlled mismatch cases.
- `test/src/account/bridge_relayer_test.cpp` - Verifies exact live index forwarding and missing-index rejection.
- `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp` - Uses semantically valid mint-v2 and v2 receipt fixtures for endpoint quorum coverage.
- `test/src/startup/startup_wiring_test.cpp` - Adapts the standalone catch-up callback to the mandatory index contract.

## Decisions Made

- Receipt-local order is the canonical event position because it survives block re-inclusion even when block-wide log indices change.
- No public API supplies a default or compatibility zero index; callers must prove and pass the position explicitly.
- Endpoint consensus weight contributes only after the exact indexed log proves the full mint claim; another matching log in the receipt is irrelevant.
- The bridge executed key is centralized as `/bridge/executed/<chain>:<tx_hash>:<receipt_index>`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] One startup catch-up callback retained the old signature**

- **Found during:** Plan-level repository-wide build
- **Issue:** `startup_wiring_test.cpp` still supplied a three-argument `BurnProcessor`, blocking compilation after the required index became mandatory.
- **Fix:** Added the explicit `uint32_t` receipt-local index parameter.
- **Files modified:** `test/src/startup/startup_wiring_test.cpp`
- **Verification:** `startup_wiring_test` builds and passes 27/27 tests; the repository-wide build reaches 100%.
- **Committed in:** `917310b8`

**2. [Rule 1 - Bug] Legacy chainlist fixtures no longer represented valid bridge mints**

- **Found during:** Adjacent validator regression verification
- **Issue:** Five tests expected exact-log validation to accept legacy `MintTransaction` objects and topic-only receipts with empty ABI data.
- **Fix:** Built indexed `MintTransactionV2` fixtures and deterministic, decodable v2 burn receipt logs whose chain, token, amount, and contract-order destination match the mint.
- **Files modified:** `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp`
- **Verification:** `bridge_e2e_chainlist_test` passes 12/12 tests.
- **Committed in:** `b9ded893`

---

**Total deviations:** 2 auto-fixed (2 bugs).
**Impact on plan:** Both updates are compatibility fallout from mandatory index propagation and exact-log validation; no production scope was added.

## Issues Encountered

- The first full build exposed the remaining legacy callback and was rerun successfully after the focused correction.
- V2 chainlist fixture construction required contract-order X/Y bytes to match the bridge decompression convention; the corrected fixture now exercises the real parser.

## User Setup Required

None - no external service configuration required.

## Verification

- Combined focused target build — PASS: `bridge_event_identity_test`, `bridge_anvil_catchup_e2e_test`, `bridge_relayer_test`, `public_chain_mint_validation_test`, and `consensus_slot_key_test`.
- `bridge_event_identity_test` — PASS, 4/4 tests.
- `bridge_relayer_test` — PASS, 27/27 tests.
- `public_chain_mint_validation_test` — PASS, 4/4 tests.
- `consensus_slot_key_test` — PASS, 11/11 tests.
- `startup_wiring_test` — PASS, 27/27 tests.
- `public_chain_input_validator_slot_test` — PASS, 6/6 tests.
- `bridge_e2e_chainlist_test` — PASS, 12/12 tests.
- Repository-wide `cmake --build build/OSX/Release -j2` — PASS, 100%.
- Public mint and catch-up APIs expose a required `uint32_t receipt_log_index` with no default or zero-index compatibility overload.

## Next Phase Readiness

- Canonical bridge identities are now serialized into mint inputs and semantically proven from exact receipt logs, ready for slot-keyed certificate persistence in 09-03.
- No blockers.

## Self-Check: PASSED

- All key created and modified files exist.
- All task and verification-fix commits are present, including the nested `evmrelay` commit.
- All plan-level and adjacent regression verification commands pass.
- SLOT-03 and SLOT-04 remain covered by executable canonical-slot and bridge-identity tests.
- The pre-existing `GeniusNode.cpp` logger-level diff remains unstaged and uncommitted.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
