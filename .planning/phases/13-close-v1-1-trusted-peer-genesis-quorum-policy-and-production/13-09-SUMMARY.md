---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "09"
subsystem: trust-operations
tags: [genesis, securecrdt, trusted-peer, burn-config, cli, secret-lifecycle, cpp]

requires:
  - phase: 13-05
    provides: durable trusted-peer and burn candidate approval and activation APIs
  - phase: 13-06
    provides: reusable production GlobalDB networking composition
provides:
  - one-shot reviewed trusted-peer genesis ceremony with protected ephemeral-key handling
  - local-only sgns-trust candidate listing, proposal, and exact approval operations
  - durable-confirmation-before-cleanup and receive-path-no-signing verification
affects: [13-07, 13-10, first-boot-e2e, operator-runbook, production-trust-operations]

tech-stack:
  added: []
  patterns:
    - injected ceremony I/O, signer, network, confirmation, cleanse, and unlink seams
    - explicit local administration over existing content-addressed SecureCrdt candidates

key-files:
  created:
    - src/trustedpeer/genesis_tool/GenesisCeremony.hpp
    - src/trustedpeer/genesis_tool/GenesisCeremony.cpp
    - src/trustedpeer/genesis_tool/LocalTrustAdmin.hpp
    - src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp
    - src/trustedpeer/genesis_tool/main.cpp
    - src/trustedpeer/genesis_tool/CMakeLists.txt
    - test/src/trustedpeer/trust_genesis_tool_test.cpp
  modified:
    - src/trustedpeer/CMakeLists.txt
    - test/src/trustedpeer/CMakeLists.txt

key-decisions:
  - "sgns-trust consumes canonical GenesisManifest bytes and a caller-supplied existing production CRDT topic."
  - "Private key material enters only through an owner-controlled 0600 no-symlink file or echo-disabled terminal input and never through argv or environment variables."
  - "Local propose and approve operations attempt durable activation after the explicit signature while receive and list paths remain signer-free."

patterns-established:
  - "Ephemeral key input is cleansed before success-only unlink, and every unsuccessful ceremony retains the file with a critical recovery instruction."
  - "Content-addressed candidate IDs are the sole approval selector; no remote administration surface or additional transport is introduced."

requirements-completed: [BOOT-01, BOOT-02, BOOT-03, SCRDT-04, TPR-01, TPR-02, BURN-01, TEST-01]

duration: 22min
completed: 2026-08-12
---

# Phase 13 Plan 09: One-Shot Genesis and Local Trust Administration Summary

**A review-gated `sgns-trust` command now submits canonical genesis through production GlobalDB/SecureCrdt, removes the ephemeral key only after durable confirmation, and exposes explicit local-only policy and burn approvals.**

## Performance

- **Duration:** 22 min
- **Started:** 2026-08-12T15:56:00Z
- **Completed:** 2026-08-12T16:17:32Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- Added a reusable genesis ceremony that displays the canonical network, bootstrapper, ordered peer set, thresholds, initial burn value, and fingerprint before requiring an exact typed match.
- Enforced owner-controlled regular 0600 key files with no symlinks, protected terminal input with echo disabled, persistence-free `GeniusSigner` import, `OPENSSL_cleanse`, and confirmation-gated unlink.
- Added `sgns-trust` with exactly `genesis`, `list`, `propose-policy`, `propose-burn`, and `approve`, composed on `GlobalDbNetworkComposition` and the caller's existing SecureCrdt topic.
- Proved receive/list operations never sign, proposals add one local approval, repeated approval is deduplicated, and approval targets only the exact candidate ID.

## Task Commits

1. **Task 1 RED: Genesis ceremony contracts** - `4d557d7f` (test)
2. **Task 1 GREEN: One-shot genesis ceremony** - `db77d8ee` (feat)
3. **Task 2 RED: Local administration contracts** - `13b46e46` (test)
4. **Task 2 GREEN: Local sgns-trust administration** - `75eca70b` (feat)

## Files Created/Modified

- `src/trustedpeer/genesis_tool/GenesisCeremony.hpp/.cpp` - Canonical review, protected secret loading, persistence-free signing, production submission seam, durable confirmation wait, cleanse, and success-only unlink.
- `src/trustedpeer/genesis_tool/LocalTrustAdmin.hpp/.cpp` - Signer-free listing plus explicit policy/burn proposal and exact candidate approval routing.
- `src/trustedpeer/genesis_tool/main.cpp` - Thin local CLI that builds the production GlobalDB, SecureCrdt, trust store, TPR, and BurnConfig composition.
- `src/trustedpeer/genesis_tool/CMakeLists.txt`, `src/trustedpeer/CMakeLists.txt` - Installable local-admin library and `sgns-trust` executable wiring.
- `test/src/trustedpeer/trust_genesis_tool_test.cpp`, `test/src/trustedpeer/CMakeLists.txt` - Secret lifecycle, failure retention, durable confirmation, signer invocation, deduplication, exact-ID, and burn proposal coverage.

