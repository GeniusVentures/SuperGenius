---
phase: 12-multi-node-finality-fault-proof
plan: 13
subsystem: test-infrastructure/consensus-observability
tags: [test-fixture, crdt, rocksdb, stale-state, certificate-validation, logging, uat-gap-closure]
requires:
  - CRDTFixture deterministic db naming (test/testutil/storage/base_crdt_test.cpp)
  - ConsensusManager::ValidateCertificate silent tally reject (src/blockchain/Consensus.cpp)
provides:
  - Run-unique CRDTFixture db/keypair paths (pid + fixture counter) with construction-time reap of the exact derived paths
  - Warn-level log on no-quorum/tally-error certificate rejection in ConsensusManager::ValidateCertificate
  - Stale-db immunity proof for transaction_manager_certificate_fallback_test (UAT round-2 test 2 truth restored)
affects:
  - All CRDTFixture consumer binaries (cert-fallback, pending-lifecycle x2, compatibility-smoke, multi-node, task_queue)
  - Any future diagnosis of certificate rejection via test logs
tech-stack:
  added: []
  patterns:
    - pid-unique test-fixture resource naming (mirrors FSFixture::clear() reap-at-construction)
    - aggregate-reject warn logging paired with existing per-stage error logging
key-files:
  created: []
  modified:
    - test/testutil/storage/base_crdt_test.cpp
    - src/blockchain/Consensus.cpp
decisions:
  - CRDTFixture derived paths embed ::getpid() ahead of the fixture counter and are reaped at construction — leftover databases from killed/crashed runs can no longer be silently reopened.
  - The reap is strictly limited to the exact derived paths (no basePath sweep) so concurrent/live fixtures are never deleted (T-12-13-01).
  - ValidateCertificate's tally reject branch logs one warn line (slot key, registry cid, vote count, tally error) with zero control-flow change; the per-vote non-member drop stays debug.
metrics:
  duration: 9m
  completed: 2026-09-02
---

# Phase 12 Plan 13: UAT Round-2 Test 2 Gap Closure (Stale CRDTFixture DB Immunity + No-Quorum Rejection Logging) Summary

Run-unique (pid + counter) CRDTFixture db/keypair paths with construction-time reap of exactly those paths, plus a warn-level log on the previously-silent no-quorum certificate rejection — proven immune to the exact stale-legacy-db precondition that reproduced the UAT round-2 test 2 failure.

## What Was Done

### Task 1: Run-unique CRDTFixture db paths with construction-time reap
Commit `1564d4b7` — `test/testutil/storage/base_crdt_test.cpp`

