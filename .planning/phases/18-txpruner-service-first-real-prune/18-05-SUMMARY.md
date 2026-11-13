---
phase: 18-txpruner-service-first-real-prune
plan: 05
subsystem: transaction
tags: [mintfunds, burn-utxo-leak, failed-transition, rollback, d-18, folded-todo, c++17]

# Dependency graph
requires:
  - phase: 18-txpruner-service-first-real-prune (18-03)
    provides: the phase directory + PATTERNS discrepancy 1 (the FAILED-transition rollback already exists — this plan's reconciliation mandate)
provides:
  - UTXOManager::RemoveBurnUTXOs — the failed-bridge-mint cleanup primitive (release reservation + erase inserted entry + persist; CONSUMED never touched)
  - The consensus-reject -> FAILED cleanup closed on BOTH the FAILED-transition mint-v2 leg and the MintFunds exception path
  - CertificateFallbackTest.BurnCleanup_* — reproduction (real reject trigger), idempotency, negative control; the reusable RejectLocalTxThroughConsensus fixture helper (HandleProposal -> reject_and_maybe_fail_local, deterministic nonce-mismatch leg)
affects: [19 SURV-01 initializeutxos-boot-wedge (the leak source is closed; boot tolerance still Phase 19), bridge relayer redelivery semantics (failed burns are re-mintable from a clean slate)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Remove-vs-release split in UTXOManager: RollbackUTXOs = release reservation only (normal txs re-spend their inputs); RemoveBurnUTXOs = the failed-MintFunds end state (the INSERTED entry is removed, CONSUMED entries are spend evidence and survive)"
    - "Deterministic consensus-reject driving in tests: signed proposal with a nonce-mismatched NonceSubject through the real HandleProposal -> HandleNonceConsensusSubject -> reject_and_maybe_fail_local path — reason-agnostic, same FAILED trigger as any live reject"

key-files:
  created:
    - .planning/todos/completed/mintfunds-leaks-burn-utxo-on-consensus-reject.md
  modified:
    - src/account/UTXOManager.hpp
    - src/account/UTXOManager.cpp
    - src/transaction/TransactionManager.cpp
    - test/src/account/transaction_manager_certificate_fallback_test.cpp

key-decisions:
  - "Reconciliation verdict: the todo's premise was half-outdated — the FAILED transition already rolled back the reservation; the real delta was rollback SEMANTICS (RollbackUTXOs never removes the inserted entry), so the fix is a removal primitive, not a wiring change"
  - "Reservation-id equality proven, not assumed: FillDAGStruct carries the burn ref string into dag.uncle_hash verbatim, so the FAILED rollback id equals the MintFunds claim id byte for byte; the parse-inconsistency fallback dies at kNotClaimable before any tx exists"
  - "CONSUMED entries are never removed: a sibling mint's spend (IsOutPointGenuinelyConsumed / DuplicateBurnMintRefusedWhenSiblingConsumedOutpoint) keeps blocking duplicate burns — the cleanup only takes READY or RESERVED-with-matching-holder entries"
  - "The MintFunds exception path got the same removal (Rule 2): release-only rollback there left the identical dangling record — same inserted-entry semantics, 4 lines away"
  - "A removal store failure ERROR-logs but never blocks the FAILED marking: the tx IS failed regardless; the next FAILED re-fire dedups, so the degraded outcome is exactly the pre-fix release-only behavior"
  - "UNCONFIRMED-expiry cleanup (ReleaseBridgeMintReservation, release-only) deliberately NOT changed: not indicted by the reconciliation, different semantics (inconclusive expiry), boot tolerance is Phase 19's companion scope"

patterns-established:
  - "Real-consensus-reject fixture helper (RejectLocalTxThroughConsensus): registry setup + signed proposal + private HandleProposal accessor on CertificateFallbackTestAccess — reusable for any local tx -> FAILED path test"

requirements-completed: [PRUNE-04]

# Metrics
duration: 24min
completed: 2026-10-21
---

# Phase 18 Plan 05: Folded-Todo Burn-UTXO Leak — Reconciliation-First Close Summary

**The consensus-reject -> FAILED path now removes the MintFunds-inserted burn UTXO_BRIDGE entry (memory + disk) and its reservation via the new UTXOManager::RemoveBurnUTXOs, after reconciliation proved the existing FAILED rollback already released the reservation — the leak was rollback SEMANTICS (entry never removed), not missing wiring; pinned by a reproduction test on the real reject_and_maybe_fail_local trigger plus idempotency and normal-tx negative controls.**

## Performance

- **Duration:** 24 min (20:36:46Z – 21:01Z)
- **Started:** 2026-10-21T20:36:46Z
- **Completed:** 2026-10-21T21:01:00Z
- **Tasks:** 2
- **Files modified:** 5 (4 code/test + 1 todo moved)

## Reconciliation Findings (Task 1 — all file:line verified at live HEAD)

Which candidate delta explains the live incident (dangling UTXO_BRIDGE after consensus reject):

**(a) Reservation-id mismatch — REFUTED.**
`MintFunds` claims the burn outpoint under the raw burn-ref string (`TransactionManager.cpp:1019-1023`); `FillDAGStruct(std::move(transaction_hash))` sets `dag.uncle_hash` from that same string (`:1561-1571`), so the FAILED rollback id (`tx->dag_st.uncle_hash()`, `:5087`) equals the claim id byte for byte — also documented at `ReleaseBridgeMintReservation` (`:4778-4781`). The `:986-996` parse-inconsistency fallback can never reach consensus: `PutUTXO` is parse-gated (`:999-1005`), so the zero-hash outpoint is unknown and `TryReserveOutpoint` returns `kNotClaimable` (`UTXOManager.cpp:624-629`); MintFunds bails at `:1024-1033` before any transaction exists.

**(b) Path exclusion — REFUTED.**
`reject_and_maybe_fail_local` fires `ChangeTransactionState(tx, FAILED)` whenever the src is local and the outgoing status is neither FAILED nor CONFIRMED (`TransactionConsensusHandler.cpp:414-453`); a local mint-v2 satisfies the cleanup branch guard (`GetSrcAddress()==local && HasUTXOParameters()`; `MintTransactionV2::HasUTXOParameters` is unconditionally true, `MintTransactionV2.cpp:171-174`) and reaches the mint-v2 leg (`:5084-5100`).

**(c) Rollback semantics — CONFIRMED (the real delta).**
`RollbackUTXOs` only transitions RESERVED -> READY in memory and erases the transient reservation (`UTXOManager.cpp:655-679`); it never removes the entry from `utxo_outpoints_`, never touches `address_outpoints_`, and never persists. The burn entry was persisted at `PutUTXO` time (`StoreUTXOsLocked` writes the READY record under `/utxo/<owner>/<txid>:<idx>`), so after a consensus-reject FAILED the inserted UTXO_BRIDGE entry survives in memory AND on disk in READY state — exactly the live-incident observable ("stays on disk in READY state ... wedged the next boot"). The MintFunds exception path (`:1054-1063`) had the identical gap.

**Verdict:** the todo's premise ("rollback fires only on the exception path") was outdated, but its observable was fully real. The leak is a removal gap, not a wiring gap.

## Accomplishments

### Task 1: Reconciliation + RED reproduction test (6af285871)

- `CertificateFallbackTest.BurnCleanup_LocalMintRejectedByConsensusRemovesBurnUtxoAndReservation` — seeds the burn exactly as MintFunds does (PutUTXO UTXO_BRIDGE + `TryReserveOutpoint` under the burn-ref string), drives the REAL trigger (`HandleProposal` -> `HandleNonceConsensusSubject` -> `reject_and_maybe_fail_local` -> `ChangeTransactionState(FAILED)`; deterministic nonce-mismatch leg — the cleanup is reason-agnostic, same trigger as the incident's witness reject), asserts the incident signature: entry gone in memory (`GetOutPointState == nullopt`), gone on disk (raw `/utxo/<owner>/<burn>:0` record absent), no lingering reservation (fresh claim returns `kNotClaimable`). Committed RED — failed on exactly the three absent-after assertions while present-before and FAILED-status assertions passed.
- Fixture helpers added: `EnsureGenesisRegistryForBurnCleanup`, `MakeLocalBridgeMintV2` (uncle_hash = burn ref string, the D-18 id shape), `SeedBurnUtxoAsMintFunds`, `UtxoRecordOnDisk`, `RejectLocalTxThroughConsensus`, plus a private `HandleProposal` accessor on `CertificateFallbackTestAccess`.

### Task 2: The narrow fix + idempotency and negative controls (81d98c130)

- `UTXOManager::RemoveBurnUTXOs(inputs, reservation_id)` (Doxygen-documented): under one `const std::scoped_lock` — for each input outpoint that is UTXO_BRIDGE and READY or RESERVED (holder id must match when reserved): erase entry + reservation + address index; persist each owner snapshot via `StoreUTXOsLocked` with the ConsumeUTXOs-style memory-restore and already-stored-owner repair on failure; CONSUMED entries never touched; no-op success when nothing matches.
- `ChangeTransactionState` FAILED mint-v2 leg: `RollbackUTXOs` -> `RemoveBurnUTXOs` (same inputs, same id); the discrimination (`mint-v2` AND local src AND UTXO params) untouched; store failure ERROR-logged, FAILED marking never blocked; the dead `(void) mint_tx` reservation stub removed (its purpose is now owned by the real cleanup).
- MintFunds exception path: same replacement (Rule 2 — identical inserted-entry semantics and observable).
- `BurnCleanup_SecondFailedTransitionIsIdempotent`: re-fire via the real consensus path (handler's previously-FAILED guard) AND a direct second FAILED transition (state-machine FAILED->FAILED dedup) — end state unchanged, no errors.
- `BurnCleanup_NormalTxFailedLeavesUtxoEntriesUntouched`: TransferFunds-shaped signed transfer (CreateTxParameter + ReserveUTXOs under tx hash) driven to FAILED through the same real reject — its own input released back to READY but NOT removed; an unrelated UTXO_BRIDGE entry untouched in memory and on disk.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing cleanup] MintFunds exception path left the same dangling burn entry**
- **Found during:** Task 1 reconciliation (delta c)
- **Issue:** The plan's fix menu assumed the exception path had the right semantics ("mirroring the exception path's observable behavior"); in reality the exception path is release-only too — the identical inserted-entry gap one try-block away.
- **Fix:** Applied the same `RemoveBurnUTXOs` replacement there (id = `transaction_hash`, the raw claim id).
- **Files modified:** src/transaction/TransactionManager.cpp
- **Commit:** 81d98c130

**2. [Rule 3 - Blocking] HandleProposal is private; the real reject path needed a test seam**
- **Found during:** Task 1 build
- **Issue:** Driving the REAL trigger requires `ConsensusManager::HandleProposal`, a private member.
- **Fix:** Added the established friend-accessor shape (`CertificateFallbackTestAccess::HandleProposal`) beside the existing `RecoverPendingCertificateWork`/`ParkRoundTimer` accessors.
- **Files modified:** test/src/account/transaction_manager_certificate_fallback_test.cpp
- **Commit:** 6af285871

**3. [Rule 1 - Bug] const-correctness on the new lock**
- **Found during:** Task 2 clang-tidy gate
- **Issue:** `misc-const-correctness` fired on the new function's `std::unique_lock`.
- **Fix:** `const std::scoped_lock` (single mutex, held to scope end — behavior identical; matches the 18-04 new-code precedent). Remaining tidy findings in the touched files are the documented pre-existing classes (identifier-naming file-wide; ChangeTransactionState/New/SendTransactionItem/ParseMintTransaction cognitive complexity — pre-existing over-threshold functions; test targets disable tidy by the `addtest` macro, cmake/functions.cmake:27).
- **Files modified:** src/account/UTXOManager.cpp
- **Commit:** 81d98c130

**4. [Rule 3 - Blocking] A line-grep compile-db filter corrupted entries**
- **Found during:** Task 1 tidy run (gtest include error)
- **Issue:** The db is pretty-printed multi-line; `grep -v ' -arch x86_64 '` dropped whole command lines leaving broken entries.
- **Fix:** Token replacement inside command/arguments via a Python filter (documented here for the next executor).
- **Files modified:** none in repo (tooling only)

## Verification Evidence

- `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test` — clean (run per task; zero errors/warnings).
- `--gtest_filter='*BurnCleanup*'` — Task 1: 1 FAILED (RED on the three incident-signature assertions; present-before + FAILED-status passed). Task 2: 3/3 PASSED.
- Full `transaction_manager_certificate_fallback_test` — 31/31 PASSED.
- `ctest -R "utxo_manager"` — 1/1 Passed (2.34s).
- `transaction_manager_pending_lifecycle_test` (MintFunds/ChangeTransactionState-heavy regression insurance) — 17/17 PASSED.
- `genius_transaction_objs` + `genius_node` — build clean.
- clang-format: `git-clang-format --diff HEAD` on all four changed files — "did not modify any files" at each commit.
- clang-tidy (llvm@22, filtered single-arch db): zero non-naming findings on all new/changed lines in UTXOManager.{hpp,cpp} and TransactionManager.cpp; remaining findings match the documented pre-existing classes (verified against HEAD baselines).
- Acceptance greps: discrimination intact (`GetType() == "mint-v2"` inside `GetSrcAddress() == account_m->GetAddress() && tx->HasUTXOParameters()`); diff touches only the two indicted expressions + the removed stub.
- No unexpected file deletions (the pending->completed todo move is the intended one).

## Threat Mitigations Landed

| Threat | Disposition |
|--------|-------------|
| T-18-0501 (dangling burn UTXO wedging boot) | mitigated — the reconciliation-proven cleanup removes entry + reservation on the observed path; reproduction test pins the signature (memory + disk); the boot-tolerance layer itself stays Phase 19 (GATE-05) |
| T-18-0502 (over-broad rollback reverting genuine normal-tx state) | mitigated — discrimination unchanged and grep-visible; normal-tx negative control proves release-without-removal; unrelated bridge entry untouched |
| T-18-0503 (double-rollback corrupting state on repeated FAILED) | mitigated — idempotency case covers the handler guard and the state-machine dedup; RemoveBurnUTXOs is a no-op on re-fire; CONSUMED never touched |
| T-18-0504 (a "fix" landing without a understood mechanism) | mitigated — reconciliation-first structure held: every fixed expression was indicted with file:line evidence first (Task 1 commit) |

## Adjacent, Not Indicted (documented, out of scope)

- `ReleaseBridgeMintReservation` on the UNCONFIRMED-expiry path stays release-only: a prior deliberate decision ("the burn has to become re-mintable"), different semantics from a consensus reject, and not part of the incident path. The dangling-entry-shape residual there is Phase 19 boot-tolerance territory (initializeutxos-boot-wedge todo).
- The CONFIRMED->FAILED un-confirm path (`RevertMintTransaction`) intentionally restores rather than removes — different domain (applied-effects revert).

## Self-Check: PASSED

- Files exist: 18-05-SUMMARY.md, src/account/UTXOManager.hpp, src/account/UTXOManager.cpp, src/transaction/TransactionManager.cpp, test/src/account/transaction_manager_certificate_fallback_test.cpp, .planning/todos/completed/mintfunds-leaks-burn-utxo-on-consensus-reject.md — all FOUND.
- Commits exist on branch: 6af285871 (RED test + reconciliation), 81d98c130 (fix + controls + todo close) — both FOUND via `git log`.
