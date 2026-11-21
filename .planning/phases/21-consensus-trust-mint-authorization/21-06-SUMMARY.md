---
phase: 21-consensus-trust-mint-authorization
plan: "06"
subsystem: auth
tags: [mint-authorization, sync-admission, certificate-conditional-defer, element-filter, stall-resolution, defer-deadline, mint-03, d-02-every-ingress]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-03
    provides: base::BoundedDeferTracker + authorization_defer_window_sec config retained on GeniusNode (consumed verbatim — no new mechanism)
  - phase: 21-consensus-trust-mint-authorization/21-04
    provides: GetCertificateBySlot probing semantics, classified verdict model, grandfathered-Approve durable record shape
  - phase: 21-consensus-trust-mint-authorization/21-05
    provides: TransactionManager::IsValueCarryingLegacyMint one-home predicate (consumed as-is — already the single home T-21-0605 demands)
provides:
  - FilterTransaction classified three-way verdict (Accept/Reject/Stall) with the certificate-conditional v1-mint branch — the third and last MINT-03 ingress
  - Stall-resolution negative control (VALIDATION.md Wave-0 gap closed): a stalled v1 mint provably admits once its certificate delta lands
  - D-03 deadline rejection through the existing proof-key tombstone structure, config-wired via SetLegacyMintDeferWindow from authorization_defer_window_sec
  - Stall/deadline-reject observability counters (GetLegacyMintSyncStallCount/GetLegacyMintSyncDeadlineRejectCount) — the migration zero-stall assertion's observable
  - Producer-path spendability compat: the GeniusInputValidator.cpp:476-512 chain proven end-to-end across the gate for certified v1 history
affects: [21-07-enforcement-tests, TRUST-02-audit, VER-01]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Certificate-conditional admission: since the sibling ingresses (21-05) block new v1 mints from ever obtaining a certificate, certificate-existence exactly separates history from attack — defer on miss, admit on hit, never a blanket type block"
    - "Stall keyed by slot id with Clear-on-admit so resolved history never leaves stale defer state; cert-present-but-mismatched defers fail-closed (existence alone never admits — T-21-0604)"
    - "Whole-delta stall cost accepted by design with the comment mirroring FilterCertificate (Consensus.cpp): stalled elements are stripped unjournaled and re-evaluated when later deltas land"
    - "Behavior-preserving refactor commit before the branch: optional-return -> ElementFilterResult with the wrapper dropping FromOptional (proof: transaction_sync green on the refactor commit alone)"

key-files:
  created:
    - test/src/transaction_sync/legacy_mint_sync_admission_test.cpp
  modified:
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - src/account/GeniusNode.cpp
    - test/src/transaction_sync/migration_sync_test.cpp
    - test/src/transaction_sync/CMakeLists.txt

key-decisions:
  - "21-05's predicate was already the one home (IsValueCarryingLegacyMint) — consumed verbatim, no extraction needed; T-21-0605 satisfied by construction with all three ingresses calling the same static"
  - "The GREEN branch treats a certificate load error and a present-but-mismatched certificate identically (defer -> deadline reject): GetCertificateBySlot Approves-or-errors and CertificateMatchesTransaction is required, so a smuggled mismatch can never ride a shared slot"
  - "Producer-path compat drives the exact :476-512 mechanism (FetchTransaction -> GetCertificateBySlot -> CertificateMatchesTransaction) with a retryable-pre-sync -> end-to-end-success arc instead of a full ValidateWitness kValid: v1 mints carry balance-model value with no UTXO outputs, so the witness half of the mechanism has no v1 surface"
  - "RED commit carries the SetLegacyMintDeferWindow/tracker scaffolding with zero FilterTransaction behavior (21-04's config-surface precedent): 3 adversarial cases fail at RED, 3 compat twins pin existing behavior"
  - "MINT-03 marked complete here: all three ingresses fail-closed for NEW v1 mints (consensus 21-05, preflight 21-05, sync admission this plan) with compat twins for migration fillers, certified history, spendability, fresh-node sync and restart recovery"

patterns-established:
  - "Sync-admission test fixture: CRDTFixture + genesis-authority pin + StoreGenesisRegistry + a durable /cert/<slot> writer embedding the real transaction in the nonce subject (extends the MakeCertificateForRegistry precedent to tx-matching certificates)"

requirements-completed: [MINT-03]

# Metrics
duration: 559min wall (dominated by two full-tree rebuilds after the wide-reach TransactionManager.hpp change and serialized real-socket suite gates)
completed: 2026-10-21
---

# Phase 21 Plan 06: Legacy-v1 Mint Sync Admission Summary

