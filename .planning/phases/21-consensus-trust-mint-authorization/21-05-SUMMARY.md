---
phase: 21-consensus-trust-mint-authorization
plan: "05"
subsystem: auth
tags: [mint-authorization, admission-gating, legacy-v1-mint, consensus-ingress, submission-preflight, fail-closed]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-02
    provides: mint-type taxonomy context (RequiresPublicChainEvidence); this plan gates admission one layer earlier, at the type-rule/preflight ingresses
provides:
  - TransactionManager::IsValueCarryingLegacyMint — the one-home MINT-03 admission predicate (type "mint" AND amount > 0), public static, ready for 21-06's sync ingress
  - Consensus-ingress gate — CheckTransactionTypeRules non-UTXO branch rejects value-carrying v1 mints with the rule named in the error log; the ValidateTransactionForConsensus TODO resolved
  - Submission preflight gate — SendTransactionItem rejects value-carrying v1 mints FIRST (before serialization, before any CRDT Put) with std::errc::operation_not_permitted
  - legacy_mint_admission_test — six-case adversarial+compat matrix incl. the CRDT-absence proof for the preflight rejection
affects: [21-06-mint-sync-ingress, TRUST-02-audit, VER-01]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Admission gating, never deserialization gating (D-02): the parser map stays byte-identical; the gate lives at the two synchronous ingresses only"
    - "One-home predicate shared by every ingress (IsValueCarryingLegacyMint) — the verdict sites log the rule name, the classification lives once"
    - "Zero-amount exemption encoded in the predicate itself so no ingress can forget it (Migration3_4_0To3_5_0 nonce fillers keep certifying)"
    - "Preflight rejection ordered before serialization and any CRDT Put so a rejected batch leaves no partial publish (SIZE-01 shape)"

key-files:
  created:
    - test/src/transaction_sync/legacy_mint_admission_test.cpp
  modified:
    - src/transaction/TransactionConsensusHandler.cpp
    - src/transaction/TransactionConsensusHandler.hpp
    - src/transaction/TransactionManager.cpp
    - src/transaction/TransactionManager.hpp
    - test/src/transaction_sync/CMakeLists.txt

key-decisions:
  - "Predicate factored into TransactionManager::IsValueCarryingLegacyMint instead of inlining type+amount at both ingresses: CLAUDE.md hard invariant 2 (second use of the same domain knowledge moves to one home); both verdict sites and 21-06's sync ingress consume it"
  - "The gate sits AFTER the UTXO branch in CheckTransactionTypeRules: UTXO-carrying types (mint-v2, transfer, escrow) keep a byte-identical code path; only the non-UTXO fall-through that previously returned true unconditionally can now return false"
  - "MINT-03 NOT marked complete: ROADMAP acceptance item 4 also requires a fresh node syncing to READY, restart recovery and pre-cutover spendability under the cutover — those multi-node twins and the sync-admission ingress are 21-06 scope"
  - "mint-v2 compat asserts type rules TRUE (not merely unchanged): PublicChainInputValidator accepts explicit input references without local ownership checks by design, so an unfunded v2 construction passes type rules today and must keep passing"

patterns-established:
  - "Friend test-access (LegacyMintAdmissionTestAccess) for private admission surfaces across the manager/handler pair, following CertificateFallbackTestAccess"

requirements-completed: []  # MINT-03 closes in 21-06 when the sync-admission ingress + multi-node compat twins land — see Requirements Status

# Metrics
duration: 35min
completed: 2026-10-20
---

# Phase 21 Plan 05: Legacy-v1 Mint Admission Gates Summary

**Value-carrying legacy-v1 mints (type "mint", amount > 0) are now rejected at both synchronous ingresses — consensus type rules and local/RPC submission preflight (named errc, before any CRDT Put) — through one shared predicate, with zero-amount migration fillers, mint-v2 and non-mint traffic proven unaffected**

## Performance

- **Duration:** 35 min (2026-10-20T21:06:56Z → 2026-10-20T21:43:00Z)
- **Tasks:** 3 (TDD: RED → GREEN for Tasks 1 and 2; completion commit for Task 3; plus two behavior-neutral format-normalization prerequisite commits)
- **Files modified:** 6 (1 created)

