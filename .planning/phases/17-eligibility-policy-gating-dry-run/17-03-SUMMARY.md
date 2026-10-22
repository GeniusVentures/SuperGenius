---
phase: 17-eligibility-policy-gating-dry-run
plan: 03
subsystem: database
tags: [cert-slot, previous-hash, nonce-chain, pruning, certificate, transaction-manager, c++17]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening
    provides: EraseTransactionEntry (D-C4 tx->CID map deletion lifecycle) used test-sanctioned to simulate the pruned state
provides:
  - Generalized cert-slot previous_hash rung: ANY certified embedded transaction type resolves from /cert/<addr>:<nonce-1> (registration-only restriction dropped, GATE-04)
  - SC-3 proof BEFORE any production erasure exists: pruned own-frontier + certificate => previous_hash resolves; cold-start nonce chain correct; fail-closed negative; rung-3/rung-4 equivalence
  - First end-to-end test drive of Phase-16 EraseTransactionEntry (real guards, real WriteBatch, real tracked-map cleanup)
affects: [18-txpruner (first real prune depends on this correctness fix), 19-survival (restart/cold-start nonce chain)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Value-independent authority: the never-pruned /cert/ slot record backs the nonce chain link when the tx value is gone (certificates are the pruning-surviving witness)"
    - "Real-topic seeding recipe inside CertificateFallbackTest: value Put on the fixture's seeded topic + retire Put (headship) + raw tx->CID map entry via StoreTransactionCID, so the Phase-16 erase guards accept exactly as they will for the first real prune"
tech-patterns: []

key-files:
  created: []
  modified:
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - test/src/account/transaction_manager_certificate_fallback_test.cpp

key-decisions:
  - "The relaxation is exactly one guard line: only the GetType() != \"registration\" disjunct was dropped; all four binding checks (subject decodes, embedded case set, nonce==nonce-1, account_id==own) and the ladder order are byte-identical, so intact-history behavior is unchanged (rungs 1-2 still answer first)"
  - "SC-3 is proven with EraseTransactionEntry (the real guarded erase), never a hand-deleted key: the erase must pass the head/pending/LRU/resolved guards, so the proof is about the REAL post-prune state Phase 18 will produce"
  - "Case 4 captures the cert rung DIRECTLY (friend accessor) in addition to the ladder: a ladder-level assertion alone cannot bind the rung because rung 4 backstops the same hash when history is intact -- the plan's own mutation acceptance (Case 4 must fail under the restored guard) forced this shape"
  - "Round timer parked AFTER the certificate is built (registry settling needs default timing) so no async dispatch tracks the frontier -- resolution provably comes from the cert rung, not re-tracked state"
  - "GATE-04 checkbox NOT ticked in REQUIREMENTS.md by this plan (17-01/17-02 phase-close precedent): the plan delivers GATE-04's full scope, but REQUIREMENTS checkboxes are ticked at phase close"

patterns-established:
  - "Cert-slot resolution contract: the certificate is the single value-independent authority for a predecessor hash, binding discipline identical to CertificateMatchesTransaction, fail-closed to \"\" on any failed check"

requirements-completed: []  # GATE-04 delivered here; checkbox ticked at phase close per 17-01/17-02 precedent (see Decisions)

# Metrics
duration: 21min
completed: 2026-10-19
---

# Phase 17 Plan 03: Cert-Slot previous_hash Generalization (GATE-04 / SC-3) Summary

**The cert-slot previous_hash rung now resolves ANY certified predecessor, so physically erasing the node's own frontier transaction (driven end-to-end through Phase-16 EraseTransactionEntry on a real store) no longer breaks the cold-start nonce chain: the never-pruned /cert/<addr>:<nonce-1> record answers instead**

## Performance

- **Duration:** 21 min
- **Started:** 2026-10-19T14:40:41Z
- **Completed:** 2026-10-19T15:01:11Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- ONE-LINE relaxation in `GetRegisteredOutgoingPreviousHash` (TransactionManager.cpp:1720): dropped only the `GetType() != "registration"` disjunct, so any certified embedded transaction type resolves from its slot certificate; every binding check stayed verbatim and the `GetOutgoingPreviousHash` ladder order is untouched (rungs 1-2 answer first -> intact-history behavior identical)
- Contract docs updated at both levels: hpp Doxygen (cert at `/cert/<addr>:<nonce-1>` is the single authority when tracked/persisted state cannot answer; binding discipline identical to `CertificateMatchesTransaction`; fail-closed to `""`) and the generalized inline comment (registration reg/{addr} rationale + GATE-04 pruning rationale)
- Five `CertSlotPreviousHash` cases on the real-store `CertificateFallbackTest` fixture: (1) pruned frontier resolves from cert -- warm tracked/CONFIRMED state, real `EraseTransactionEntry`, then rung 3 answers; (2) cold start after prune -- nothing ever tracked, no persisted head, chain resolves exactly frontier nonce -> +1, fails closed below the frontier; (3) negative control -- erase without a cert returns `""` (never a guessed hash); (4) Pitfall 7 equivalence -- cert-rung answer == full CRDT-scan answer for a certified NON-registration predecessor (rung captured directly so the mutation binds); (5) registration predecessor still resolves (pre-existing behavior not regressed)
- First test in the repo to drive `EraseTransactionEntry` end-to-end: real-topic Put seeding (delta CID + seal + retired headship) + raw tx->CID map entry in the exact `StoreTransactionCID` shapes, so all four EraseEntry guards accept exactly as they will for the TxPruner's first real prune

## Task Commits

Each task was committed atomically:

1. **Task 1: Relax the cert-rung type restriction (one line + contract docs)** - `9179cc9c2` (feat)
2. **Task 2: SC-3 proof family (pruned frontier, cold start, negative, equivalence, registration)** - `8b5355f6e` (test)

## Files Created/Modified
- `src/transaction/TransactionManager.hpp` - Doxygen on `GetRegisteredOutgoingPreviousHash` stating the generalized GATE-04 contract (cert as value-independent authority, CertificateMatchesTransaction discipline, fail-closed)
- `src/transaction/TransactionManager.cpp` - one guard line relaxed (`embedded.has_error() || !embedded.value()` only); generalized function comment; nothing else in the function changed
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` - +240 lines: 4 friend accessors (GetOutgoingPreviousHash, GetRegisteredOutgoingPreviousHash, QueryOutgoingPreviousHashFromCRDT, StoreTransactionCID), kSeedTopic constant, `SeedErasableFrontierEntry` fixture helper, five `CertSlotPreviousHash_*` cases

## Decisions Made
- Verified-by-reading before writing tests: `EraseTransactionEntry` resolves path+CID from the raw tx->CID map (the map key IS the tx path, value the CID string), so the seeding helper stores exactly those shapes; empty-topic `PersistTransaction` seeds were unusable for the erase path (no head, no seal -- Phase-16 lesson), hence the real-topic `kSeedTopic` recipe on the fixture's pre-seeded "CRDT.Datastore.TEST.Channel"
- Round-timer parking (existing `ParkRoundTimer` handshake helper, ConcurrentCertificateIngress precedent) applied in Cases 1/2/4/5 -- every case with a durable cert that an async dispatch could track; Case 3 needs no parking (no cert exists, nothing durable to dispatch)
- Case 1 additionally asserts the pre-erase resolution (tracked rung answers while history intact) so the same test demonstrates both "intact history unchanged" and "pruned history still resolves"
- Case 2 proves the derived nonce chain is exactly frontier -> frontier+1: resolution at nonce+1 returns the frontier hash while resolution at the frontier's own nonce fails closed (no cert at nonce-1)
- `requirements.mark-complete` deliberately not run for GATE-04 (phase-close tick precedent from 17-01/17-02)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test adequacy] Case 4 as literally specified could not satisfy the plan's own mutation acceptance**
- **Found during:** Task 2 mutation check
- **Issue:** The plan's Case 4 text says "capture the ladder's answer ... assert the cert-rung answer equals what the full CRDT scan finds" with history intact. But with history intact, rung 4 (the CRDT scan) backstops the ladder: under the restored registration-only guard the ladder simply falls through to rung 4 and returns the SAME hash, so a ladder-level Case 4 passes under the mutation -- violating the plan's acceptance criterion "restoring the registration-only guard makes Case 1/2/4 fail".
- **Fix:** Case 4 now captures the cert rung DIRECTLY via a new `GetRegisteredOutgoingPreviousHash` friend accessor (in addition to the ladder and scan assertions). Under the mutation the direct capture returns `""` and the case fails, exactly as the acceptance requires; the Pitfall 7 equivalence assertion is unchanged.
- **Files modified:** test/src/account/transaction_manager_certificate_fallback_test.cpp
- **Commit:** 8b5355f6e

### Documented protocol notes (no other auto-fixes were needed)

**2. [TDD-ordering note] Task 2 RED phase vacuous by plan design**
- **Found during:** Task 2 (tdd="true")
- **Issue:** A strict RED commit is unachievable in the plan's task order -- the tests bind the relaxed guard landed by Task 1's commit, and a pre-Task-1 failing-tests commit has no tree to compile against (same as 17-01 Task 3 / 17-02 Task 3).
- **Resolution:** Single `test(17-03)` commit after the relaxation. Non-vacuity is carried by the plan's own mutation check, performed and passing: restoring the registration-only guard makes Cases 1/2/4 FAIL while 3/5 stay green, then reverted (working tree re-verified byte-identical to the Task-1 commit before the Task-2 commit).

---

**Total deviations:** 1 auto-fixed (Rule 1), 1 protocol note documented
**Impact on plan:** None on scope or gates -- the landed shape satisfies every acceptance criterion including the one the literal case text could not.

## Verification Log

| Gate | Result |
|------|--------|
| Task 1: build `genius_transaction_objs` | exit 0, zero warnings on the recompiled TUs — PASS |
| Task 1: `GetType() != "registration"` occurrences in GetRegisteredOutgoingPreviousHash | 0 — PASS |
| Task 1: binding checks present in the function (DecodeNonceSubject / TRANSACTION_NOT_SET / nonce-1 / own account) | all four verbatim (5 grep hits incl. slot-key construction) — PASS |
| Task 1: git diff on TransactionManager.cpp | exactly ONE changed code line (the guard) + comment/Doxygen only; ladder order untouched — PASS |
| Task 1: format gate (git-clang-format --diff, changed lines) | no `+` hunks — PASS |
| Task 1: tidy gate (clang-tidy 22, single-arch filtered db, TransactionManager.cpp) | zero findings on changed lines; all reported findings at pre-existing line numbers (72/73/105/110/338/939/2002/2726) — PASS |
| Task 2: build `transaction_manager_certificate_fallback_test` | exit 0 — PASS |
| Task 2: `--gtest_filter='*CertSlotPreviousHash*'` | 5/5 PASSED — PASS |
| Task 2: filtered double-run on fresh fixtures | 5/5 PASSED twice — PASS |
| Task 2: full suite double-run | 28/28 PASSED twice (~4s each; 23 pre-existing + 5 new) — PASS |
| Task 2: full suite after clang-format applied | 28/28 PASSED — PASS |
| Task 2: mutation check (guard restored) | Cases 1/2/4 FAILED, Cases 3/5 OK — exactly the plan's acceptance; then reverted and diff-verified clean — PASS |
| Task 2: format gate final (git-clang-format --diff) | 0 `+` hunks after applying canonical formatting to changed lines — PASS |
| Plan: no tracked-file deletions in either commit; no new untracked files beyond the pre-existing supergenius_* runtime dirs | confirmed — PASS |

## Threat Model Disposition

- **T-17-0301 (Tampering, generalized rung returning a wrong hash — mitigate):** all four binding checks kept verbatim (audited by grep + one-line diff gate); fail-closed proven by NoCertNoValueFailsClosed and Case 2's below-the-frontier assertion.
- **T-17-0302 (Tampering, rung-3/rung-4 divergence — mitigate):** CertRungMatchesCRDTScan asserts the directly-captured cert-rung answer equals the full CRDT-scan answer for the same certified non-registration predecessor.
- **T-17-0303 (Elevation, relaxation widening beyond intended scope — mitigate):** the committed production diff is exactly one guard line; ladder order untouched so the intact-history hot path remains rungs 1-2.
- **T-17-0304 (Spoofing, forged certificate at a slot — accept):** `GetCertificateBySlot` rechecks the slot key and runs ValidateCertificate internally; the tests build certificates through the real signing-manager path (genesis registry -> proposal -> vote -> certificate), no new surface.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Correctness dependency #1 of the first real prune is closed: Phase 18's TxPruner may erase the node's own frontier transaction knowing the nonce chain resolves from `/cert/<addr>:<nonce-1>`.
- The erase-lifecycle seeding recipe (real-topic Put + retire + map entry) is now proven inside the TransactionManager fixture family and is reusable for Phase 18/19 pruned-state tests.
- No blockers. Wave-2 siblings 17-04..17-06 are unaffected (no file overlap).

## Self-Check: PASSED

- Files exist and are modified in this plan's commits: src/transaction/TransactionManager.hpp, src/transaction/TransactionManager.cpp, test/src/account/transaction_manager_certificate_fallback_test.cpp — FOUND
- Commits exist: 9179cc9c2, 8b5355f6e — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