## Decisions Made

- Used the canonical manifest codec as the CLI file contract so the bytes reviewed, fingerprinted, signed, submitted, and durably verified are identical.
- Required callers to supply the existing listen/broadcast topic and passed that one topic through `GlobalDbNetworkComposition` and `SecureCrdt`; the tool defines no transport of its own.
- Kept local administration as one-shot process invocation with protected signer input rather than adding an admin session, HTTP endpoint, JSON-RPC method, or receive-side signer.

## TDD Gate Compliance

- Task 1 RED failed because `GenesisCeremony.cpp` and its public contract did not exist; Task 1 GREEN passed all ceremony, unsafe-file, failure-retention, and secret-surface cases.
- Task 2 RED failed because `LocalTrustAdmin.cpp` and `main.cpp` did not exist; Task 2 GREEN passed all local list/propose/approve and exact-candidate cases.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Attempt activation after explicit local signatures**
- **Found during:** Task 2
- **Issue:** The planned command surface has no separate activation operation, so a candidate that reached quorum through `propose` or `approve` would otherwise remain durable but ineffective.
- **Fix:** Local proposal and approval methods attempt the existing TPR/BurnConfig activation path after successfully adding the explicit signature; below-quorum attempts remain pending.
- **Files modified:** `src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp`
- **Verification:** Admin deduplication, exact-candidate isolation, and burn proposal tests pass; the focused operator approval suite remains green.
- **Committed in:** `75eca70b`

**2. [Rule 3 - Blocking] Isolate BurnConfig-dependent administration from trustedpeer**
- **Found during:** Task 2 build wiring
- **Issue:** Adding `LocalTrustAdmin.cpp` directly to `trustedpeer` would make the foundational library depend back on `burnconfig`, which already depends on `trustedpeer`.
- **Fix:** Added a narrow `local_trust_admin` library in the genesis-tool CMake directory and linked both the CLI and focused test to it.
- **Files modified:** `src/trustedpeer/CMakeLists.txt`, `src/trustedpeer/genesis_tool/CMakeLists.txt`, `test/src/trustedpeer/CMakeLists.txt`
- **Verification:** CMake generation and the combined `sgns-trust`, `trustedpeer`, `burnconfig`, and test build graph pass.
- **Committed in:** `75eca70b`

---

**Total deviations:** 2 auto-fixed (1 missing critical functionality, 1 blocking build issue).
**Impact on plan:** Both fixes preserve the planned local-only architecture and make explicit approvals operational without adding a protocol, endpoint, or package.

## Issues Encountered

- Network-backed fixtures require permission to open ephemeral local libp2p listeners; all focused tests passed in the approved local-listener environment.

## Verification

- `cmake --build build/OSX/Release --target trust_genesis_tool_test -j8 && build/OSX/Release/test_bin/trust_genesis_tool_test` - PASS (10/10).
- `cmake --build build/OSX/Release --target sgns-trust trust_genesis_tool_test -j8 && build/OSX/Release/test_bin/trust_genesis_tool_test --gtest_filter='*Admin*:*Argv*:*Secret*'` - PASS (10/10 selected).
- `ctest --test-dir build/OSX/Release --output-on-failure -R 'trust_genesis_tool|operator_approval|securecrdt_candidate'` - PASS (4/4 targets).
- `cmake --build build/OSX/Release --target crdt_globaldb globaldb_app securecrdt sgns-trust -j8` - PASS.
- `sgns-trust --help` lists exactly the five local operations and no private-key value, secret, token, HTTP, or RPC option - PASS.
- `rg -n "NewFromPrivateKey" src/trustedpeer/genesis_tool` and the remote-control/receive-side auto-sign scan return zero matches - PASS.

## Known Stubs

None.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 13-07 can drive the same production composition for multi-node first-boot confirmation.
- Plan 13-10 can document canonical manifest creation, protected-key handling, and local candidate approval using the shipped command.
- No blocker remains for later Phase 13 production verification.

## Self-Check: PASSED

- All seven created files and both modified CMake files exist.
- All four Task 1/Task 2 RED and GREEN commits exist in repository history.
- Every plan acceptance and overall verification command passes.
- Stub scan found no incomplete production path; empty values found by the mechanical scan are test seams only.
- Threat-surface scan found only the planned local file/terminal and existing production CRDT boundaries; no unplanned network endpoint, authentication path, package, or topic was introduced.
- No tracked file was deleted, and both unrelated pre-existing untracked paths remain untouched.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