**Certificate-conditional sync admission for value-carrying v1 mints: FilterTransaction now returns a classified Accept/Reject/Stall verdict that defers a v1 mint on the D-03 bounded window until its slot certificate arrives locally (stall provably resolves), rejects with the proof-key tombstone on deadline expiry, admits certified history first-pass, and leaves zero-value migration fillers, mint-v2 and transfers untouched — closing D-02's every-ingress for MINT-03**

## Performance

- **Duration:** 559 min wall clock (2026-10-20T23:20:55Z → 2026-10-21T08:40:32Z); two ~40 min full-tree rebuilds (the header change reaches most TUs) plus serialized multi-node gates dominate
- **Tasks:** 2 (Task 1 TDD: behavior-preserving refactor commit → RED → GREEN; Task 2 compat tests + migration twin)
- **Files modified:** 6 (1 created)

## Accomplishments

- **Classified filter + certificate-conditional branch (Task 1, TDD):** FilterTransaction returns `crdt::CRDTDataFilter::ElementFilterResult` (the tx-pattern wrapper returns it directly; FromOptional stays for the proof/reg/genesis filters). After the signature-authorization check, the branch applies `IsValueCarryingLegacyMint`; for value-carrying v1 it probes `blockchain_->GetCertificateBySlot(slot)` + `CertificateMatchesTransaction` exactly once per evaluation — hit admits certified history through today's checks (tracker Clear), miss/mismatch defers on the 21-03 `BoundedDeferTracker` keyed by slot id: within window `Stall()` (whole-delta stall cost accepted by design, comment mirroring Consensus.cpp's certificate filter), expired first-seen deadline logs the rule-named error ("legacy v1 mint sync admission defer expired (MINT-03)") and Rejects through the existing should_delete/proof-key tombstone structure. Zero-value fillers short-circuit inside the predicate before the probe.
- **Config wiring:** `SetLegacyMintDeferWindow` on TransactionManager, applied at the GeniusNode construction site from the retained `authorization_defer_window_sec_` (D-03 default 1800 s) right after the null check, beside the 21-03/21-04 wiring.
- **Stall-resolution negative control (Wave-0 gap, Task 2):** stalled element re-evaluated after the certificate record lands → Accept, polled through EXPECT_WAIT_FOR_CONDITION. Deadline test uses a 1 s window + real elapse → Reject with the proof-key tombstone asserted present and naming the mint's proof path.
- **Spendability + migration compat twins (Task 2):** the producer-path test drives the exact GeniusInputValidator.cpp:476-512 chain across the gate (retryable pre-sync lookups → end-to-end success after certified admission + storage). `BalanceAfterMigration` now asserts zero v1 sync stalls and zero deadline-rejects on both peers after the real multi-node 0.2.0→3.7.0 migration.

## Task Commits

