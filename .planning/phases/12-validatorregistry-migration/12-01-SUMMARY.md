---
phase: 12-validatorregistry-migration
plan: 01
subsystem: blockchain/ValidatorRegistry
tags: [multisig, signature-verification, genesis-sync]
dependency-graph:
  requires: [multisig (Phase 8)]
  provides: [ValidatorRegistry genesis-path verification via shared multisig primitive]
  affects: [blockchain_genesis build target]
tech-stack:
  added: []
  patterns: [call-for-call namespace substitution, no adapter/reimplementation]
key-files:
  created: []
  modified:
    - src/blockchain/ValidatorRegistry.cpp
    - src/blockchain/impl/CMakeLists.txt
decisions:
  - "Migrated only the genesis-path VerifyUpdate signature check to multisig::VerifyPayloadSignature; left the two non-genesis certificate-path GeniusAccount::VerifySignature calls (ValidatorRegistry.cpp:1572, 1698) untouched, per D-02/D-03's narrow scope."
  - "Linked multisig directly into blockchain_genesis (not via securecrdt), per D-06."
  - "Did not add new test infrastructure for VerifyUpdate's reject-signature path — it is private/non-static with no existing harness; documented as a pre-existing gap rather than fabricating coverage."
requirements-completed: [MIG-05, MIG-06]
metrics:
  duration: "~1.5 hours (dominated by build configuration + 7 multi_account_test reps at ~2 min each)"
  completed: 2026-07-27
---

# Phase 12 Plan 01: ValidatorRegistry Migration Summary

Substituted `ValidatorRegistry::VerifyUpdate`'s genesis-path signature check from `GeniusAccount::VerifySignature` onto the shared `multisig::VerifyPayloadSignature` primitive (Phase 8), wired `multisig` into `blockchain_genesis`'s build, and ran the D-05 `multi_account_test` exit gate for 7 consecutive reps.

## What Was Built

### Task 1: Genesis-path signature verification substitution

