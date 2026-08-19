---
phase: 12-consensus-race-and-compatibility-verification
artifact: full-suite-report
run: 4
status: BLOCKED — focused gate flaky failure (stop gate fired; full suite not characterized)
created: 2026-08-19
---

# Phase 12 Full-Suite Verification Report — Run 4

**Result: BLOCKED.** The focused regression gate failed with a nondeterministic
failure in `consensus_finalization_test`
(`ConsensusFinalizationHarness.StructuredTraceUsesStableIdentityForExactReplay`).
Per the 12-05 stop gate ("if any focused or isolated test fails … stop and
report the exact blocker; do not proceed to characterize the full suite as
passing"), the isolated 11-node race and the unfiltered full suite were **not
run** in this run. Phase 12 closure is **not achieved**.

## Build Identity (reproducibility)

| Fact | Value |
|---|---|
| Repository HEAD | `23b38cd9` (`docs(12-05): reset blocked summary for re-run after fork endpoint and timeout fixes`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` (main working tree, sequential execution) |
| Working tree | clean except one pre-existing untracked scratch file (unrelated to this run) |
| Build directory | `build/OSX/Release` |
| Generator | Unix Makefiles |
| Build type | Release |
| Compiler | Apple clang 16.0.0 (clang-1600.0.26.6) |
| CMake / CTest | 3.31.4 |
| Platform | macOS (Darwin, arm64) |
| Race test properties | `bridge_race_single_burn_test`: `TIMEOUT 900`, `RUN_SERIAL TRUE` (verified in generated `build/OSX/Release/test/src/bridge_race/CTestTestfile.cmake`, commit `63645bbc`) |

No credentials, RPC endpoint URLs, seeds, or key material are recorded in this
report. External-service facts are stated as availability booleans only.

## Commands Executed

1. Focused target build:
   `cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2`
   → **success** (all targets up to date / built, `[100%] Built target bridge_race_single_burn_test`).
2. Focused gate:
   `ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2`
   → **86% tests passed, 1 failed of 7** (total 89.87 s).
3. Focused failure characterization (evidence only, no source changes):
   `build/OSX/Release/test_bin/consensus_finalization_test --gtest_filter='ConsensusFinalizationHarness.StructuredTraceUsesStableIdentityForExactReplay'` ×3
   → **PASS / FAIL / PASS** (nondeterministic).

Timestamps (UTC): build+gate window 2026-08-19T20:12:02Z → ~20:18Z.

## Focused Gate Results (run 4)

| Test | Result |
|---|---|
| consensus_finalization_test | **FAILED** (1 of 10 gtest cases: `StructuredTraceUsesStableIdentityForExactReplay`) |
| consensus_finality_race_test | Passed (5.01 s) |
| consensus_vote_journal_test | Passed |
| consensus_burn_reservation_test | Passed |
| consensus_certificate_store_test | Passed |
| certificate_compatibility_test | Passed (19.86 s) |
| transaction_manager_pending_lifecycle_test | Passed |

All 7 named Phase 12 regressions were discovered (`--no-tests=error` did not
trip). Teardown completed cleanly (GlobalDB/CRDT shutdown logs normal; no
segfault, no leaked fixture).

## Blocker: Flaky `StructuredTraceUsesStableIdentityForExactReplay`

**Failure** (`test/src/blockchain/consensus_finalization_test.cpp:691`):

```
Expected: (different_target) != (identity( votes[0] ))
Actual: both tuples identical (same validator_id, slot_id, proposal_id, payload_digest)
```

The test builds a "competitor" certificate over the **same subject** with the
**same proposer account** and asserts its `proposal_id` differs from the
original vote's. `ConsensusManager::CreateProposal`
(`src/blockchain/Consensus.cpp:2222–2250`) derives `proposal_id` from proposal
bytes whose only varying field in this scenario is
`proposal.set_timestamp(CurrentTimeMs())`. When both `MakeCertificate` calls
land inside the same millisecond, the competitor proposal is byte-identical to
the original, the IDs collide, and `EXPECT_NE` fails.

**Measured flakiness:** 3 isolated reruns with no system load → PASS, FAIL,
PASS (~1/3 failure rate), consistent with a 1 ms timestamp-resolution race on
a fast host. The same test passed in run 3's focused gate (7/7), confirming
nondeterminism rather than a deterministic regression.

**Classification:** test-determinism defect (the "distinct competitor target"
precondition depends on wall-clock tick luck). Production `CreateProposal`
behavior is unchanged and out of scope for this reporting plan; per plan
scope, no source or test code was modified. Fixing the test (e.g., a
deterministic distinct proposal field instead of relying on ms-timestamp
uniqueness) is a scoped follow-up decision.

## Isolated Race and Full Suite — Not Run (stop gate)

- `bridge_race_single_burn_test` isolated (`-j1`): **not run this run** (stop
  gate fired at the focused gate). Last known-good evidence: run 3 isolated
  PASS in ~271 s (within the 900 s budget now configured).
- Dynamic inventory (`ctest -N`) and the unfiltered full suite
  (`ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2`):
  **not run this run**. Run 3 recorded 91/98 with 6 bridge/Anvil fixture
  failures (stale fork-RPC endpoint in Aug-7 binaries; rebuilt Aug 19 with a
  verified-working endpoint) and 1 race timeout at 500 s under `-j2` (fixed by
  `63645bbc`: `TIMEOUT 900` + `RUN_SERIAL TRUE`). Those fixes are in place but
  their full-suite effect is **unverified** because the stop gate fired first.

## Prerequisite Environment Facts

| Prerequisite | Status (run 4) |
|---|---|
| Bridge/Anvil fixture binaries rebuilt (Aug 19) | yes |
| External Sepolia fork RPC reachable from fixtures | available (verified during rebuild validation) |
| Foundry `anvil`/`cast` | available (used by fixtures) |
| Credential material | present in environment; values intentionally not recorded |

## Requirement Matrix (TEST-01..06)

| Req | Focused evidence (run 4) | Isolated race | Full suite | Status |
|---|---|---|---|---|
| TEST-01 | finalization/vote-journal pass; race not run | not run (run 3: PASS 271 s) | not run | **BLOCKED** |
| TEST-02 | consensus_finality_race_test Passed | — | not run | **BLOCKED** |
| TEST-03 | consensus_vote_journal_test Passed | — | not run | **BLOCKED** |
| TEST-04 | consensus_vote_journal_test Passed | — | not run | **BLOCKED** |
| TEST-05 | certificate store / compatibility pass | — | not run | **BLOCKED** |
| TEST-06 | transaction_manager_pending_lifecycle_test Passed | — | not run | **BLOCKED** |

"BLOCKED" = full-suite leg of the evidence matrix missing because the plan's
stop gate fired at the focused gate; no result here is represented as
successful closure.

## Reviewed Skips

None claimed in this run — no test outcome is reported as skipped; the
unexecuted legs are recorded as not-run with the stop-gate reason above.

## Follow-up (scoped decisions, not executed under this plan)

1. Fix `StructuredTraceUsesStableIdentityForExactReplay` determinism: give the
   competitor proposal a guaranteed-distinct field (e.g., distinct proposer or
   explicit nonce-bearing subject) instead of relying on `CurrentTimeMs()`
   uniqueness.
2. Re-run 12-05: focused gate → isolated race → dynamic inventory → one
   unfiltered full suite at `-j2`.
