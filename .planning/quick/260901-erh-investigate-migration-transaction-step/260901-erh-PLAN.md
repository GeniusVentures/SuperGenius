---
phase: quick-260901-erh
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md
  - .planning/quick/260901-erh-investigate-migration-transaction-step/260901-erh-SUMMARY.md
autonomous: true
requirements: [QUICK-260901]
must_haves:
  truths:
    - "The exact trigger path from a 3.5.20 node upgrade to Migration3_6_0To3_7_0::Apply is documented with file:line citations"
    - "Failure and retry behavior of the migration-transaction step on first mainnet run is documented, including timed_out handling"
    - "A verdict exists for cross-node allow-list convergence (Reject vs Pending semantics at the remote validation gate)"
    - "A verdict exists for double-claim/idempotency on migration retry"
    - "A final ADJUST / NO-ADJUST / DEFER verdict per finding is written into the SUMMARY"
    - "Any applied code change is main-repo-only and migration_sync_test passes"
  artifacts:
    - path: ".planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md"
      provides: "Per-thread investigation notes with file:line evidence"
    - path: ".planning/quick/260901-erh-investigate-migration-transaction-step/260901-erh-SUMMARY.md"
      provides: "Final assessment with verdict and recommendation"
  key_links:
    - from: "src/migration/Migration3_6_0To3_7_0.cpp"
      to: "src/account/TransactionManager.cpp"
      via: "MigrationFunds submit + WaitForTransactionOutgoing confirmation (4 min budget)"
      pattern: "MigrationFunds|WaitForTransactionOutgoing"
    - from: "src/account/TransactionManager.cpp"
      to: "src/account/MigrationAllowList.hpp"
      via: "remote consensus validation constructs local allow-list view at lines 3806-3823"
      pattern: "MigrationAllowList allow_list"
---

<objective>
Investigate whether the migration step that requires a migration transaction (`Migration3_6_0To3_7_0`) needs adjustment before the upcoming mainnet 3.7.0 release.

Context: The only released testnet phase was 3.520, which had no consensus or finality — so the migration-transaction path (`MigrationFunds` claim, remote allow-list validation, consensus confirmation) has never executed on a live network. The upcoming mainnet release (3.7.0, per `cmake/version.cmake:1`) will be its first real use when 3.5.x nodes upgrade.

Purpose: Produce a concrete adjust/no-adjust verdict backed by code references before mainnet ships; apply small fixes if identified, document large ones.

Output: `FINDINGS.md` (evidence per investigation thread), `260901-erh-SUMMARY.md` (final verdict), and optionally a small main-repo code change with passing `migration_sync_test`.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md

Background facts already established during planning (verify, do not re-derive):