## Accomplishments

- **Consensus ingress (Task 1, T-21-0501):** `CheckTransactionTypeRules`' non-UTXO branch — the exact `return true;` slip at the old :818 — now applies `TransactionManager::IsValueCarryingLegacyMint` and returns false with a rule-naming error log ("legacy v1 mint admission disabled (MINT-03)"). The `ValidateTransactionForConsensus` TODO that preceded the call is resolved with a comment documenting the rule. UTXO-carrying types never reach the new branch.
- **Submission preflight (Task 2, T-21-0502):** `SendTransactionItem`'s pre-pubSub validation loop rejects value-carrying v1 mints as its FIRST per-transaction check — before `SerializeToEmbeddedTransaction`, the SIZE-01 limit and any CRDT Put/commit — returning `std::errc::operation_not_permitted` (named errc per the message_size/invalid_argument precedent). The RPC submission path routes through this preflight, closing the local+RPC ingress.
- **One-home predicate:** `IsValueCarryingLegacyMint` is a public static on TransactionManager (beside `CheckTransactionAuthorization`): type string "mint" + `dynamic_cast<const MintTransaction*>` + `GetAmount() > 0`. Zero-amount v1, "mint-v2", "migration" and every non-mint type return false. 21-06's sync ingress consumes the same predicate.
- **Test matrix (Task 3):** six cases in `legacy_mint_admission_test` — adversarial consensus-ingress (full-chain Reject + type-rules false), adversarial preflight (errc asserted by value + the tx/ path proven absent from the DB after rejection), zero-value full-chain Approve (migration-filler twin), mint-v2 type-rules TRUE (T-21-0504), transfer verdict unchanged, zero-value/v2 preflight takes today's path. Every case carries VER-01 comments naming attacker authority, expected rejection and pre-fix behavior. Fixture: CRDTFixture + real GeniusAccount/Blockchain/TransactionManager (no GeniusNode), the smallest shape that reaches both private surfaces.

## Task Commits

