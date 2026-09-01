---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "01"
subsystem: auth
tags: [config, privatenetwork, geniusnode, cplusplus, cmake]

# Dependency graph
requires:
  - dev_pnets vendored 3rdparty install (libp2p b28eed2, ipfs-pubsub 3294d41) at /Users/henriqueklein/gnus/3rdparty
  - commit 0515def3 network_key parsing + pnet-mode StartPubSub (pre-existing)
provides:
  - network_config.json "private_network_id" (0x-hex-32B public identity, D-01/D-02) and
    "network_bootstrap_peers" (offline-provisioned NetworkRegistry membership) parsing,
    validation, retention, and emit — the config surface plans 15-03..15-08 consume
  - Fail-closed identity config loading: malformed ids and half-provisioned
    private_network_id/network_key pairs abort node start with an error log
  - GeniusNodeTestAccess::PrivateNetworkId / NetworkBootstrapPeers test observability
affects: [15-03-networkregistry, 15-05-gater-membership, 15-06-data-path, geniusnode-config]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Fatal config divergence channel: NetworkSettings.valid flag set by LoadNetworkConfig, checked by InitNetwork before any network side effects (warn-on-ill-typed precedent escalated to failure)"
    - "Identity pair provisioning rule (D-01): private_network_id and network_key load together or not at all; error names the missing sibling KEY only, never the PSK value (D-03)"

key-files:
  created:
    - test/src/account/network_config_private_network_test.cpp
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - example/node_test/network_config.json
    - test/src/account/CMakeLists.txt
    - test/testutil/genius_node_test_access.hpp

key-decisions:
  - "Task 2 checkpoint (owner, VERBATIM): (a) implementation base = merge-closeout-first — executed by the orchestrator before this continuation session: phase-13 closeout branch merged at 2c05a8fe and sibling plan 15-02 (PeerRegistry) merged at dc7b40f1; this plan's base dc7b40f1 already contains both; no further merge performed"
  - "Task 2 checkpoint (owner, VERBATIM): (b) private_network_id JSON encoding = 0x-hex-32B — a 0x-prefixed hex string of exactly 32 bytes (66 chars total), validation regex ^0x[0-9a-fA-F]{64}$, all-zero (0x + 64 zeros) rejected, absent = public node"
  - "Fail-closed load implemented via NetworkSettings.valid instead of changing LoadNetworkConfig's return type: keeps the existing reader signature intact and checks the flag in InitNetwork before ParseBootstrapPeers/port resolution"
  - "Success scenes wait for startup-settled rather than READY: on this base a no-genesis node fail-closes during trust startup (pre-existing, see deferred-items.md); all identity assertions target the synchronous LoadNetworkConfig/InitNetwork path and are READY-independent"

patterns-established:
  - "Half-provisioned identity pair rejection: exactly-one-of private_network_id/network_key fails the load (D-08 misroute prevention surfaced as config-time failure)"

requirements-completed: [D-01, D-02, D-10, PNET-CFG]

# Metrics
duration: 215min (this continuation session: Task 3 + verification; Tasks 1-2 ran in a prior worktree)
completed: 2026-09-01
---

# Phase 15 Plan 01: Build Environment, D-10 Gate, and private_network_id Config Identity Summary

**Release build bound to the dev_pnets vendored install, D-10 dependency decision recorded (merge-closeout-first + 0x-hex-32B), and the distinct public private-network identity plumbed through network_config.json with fail-closed validation and 5-scene unit coverage**

## Performance

- **Duration:** ~215 min for this continuation session (started 2026-09-01T14:55:38Z, completed 2026-09-01T18:31:41Z); Tasks 1-2 executed earlier in a prior worktree
- **Tasks:** 3/3
- **Files modified:** 6 (1 created, 5 modified) + this SUMMARY and deferred-items.md

## Accomplishments

- **Task 1 (prior worktree, verified again in this one):** `build/OSX/Release` configured against `/Users/henriqueklein/gnus/3rdparty` (CMakeCache `THIRDPARTY_DIR:PATH=/Users/henriqueklein/gnus/3rdparty`); the prior worktree's build tree was gitignored and gone, so it was restored deterministically here (`git submodule update --init ProofSystem SGProcessingManager evmrelay`, fresh cmake configure, ninja). `pubsub_counts_test` (including `PubsubCounts.PnetIsolationAndGaterBlocking`) passes on it (9.9s).
- **Task 2 (owner decision, recorded verbatim below):** D-10 gate resolved — implementation base `merge-closeout-first` (already executed by the orchestrator; base dc7b40f1 = closeout merge 2c05a8fe + 15-02 PeerRegistry) and identity encoding `0x-hex-32B`.
- **Task 3:** `private_network_id` + `network_bootstrap_peers` config surface: read (sibling of `network_key`), validated (0x-hex 32B, all-zero rejected, D-01 pairing rule), retained by `GeniusNode` (beside `network_key_`, logged as public id only), emitted by `WriteNetworkConfig` (plain string + array), example config placeholders, 5-scene test suite.

