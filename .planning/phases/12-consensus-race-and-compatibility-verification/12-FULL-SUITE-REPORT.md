---
phase: 12-consensus-race-and-compatibility-verification
artifact: full-suite-report
run: 5
status: IN PROGRESS — focused gate and isolated race PASSED; full suite running
created: 2026-08-19
---

# Phase 12 Full-Suite Verification Report — Run 5

Run 5 re-executes Plan 12-05 after the run-4 blocker fix (commit `84bce439`:
the exact-replay competitor proposal now uses a distinct proposer account, so
its `proposal_id` is deterministically distinct and no longer depends on
`CurrentTimeMs()` tick luck).

## Build Identity (reproducibility)

| Fact | Value |
|---|---|
| Repository HEAD | `101850cd` (`docs(12-05): reset blocked summary for re-run after exact-replay flake fix`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` (main working tree, sequential execution) |
| Working tree | clean except one pre-existing untracked scratch file (unrelated to this run) |
| Build directory | `build/OSX/Release` |
| Generator | Unix Makefiles |
| Build type | Release |
| Compiler | Apple clang 16.0.0 (clang-1600.0.26.6) |
| CMake / CTest | 3.31.4 |
| Platform | macOS (Darwin, arm64) |
| evmrelay submodule | `4787e582` (mint destination byte-order fix in place) |
| Race test properties | `bridge_race_single_burn_test`: `TIMEOUT 900`, `RUN_SERIAL TRUE` (verified in generated `build/OSX/Release/test/src/bridge_race/CTestTestfile.cmake`, commit `63645bbc`) |

No credentials, RPC endpoint URLs, seeds, or key material are recorded in this
report. External-service facts are stated as availability booleans only.

## Commands Executed (run 5)

1. Focused target build:
   `cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2`
   → **success** (`[100%] Built target bridge_race_single_burn_test`).
2. Focused gate:
   `ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2`
   → **100% tests passed, 0 failed of 7** (total 91.45 s).
3. Isolated race:
   `ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1`
   → **Passed, 271.20 s** (well within the configured 900 s timeout).

Timestamps (UTC): build+focused gate 2026-08-19T20:33:13Z → 20:35:16Z;
isolated race 20:35:27Z → 20:39:59Z.

## Focused Gate Results (run 5)

| Test | Result | Duration |
|---|---|---|
| consensus_finalization_test | Passed | 10.90 s |
| consensus_burn_reservation_test | Passed | 43.78 s |
| consensus_vote_journal_test | Passed | 33.84 s |
| consensus_certificate_store_test | Passed | 27.67 s |
| transaction_manager_pending_lifecycle_test | Passed | 33.03 s |
| consensus_finality_race_test | Passed | 4.34 s |
| certificate_compatibility_test | Passed | 19.02 s |

All 7 named Phase 12 regressions were discovered (`--no-tests=error` did not
trip) and passed. The run-4 flaky case
`ConsensusFinalizationHarness.StructuredTraceUsesStableIdentityForExactReplay`
passed deterministically under the `84bce439` fix (distinct competitor
proposer account ⇒ deterministically distinct `proposal_id`).

## Isolated Real Race (run 5)

`BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn` — **PASSED**, 271.20 s
(gtest body 139.7 s + bounded stability window + teardown), `-j1`, within the
900 s `TIMEOUT`.

| Fact | Value |
|---|---|
| Nodes ready | all 11 nodes READY before burn submission |
| Proposals | 11 (one per node) over one canonical burn slot |
| Validators | 11 (1 full authority + 10 additional genesis validators) |
| Outcome | exactly one winner certificate; `proposals=11 validators=11 stable_ms=16000` |
| Teardown | clean: per-node destroy-start/destroy-complete for nodes 0–10 (`teardown phase=nodes-complete`), then `teardown phase=anvil-complete` |
| Fixture | Anvil fork of Sepolia started and stopped cleanly; pre-burn baseline block recorded |

Full evidence: `build/OSX/Release/Testing/Temporary/LastTest.log` (run
section 93/98) and xunit output under `build/OSX/Release/xunit/`. Burn hash,
slot ID, and winner ID are recorded in the test log but not reproduced here.

## Dynamic Inventory and Full Suite

_Pending — filled in by Task 2._

## Prerequisite Environment Facts

| Prerequisite | Status (run 5) |
|---|---|
| Bridge/Anvil fixture binaries rebuilt (Aug 19) with working fork endpoint | yes (fixtures embed a verified-working Sepolia fork RPC; the isolated race used it successfully) |
| External Sepolia fork RPC reachable from fixtures | available (used by the isolated race this run) |
| Foundry `anvil`/`cast` | available (`anvil` 1.7.1) |
| Credential material | present in environment; values intentionally not recorded |

## Requirement Matrix (TEST-01..06)

_Pending full-suite leg — filled in by Task 2._

## Reviewed Skips

_Pending — filled in by Task 2._
