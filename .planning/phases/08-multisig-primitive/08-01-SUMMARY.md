---
phase: 08-multisig-primitive
plan: 01
subsystem: multisig
tags: [crypto, signature-verification, quorum]
dependency-graph:
  requires: []
  provides: [multisig-primitive, VerifyPayloadSignature, EvaluateQuorum]
  affects: [09-securecrdt-layer]
tech-stack:
  added: []
  patterns:
    - "Delegated signature verification: no custom crypto, forwards to GeniusAccount::VerifySignature"
    - "Dedup-before-verify ordering in quorum evaluation (mirrors ValidatorRegistry's pure-static evaluator pattern)"
key-files:
  created:
    - src/multisig/MultiSig.hpp
    - src/multisig/MultiSig.cpp
    - src/multisig/CMakeLists.txt
    - test/src/multisig/CMakeLists.txt
    - test/src/multisig/multisig_verify_test.cpp
    - test/src/multisig/multisig_quorum_test.cpp
  modified:
    - src/CMakeLists.txt
    - test/src/CMakeLists.txt
decisions:
  - "EvaluateQuorum built as a stateless pure function (no signer-role/epoch tracking) per D-03/D-04 — replay protection deferred to Phase 9's CRDT-backed caller, per threat register T-08-05"
metrics:
  duration: "~45m"
  completed: 2026-07-21
---

# Phase 8 Plan 01: MultiSig Primitive Summary

Standalone `sgns::multisig` library (`VerifyPayloadSignature` + `EvaluateQuorum`) that delegates all crypto to the existing `GeniusAccount::VerifySignature`, with zero CRDT/node/pubsub link dependency.

## What Was Built

- **`src/multisig/MultiSig.hpp`/`.cpp`**: `VerifyPayloadSignature(address, signature, payload)` forwards directly to `sgns::GeniusAccount::VerifySignature` — no signing-bytes construction, no reimplemented crypto. `EvaluateQuorum(signer_set, threshold, collected_signatures, payload)` returns a `QuorumResult{has_quorum, valid_unique_count}` via a dedup-before-verify loop: for each `(address, signature)` pair, skip if already counted, skip if `address` is not in `signer_set`, skip if the signature fails verification — only then insert into the unique-signer set. `has_quorum = valid_unique_signers.size() >= threshold`.
- **`src/multisig/CMakeLists.txt`**: `add_library(multisig MultiSig.cpp)` linking only `sgns_genius_account` (`PUBLIC`), installed via `supergenius_install(multisig)`. No `crdt_globaldb`, `ipfs-pubsub`, or `genius_node` link (verified by grep: 0 matches).
- **`test/src/multisig/multisig_verify_test.cpp`**: 4 cases — valid signature verifies true; tampered payload verifies false; tampered signature (bit-flip) verifies false; wrong-size signature verifies false. Fixture follows the exact pattern from `test/src/account/account_signature_test.cpp` (`GeniusAccount::SetSecureStorageFactory` injecting `MemorySecureStorage`, `GeniusAccount::NewFromPrivateKey` for a real signing key, no keychain prompts).
- **`test/src/multisig/multisig_quorum_test.cpp`**: 6 cases covering the full `<behavior>` block — N-1 (2/5, no quorum), exactly-N (3/5, quorum), all-M (5/5, quorum), duplicate-signer-with-garbage dedup (must not flip quorum), unauthorized-signer exclusion, and threshold=0 with empty input (quorum true, count 0). Five distinct `GeniusAccount` instances (5 deterministic private keys) plus one outsider account not in the signer set.
- Wired `add_subdirectory(multisig)` into both `src/CMakeLists.txt` and `test/src/CMakeLists.txt`.

Task 1 introduced `EvaluateQuorum`/`QuorumResult` in the header with a stub body (returns default `QuorumResult{}`) so the header contract was stable before Task 2's real implementation replaced the stub body — both were committed as separate atomic commits per the plan's `tdd="true"` staging.

## Deviations from Plan

