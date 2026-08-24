---
phase: 11
fixed_at: 2026-08-24T14:44:06Z
review_path: .planning/phases/11-convergent-certificate-consumption-mint-recovery/11-REVIEW.md
iteration: 1
findings_in_scope: 1
fixed: 1
skipped: 0
status: all_fixed
---

# Phase 11: Code Review Fix Report

**Fixed at:** 2026-08-24T14:44:06Z
**Source review:** `.planning/phases/11-convergent-certificate-consumption-mint-recovery/11-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 1
- Fixed: 1
- Skipped: 0

## Fixed Issues

### CR-03: Concurrent UTXO consumption can overwrite a durable Mint output with a stale snapshot

**Files modified:** `src/account/UTXOManager.cpp`, `src/account/UTXOManager.hpp`, `test/src/account/utxo_manager_test.cpp`
**Commit:** 3bf32ee4
**Status:** fixed: requires human verification
**Applied fix:** All persisted UTXO writers now keep the registry lock through mutation, snapshot capture, and RocksDB batch persistence. `DeleteUTXO`, `ConsumeUTXOs`, and `SetUTXOs` restore their in-memory registry state when persistence fails; public `StoreUTXOs` is serialized too. A deterministic Consume/Mint barrier regression confirms Mint cannot reach persistence while the consumer owns its captured snapshot and confirms the Mint output survives a RocksDB reload.

**Verification:** `git diff --check` passed. Both changed C++ translation units passed `clang++ -fsyntax-only` using the project release compile flags. The isolated CMake target could not be run because its checked-in protobuf command passes a physical `/private/tmp/...` proto path with a logical `/tmp/.../src` include prefix, which `protoc` rejects before compiling the target.

---

_Fixed: 2026-08-24T14:44:06Z_
_Fixer: the agent (gsd-code-fixer)_
_Iteration: 1_
