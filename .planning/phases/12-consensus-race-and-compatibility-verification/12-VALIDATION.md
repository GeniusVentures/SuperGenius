---
phase: 12
slug: consensus-race-and-compatibility-verification
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-07-30
---

# Phase 12 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest binaries registered with CTest |
| **Config file** | `cmake/functions.cmake`, `test/CMakeLists.txt`, subsystem `CMakeLists.txt` files |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R '^(consensus_finality_race_test|consensus_vote_journal_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test|utxo_manager_test)$' --output-on-failure --no-tests=error` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2` |
| **Estimated runtime** | Focused: ~3 minutes; isolated race: up to 500 seconds; full suite: environment-dependent |

---

## Sampling Rate

- **After every task commit:** Run the narrow target named by that task's `<automated>` command.
- **After every plan wave:** Run all Phase 12 focused targets completed through that wave.
- **Before `$gsd-verify-work`:** Run the isolated 11-node race, enumerate all configured CTest targets, and run the entire suite.
- **Max feedback latency:** 5 minutes for focused work; the explicitly isolated race/full-suite gates run only at plan or phase boundaries.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 12-01-01 | 01 | 1 | TEST-01 | T-12-01, T-12-06 | Observer is per-manager, read-only, lock-safe, and contains no secret material | unit | `cmake --build build/OSX/Release --target consensus_finalization_test consensus_vote_journal_test -j2` | ✅ | ⬜ pending |
| 12-01-02 | 01 | 1 | TEST-01 | T-12-02, T-12-07 | Structured events distinguish exact replay from a distinct signed target | unit | `ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_vote_journal_test)$' --output-on-failure --no-tests=error` | ✅ | ⬜ pending |
| 12-02-01 | 02 | 2 | TEST-02 | T-12-01, T-12-03 | Authority is queryable before paused application and competitor ingress | integration | `cmake --build build/OSX/Release --target consensus_finality_race_test -j2` | ❌ W0 | ⬜ pending |
| 12-02-02 | 02 | 2 | TEST-02, TEST-05 | T-12-04 | All external ingress routes apply identical certificate once and preserve winner on conflict | integration | `ctest --test-dir build/OSX/Release -R '^consensus_finality_race_test$' --output-on-failure --no-tests=error` | ❌ W0 | ⬜ pending |
| 12-03-01 | 03 | 1 | TEST-03 | T-12-02, T-12-07 | Reopened manager retains exact vote lock and cannot sign competitor | unit | `build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_filter='*Restart*Competing*'` | ✅ | ⬜ pending |
| 12-03-02 | 03 | 1 | TEST-04 | T-12-02 | Better pre-window candidate can win; post-publication candidate cannot re-sign | unit | `build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_filter='*Deadline*:*Window*'` | ✅ | ⬜ pending |
| 12-03-03 | 03 | 1 | TEST-05, TEST-06 | T-12-04 | Typed index failures preserve authority and real nonce/UTXO consumers retain semantics | integration | `ctest --test-dir build/OSX/Release -R '^(consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test|utxo_manager_test)$' --output-on-failure --no-tests=error` | ✅ | ⬜ pending |
| 12-04-01 | 04 | 2 | TEST-01 | T-12-01, T-12-02, T-12-06 | All 11 proposals share one slot and validators publish at most one distinct vote target | e2e | `cmake --build build/OSX/Release --target bridge_race_single_burn_test -j2` | ✅ | ⬜ pending |
| 12-04-02 | 04 | 2 | TEST-01 | T-12-02, T-12-05 | One certificate/winner confirms once across every node with bounded diagnostics | e2e | `ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error` | ✅ | ⬜ pending |
| 12-05-01 | 05 | 3 | TEST-01..06 | T-12-05 | Every configured test is run or has an explicit reviewed prerequisite skip | system | `ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2` | ✅ | ⬜ pending |

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_finality_race_test.cpp` — dedicated deterministic TEST-02/05 integration target created in Plan 12-02.
- [ ] `test/src/blockchain/CMakeLists.txt` — register `consensus_finality_race_test` with the same real fixture dependencies as the existing finalization targets.

Existing infrastructure covers all other phase requirements.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| External prerequisite skip review | D-17 completion gate | Availability of credentials, Foundry tools, and external RPC services is environmental | Compare `ctest -N` inventory with the full run; verify each skipped test reports a specific unavailable prerequisite and record it in `12-FULL-SUITE-REPORT.md`. |

All consensus correctness behaviors have automated verification.

---

## Validation Sign-Off

- [x] All planned tasks have an automated verification command or explicit Wave 0 dependency.
- [x] Sampling continuity prevents three consecutive tasks without automated verification.
- [x] Wave 0 identifies the only missing target/source.
- [x] No watch-mode flags are used.
- [x] Focused feedback latency is bounded below the expensive E2E/full-suite gates.
- [x] `nyquist_compliant: true` is set in frontmatter.

**Approval:** approved 2026-07-30