- Migration chain registered in `src/migration/MigrationManager.cpp:46-84`: 0.2.0→1.0.0→3.4.0→3.5.0→3.6.0→3.7.0. `Migrate()` (lines 98-134) runs every step's `Init`/`IsRequired`/`Apply`/`ShutDown` in one pass.
- Upcoming release is 3.7.0 (`cmake/version.cmake:1`). A node on 3.5.20 has a DB at path appendix(3,5); upgrading to 3.7.0 runs BOTH `Migration3_5_0To3_6_0` and `Migration3_6_0To3_7_0` in a single migration pass (the 3_5→3_6 step creates the (3,6) DB that the 3_6→3_7 step then treats as legacy).
- The migration-transaction step is `src/migration/Migration3_6_0To3_7_0.cpp`. `IsRequired()` at lines 88-123; `Apply()` at lines 137-351 (blockchain bootstrap with 2-min retry + two 4-min waits for genesis/account-creation/validator-registry, balance computation, allow-list store, claim submission at line 321-325, 4-min confirmation wait at line 328, hard `timed_out` failure at line 335).
- Trigger: `GeniusNode::BeginDBInitialization` (src/account/GeniusNode.cpp:580-583) → `MIGRATING_DATABASE` state → `MigrateDatabase` (lines 1905-1933). On failure, ONLY `BLOCKCHAIN_INIT_FAILED` schedules a retry (lines 627-635, `ScheduleMigrationRetry` 1935-1947); a `timed_out` claim failure returns without retry and without advancing state.
- Claim submission: `TransactionManager::MigrationFunds` (src/account/TransactionManager.cpp:773-793).
- Confirmation wait: `TransactionManager::WaitForTransactionOutgoing` (src/account/TransactionManager.cpp:2271-2312) — terminal on CONFIRMED/UNCONFIRMED/INVALID/FAILED.
- Remote consensus validation of migration claims: src/account/TransactionManager.cpp:3740-3839. Allow-list gate at lines 3806-3823 — database error → `Pending()`, missing/not-eligible entry → Reject (`reject_and_maybe_fail_local`, line 3821). Double-spend guard `HasConfirmedInputConflict` at line 3793.
- One-time claim key: `MigrationTransaction::DeriveUniqueSourceKey` (src/account/MigrationTransaction.hpp:59-61), enforced in `MigrationInputValidator::ValidateWitness` (src/account/MigrationInputValidator.cpp:68-77) — the consumed "outpoint" is a synthetic key derived from (from_version, source_address, token_id), not a real UTXO.
- Allow-list: `src/account/MigrationAllowList.hpp` — `IsEligible` allows claims up to 2x observed balance; entries stored via the target GlobalDB RocksDB datastore (`Migration3_6_0To3_7_0.cpp:264-265`).
- Migratable-address filter: `IsMigratableBalanceAddress` (src/migration/Migration3_6_0To3_7_0.cpp:50-53) — only `IsAccountPublicKeyAddress`; other addresses silently dropped.
- Note: `Apply()` does NOT branch on `node_type_` — verify whether light nodes with partial legacy DBs run the claim path.
- Existing end-to-end test: `test/src/transaction_sync/migration_sync_test.cpp` (target `migration_sync_test`, registered in `test/src/transaction_sync/CMakeLists.txt:47`) — `MigrationParamTest.BalanceAfterMigration`, `RejectsOverclaimWhenAllowListEnabled`, `ArchiveSurvivesMigrationWithoutJoiningRegistry`, `MigrationInputValidatorTest.RegisteredWithoutLocalUTXOWitnessRequirement`.
- All code under investigation is in the main repo (src/, test/) — no submodules involved. If any proposed fix would require submodule changes, document it as a finding instead of implementing.
</context>

<tasks>

<task type="auto">
  <name>Task 1: Map trigger conditions and failure/retry behavior of the migration-transaction step</name>
  <files>.planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md</files>
  <action>
  Create `FINDINGS.md` in this quick-task directory with a section `## Trigger & Lifecycle Map`. Read the code paths listed in the context block and document, each with file:line citations:

  1. Exact upgrade flow for a 3.5.20 node moving to 3.7.0: which migration steps run, in what order, which DB directories are created (path appendix uses major.minor + network id, `src/base/sgns_version.cpp:68-73`), and what version marker each step writes (`kSGNSCRDTVersion` key).
  2. Which node types enter `MIGRATING_DATABASE` and reach `Migration3_6_0To3_7_0::Apply` — specifically whether Light nodes and Full nodes both run `ComputeLegacyBalances` + the claim submission, and what a Light node with a partial legacy DB would claim. Confirm there is no `node_type_` gate in `Apply()` (src/migration/Migration3_6_0To3_7_0.cpp:137-351) and determine whether one is needed given how light nodes populate their local DB.
  3. Failure taxonomy of `Apply()`: enumerate every failure return (blockchain start, genesis/registry wait at lines 236-260, transaction-manager READY wait at lines 309-313, claim confirmation `timed_out` at lines 328-336) and map each to node-level behavior in the `MigrateDatabase` callback (src/account/GeniusNode.cpp:622-639). Confirm: only `BLOCKCHAIN_INIT_FAILED` retries; `timed_out` leaves the node in `MIGRATING_DATABASE` with no retry and no state advance — state the user-visible consequence (node stuck? manual restart required?) by tracing what happens after the callback returns without transition.
  4. Re-run semantics: after a failed or interrupted run, what does the next `Migrate()` pass do — which steps re-run, is the allow-list re-stored, is the version marker still absent (src/migration/Migration3_6_0To3_7_0.cpp:343-347).

  Every claim in this section must carry a `path:line` citation. No code changes in this task.
  </action>
  <verify>
    <automated>grep -c 'src/.*:[0-9]' .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md | grep -qv '^0' && grep -q '## Trigger & Lifecycle Map' .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md</automated>
  </verify>
  <done>FINDINGS.md section "Trigger & Lifecycle Map" exists, covers all 4 points above, and every behavioral claim cites at least one path:line reference.</done>