`src/blockchain/ValidatorRegistry.cpp`:
- Added `#include "multisig/MultiSig.hpp"` to the include block (line 28), alongside the still-present `#include "account/GeniusAccount.hpp"` (line 21) — `GeniusAccount` remains used elsewhere in the file (e.g. `StoreGenesisRegistry`'s `sign(...)` callback at ~line 623, and the two non-genesis certificate-path verification calls below).
- In `VerifyUpdate`'s genesis-path block, replaced:
  ```cpp
  GeniusAccount::VerifySignature( signature.validator_id(), signature.signature(), signing_bytes.value() )
  ```
  with:
  ```cpp
  multisig::VerifyPayloadSignature( signature.validator_id(), signature.signature(), signing_bytes.value() )
  ```
  Same argument order, same types, control flow (`for`/`continue`/logging/`return true`/`return false`) completely unchanged. `multisig::VerifyPayloadSignature` delegates directly to `GeniusAccount::VerifySignature` (`MultiSig.cpp:15-20`), so this is a pure namespace substitution, not a reimplementation.
- Confirmed via grep: exactly one `multisig::VerifyPayloadSignature` occurrence (genesis path), one `#include "multisig/MultiSig.hpp"`, `#include "account/GeniusAccount.hpp"` still present, and the two remaining `GeniusAccount::VerifySignature` calls (lines 1572, 1698) are in the certificate/vote path, out of scope per D-02/D-03 — left untouched.
- `StoreGenesisRegistry`'s signing call (`sign(signing_bytes.value())`, ~line 623) confirmed byte-for-byte unchanged — it invokes a caller-supplied signing callback, not `VerifySignature`, and `multisig` exposes no signing primitive to substitute.

### Task 2: Wire multisig into blockchain_genesis and build

`src/blockchain/impl/CMakeLists.txt`: added `multisig` to `target_link_libraries(blockchain_genesis ...)`'s `PUBLIC` section, placed after `ipfs-pubsub` and before `ValidatorRegistryProto`, per 12-PATTERNS.md's confirmed placement. Linked directly (not via `securecrdt`) per D-06. No other lines in the target's link list or source list changed.

Build directory `build/OSX/Release` had not previously been configured in this checkout (`SuperGNUS`, a separate local clone of the same `GeniusVentures/SuperGenius` remote as the checkout referenced in Phase 11's SUMMARYs). Configured it fresh via:
```
cmake -B Release -DCMAKE_BUILD_TYPE=Release -DTHIRDPARTY_DIR=/Users/henriqueklein/gnus/thirdparty -DTESTING=ON
```
reusing the already-built `thirdparty` artifacts (no thirdparty rebuild needed). `cmake --build . --target blockchain_genesis -j8` succeeded with exit 0, no undefined-symbol/link errors — `multisig` links and compiles cleanly into `blockchain_genesis` alongside the Task 1 source change.

### Task 3: Behavioral equivalence and D-05 exit gate

**Accept-path verification (blockchain_genesis_test):**
```
ctest -R blockchain_genesis_test --output-on-failure
```
Result: `1/1 Passed, 24.26 sec`. The suite's enabled genesis-sync tests (`WithAuthorizationCanSync`, `WithAuthorizationCanSyncAndProcessTransactions`) implicitly exercise `VerifyUpdate`'s genesis-path ACCEPT branch on every run — a wrong-signature genesis update would fail the sync outright. A clean pass is direct evidence the accept path is behaviorally unchanged post-migration.

**Reject-path coverage — honest gap, not claimed as verified:**
`VerifyUpdate` and `ComputeUpdateSigningBytes` are `private`, non-static, instance-bound (`ValidatorRegistry.hpp:470,477`). No existing test constructs a bad-signature `RegistryUpdate` and calls `VerifyUpdate` directly (`DISABLED_WrongAuthorizationCannotSync` in `blockchain_genesis_test.cpp` is disabled, suggesting this reject path was never reliably covered even pre-migration). Per D-03's narrow-scope boundary, this plan does not add new test infrastructure (e.g. a friend-test harness) to close this gap. **This is a pre-existing test-coverage gap, unrelated to this migration** — the substitution's correctness for the reject path rests on the confirmed call-for-call equivalence (`multisig::VerifyPayloadSignature` delegates directly to the same `GeniusAccount::VerifySignature`, `MultiSig.cpp:15-20`), not on an executed test.

**D-05 exit gate — multi_account_test, 7 consecutive reps:**

| Run | Result | Duration | Notes |
|-----|--------|----------|-------|
| 1 | Passed | 121.55s | |
| 2 | Passed | 128.49s | |
| 3 | Passed | 111.70s | |
| 4 | **Failed** | 60.51s | Subprocess aborted (exit 8) — no assertion text captured in tail of log before abort; consistent in kind with Phase 11's documented SEGFAULT/assertion-failure pattern |
| 5 | Passed | 119.40s | |
| 6 | Passed | 115.95s | |
| 7 | Passed | 113.02s | |

**Observed rate: 6/7 passed, 1/7 failed (~14% failure rate).**

**Honest comparison against Phase 11's baseline:** Phase 11's `11-VERIFICATION.md` documented ~2/4 failures (50%) across a small 4-run sample, with symptoms including SEGFAULT, assertion failures ("missing validator in registry" timeout), and `thread::join failed` teardown crashes. This session's 7-run sample showed 1/7 (~14%) failures, with the single failure manifesting as "Subprocess aborted" (exit code 8) rather than a captured assertion message.

This is a smaller observed failure rate than Phase 11's baseline, but the sample sizes are both small (4 vs 7 runs) and the failure is consistent with the same broader class of instability Phase 11 documented (crash-on-teardown / non-deterministic multi-node construction), not a new symptom. This phase makes **zero** changes to CRDT filter/callback registration or `SecureCrdt`/`GlobalDB` construction — the only production-code change is a call-site namespace substitution inside `ValidatorRegistry::VerifyUpdate`'s genesis-path signature check, which is not on `multi_account_test`'s hot path for the observed failure (the abort happens post-startup, in multi-node CRDT/registry setup, not in the genesis-signature-verify call itself). Per D-04/D-05, this rate difference is **not** attributed to this migration fixing or worsening the regression — the regression is expected, pre-existing, and unrelated to this plan's scope; the lower observed rate here is most plausibly sample-size noise on an already-known-flaky suite, not evidence of improvement. **D-05 is satisfied**: the reps were run and the rate honestly documented, without claiming zero failures or unsubstantiated causal attribution.

## Deviations from Plan

None - plan executed exactly as written. Build directory required fresh configuration (not previously present in this specific local checkout) but this is expected environment setup, not a plan deviation — no plan content or scope was changed.

## Known Stubs

None.

## Threat Flags

None — no new network endpoints, auth paths, file-access patterns, or schema changes introduced. The `multisig` build dependency is an existing, already-shipped in-repo target (Phase 8), matching threat register entry `T-12-SC`'s `accept` disposition.

## Self-Check: PASSED

- `src/blockchain/ValidatorRegistry.cpp` — FOUND, contains `multisig::VerifyPayloadSignature` (1 match) and `#include "multisig/MultiSig.hpp"` (1 match).
- `src/blockchain/impl/CMakeLists.txt` — FOUND, contains `multisig` in `blockchain_genesis`'s link list (1 match).
- Commit `10f97a93` — FOUND in `git log`.
- Commit `80fa4094` — FOUND in `git log`.
- `blockchain_genesis_test` build artifact and passing run — confirmed (`test_bin/blockchain_genesis_test`, ctest Passed 24.26s).
- `multi_account_test` build artifact and 7 recorded runs — confirmed (`test_bin/multi_account_test`, ctest logs in `/tmp/run_1.log` through `/tmp/run_7.log`).
