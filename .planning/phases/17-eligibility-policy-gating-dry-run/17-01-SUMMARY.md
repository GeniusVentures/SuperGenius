---
phase: 17-eligibility-policy-gating-dry-run
plan: 01
subsystem: database
tags: [utxo, spent-by-txid, spend-recency, rocksdb, pruning, c++17]

# Dependency graph
requires:
  - phase: 16-erasure-primitives-bookkeeping-hardening
    provides: D-C2 genuine-spend discriminator (IsOutPointGenuinelyConsumed) this carrier pairs with
provides:
  - spent_by_txid populated at ConsumeUTXOs on both branches (D-02 Candidate-B spend-recency carrier)
  - Public UTXOManager::GetOutPointSpender(outpoint) read accessor for the 17-04 eligibility evaluator
  - All four production spend paths (transfer, migration, bridge mint-v2, escrow) recording their spender hash
affects: [17-eligibility-policy-gating-dry-run (17-04 window evaluator), 18-txpruner]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Trailing-default optional parameter (consuming_txid) extends a hot signature without touching any other caller"
    - "Uniform field population on both consume branches (genuine + synthesized tombstone) with the discriminator, not the field, excluding tombstones from eligibility (D-C2)"
tech-patterns: []

key-files:
  created: []
  modified:
    - .planning/REQUIREMENTS.md
    - src/account/UTXOManager.hpp
    - src/account/UTXOManager.cpp
    - src/transaction/TransactionManager.cpp
    - test/src/account/utxo_manager_test.cpp

key-decisions:
  - "spent_by_txid is the D-02 Candidate-B spend-recency carrier, populated uniformly on both ConsumeUTXOs branches; spent_epoch stays dead (D-02 forbids wall-clock semantics)"
  - "GetOutPointSpender mirrors IsOutPointGenuinelyConsumed (shared_lock, copy return) as the carrier read side; synthesized tombstones carry a spender yet remain non-genuine (D-C2)"
  - "Call sites convert GetHash() inline via base::Hash256::fromReadableString(...).value() (existing :2669 precedent) so the spender expression stays inside the ConsumeUTXOs statement — gate-visible"

patterns-established:
  - "Spend-recency carrier contract: populate at the outpoint-lifecycle owner (UTXOManager), read via public accessor, revert resets symmetrically"

requirements-completed: [PRUNE-01]  # Contribution only — PRUNE-01's window logic lands in 17-04; tick at phase close, not here (see Decisions)

# Metrics
duration: 18min
completed: 2026-10-19
---

# Phase 17 Plan 01: D-02 Candidate-B Spend-Recency Carrier (spent_by_txid) Summary

**spent_by_txid now records the consuming transaction's hash at every production spend (4 call sites), survives bucket rewrite + restart, and is readable per outpoint via the new UTXOManager::GetOutPointSpender accessor — the only spend-time signal the Phase-17 eligibility window can read**

## Performance

- **Duration:** 18 min
- **Started:** 2026-10-19T13:53:33Z
- **Completed:** 2026-10-19T14:13:07Z
- **Tasks:** 3
- **Files modified:** 5

## Accomplishments
- `ConsumeUTXOs` extended with a trailing-default `consuming_txid` parameter; `spent_by_txid` is populated on the genuine-found branch AND uniformly on the synthesized zero-amount tombstone branch (`spent_epoch` stays nullopt everywhere — D-02 forbids wall-clock)
- All four production call sites (TransferTransaction :2637, MigrationTransaction :2657, MintTransactionV2 bridge :2702, EscrowTransaction :2871) pass the spending transaction's hash; every other caller (header-inline forwarder, tests) compiles unchanged via the trailing default
- New public `GetOutPointSpender(utxo_id, output_idx)` accessor (shared_lock + copy return, mirroring `IsOutPointGenuinelyConsumed`) — the read side the 17-04 evaluator consumes
- REQUIREMENTS.md out-of-scope row amended per D-02's pre-authorized conditional: Candidate A failed verification (CONSUMED entries are raw `/utxo/` RocksDB puts outside the CRDT namespace — no priority exists), so `spent_by_txid` is now the in-scope spend-recency carrier while `spent_epoch` stays excluded
- Four new unit tests prove populate / synthesized-but-not-genuine (D-C2 negative assertion) / revert-reset / bucket-rewrite+restart round-trip; full 34-test suite green twice consecutively on fresh DBs

## Task Commits

Each task was committed atomically:

1. **Task 1: Amend REQUIREMENTS.md out-of-scope row per D-02 Candidate B** - `2086039f7` (docs)
2. **Task 2: Populate spent_by_txid at ConsumeUTXOs + GetOutPointSpender + 4 call sites** - `d756a3733` (feat)
3. **Task 3: Candidate-B unit tests (populate, synthesized, revert-reset, round-trip)** - `91194dd9c` (test)

## Files Created/Modified
- `.planning/REQUIREMENTS.md` — one out-of-scope row rewritten (spent_by_txid in scope as D-02 Candidate-B carrier; spent_epoch stays dead) + footer amendment note
- `src/account/UTXOManager.hpp` — ConsumeUTXOs 4th parameter with Doxygen; GetOutPointSpender declaration with Doxygen
- `src/account/UTXOManager.cpp` — spent_by_txid population on both consume branches; GetOutPointSpender implementation (shared_lock)
- `src/transaction/TransactionManager.cpp` — all four call sites pass `base::Hash256::fromReadableString(<tx>->GetHash()).value()`
- `test/src/account/utxo_manager_test.cpp` — SpentByTxid test group (+128 lines: 4 TEST_F cases)