</task>

<task type="auto">
  <name>Task 2: First-real-use correctness analysis of the migration-transaction path</name>
  <files>.planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md</files>
  <action>
  Append a section `## First-Run Risk Analysis` to FINDINGS.md with one subsection per thread below. Each subsection ends with a verdict line `Verdict: ADJUST | NO-ADJUST | DEFER — <one-sentence reason>` plus file:line citations.

  (a) Confirmation timing under real consensus/finality. Trace how a migration transaction reaches `TransactionStatus::CONFIRMED`: `EnqueueTransaction` broadcast path, consensus proposal (`ConsensusManager` subject lifecycle around src/account/TransactionManager.cpp:3740-3839), and what makes status CONFIRMED. Compare against the fixed 4-minute wait (src/migration/Migration3_6_0To3_7_0.cpp:328) and the earlier 2-min blockchain-start retry + two 4-min waits in the same `Apply()`. Determine: on a mainnet where the 3.7.0 network is still forming (validator registry syncing, quorum not yet up), does a healthy-but-slow network cause a hard migration failure? Note that the testnet never exercised this because consensus/finality were absent.

  (b) Cross-node allow-list convergence. Verify whether `MigrationAllowList::StoreObservedBalance` writes propagate to peers via GlobalDB CRDT sync (trace the datastore `put` in src/account/MigrationAllowList.cpp through the GlobalDB datastore — is it the CRDT-backed store, are the allow-list keys on a broadcast/listen topic?). Then assess the remote gate (src/account/TransactionManager.cpp:3806-3823): the claiming node stores entries locally then submits immediately; validators that have not yet received the entries evaluate `IsEligible` as false and REJECT (line 3819-3822) rather than Pending. Determine what a Reject does to the consensus round and whether the transaction is re-proposed later or permanently failed. Conclude whether claim confirmation depends on a timing race with allow-list propagation, and whether that race is bounded within the 4-minute confirmation budget.

  (c) Double-claim and idempotency on retry. Analyze `MigrationTransaction::DeriveUniqueSourceKey` (synthetic consumed outpoint) with `HasConfirmedInputConflict` (src/account/TransactionManager.cpp:3793) and `MigrationInputValidator::ValidateWitness` (src/account/MigrationInputValidator.cpp:56-117). Questions: after a migration retry re-runs `Apply()`, does re-submitting produce the same tx hash or a new one (check `FillDAGStruct` inputs — timestamps/nonces)? If the first claim confirmed but the version marker write failed (crash between lines 328-347), does the retry's re-claim get correctly rejected as a double spend? Is a pending (unconfirmed) first claim collided-with by the retry submission?

  (d) Balance coverage policy. Determine which balance-computation path applies to mainnet 3.5.20 data: UTXO snapshots under `/utxo/` (src/migration/Migration3_6_0To3_7_0.cpp:386-437) or reconstruction from transactions (lines 441-524). Check whether 3.5.x nodes maintain `/utxo/` snapshots; if reconstruction applies, verify the UTXO-parameter and MintTransaction handling covers all transaction types that existed on mainnet 3.5.x (identify any transaction type with value transfer that reconstruction would miss, e.g. escrow/payout shapes). Also state the consequence of `IsMigratableBalanceAddress` (lines 50-53) dropping non-account-public-key addresses: which address classes lose funds silently.

  No code changes in this task. Flag explicitly if any thread's root cause lives in a submodule — if so, its verdict must be DEFER with the submodule path named.
  </action>
  <verify>
    <automated>test $(grep -c '^Verdict: \(ADJUST\|NO-ADJUST\|DEFER\)' .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md) -eq 4 && grep -q '## First-Run Risk Analysis' .planning/quick/260901-erh-investigate-migration-transaction-step/FINDINGS.md</automated>
  </verify>
  <done>FINDINGS.md contains four analysis subsections (a)-(d), each with path:line citations and exactly one final verdict line.</done>
