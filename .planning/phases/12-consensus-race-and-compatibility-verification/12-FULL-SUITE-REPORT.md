# Phase 12 Full-Suite Verification Report

## Disposition

**IN PROGRESS — Task 1 complete, Task 2 (full suite) pending.** This is the
third execution of Plan 12-05, re-run after the `evmrelay` submodule was
advanced from the stale worktree identity `62a9bbb1` to the
superproject-recorded pointer `4787e582` (`fix(eth): preserve bridge
destination byte order`). The fresh Phase 12 focused gate passed 7/7 and the
mandatory isolated 11-node race `bridge_race_single_burn_test` **PASSED** in
isolated `-j1` execution — the mint output destination now equals the burn
destination (SGNS address), resolving the deterministic blocker recorded on
2026-08-19. No source or test code was modified under this reporting task.

This report will be updated with the Task 2 dynamic inventory and the single
unfiltered full-suite result. Closure claims are deferred to the final
"Closure Decision" section below.

## Reproduction Identity

| Field | Value |
|---|---|
| Verification date | `2026-08-19` |
| Build window | `15:25:05-15:26:44 UTC` |
| Focused gate window | `15:26:57-15:28:28 UTC` |
| Isolated race window | `15:28:41-15:33:13 UTC` |
| Repository commit | `a25c2312ee9923d317cdd54403e0ab7194d7c125` (`docs(12-05): reset blocked summary for re-execution after evmrelay fix`) |
| Branch | `gsd/phase-09-canonical-slot-and-certificate-storage` |
| Worktree state | Pre-existing untracked log artifact (`2026-08-05T13:22:40.8911080Z Current run`), plus this report; no source modifications |
| `evmrelay` recorded pointer | `4787e58204e4ca5590835779ec8a36ce02c59cb3` (superproject index) |
| `evmrelay` worktree identity | `4787e58204e4ca5590835779ec8a36ce02c59cb3` (`fix(eth): preserve bridge destination byte order`) — **matches the recorded pointer** |
| `ProofSystem` identity | `a107566e745797f821d18d84994d4280b84f1cdc` (matches recorded pointer) |
| Build directory | `build/OSX/Release` (reconfigured with `cmake -S build/OSX -B build/OSX/Release` before building) |
| Configuration / generator | `Release` / `Unix Makefiles` |
| CMake / CTest | `3.31.4` / `3.31.4` |
| Compiler | AppleClang C++ `16.0.0.16000026` (`clang-1600.0.26.6`) |
| Toolchain | `build/apple.toolchain.cmake` |
| Platform | macOS `15.7.4` (`24G517`), arm64 host |

No credential, signing key, private key, account seed, RPC URL, or secret
environment-variable value is recorded in this report. Transaction hashes,
slot IDs, and validator identity abbreviations quoted below are public test
artifacts, not secrets.

## Prerequisite Review

| Prerequisite | Presence/status only | Disposition |
|---|---|---|
| Host TCP/process permissions | Available | Used for the focused gate and the isolated race run |
| Anvil | Available | `/Users/henriqueklein/.foundry/bin/anvil`; started and stopped cleanly in the race run |
| Cast | Available | `/Users/henriqueklein/.foundry/bin/cast`; used by the Anvil fixture to submit the sole burn |
| Fork RPC | Available | Fork startup, readiness, account funding, and the local burn succeeded in the race run; source value omitted |
| Local bridge contract/funding | Available | Account #0 funding and bridge burn setup succeeded in the race run |
| `RUN_E2E_BRIDGE` | Absent | Reviewed prerequisite-unavailable condition for positive `bridge_e2e_test` cases |
| `SIGNING_KEY` / `PRIVATE_KEY` | Absent | Confirms live signing cannot run; values were not printed |
| `RUN_E2E_RLPX` | Absent | Reviewed prerequisite-unavailable condition for the RLPx case |
| Live Sepolia signing | Unavailable | `bridge_sepolia_e2e_test` retains its `DISABLED_` body; suppressed prerequisite coverage, not an executed live test |

## Task 1 — Focused Phase 12 Gate

### Build

```sh
cmake --build build/OSX/Release --target consensus_finalization_test consensus_finality_race_test consensus_vote_journal_test consensus_burn_reservation_test consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test bridge_race_single_burn_test -j2
```

