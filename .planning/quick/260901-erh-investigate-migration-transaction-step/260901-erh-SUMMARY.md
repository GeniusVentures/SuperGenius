---
phase: quick-260901-erh
plan: 01
subsystem: migration
tags: [migration, consensus, mainnet-readiness, migration-allowlist]
requires: []
provides:
  - "Trigger/lifecycle map of Migration3_6_0To3_7_0 with file:line evidence"
  - "First-real-use risk verdicts for the migration-transaction step"
affects:
  - src/account/GeniusNode.cpp
key-files:
  modified:
    - src/account/GeniusNode.cpp
decisions:
  - "timed_out migration failure now schedules a node-level migration retry (same mechanism as BLOCKCHAIN_INIT_FAILED)"
  - "Missing-allow-list-entry -> Pending() patch rejected: entries never propagate, Pending cannot converge and weakens overclaim rejection"
  - "Allow-list network visibility declared a design-level pre-release decision (F-02), not a small fix"
metrics:
  duration: 21min
  completed: 2026-09-01
---

# Quick Task 260901-erh: Migration3_6_0To3_7_0 first-real-use investigation Summary

## Headline Verdict

**ADJUST** — one small liveness fix applied (retry on claim-confirmation `timed_out`, commit `4084545d`, `migration_sync_test` green), plus one design-level gap (F-02) that MUST be consciously resolved before mainnet 3.7.0 ships: migration allow-list entries never propagate across nodes, so a validator set that did not itself migrate from 3.5.x can never approve any migration claim.

## Findings Table

| ID | Finding | Severity | Verdict | Evidence (path:line) |
|----|---------|----------|---------|----------------------|
| F-01 | A migration claim that misses its 4-minute confirmation window returns `std::errc::timed_out`, which the `MigrateDatabase` callback does not retry — the node wedges in `MIGRATING_DATABASE` forever (no state advance, services never start, `ShutDown` skipped); only manual restart recovers, and restart can then fail deterministically (F-03) | HIGH | **ADJUST — fixed** (commit `4084545d`: timed_out now schedules `ScheduleMigrationRetry`) | src/migration/Migration3_6_0To3_7_0.cpp:328-336; src/account/GeniusNode.cpp:627-639 (pre-fix), 1935-1947; src/migration/MigrationManager.cpp:116 |
| F-02 | Allow-list writes go to the raw RocksDB handle (`GlobalDB::GetDataStore()`), never through the CRDT delta path — entries are strictly node-local and never propagate; remote validation reads the validator's own raw store, so a missing entry (always, on fresh genesis validators) rejects the claim; a claim certifies only if quorum-weight validators each observed the balance in their own legacy DB | HIGH | **ADJUST — design follow-up (NOT small-fixable; no consensus/sync semantics may change in a quick task)** | src/crdt/globaldb/globaldb.hpp:159; src/account/MigrationAllowList.cpp:35-50,92-96; src/migration/Migration3_6_0To3_7_0.cpp:264; src/account/TransactionManager.cpp:3806-3823; src/crdt/impl/crdt_datastore.cpp:1330-1341; src/blockchain/Consensus.cpp:1355-1380 |
| F-03 | If a first claim confirms after its run's 4-minute window, every retry re-submits a new claim with the same synthetic one-time outpoint and is rejected as an input conflict — the node can never write its version marker or reach READY (funds migrated, node stuck); `Apply()` has no "already claimed" check | MED | NO-ADJUST now — follow-up: skip re-claim when a confirmed migration claim for (from_version, address, token) exists | src/migration/Migration3_6_0To3_7_0.cpp:321-347; src/account/TransactionManager.cpp:3793-3797, 3386-3431 |
| F-04 | Double-mint protection is sound on all retry orderings: deterministic synthetic outpoint + witness binding + confirmed-input-conflict rejection; two pending claims resolve to at most one certificate | (positive) | NO-ADJUST | src/account/MigrationTransaction.cpp:199-226; src/account/MigrationInputValidator.cpp:56-117; src/account/TransactionManager.cpp:3793 |
| F-05 | Candidate patch "missing allow-list entry -> Pending()" rejected: Pending cannot converge (nothing propagates the entries; 3-minute TTL then expiry) and it would stall genuinely ineligible overclaims for 3 minutes instead of rejecting them | MED | NO-ADJUST (candidate rejected) | src/account/TransactionManager.cpp:3819-3822; src/blockchain/Consensus.hpp:206-216; src/blockchain/Consensus.cpp:1004-1029 |
| F-06 | Balance reconstruction covers every 3.5.x value-carrying type (transfer, mint, escrow-hold, escrow-release via UTXO params; mint v1 via cast); the `/utxo/` snapshot branch is dead code for 3.5.x (3.5.20 kept UTXOs in memory only); the dropped "process" type carried no value | LOW | NO-ADJUST | src/migration/Migration3_6_0To3_7_0.cpp:386-524; git show TestNet-Phase-3.520:src/account/UTXOManager.hpp:104-105; src/account/TransactionManager.cpp:1703-1708; src/migration/Migration3_5_0To3_6_0.cpp:109-176 |
| F-07 | Policy risks to publish, not code defects: (1) balances held in unreleased escrow locks at upgrade are not migrated (lock addresses are unclaimable by design); (2) reconstruction trusts the unfinalized 3.5.x ledger — losing double-spend outputs still credit recipients; the 2x allow-list cap and validator-side observation are the only bounds | MED | NO-ADJUST (documented policy risk) | src/migration/Migration3_6_0To3_7_0.cpp:50-53,471-482; src/account/UTXOStructs.cpp:22-30; src/account/MigrationAllowList.cpp:98-102 |
| F-08 | No `node_type_` gate in `Apply()`; Light nodes claim from their own-account view and are bounded by the remote allow-list — correct, and gating them out would strand their funds | LOW | NO-ADJUST | src/migration/Migration3_6_0To3_7_0.cpp:137-351; src/account/NodeType.hpp:39-48 |