</task>

<task type="auto">
  <name>Task 3: Final verdict, focused adjustment if small, and summary write-up</name>
  <files>.planning/quick/260901-erh-investigate-migration-transaction-step/260901-erh-SUMMARY.md, src/migration/Migration3_6_0To3_7_0.cpp, src/account/TransactionManager.cpp</files>
  <action>
  Consolidate FINDINGS.md into `260901-erh-SUMMARY.md` in this quick-task directory with:
  1. A one-line headline verdict: `ADJUST` or `NO-ADJUST` for the mainnet 3.7.0 release.
  2. A findings table: | ID | Finding | Severity (HIGH/MED/LOW) | Verdict | Evidence (path:line) |.
  3. A recommendation paragraph stating exactly what to do before release, in priority order.

  Then, conditionally implement: pick ONLY findings whose fix is small — defined as a change confined to the main repo, touching at most 2 of the listed files, under ~30 changed lines, and NOT altering consensus semantics or the allow-list sync design. Candidate small fixes (implement only those the evidence actually supports): treating a missing allow-list entry as `Pending()` instead of Reject (src/account/TransactionManager.cpp:3819-3822); scheduling a migration retry for `timed_out` (src/account/GeniusNode.cpp:627-635 and/or src/migration/Migration3_6_0To3_7_0.cpp:328-336); extending the confirmation budget; gating light nodes out of the claim path. Anything larger (consensus behavior, CRDT propagation design, balance-reconstruction coverage) is documented in the SUMMARY as a follow-up, NOT implemented.

  If a code change is made: build and run `ctest --test-dir build -R migration_sync_test --output-on-failure` (build target first: `cmake --build build --target migration_sync_test`), record the result in the SUMMARY, and note that the pre-existing test suite must stay green. If no change is made, state that explicitly in the SUMMARY with the reason.
  </action>
  <verify>
    <automated>grep -q 'Verdict\|verdict' .planning/quick/260901-erh-investigate-migration-transaction-step/260901-erh-SUMMARY.md && git -C /Users/henriqueklein/gnus/SuperGNUS diff --name-only | grep -v '^\.planning' | grep -cv '^submodules/' >/dev/null; echo "summary-present:$?"</automated>
  </verify>
  <done>SUMMARY exists with headline verdict, findings table with severity and evidence, and a prioritized recommendation. Any code change is main-repo-only, within the smallness limits, and migration_sync_test passes (result recorded). Large findings are documented as follow-ups, not implemented.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| migrating node → consensus validators | migration claim (value-minting transaction) crosses to remote validators that adjudicate against local allow-list state |
| legacy DB (unfinalized 3.5.x data) → new chain balance | balances observed on a chain without finality become minted supply on 3.7.0 |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-QUICK-01 | Spoofing/Elevation | Migration claim double-spend via migration retry | mitigate | Verify DeriveUniqueSourceKey + HasConfirmedInputConflict (TransactionManager.cpp:3793) reject re-claims; Task 2c verdict |
| T-QUICK-02 | Tampering | Overclaim beyond observed balance | mitigate | Allow-list 2x cap already tested (RejectsOverclaimWhenAllowListEnabled); verify convergence semantics in Task 2b |
| T-QUICK-03 | Denial of Service | Validators reject claims before allow-list sync converges | mitigate | Task 2b verdict; candidate fix: missing entry → Pending |
</threat_model>

<verification>
- FINDINGS.md exists with `## Trigger & Lifecycle Map` and `## First-Run Risk Analysis`, 4 verdict lines, path:line citations throughout.
- 260901-erh-SUMMARY.md exists with headline verdict, findings table, prioritized recommendation.
- If code changed: `migration_sync_test` green; changed files confined to main repo.
</verification>

<success_criteria>
- A reader can decide, from the SUMMARY alone, whether the migration-transaction step is safe for the mainnet 3.7.0 release and what (if anything) must change first.
- Every verdict is traceable to file:line evidence.
- No submodule is modified.
</success_criteria>

<output>
Create `.planning/quick/260901-erh-investigate-migration-transaction-step/260901-erh-SUMMARY.md` when done
</output>
