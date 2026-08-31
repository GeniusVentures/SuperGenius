---
phase: 12-multi-node-finality-fault-proof
fixed_at: 2026-08-31T12:20:34Z
review_path: .planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md
iteration: 1
findings_in_scope: 3
fixed: 2
skipped: 1
status: partial
---

# Phase 12: Code Review Fix Report

**Fixed at:** 2026-08-31T12:20:34Z
**Source review:** `.planning/phases/12-multi-node-finality-fault-proof/12-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 3
- Fixed: 2
- Skipped: 1

## Fixed Issues

### CR-01: Real observer failures cannot be gated on focused-GTest failure evidence

**Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
**Commit:** 12878efa
**Status:** fixed: requires human verification
**Applied fix:** Added the sole test-owned evaluator, which parses serialized observer START/TERMINAL lines plus the focused-GTest footer and process exit. It derives completion/failure fail-closed and rejects nonzero exits unless the serialized run contains a focused failure footer.

### CR-02: The repair-authorization fence is never applied to emitted evidence

**Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
**Commit:** 99a6fce4
**Status:** fixed: requires human verification
**Applied fix:** Removed hand-authored observer records. The aggregate repair gate now accepts only evaluator-produced records and requires two eligible matching boundary/state/error triples; eligibility is derived from an explicit observer-lifecycle allowlist. Serialized fixtures cover matching, triple mismatch, non-observer, malformed, and foreign-process evidence.

## Skipped Issues

### WR-01: The claimed CR-02 regression was not present in the focused executable

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1165`
**Reason:** The isolated-source target could not complete its rebuild: generated CRDT code failed before the test target compiled because `protoc` reported `/private/tmp/sv-12-reviewfix-oAFDmQ/src/crdt/proto/delta.proto` is outside every configured `--proto_path`. The existing binary is stale (binary mtime `2026-08-31T08:18:30Z`; source mtime `2026-08-31T09:16:10Z`) and `--gtest_list_tests` exposes only `DistinguishesCompletePassFailurePartialAndForeignEvidence`, so no stale focused test was run.
**Original issue:** The focused executable did not include the new repair-gate regression, leaving the repair claim unverified.

## Verification

- Re-read the evaluator, repair gate, and both classifier tests; the only changed tracked source is the requested test file.
- `git diff --check -- test/src/blockchain/multi_node_finality_fault_test.cpp` passed before each commit.
- A syntax-only compile using the focused target's existing C++ compile command against the isolated-worktree source passed.
- A disposable CMake configure reached the project after worktree-local submodules were initialized, but the target build stopped at the pre-existing/generated `protoc --proto_path` failure described above.

---

_Fixed: 2026-08-31T12:20:34Z_
_Fixer: the agent (gsd-code-fixer)_
_Iteration: 1_