1. **Task 1: Classified FilterTransaction with certificate-conditional v1 defer (tdd)** — `0efbb75b0` (refactor: ElementFilterResult return + wrapper mapping, behavior-preserving; transaction_sync + legacy_mint green on this commit alone) + `5b9f0c1c4` (test: RED — 3 adversarial fail, 3 compat twins pass; setter/tracker scaffolding only) + `f1f5ad2f0` (feat: GREEN — the branch, tracker integration, GeniusNode window wiring, observability counters)
2. **Task 2: Stall-resolution, deadline, and compat tests** — `62009c922` (test: producer-path spendability compat + migration zero-stall twin; 7/7 suite green)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/transaction/TransactionManager.hpp` — ElementFilterResult return type, SetLegacyMintDeferWindow + counter getters, BoundedDeferTracker member + atomics, LegacyMintSyncAdmissionTestAccess friend, direct crdt_data_filter.hpp/bounded_defer.hpp/atomic includes.
- `src/transaction/TransactionManager.cpp` — classified FilterTransaction with the v1 branch (probe → admit/defer/deadline-reject), stall-cost comment, setter definition; tx-pattern wrapper returns the classified verdict.
- `src/account/GeniusNode.cpp` — applies `authorization_defer_window_sec_` via SetLegacyMintDeferWindow at the TransactionManager construction site.
- `test/src/transaction_sync/legacy_mint_sync_admission_test.cpp` — new target (480 lines ≥ min_lines 120): seven-case matrix with VER-01 comments; fixture with genesis-authority pin, genesis-registry store, and the durable /cert/<slot> writer whose nonce subject embeds the real transaction (CertificateMatchesTransaction-true by construction, slot asserted equal to the mint's).
- `test/src/transaction_sync/migration_sync_test.cpp` — BalanceAfterMigration zero-stall/zero-reject assertion on both peers' managers.
- `test/src/transaction_sync/CMakeLists.txt` — `addtest(legacy_mint_sync_admission_test …)` with the directory's link/whole-archive pattern.

## Decisions Made

- A present-but-mismatched certificate defers like a missing one (then deadline-rejects): `GetCertificateBySlot` Approves-or-errors and the branch additionally requires `CertificateMatchesTransaction`, so a competing transaction on a shared slot can never inherit admission (T-21-0604 — the GeniusInputValidator:506-512 semantics).
- The defer tracker is keyed by the tx slot id and cleared on certified admission — resolved history leaves no stale deadline behind, and the 21-03 first-seen anchoring makes replay unable to extend a holdback.
- The new test suite is not added to the `genius_node_pool` RESOURCE_LOCK set: it is a single-node CRDTFixture suite, matching legacy_mint_admission_test's placement (the lock exists for multi-node-booting suites).
- MINT-03 closes here (see Requirements Status): the two 21-05 gates plus this sync gate cover every ingress, and the compat twins cover the ROADMAP acceptance items (migration fillers, spendability, fresh-node READY, restart recovery via the multi-account/multi-node suites run as gates).

## Requirements Status

`requirements: [MINT-03]` marked COMPLETE. "Legacy-v1 mint reception is disabled via admission gating — never deserialization gating, so boot/sync/history spends keep working": consensus ingress (21-05 CheckTransactionTypeRules), local/RPC submission preflight (21-05 SendTransactionItem, before any CRDT Put), and now CRDT sync admission (certificate-conditional Stall/Reject here) are all fail-closed for NEW value-carrying v1 mints; deserialization, zero-value fillers, certified history, spendability of pre-cutover outputs, fresh-node sync-to-READY and restart recovery are all proven by twins (this plan's seven cases + migration_sync zero-stall + multi_account 10/10 incl. PersistedHistoricalTrustAndTransactionsRestart + multi_node_finality_fault suites).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Test-shape] RED compile scaffolding on the config surface**
- **Found during:** Task 1 RED authoring
- **Issue:** The deadline test calls `SetLegacyMintDeferWindow`, which did not exist.
- **Fix:** The RED commit carries the setter + tracker member plumbing (mechanism configuration on the already-tested 21-03 tracker) with zero FilterTransaction behavior — 3 tests fail at RED, 3 compat guards pass (21-04's precedent).
- **Committed in:** 5b9f0c1c4

**2. [Rule 1 - Test-shape] Producer-path test initially hit the duplicate-delta tombstone path**
- **Found during:** Task 2 first run
- **Issue:** The first draft stored the mint BEFORE filtering; the pre-existing `KeyExistsInDB` check then strips the element as an already-stored duplicate (kReject), not the admission verdict under test.
- **Fix:** Restructured into the delivery-accurate arc — pre-sync retryable lookups (both producer fetch and cert probe fail) → certificate lands → filter Accepts → transaction stored → producer-path chain succeeds end to end.
- **Files modified:** test/src/transaction_sync/legacy_mint_sync_admission_test.cpp
- **Committed in:** 62009c922

**3. [Plan-shape] Spendability compat asserted via the producer-path chain rather than a full ValidateWitness kValid**
- **Found during:** Task 2 design
- **Issue:** The plan's case-3 wording ("GeniusInputValidator kValid on the produced output's witness") assumes v1 mints produce UTXO outputs; MintTransaction v1 carries balance-model value with no UTXO parameters, so no witness surface exists for a v1 producer — a hand-built spending witness cannot reach kValid for reasons unrelated to this plan.
- **Fix:** The compat test drives the exact :476-512 mechanism the plan cites (FetchTransaction → GetCertificateBySlot → CertificateMatchesTransaction — the calls that decide kNotSynced/kInvalid/proceed for any producer) with the retryable-pre-sync → end-to-end-success arc; the test comment documents the balance-model reasoning.
- **Files modified:** test/src/transaction_sync/legacy_mint_sync_admission_test.cpp
- **Committed in:** 62009c922

**4. [Plan-sanctioned] Observability counters added for the migration acceptance**
- **Found during:** Task 2 design
- **Issue:** "Migration filler sync produces zero v1-related stalls/rejects" needs an observable on real booted nodes.
- **Fix:** `GetLegacyMintSyncStallCount`/`GetLegacyMintSyncDeadlineRejectCount` public getters over two atomics incremented in the branch (also the T-21-0602 DoS-observability half-measure); migration_sync asserts both are zero on both peers (21-04 grandfather-counter precedent).
- **Files modified:** src/transaction/TransactionManager.hpp/.cpp
- **Committed in:** f1f5ad2f0

---

**Total deviations:** 2 auto-fixed/test-shape + 1 plan-shape + 1 plan-sanctioned observability
**Impact on plan:** All required by compilability, the plan's own acceptance observability, or honest test shape — no scope creep.

## Issues Encountered

- `multi_account_test` timed out under ctest three times in a row during the wave-merge gate. Investigation: the failing run's full log showed ZERO v1-gate activity ("sync admission deferred"/"defer expired" absent — the branch never fired), the suite's traffic is mint-v2 + registry deltas (provably outside the branch), and the failing case is the registry-CID-fetch wait. The isolated rerun passed in 15 s, the full binary then passed 10/10 (including CRDTFilterDuplicateTx and SyncThroughEachOther — the tests that drive the tx filter directly), and the official ctest record is 2/2 green. Root cause: environmental socket contention from overlapping real-socket runs during debugging (two multi_account instances were briefly concurrent). Not a 21-06 regression; all gates re-verified green serially.
- The build tree's CMake source directory is `build/OSX` (no root CMakeLists.txt); after the CMakeLists edit the tree needed `cmake -S build/OSX -B build/OSX/Release` before the new target resolved.
- clang-tidy (filtered single-arch DB at /tmp/sgns-tidy-db, extended with a legacy_mint_sync_admission_test.cpp entry): zero new Required-group findings on TransactionManager.cpp's branch region, the GeniusNode wiring (only nesting notes attached to the state machine's pre-existing over-threshold finding), and the test file carries the same Recommended-group noise classes as the accepted 21-05 twin (k-constant naming, gtest SetUp visibility, member-const suggestions).
- Test-file compile fixes during RED authoring: wait-helper signatures take a duration POINTER (`&waited`), macro lambdas cannot carry commas in capture lists, and `ConsensusManager::CreateProposalId` is private — the certificate writer replicates it from ProposalSigningBytes + sha2_256 + hex_lower exactly as consensus_pending_lifecycle_test's MakeCertificateForRegistry does.

## Verification

- Task 1 gate `ctest -R "transaction_sync|legacy_mint"` — 3/3 passed (transaction_sync_test full suite, legacy_mint_admission, legacy_mint_sync_admission) on the refactor commit AND on the final tree.
- Task 2 gate `ctest -R "legacy_mint|migration_sync|transaction_sync"` — legacy_mint_sync_admission 7/7, migration_sync_test green (90 s, zero-stall assertions active), transaction_sync green.
- TDD gates: RED `5b9f0c1c4` (3 fail / 3 pass) precedes GREEN `f1f5ad2f0` (6/6); refactor commit `0efbb75b0` precedes RED with suites green and zero behavior change.
- Wave-merge compat gates: `multi_node_finality_fault` suites 4/4, `multi_account_test` 10/10 via direct run and 2/2 via ctest record with `policy_lifetime_multi_account_test`, `multi_node_finality_fault_compatibility_smoke_test` green through the GeniusNode wiring.
- clang-format `--dry-run --Werror` clean on all five changed C++ files; clang-tidy zero new Required-group findings (see Issues Encountered).
- Acceptance greps: `IsValueCarryingLegacyMint` consumed (not duplicated) in the filter; `GetCertificateBySlot` + `CertificateMatchesTransaction` probed once per v1 evaluation; `SetLegacyMintDeferWindow` applied from `authorization_defer_window_sec_` at the construction site; wrapper returns FilterTransaction's verdict directly (no FromOptional on the tx pattern).

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - the defer, deadline, admission, and compat paths are all live production filter behavior; no placeholder data.

## Threat Flags

None - all five `mitigate` dispositions implemented and test-proven (T-21-0601 certificate-conditional defer — a new v1 mint can never present a certificate, so it converges to deadline rejection; T-21-0602 D-03 bounded window + FIFO cap + only value-carrying v1 stalls + observability counters; T-21-0603 certified v1 admitted on probe hit with the stall-resolution negative control and the multi-node/migration/restart suites green; T-21-0604 CertificateMatchesTransaction required, mismatch defers fail-closed; T-21-0605 the one-home predicate consumed, zero copies). No new security surface beyond the plan's model.

## Next Phase Readiness

- 21-07's enforcement tests can assert the sync-gate counters (GetLegacyMintSyncStallCount/DeadlineRejectCount) and reuse the durable-cert-writer fixture for multi-node MINT-03 pairs.
- TRUST-02's audit should note: the sync gate's stall is the same whole-delta cost the certificate filter already accepts, and its deadline-reject is first-seen anchored (replay cannot extend holdback) — the D-03 memoization soundness argument carries over.
- VER-01: the seven-case matrix (plus migration twin) completes Phase 21's in-phase adversarial+compat obligation for the mint domain.

## Self-Check: PASSED

All 6 key files exist on disk; all 4 task commits (0efbb75b0, 5b9f0c1c4, f1f5ad2f0, 62009c922) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-21*
