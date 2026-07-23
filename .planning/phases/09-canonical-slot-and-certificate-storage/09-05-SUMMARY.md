---
phase: 09-canonical-slot-and-certificate-storage
plan: 05
subsystem: bridge-validation
tags: [external-mint, canonical-identity, receipt-binding, fail-closed]

requires:
  - phase: 09-02
    provides: Canonical mint slot identity from chain, burn hash, and receipt index
  - phase: 09-04
    provides: Verified certificate lookup and compatibility semantics
provides:
  - Pre-mutation canonical external mint identity validation
  - Nonzero burn-hash enforcement during mint slot derivation
  - Exact request-to-receipt transaction-hash binding
  - Explicit local Genius chain bypass classification
affects: [10-finalization-state-machine, bridge-validation, transaction-validation]

tech-stack:
  added: []
  patterns:
    - Caller-controlled external identities are validated and parsed once before state mutation
    - External witnesses derive their proof source only from the single nonzero mint input
    - RPC endpoint weight is awarded only after exact receipt transaction-hash equality

key-files:
  created: []
  modified:
    - src/account/MintTransactionV2.cpp
    - src/account/TransactionManager.cpp
    - src/account/PublicChainInputValidator.cpp
    - test/src/blockchain/consensus_slot_key_test.cpp
    - test/src/account/public_chain_mint_validation_test.cpp
    - test/src/account/transaction_manager_pending_lifecycle_test.cpp

key-decisions:
  - "External chain IDs use canonical unsigned-decimal spelling and burn identities use exactly 64 lowercase hexadecimal characters representing a nonzero hash."
  - "Only the explicit supergenius and supergenius_chain identifiers bypass external receipt verification."
  - "A receipt whose transaction hash differs from the requested burn is an endpoint failure with zero consensus weight."

patterns-established:
  - "External identity boundary: validate text -> parse once -> compare bytes -> mutate or verify."
  - "Bridge proof binding: mint input hash == request hash == returned receipt transaction hash."

requirements-completed:
  - SLOT-03
  - SLOT-04

duration: 14 min
completed: 2026-07-23
---

# Phase 09 Plan 05: External Mint Proof Closure Summary

**External mints now require one canonical nonzero burn identity from API entry through slot derivation and exact RPC receipt verification, with no empty-source or implicit-local bypass.**

## Performance

- **Duration:** 14 min
- **Started:** 2026-07-23T20:26:33Z
- **Completed:** 2026-07-23T20:40:26Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Made `MintFunds` reject malformed chain IDs and burn hashes before datastore reads, UTXO creation, reservation, persistence-key construction, or queue mutation.
- Reused one parsed nonzero burn hash throughout mint construction and rejected zero hashes during canonical mint slot derivation.
- Removed external proof fallback to `uncle_hash`, constrained local bypass to two explicit Genius chain identifiers, and rejected empty external metadata.
- Bound each RPC response to the requested burn hash before receipt status, indexed-log inspection, ABI decoding, or consensus-weight accumulation.
- Added CRDT-backed mutation guards plus direct slot, witness, source/input disagreement, receipt mismatch, and local-chain controls.

## Task Commits

Each task was committed atomically:

1. **Task 1: Reject noncanonical mint chain and burn identities before mutation** - `bce134dd` (fix)
2. **Task 2: Make external receipt proof fail closed and bind it to the requested transaction** - `4ba67aad` (fix)

## Files Created/Modified

- `src/account/MintTransactionV2.cpp` - Reject all-zero external input hashes before forming a mint slot preimage.
- `src/account/TransactionManager.cpp` - Validate and parse canonical external identity before any mint side effect.
- `src/account/PublicChainInputValidator.cpp` - Enforce single-source witness identity, explicit local classification, byte equality, and receipt hash binding.
- `test/src/blockchain/consensus_slot_key_test.cpp` - Prove a zero burn hash cannot derive a mint preimage or slot.
- `test/src/account/public_chain_mint_validation_test.cpp` - Cover malformed sources/chains, source disagreement, receipt mismatch, local bypasses, and direct zero-input witness rejection.
- `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - Exercise real `MintFunds` against CRDT state and prove invalid identities do not mutate UTXO, queue, or executed-key state.

## Decisions Made

- Canonical numeric chains reject aliases such as signs, whitespace, and leading zeroes instead of normalizing them.
- External burn hashes are never recovered from DAG metadata; the mint input is the sole witness source.
- Receipt transaction-hash mismatch is handled per endpoint and contributes no weight, allowing other independent endpoints to prove the request.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- The CRDT-backed lifecycle fixture required deterministic in-memory secure storage before constructing its account; installing the factory in the fixture constructor preserved the base fixture's suite initialization.
- A malformed non-hex test value could throw inside the test's state-inspection helper, so inspection is limited to syntactically parseable hashes while the production call still exercises every invalid class.

## Known Stubs

None introduced.

## User Setup Required

None - no external service configuration required.

## Verification

- Task 1 prescribed build and focused tests — PASS: slot 1/1, real `MintFunds` lifecycle 3/3.
- Task 2 prescribed build and focused tests — PASS: 6/6.
- Full `public_chain_mint_validation_test` — PASS: 10/10.
- `ctest --test-dir build/OSX/Release -R '(consensus_slot_key|public_chain_mint_validation|transaction_manager_pending_lifecycle)' --output-on-failure` — PASS: 3/3 targets.
- Source audit — PASS: no `GetUncleHash()` external fallback, no empty-source success return, and receipt hash equality precedes status/log inspection.
- `git diff --check` — PASS.

## Next Phase Readiness

- Canonical mint slots and public-chain proof validation now share one exact external burn identity.
- Phase 10 can rely on malformed or mismatched bridge claims failing before slot-state transitions.
- No blockers.

## Self-Check: PASSED

- All six modified plan files exist.
- Both atomic task commits are present in git history.
- All task and plan-level verification commands pass.
- SLOT-03 and SLOT-04 retain canonical bridge coverage while malformed inputs fail closed.
- Pre-existing user changes and unrelated untracked files remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