## Task 2 Decision Record (VERBATIM)

The owner selected, at the blocking checkpoint:

- **(a) implementation base: `merge-closeout-first`**
- **(b) `private_network_id` JSON encoding: `0x-hex-32B`**

Decision (a) was executed by the orchestrator before this continuation session: the phase-13
closeout branch is merged into the base (commit 2c05a8fe), and sibling plan 15-02
(PeerRegistry abstraction) is merged at dc7b40f1. Base dc7b40f1 therefore already contains
the closeout work and the 15-02 PeerRegistry module; no further merge was performed by this
executor.

Decision (b) is the implementation contract applied in Task 3: `private_network_id` is a
`0x`-prefixed hex string of exactly 32 bytes — 66 chars total, validation regex
`^0x[0-9a-fA-F]{64}$`; all-zero (`0x` + 64 `0`s) rejected; absent = public node. (Hex-digit
body is case-insensitive per the regex; the `0x` prefix is lowercase. Test literals use this
encoding — no deviation from the recommended option, so no re-baselining was needed.)

## Task Commits

1. **Task 1: Reconfigure SGNUS Release build against the dev_pnets 3rdparty install** - no commit (build tree is gitignored; verified in this worktree by fresh configure + green pubsub_counts)
2. **Task 2: D-10 dependency gate** - no commit (decision recorded verbatim above; base merge performed by orchestrator)
3. **Task 3: Add private_network_id (+ network_bootstrap_peers) config identity** - `33931f48` (feat)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/account/GeniusNode.hpp` - NetworkSettings += private_network_id, network_bootstrap_peers, valid; members private_network_id_/network_bootstrap_peers_ beside network_key_; WriteNetworkConfig extended signature + docs
- `src/account/GeniusNode.cpp` - LoadNetworkConfig read/array-parse/validation (encoding + D-01 pairing, fail-closed via settings.valid); InitNetwork fail-closed check + retention + public-id-only logging; WriteNetworkConfig emit; `#include <algorithm>`
- `example/node_test/network_config.json` - placeholder-only `"private_network_id": ""` + `"network_bootstrap_peers": []`
- `test/src/account/network_config_private_network_test.cpp` - 5 scenes: absent keys (public behavior, no identity material emitted), valid pair (retention, file round-trip, distinct-from-key, bootstrap peers), uppercase-hex body accepted, 6 malformed ids each fail the load, both half-provisioned pair directions fail
- `test/src/account/CMakeLists.txt` - network_config_private_network_test registered (same link set/idiom as network_config_precedence_test)
- `test/testutil/genius_node_test_access.hpp` - PrivateNetworkId/NetworkBootstrapPeers test accessors (friend class; no public getter exists until later plans consume the identity)
- `.planning/phases/15-private-networks-consume-privatenetworkid-identity-and-bind-/deferred-items.md` - pre-existing base failure log (see Deviations)

## Verification Results