## Decisions Made
- **PRUNE-01 NOT ticked in REQUIREMENTS.md by this plan.** The plan frontmatter carries the phase requirement, but 17-01 delivers only the carrier, not PRUNE-01's window semantics (genuinely-consumed AND depth window — that is 17-04's evaluator). Phase-16 precedent: REQUIREMENTS checkboxes are ticked at phase close (`docs(phase-16): evolve ... traceability after completion`), not per plan. `requirements.mark-complete` deliberately not run.
- Spender conversion is inline in the call (`fromReadableString(...).value()`) rather than a pre-extracted variable, following the existing `:2669` mint-site precedent and keeping the spender expression inside the `ConsumeUTXOs` statement — the plan's awk statement-scan gate and the four-way key_link are satisfied syntactically at the call itself.
- Verified-by-reading (no edits): `RestoreConsumedUTXOs` (:388-392) resets both fields, `StoreUTXOSnapshot` (:1052-1056) serializes them, `LoadUTXOs` (:936-943) restores them — the persistence/restore/reset plumbing was already symmetric; zero persistence work needed, exactly as the plan stated.

## Deviations from Plan

### Documented protocol notes (no auto-fixes were needed)

**1. [TDD-ordering note] Task 3 RED phase vacuous by plan design**
- **Found during:** Task 3 (tdd="true")
- **Issue:** A strict RED commit (failing tests before implementation) is unachievable — Task 3's tests reference `GetOutPointSpender` and the 4-arg `ConsumeUTXOs`, which only exist after Task 2, and the plan's own task order mandates the Task 2 commit first. The tests cannot compile against the pre-Task-2 tree, so "write failing tests first" has no compilable target.
- **Resolution:** Followed plan task order (implementation → tests), single `test(17-01)` commit. Non-vacuity is carried by the assertions themselves: test 2's negative assertion fails if tombstones stop carrying the spender; test 4 fails if persistence drops the field.
- **Impact:** None on correctness or gates; the plan's Task 3 `<verify>` (build + filtered run + full-suite double-run + format) all pass.

---

**Total deviations:** 0 auto-fixed (1 protocol note documented)
**Impact on plan:** None — plan executed exactly as written.

## Verification Log

| Gate | Result |
|------|--------|
| Task 1: `grep -c "Candidate B" REQUIREMENTS.md` / `grep -c "spent_epoch"` | 2 / 2 — PASS |
| Task 1: diff shape | exactly one table row rewritten + one footer line — PASS |
| Task 2: `cmake --build ... --target genius_transaction_objs genius_node` | exit 0, zero warnings on recompiled changed TUs — PASS |
| Task 2: `grep -c GetOutPointSpender src/account/UTXOManager.hpp` | 1 (declaration) — PASS |
| Task 2: awk statement-scan spender gate on TransactionManager.cpp | n==4 exactly — PASS |
| Task 2: `spent_by_txid` writer inventory in src/ | populate (:291 ConsumeUTXOs), reset (:392 RestoreConsumedUTXOs), struct field (hpp:85), read (:796 GetOutPointSpender) + pre-existing fresh-entry init (PutUTXO :179) and persist/restore (:936-943, :1052-1056) — no NEW writer — PASS |
| Task 2: format gate (git-clang-format --diff, changed lines) | "clang-format did not modify any files" — PASS |
| Task 2: tidy gate (clang-tidy 22, single-arch filtered db, 2 production TUs) | 0 Required-group (bugprone/cert/cppcoreguidelines) findings on added lines — PASS |
| Task 3: build utxo_manager_test | exit 0 — PASS |
| Task 3: `--gtest_filter='*Spender*:*SpentByTxid*'` | 4/4 PASSED — PASS |
| Task 3: full suite double-run | 34/34 PASSED twice — PASS |
| Task 3: format gate on test file added lines | no `+` hunks — PASS |
| Plan: REQUIREMENTS amendment committed before code | 2086039f7 (docs) precedes d756a3733 (feat) — PASS |

Note on the must_haves `key_links` regex `ConsumeUTXOs\([^;]*GetHash\(\)`: single-line grep cannot match the intentionally multi-line call sites; the plan's authoritative awk statement-scan gate (tolerates the multi-line :2702 call) proves the link with n==4.

## Threat Model Disposition

- **T-17-0101 (Tampering, consuming_txid parameter — accept):** parameter originates from the in-scope transaction object at every call site (all four verified this session).
- **T-17-0102 (Repudiation, spent_by_txid record — mitigate):** field persisted in the durable UTXO record and restored at boot — proven by Task 3 test 4 (bucket rewrite + fresh-manager LoadUTXOs).
- **T-17-0103 (Tampering, GetOutPointSpender — mitigate):** `std::shared_lock( utxos_mutex_ )` mirroring `IsOutPointGenuinelyConsumed`; returns a copy, never a reference into the map.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- The spend-recency carrier is live and readable: 17-04's eligibility evaluator can call `GetOutPointSpender(outpoint)` per output and resolve the spender's DAG priority (with the Wave-0 `CrdtSet::GetPriority` passthrough).
- REQUIREMENTS.md no longer claims `spent_by_txid` is dead schema; `spent_epoch` remains excluded (wall-clock-adjacent).
- No blockers. Plans 17-02..17-06 can proceed per wave order.

## Self-Check: PASSED

- Files exist: src/account/UTXOManager.hpp, src/account/UTXOManager.cpp, src/transaction/TransactionManager.cpp, test/src/account/utxo_manager_test.cpp, .planning/REQUIREMENTS.md — FOUND (verified below the summary body)
- Commits exist: 2086039f7, d756a3733, 91194dd9c — FOUND in git log
- All task acceptance criteria re-run and passing (see Verification Log)

---
*Phase: 17-eligibility-policy-gating-dry-run*
*Completed: 2026-10-19*