- Added `#include <unistd.h>` (plain include, matching the `::getpid()` convention at `test/src/blockchain/multi_node_finality_fault_test.cpp:49/755`).
- Suffix derivation changed from counter-only to `std::to_string(::getpid()) + "_" + std::to_string(fixture_id)`; the `keypair_path_`/`db_path_` formulas are otherwise identical. A killed run's `unit_N` dir can never be reopened by the next run's Nth fixture, and two live binaries sharing a cwd cannot collide.
- Immediately after path derivation and before `KeyPairFileStorage`/`GlobalDB::New`, both exact derived paths are reaped if present (`fs::exists` → `fs::remove_all` inside the destructor's `try/catch(fs::filesystem_error)` + `std::cerr` pattern), printing `[CRDTFixture] removed pre-existing <path>` so future poisoning is self-announcing. The reap covers the pid-reuse edge case.
- No sweep of `basePath` or legacy counter-only names (T-12-13-01 mitigation honored); destructor, `SetUpTestSuite` logging, and the pubsub/GlobalDB construction sequence untouched.

### Task 2: Warn log on no-quorum certificate rejection
Commit `040fac6a` — `src/blockchain/Consensus.cpp`

- One `ConsensusManagerLogger()->warn(...)` inserted immediately before `return Check::Reject` in `ConsensusManager::ValidateCertificate`'s tally branch, covering both reject causes: `__func__`, `GetSlotKey(certificate.proposal())`, `certificate.registry_cid()`, `certificate.votes_size()`, and `tally.error().message()` when `tally.has_error()` (else `"quorum not reached"` — same outcome-error formatting as adjacent error paths).
- Diff is 7 inserted lines, log-only: no control-flow, return-value, or signature change. Per-vote non-member drop (lines ~1894-1903) stays debug.

### Task 3: Stale-db immunity and blast-radius proof (evidence-only, no commit)
1. Seeded `build/OSX/Release/CRDT.Datastore.TEST.unit_12/` (with a `SEED_MARKER.txt` so "untouched" is provable).
2. Direct run `./test_bin/transaction_manager_certificate_fallback_test > /tmp/p12_13_stale_seed.log 2>&1`: **exit 0, 20/20 cases PASSED** (30.2s). Gates: 0 matches of `registry already initialized, skipping`; 0 matches of `InitializeCache: cache initialized`; 40 matches of `CRDT.Datastore.TEST.unit_[0-9]+_` (GlobalDB paths are `CRDT.Datastore.TEST.unit_10417_N`); seeded legacy dir present with marker intact; 0 `removed pre-existing` lines (correct — the legacy name is never touched and no pid-colliding leftovers existed).
3. Official ctest gate: `transaction_manager_certificate_fallback_test` **Passed** (30.75s).
4. Blast radius (serial ctest, one at a time): `transaction_manager_pending_lifecycle_test` (2.07s), `consensus_subject_test` (0.57s, WR-03 SizeGate symmetric evidence for the Task 2 edit), `consensus_pending_lifecycle_test` (44.28s), `multi_node_finality_fault_compatibility_smoke_test` (6.36s), `task_queue_test` (12.03s) — **5/5 Passed, 0 failed**.
5. Cleanup: seeded dir removed; no `CRDT.Datastore.TEST*` leftovers remain (only the pre-existing empty `CRDT.Datastore.TEST` keypair parent dir) — each green run's destructor removed its own pid-suffixed dirs.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] consensus_certificate_test target does not exist**
- **Found during:** Task 1 build / Task 3 step 4
- **Issue:** The plan's build command and consumer list include `consensus_certificate_test`, but its `addtest()` registration in `test/src/blockchain/CMakeLists.txt:14-21` is commented out (pre-existing; annotated "Test deferred to E2E integration (Phase 4)"), so `cmake --build ... --target consensus_certificate_test` fails with "No rule to make target".
- **Fix:** Built and ran the six existing CRDTFixture consumer targets; the Task 3 ctest regex containing `^consensus_certificate_test$` simply matches nothing. Did NOT re-enable the disabled test target (out-of-scope build change, and its source still references the old fixture assumptions).
- **Files modified:** none
- **Commit:** none (command-level adjustment)

Or (other than the above): plan executed exactly as written.

## Known Stubs

None — both changes are real behavior (fixture path lifecycle, log emission); no placeholders, no unwired data.

## Threat Flags

None — no new security surface beyond the plan's threat model. T-12-13-01 mitigation implemented as specified (reap strictly limited to the exact pid-unique derived paths); T-12-13-02 accepted as designed (slot key/registry cid/vote count only, no key material).

## Findings

- The stale-db poisoning chain is decisively broken at the fixture layer: with the exact reproduction precondition seeded, the cert-fallback suite is green with fresh pid-unique databases and zero registry-skip lines.
- Destructor-based cleanup scales cleanly to pid-suffixed names — no leftovers accumulated across the six suite runs in this plan.
- `consensus_certificate_test.cpp` still exists as a source file but is unregistered; any future plan referencing it as a runnable suite will hit the same missing-target deviation.

## UAT Round-2 Test 2 Truth

Restored: `transaction_manager_certificate_fallback_test` passes (20/20, exit 0, ctest green) with the stale legacy db dir `CRDT.Datastore.TEST.unit_12` deliberately present in the test working directory.

## Self-Check: PASSED

All key files present; both task commits (1564d4b7, 040fac6a) found in git log.