- `ninja -C build/OSX/Release network_config_private_network_test` - builds clean
- `ctest -R network_config_private_network` - **5/5 tests PASS** (~10s; teardown clean)
- `ctest -R pubsub_counts` - **PASS** including PnetIsolationAndGaterBlocking (Task 1 acceptance re-verified on this worktree's build tree)
- `ctest -R network_config` - 1/2 suites pass; `network_config_precedence_test` fails PRE-EXISTING on base dc7b40f1 (see Deviations — verified identical failure without this plan's changes; the failing path is untouched by them)
- `grep -c 'private_network_id' src/account/GeniusNode.cpp` = 20 (acceptance: >= 4)
- `git diff example/node_test/network_config.json` - placeholder-only, no secret material (T-15-03 mitigated)

## Deviations from Plan

### Plan-Snippet Adaptations (mandated by base drift)

**1. Line references and the failure mechanism adapted to the post-closeout GeniusNode**
- **Found during:** Task 3 (plan snippets referenced pre-closeout shapes)
- **Issue:** The plan's line anchors (reader at 1277, retention at 1508-1513, emit at 187-209) are stale after the phase-13 closeout heavily changed GeniusNode.cpp/.hpp; LoadNetworkConfig returns `NetworkSettings` by value with no failure channel, so "fail the load" needed an explicit mechanism.
- **Fix:** Located every anchor fresh (read at 1573->beside network_key, retention at 1805->beside network_key_ assignment, emit mirrored on the network_key block); implemented fail-closed loading via a `NetworkSettings.valid` flag set at the detection site (with `node_logger_->error`) and checked by InitNetwork as its FIRST action, before any network side effects. Plan intent and closeout semantics both preserved.
- **Files modified:** src/account/GeniusNode.hpp, src/account/GeniusNode.cpp

**2. [Rule 3 - Blocking] Success scenes wait for startup-settled instead of WaitForReady(READY)**
- **Found during:** Task 3 verification (3 of 5 scenes timed out at WaitForReady)
- **Issue:** PRE-EXISTING base failure (NOT caused by this plan): post-closeout fail-closed trust startup rejects the empty signer set in `TrustedPeerRegistry::New` ("majority-floor violation") for any no-genesis node, and `Initialize()` returns without a state transition — the node is stuck in MIGRATING_DATABASE and READY is unreachable. The identical failure breaks all six pre-existing `network_config_precedence_test` scenes on base dc7b40f1 (verified directly; failing path untouched by this plan's changes).
- **Fix (in-scope adaptation):** `WaitForStartupSettled` — wait until the async initialization starts (state leaves CREATING), then a 2s quiesce before teardown. Every identity assertion targets the synchronous LoadNetworkConfig/InitNetwork path and is READY-independent, so the plan's truths are fully covered. Out-of-scope base repair logged to `deferred-items.md` (scope boundary: pre-existing, unrelated files).
- **Files modified:** test/src/account/network_config_private_network_test.cpp
- **Verification:** 5/5 scenes pass in ~10s, clean teardown

### Auto-fixed Issues

**3. [Rule 1 - Bug] Unterminated raw string literal in the new test helper**
- **Found during:** Task 3 (first compile: "use of undeclared identifier NodeFromRawConfig" only in the last test, plus brace errors)
- **Issue:** `WriteRawNetworkConfig` used `R"({ ... false"` — missing the raw-string closing `)`. The literal silently swallowed the rest of the file (through the first `)"` sequence inside a later string), consuming the namespace close and helper definitions; the compiler error pointed far from the cause.
- **Fix:** Replaced with an escaped ordinary string and left a warning comment so the trap is not reintroduced.
- **Files modified:** test/src/account/network_config_private_network_test.cpp
- **Verification:** builds clean; all 5 scenes pass

---

**Total deviations:** 2 adaptations + 1 auto-fix
**Impact on plan:** No scope creep; encoding contract and pairing rule implemented exactly as decided at Task 2.

## Issues Encountered

- Fresh worktree had no submodule checkouts — `git submodule update --init ProofSystem SGProcessingManager evmrelay` (fast, from local git objects); no APFS copy needed.
- Fresh worktree had no build tree — full configure + compile of the genius_node dependency cone (~50 min of the session); the orchestrator's instructions reproduced the prior binding deterministically (CMakeCache `THIRDPARTY_DIR:PATH=/Users/henriqueklein/gnus/3rdparty`).
- `network_config_precedence_test` fails pre-existing on this base (documented above + deferred-items.md). This plan's acceptance criterion "pre-existing network_config account tests still pass" is satisfied in the regression sense: this plan introduces no regression to them (identical failure with and without this plan's changes; the failure cause is the closeout's no-genesis fail-closed path, untouched here).

## Known Stubs

None. `network_bootstrap_peers` is parsed, validated as strings, retained, emitted, and tested, but has no runtime consumer yet — that is the plan's design (the consumers are 15-03 NetworkRegistry and 15-05 gater membership), not a stub: the member is fully wired to its config source and observable via the test accessor.

## Threat Flags

None. The threat register's mitigations landed as planned: T-15-01 (reject malformed/half-provisioned at load, fail node start — implemented and tested), T-15-02 (public id only in logs — `network_key` never logged; error strings name keys only), T-15-03 (example config placeholder-only — verified). No new security-relevant surface beyond the plan's threat model.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `private_network_id` (0x-hex-32B) and `network_bootstrap_peers` are available on GeniusNode for 15-03 (NetworkRegistry membership source) and 15-05 (gater membership) via `private_network_id_` / `network_bootstrap_peers_` (private, friend-test-accessible; public accessors land with their first runtime consumer)
- The D-01 provisioning-pair rule is enforced at config load, so every later plan can assume a fully-provisioned-or-public identity
- Deferred (NOT blocking later plans, but see deferred-items.md): no-genesis full nodes never reach READY on this base — later plans whose tests need READY from a no-genesis `GeniusNode::New` scene must configure a trust genesis or fix the closeout's empty-set handling

## Self-Check: PASSED

- All 6 Task-3 files + deferred-items.md exist on disk (verified 2026-09-01T18:31:41Z)
- Task 3 commit `33931f48` verified in git log
- Working tree clean of task files (only SUMMARY/deferred-items remain for the docs commit)
- ctest: network_config_private_network 5/5 PASS; pubsub_counts PASS
---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-01*