## Recommendation (priority order)

1. **Shipped in this task (F-01):** `timed_out` migration failures now schedule the same 5-second migration retry as `BLOCKCHAIN_INIT_FAILED` (src/account/GeniusNode.cpp, commit `4084545d`). This converts a permanent silent wedge into a self-healing retry on any network that can eventually certify the claim. `migration_sync_test` passed (87.4 s) after the change; the change is main-repo-only, 9 lines, one file, no consensus semantics touched.
2. **Required decision before mainnet 3.7.0 launches (F-02):** ensure the launch validator set can approve migration claims. Either (a) compose the initial validator registry from 3.5.20 upgraders whose (3,6) DBs contain the full legacy ledger (all 3.5.x nodes were full replicators, so their reconstructed allow-lists converge), or (b) make allow-list observations network-visible — CRDT-publish the entries or seed them into the genesis/trust state. If fresh genesis validators dominate the registry, every migration claim is rejected (`migration source address not locally eligible`) and no 3.5.x upgrader with a balance can ever complete migration. This is a design change (new CRDT schema or genesis ceremony content) and was deliberately not implemented here.
3. **Follow-up engineering (F-03):** before submitting a migration claim, `Apply()` should detect an already-confirmed claim for the same (from_version, source address, token) and write the version marker instead of re-claiming. Needs a TransactionManager query API (3+ files) — out of quick-task scope.
4. **Release-notes items (F-07):** in-flight escrow balances do not migrate; reconstructed balances reflect the unfinalized 3.5.x ledger and are bounded at claim time by the 2x observed-balance cap.

## Consolidated verdict for a 3.5.20 testnet chain that never exercised the migration transaction

**ADJUST.** The step is structurally safe against double minting (F-04) and computes complete balances for all 3.5.x value shapes (F-06), but it has never run against real consensus: its confirmation path hard-fails on `timed_out` with no retry (fixed here), and its remote allow-list gate assumes validators hold local observations that fresh 3.7.0 genesis validators will not have (unresolved design gap F-02 — a launch-blocking decision, not a code patch).

## Code Change Record

- **Commit:** `4084545d` — `fix(260901-erh): retry migration on claim-confirmation timeout`
- **File:** src/account/GeniusNode.cpp (MIGRATING_DATABASE failure callback, +7/-2 lines)
- **Test:** `cmake --build build/OSX/Release --target migration_sync_test` succeeded; `ctest --test-dir build/OSX/Release -R migration_sync_test --output-on-failure` — **1/1 Passed (87.37 s)**, which runs `MigrationParamTest.BalanceAfterMigration` (2 node params), `RejectsOverclaimWhenAllowListEnabled`, `ArchiveSurvivesMigrationWithoutJoiningRegistry`, and `MigrationInputValidatorTest.RegisteredWithoutLocalUTXOWitnessRequirement`.
- No submodule was touched (all changes main-repo). The `Pending()` candidate and all larger fixes were intentionally NOT implemented (smallness limits; consensus/CRDT design out of scope).

## Deviations from Plan

None — investigation executed as planned; the one conditional small fix matched the plan's candidate list ("scheduling a migration retry for timed_out"). FINDINGS.md and this SUMMARY are uncommitted docs artifacts per orchestrator instruction.

## Threat Model Cross-Check

- **T-QUICK-01 (double-claim):** mitigated — verified `DeriveUniqueSourceKey` + `HasConfirmedInputConflict` reject re-claims after confirmation (F-04).
- **T-QUICK-02 (overclaim):** the 2x cap is enforced and tested; the convergence semantics gap is F-02 (entries node-local).
- **T-QUICK-03 (reject-before-sync DoS):** root cause is not timing but total absence of propagation; the proposed Pending mitigation was evaluated and rejected (F-05).

## Self-Check: PASSED

- FINDINGS.md exists at .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md (97 lines, 32 path:line citation lines, 4 verdict lines).
- Commit 4084545d exists on branch gsd/phase-13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production (`git log --oneline -1` verified).
- migration_sync_test green post-change (ctest output recorded above).
- No submodule modified: `git show --stat 4084545d` touches only src/account/GeniusNode.cpp.