1. **Task 1: Consensus-ingress v1-mint admission gate (tdd)** — `900f4b3cd` (style: behavior-neutral clang-format normalization of TransactionManager) + `a626e6f0c` (test: RED — ingress adversarial case fails, three compat twins pass) + `a7d77179e` (style: same normalization for TransactionConsensusHandler) + `9b70f99f9` (feat: GREEN — the type-rules gate + TODO resolution)
2. **Task 2: Local/RPC submission preflight rejection (tdd)** — `90e5d912f` (test: RED — preflight adversarial fails, path-compat twin passes) + `3b2a50bf0` (feat: GREEN — operation_not_permitted first check)
3. **Task 3: Unit adversarial + compat tests** — `2b27e3e9b` (test: CRDT-absence proof completing the five-group matrix; groups 1-5 shipped with the RED commits)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/transaction/TransactionConsensusHandler.cpp` — MINT-03 gate in the non-UTXO branch of `CheckTransactionTypeRules`; TODO replaced with the rule reference; whole-file clang-format normalization (a7d77179e).
- `src/transaction/TransactionConsensusHandler.hpp` — forward declaration + `friend class LegacyMintAdmissionTestAccess` for the private validation surfaces.
- `src/transaction/TransactionManager.cpp` — `IsValueCarryingLegacyMint` implementation; preflight rejection branch in `SendTransactionItem`; whole-file clang-format normalization (900f4b3cd).
- `src/transaction/TransactionManager.hpp` — friend declaration (protected block, beside the other test-access friends) + public static predicate declaration with Doxygen contract (zero-amount/mint-v2/migration never match; admission-only per D-02).
- `test/src/transaction_sync/legacy_mint_admission_test.cpp` — new target (299 lines): six-case matrix, VER-01 comments, CRDTFixture-based fixture with the certificate-fallback teardown invariant.
- `test/src/transaction_sync/CMakeLists.txt` — `addtest(legacy_mint_admission_test …)` + link (`genius_node_test`, `json_secure_storage`, `base_crdt_test`) + whole-archive link options matching the directory pattern.

## Decisions Made

- The plan's action text said "the same predicate" at both ingresses; CLAUDE.md hard invariant 2 (never duplicate domain knowledge — the second use moves to a shared owner) makes that literal: one public static predicate, two verdict sites that only log and act. This also hands 21-06 the classification it needs without re-deriving it.
- The consensus gate logs the specific rule inside the predicate site (`CheckTransactionTypeRules`) and keeps the chain's generic "Type rules failed" line — matching the house style where every other predicate in the chain logs its specific reason.
- `dynamic_cast<const MintTransaction*>( &tx )` follows the neighboring type-specific cast style (`CheckParentChildAuthority`'s RevokeTransaction cast). A "mint"-typed object that fails the cast is not classified as a value-carrying legacy mint — the type string alone never rejects.
- MINT-03 stays open (see Requirements Status); the plan's own objective scopes this plan to the two synchronous ingresses and defers the sync-admission ingress to 21-06.

## Requirements Status

`requirements: [MINT-03]` in the plan frontmatter is intentionally NOT marked complete:

- **MINT-03** ("Legacy-v1 mint reception is disabled … via admission gating — never deserialization gating, so boot/sync/history spends keep working") — this plan closed the consensus-ingress and submission-preflight halves with unit compat twins. "Reception" also covers the sync path (a v1 mint arriving from a peer), and ROADMAP acceptance item 4 additionally requires a fresh node syncing to READY, restart recovery and pre-cutover spendability proofs — all 21-06 scope (its plan row: "MINT-03 sync admission: certificate-conditional Stall/Reject … migration/spendability compat twins"). Marking complete now would falsify the REQUIREMENTS.md traceability row, exactly as TRUST-01 was held for 21-04 in 21-01.

## Deviations from Plan

### Auto-fixed Issues

**1. [Change discipline] Format-normalization split into two prerequisite style commits**
- **Found during:** Task 1 RED formatting pass
- **Issue:** TransactionManager.{cpp,hpp} and TransactionConsensusHandler.cpp carried pre-existing clang-format drift; a whole-file format pass mixed ~340 lines of behavior-neutral churn into the admission diff (violates "refactor first, then change").
- **Fix:** `900f4b3cd` and `a7d77179e` carry the behavior-neutral normalizations alone; the behavioral commits (`a626e6f0c`…`3b2a50bf0`) are minimal. Same discipline as 7a532a05c in 21-03.
- **Files modified:** src/transaction/TransactionManager.cpp/.hpp, src/transaction/TransactionConsensusHandler.cpp
- **Committed in:** 900f4b3cd / a7d77179e

**2. [CLAUDE.md invariant 2] Predicate factored to one home instead of inlined twice**
- **Found during:** Task 1 implementation design
- **Issue:** The plan's key_links sketch the type/amount predicate inline at both TCH.cpp and TM.cpp; two copies of the same domain rule violate the never-duplicate invariant.
- **Fix:** `TransactionManager::IsValueCarryingLegacyMint` (public static, beside CheckTransactionAuthorization) consumed by both ingresses and available to 21-06's sync ingress. Frontmatter artifact greps still hold: "legacy" in TCH.cpp, "operation_not_permitted" in TM.cpp (log + return at the verdict sites).
- **Files modified:** src/transaction/TransactionManager.hpp/.cpp, src/transaction/TransactionConsensusHandler.cpp
- **Committed in:** a626e6f0c / 9b70f99f9 / 3b2a50bf0

**3. [Rule 1 - Test-shape] mint-v2 compat expectation corrected to TRUE**
- **Found during:** Task 1 RED run
- **Issue:** First draft asserted an unfunded mint-v2 keeps a false type-rules verdict (assumed GeniusInputValidator semantics); RED exposed that a v2 with a public chain id routes to PublicChainInputValidator, which accepts explicit input references without local ownership checks by design — v2 passes type rules today.
- **Fix:** Compat twin asserts TRUE ("mint-v2 must keep passing type rules") with the routing documented in the comment — the stronger, plan-literal claim (plan case 4: "a mint-v2 construction passes type rules").
- **Files modified:** test/src/transaction_sync/legacy_mint_admission_test.cpp
- **Committed in:** a626e6f0c

**4. [Rule 3 - Blocking] Friend test-access required for the private surfaces under test**
- **Found during:** Task 1 RED authoring
- **Issue:** ValidateTransactionForConsensus/CheckTransactionTypeRules (handler) and SendTransactionItem (manager) are private; the plan's behavior block targets them directly.
- **Fix:** `LegacyMintAdmissionTestAccess` friend class in both headers, following the established CertificateFallbackTestAccess pattern (nine precedents in TransactionManager.hpp alone).
- **Files modified:** src/transaction/TransactionConsensusHandler.hpp, src/transaction/TransactionManager.hpp
- **Committed in:** a626e6f0c

---

**Total deviations:** 3 auto-fixed + 1 change-discipline split
**Impact on plan:** All required by repo change discipline, the DRY invariant, or the plan's own compat contracts — no scope creep.

## Issues Encountered

- clang-tidy cannot consume this tree's dual-arch `compile_commands.json` (21-01/21-02/21-03 methodology): used the filtered single-arch DB at /tmp/sgns-tidy-db, extended with a `legacy_mint_admission_test.cpp` entry modeled on transaction_crash_test.cpp. Zero findings on all new lines in TransactionManager.cpp, TransactionConsensusHandler.cpp and the test file; the files carry heavy pre-existing Recommended-group noise (18k+ findings in TransactionManager.cpp) that stays out of scope, and the only new-code mention anywhere is cognitive-complexity nesting notes attached to SendTransactionItem's pre-existing over-threshold finding.
- Full-build warnings observed (price_manager_test -Wmissing-field-initializers, HttpStubServer unused-lambda-capture) are pre-existing in untouched files — out of scope per the phase's standing notes.

## Verification

- `ctest -R "legacy_mint_admission|blockchain"` (Task 1 gate) — 2/2 passed.
- `ctest -R "legacy_mint_admission|transaction_sync"` (Task 2 gate) — 2/2 passed (transaction_sync_test full suite, 111 s).
- `ctest -R legacy_mint_admission` (Task 3 gate) — 6/6 cases passed; file is 299 lines (min_lines 100).
- Migration/boot-path regression through the changed send/consensus code: `migration_sync_test` (the zero-value filler compat proof through real multi-node migration boots), `transaction_crash_test`, `genius_node_bootstrap_reconnect_test`, `blockchain_genesis_test`, `multi_node_finality_fault_compatibility_smoke_test`, `bridge_race_fault_rpc_test` (full 11-node mint-v2 pipeline through the changed preflight) — 6/6 passed.
- clang-format clean (`--dry-run --Werror`) on all five changed C++ files; clang-tidy zero findings on new lines (see Issues Encountered).
- Acceptance greps: `IsValueCarryingLegacyMint` called in CheckTransactionTypeRules' non-UTXO branch and as the first SendTransactionItem preflight check; `transaction_parsers` map byte-identical (git diff of RED/GREEN commits shows zero changes in the parser region); the :545 TODO no longer exists.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - both gates are real verdicts on the real ingresses; no placeholder data.

## Threat Flags

None - all four `mitigate` dispositions implemented and test-proven (T-21-0501 consensus-ingress gate with adversarial full-chain Reject; T-21-0502 preflight named-errc fail-closed before any CRDT Put with the DB-absence proof; T-21-0503 zero-amount exemption + deserialization untouched + migration_sync compat; T-21-0504 exact type-string+amount predicate with the mint-v2 compat twin). No new security surface beyond the plan's model.

## Next Phase Readiness

- 21-06 consumes `IsValueCarryingLegacyMint` for the sync-admission ingress (certificate-conditional defer on the 21-03 defer window) and ships the multi-node migration/spendability/restart twins that close MINT-03.
- The preflight gate ordering (before SIZE-01) means a v1 mint mixed into any batch fails the whole item before partial publish — the same batch atomicity SIZE-01 established.

## Self-Check: PASSED

All 6 key files exist on disk; all 7 task commits (900f4b3cd, a626e6f0c, a7d77179e, 9b70f99f9, 90e5d912f, 3b2a50bf0, 2b27e3e9b) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-20*