Result: **PASS**, exit `0` (`15:25:05-15:26:44 UTC`). A CMake reconfigure
(`cmake -S build/OSX -B build/OSX/Release`, exit `0`) ran first so the
updated `evmrelay` submodule code (`4787e582`) was compiled and linked into
all eight owning targets.

### Focused CTest

```sh
ctest --test-dir build/OSX/Release -R '^(consensus_finalization_test|consensus_finality_race_test|consensus_vote_journal_test|consensus_burn_reservation_test|consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test)$' --output-on-failure --no-tests=error -j2
```

Result: **PASS** — 7/7 entries passed, zero failed, zero `Not Run`, total
real time 90.46 seconds (`15:26:57-15:28:28 UTC`).

| CTest entry | Result | Duration |
|---|---:|---:|
| `consensus_vote_journal_test` | PASS | 34.58s |
| `consensus_burn_reservation_test` | PASS | 45.23s |
| `transaction_manager_pending_lifecycle_test` | PASS | 33.97s |
| `consensus_certificate_store_test` | PASS | 28.38s |
| `consensus_finalization_test` | PASS | 11.73s |
| `certificate_compatibility_test` | PASS | 20.03s |
| `consensus_finality_race_test` | PASS | 5.10s |

### Mandatory isolated 11-node race

```sh
ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error -j1
```

Result: **PASS** — CTest exit `0`, duration 271.33s (`15:28:41-15:33:13
UTC`); GoogleTest body `BridgeRaceE2ETest.ExactlyOneCertificateForOneBurn`
OK in 139.650s. Configured timeout 500 seconds; no timeout.

Race summary from the run log (public test artifacts):

- One external burn (`5eb61c9afffea872` abbrev) on the Anvil fork;
  pre-burn baseline block `11522938`, bridge creation block `11522939`.
- All 11 nodes READY before RPC endpoint configuration; 11 validators
  registered (full authority plus 10 genesis validators).
- Exactly one canonical slot (`bb57eeb6700c56bf` abbrev); 11 distinct
  proposals; one winner (`02d30b680dd420bc` abbrev); 16-second stability
  window observed before convergence was declared.
- The winning mint's applied output destination equals the burn
  destination SGNS address — the `application_converged` predicate held
  (this is the exact assertion that failed deterministically in run 2
  before the `evmrelay` fix).
- Clean teardown: all 11 node shutdowns completed
  (`phase=shutdown-complete` for nodes 0-10, 0.3s-14.7s each), Anvil
  stopped (`exit_code=383` is the fixture's expected SIGTERM), and the
  process exited naturally.

**Assessment:** the `evmrelay` update from `62a9bbb1` to `4787e582`
(`fix(eth): preserve bridge destination byte order`) resolved the
deterministic mint-destination mismatch recorded in run 2. The D-14
mandatory isolated-race invariant is now satisfied.

Raw evidence remains in
`build/OSX/Release/Testing/Temporary/LastTest.log` and the generated
`build/OSX/Release/xunit/` files for this worktree.

## Task 2 — Dynamic Inventory and Full Suite

_Pending — this section will be completed by Task 2 of this plan._

## Requirement Matrix

_Pending Task 2 full-suite evidence._

| Requirement | Focused / isolated evidence | Full-suite evidence | Status |
|---|---|---|---|
| TEST-01 | `bridge_race_single_burn_test` passed in isolation: one slot, 11 proposals, one winner, application converged with correct destination, clean teardown | Pending | **PENDING FULL SUITE** |
| TEST-02 | `consensus_finality_race_test` passed in the fresh focused gate | Pending | **PENDING FULL SUITE** |
| TEST-03 | Restart vote-lock regression passed in `consensus_vote_journal_test` | Pending | **PENDING FULL SUITE** |
| TEST-04 | Before/after-deadline regression passed in `consensus_vote_journal_test` | Pending | **PENDING FULL SUITE** |
| TEST-05 | Finality-race, certificate-store, burn-reservation, and compatibility entries passed | Pending | **PENDING FULL SUITE** |
| TEST-06 | TransactionManager lifecycle and compatibility entries passed | Pending | **PENDING FULL SUITE** |

## Closure Decision

_Pending Task 2._