None — plan executed exactly as written, including the Task 1 stub → Task 2 real-implementation staging called out explicitly in the `<action>` blocks.

## Build/Test Verification (orchestrator follow-up)

The executor's worktree had no configured CMake build directory, so it could only verify structurally (see checks below). The orchestrator subsequently ran the real build against the project's existing configured build (`build/OSX/Release`, sibling `thirdparty` deps already built):

- Found a pre-existing, unrelated compile error blocking the whole build: `AccountMessenger.cpp:1029` passed a `const std::string` member to `GossipPubSub::getPeerCount(std::string&)` (non-const ref param) — introduced by commit `e46f23d2` ("AccountManager skips requests if there are none"), which had apparently never successfully compiled on this platform/toolchain. Fixed with a minimal local copy (commit `a64761b6`), out of Phase 8's scope but necessary to unblock verification.
- `cmake --build . --target multisig_verify_test multisig_quorum_test` — **succeeded**, both targets compile and link.
- `ctest -R multisig --output-on-failure` — **2/2 tests passed** (`multisig_verify_test`, `multisig_quorum_test`).
- Link-dependency check: `link.txt` for both test targets contains no `crdt`/`genius_node`/`pubsub` libraries — MSIG-03 confirmed empirically, not just structurally.
- Full project build (`cmake --build .`) succeeded; full `ctest` run: 72/74 tests passed. Two unrelated pre-existing failures investigated and confirmed out of scope:
  - `account_management_test` (SEGFAULT on full-suite run) — did not reproduce on isolated rerun or a second full-suite rerun (5/5 passed); flaky pre-existing test infra, unrelated to any Phase 8 or fix-commit change.
  - `transaction_sync_test.MissedCrdtHeadIsRecoveredAfterReconnect` — reproduces consistently, but originates from commit `a11f8386` ("Added reconnection test in transaction_sync") + `e4676de8` ("Fixed tests"), part of unrelated bridge-relayer/coverage-analysis work already on `develop` before this milestone. Not touched by Phase 8 or the unblocking fix.

MSIG-01, MSIG-02, MSIG-03 are now fully proven by actual compiled/executed tests, not just structural inspection.

## Acceptance Criteria (verified)

- `grep -n "add_library(multisig" src/multisig/CMakeLists.txt` — matches
- `grep -n "GeniusAccount::VerifySignature" src/multisig/MultiSig.cpp` — matches
- `grep -n "add_subdirectory(multisig)" src/CMakeLists.txt` and `test/src/CMakeLists.txt` — both match
- `grep -c "crdt_globaldb\|ipfs-pubsub\|genius_node" src/multisig/CMakeLists.txt` — 0
- Dedup-before-verify ordering: `valid_unique_signers.count(address)` check appears before `VerifyPayloadSignature(...)` call in `EvaluateQuorum`'s loop body — confirmed by inspection
- `grep -c "\.pb\.h\|genius_node\|crdt" test/src/multisig/multisig_quorum_test.cpp` — 0
- `ctest -R multisig_verify_test` / `ctest -R multisig` — **run, 2/2 passed** (orchestrator follow-up)

## Known Stubs

None in the shipped code — the Task 1 `EvaluateQuorum` stub was fully replaced by Task 2's real implementation within this same plan; no stub remains in the final state.

## Threat Flags

None — all new surface (`VerifyPayloadSignature`, `EvaluateQuorum`) is already covered by the plan's `<threat_model>` (T-08-01 through T-08-06).

## Self-Check

- FOUND: src/multisig/MultiSig.hpp
- FOUND: src/multisig/MultiSig.cpp
- FOUND: src/multisig/CMakeLists.txt
- FOUND: test/src/multisig/CMakeLists.txt
- FOUND: test/src/multisig/multisig_verify_test.cpp
- FOUND: test/src/multisig/multisig_quorum_test.cpp
- Commit 245c728d: FOUND
- Commit 10cfe5d9: FOUND

## Self-Check: PASSED (build+test verified by orchestrator follow-up)
