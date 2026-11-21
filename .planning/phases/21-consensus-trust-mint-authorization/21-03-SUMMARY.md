---
phase: 21-consensus-trust-mint-authorization
plan: "03"
subsystem: auth
tags: [defer-mechanism, bounded-memory, network-config, validator-admission-cap, genesis-fingerprint-pin, trust-root, folded-todo]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-01
    provides: nothing directly (parallel wave); ValidatorRegistry setter surface consumed
provides:
  - base::BoundedDeferTracker — generic D-03 mechanism (first-seen deadlines, FIFO cap 4096, single-oldest eviction, injectable clock, thread-safe)
  - network_config.json keys authorization_defer_window_sec (1800 s default) and max_new_validators_per_update (registry default 10) — config-wins, ill-typed keeps default
  - ValidatorRegistry admission cap applied from config at blockchain construction (first production caller of SetMaxNewValidatorsPerUpdate; enforcement test in 21-07)
  - genesis_manifest_fingerprint sgns_config.json pin — fresh-node trust root fails closed (FATAL_TRUST_MISMATCH) on mismatch before any trust startup; empty pin warns and proceeds
  - Example config pinned to its own computed ceremony fingerprint (4f6840f5…, machine-verified through the real boot path)
affects: [21-04-consensus-consumption, 21-06-mint-ingress, 21-07-enforcement-tests, TRUST-02-audit]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Defer-and-hold mechanism is consumer-agnostic: the tracker owns deadlines/FIFO only; verdicts stay with the boundary (mechanism vs policy split)"
    - "First-seen deadline anchoring: replaying a key never extends its holdback window"
    - "Config keys read beside the reconnect knobs with int staging for size_t destinations (negative = ill-typed, keeps default)"
    - "Ceremony pin verified between manifest assembly and controller construction — the tampered manifest never reaches TrustStartupController"

key-files:
  created:
    - src/base/bounded_defer.hpp
    - src/base/bounded_defer.cpp
    - test/src/base/bounded_defer_test.cpp
  modified:
    - src/base/CMakeLists.txt
    - test/src/base/CMakeLists.txt
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/testutil/genius_node_test_access.hpp
    - test/testutil/local_trust_setup.hpp
    - test/src/account/network_config_precedence_test.cpp
    - test/src/account/burnconfig_policy_e2e_test.cpp
    - example/node_test/sgns_config.json

key-decisions:
  - "D-03 window and D-04 cap retained as members (authorization_defer_window_sec_/max_new_validators_per_update_) because NetworkSettings is InitNetwork-local while the cap applies in the INITIALIZING_BLOCKCHAIN state case — same retention pattern as network_key_/network_bootstrap_peers_"
  - "Negative max_new_validators_per_update is treated as ill-typed (keeps default 10): a size_t destination cannot carry it and T-21-0305 defers floors to the registry's existing enforcement"
  - "Pin enforcement sits between manifest assembly and TrustStartupController::New and reuses the sibling fail-closed exit (account_service_switching_=false + FATAL_TRUST_MISMATCH + return); logs carry the rule name and 16-char truncated digests only"
  - "RESEARCH Open Question 5 resolved: the notice's 4f6840f5… fingerprint equals Fingerprint() of the example's own trust fields — the number was machine-correct all along; the pin field now enforces it"
  - "Whole-file JSON replacement including the pin stays an accepted boundary (T-21-0302): the pin catches partial tamper/drift; replacing it too implies local write control equal to controlling the node"

patterns-established:
  - "GeniusNode boot-path adversarial+compat quintet for trust-root tamper: correct-pin boot, tamper-fail-closed, unpinned residual, restart-from-confirmed unchanged, shipped-config-boots-under-own-pin"

requirements-completed: []  # TRUST-01 closes in 21-04 (Consensus.cpp migration); TRUST-02 is an audit deliverable (cap wiring is only its growth-bounds half); MINT-03 (v1 mint admission disable) is 21-05/21-06 scope — see Requirements Status

# Metrics
duration: 38min
completed: 2026-10-20
---

# Phase 21 Plan 03: Shared Plumbing — Bounded Defer, Config Keys, Genesis Pin Summary

**D-03 bounded-defer mechanism with first-seen deadlines and oldest-first FIFO eviction, D-03/D-04 network_config.json keys with safe defaults wired to the ValidatorRegistry admission cap, and the folded trusted-peer-genesis residual closed: fresh-node trust roots are pinnable to a ceremony fingerprint and fail closed on mismatch before any trust startup**

## Performance

- **Duration:** 38 min (2026-10-20T20:25:09Z → 2026-10-20T21:03:03Z)
- **Tasks:** 3 (TDD: RED → GREEN for Tasks 1 and 3; Task 2 shipped with a defaults/override/ill-typed test triplet; plus one format-normalization prerequisite commit)
- **Files modified:** 12 (3 created)

## Accomplishments

- **D-03 mechanism (Task 1):** `base::BoundedDeferTracker` — pure std, policy-free, thread-safe. Deadlines are first-seen anchored (repeated `Defer` reports expiry under the ORIGINAL deadline, never refreshes it — replay cannot extend holdback); at the cap (4096 default) inserting a new key evicts exactly the oldest entry (never a wholesale clear — flooding cannot flush younger entries); evicted/cleared keys re-defer with a fresh window; `SetWindow` applies only to keys deferred after the call. Injectable steady-clock now-provider; 8 unit tests, zero sleep_for.
- **D-03/D-04 config keys + cap wiring (Task 2):** `NetworkSettings` gains `authorization_defer_window_sec` (1800 s default; consumers named at the field — 21-04 ConsensusManager, 21-06 TransactionManager) and `max_new_validators_per_update` (initialized from `ValidatorRegistry::DefaultMaxNewValidatorsPerUpdate`). `LoadNetworkConfig` reads both beside the reconnect knobs (config-wins, ill-typed or negative keeps the default). After `Blockchain::New`, the retained cap is applied via `SetMaxNewValidatorsPerUpdate` — the setter's first production caller (D-04; enforcement test lands in 21-07 per plan).
- **Folded todo residual (Task 3, T-21-0301):** `sgns_config.json` `genesis_manifest_fingerprint` parses into `genesis_manifest_fingerprint_` (presence logged, never the value). At manifest assembly: `has_configured_trust` + non-empty pin requires `manifest->Fingerprint()` to equal the pin, else a critical log (rule name + 16-char truncated digests) and `FATAL_TRUST_MISMATCH` with NO controller constructed from the tampered manifest; empty pin warns "trust root unpinned" and proceeds (documented T-21-0302 residual). The v1.1 persisted-state restart path is untouched and compat-pinned.
- **Example config:** pinned to `4f6840f5bdffca6d51d463c4f02d85bde76b5ce88dd3d1ab4ed59e39663a7afb`; a probe over the example's own trust fields (net 333, 4 peers, bootstrapper, floors 3/3) computed exactly this value — RESEARCH Open Question 5 resolved (the notice's number was machine-correct; now it is enforced, and the boot test proves the shipped pin through the real enforcement path).

## Task Commits

1. **Task 1: base::BoundedDeferTracker (tdd)** — `36c607f9c` (test: RED — API skeleton with no tracking behavior; 6 of 8 cases fail) + `38d7f6934` (feat: GREEN — full mechanism, 8/8)
2. **Task 2: network_config keys + registry cap wiring** — `7a532a05c` (style: behavior-neutral clang-format normalization of GeniusNode so the behavioral diff stays reviewable) + `90c977eda` (feat: fields, reads, retention, cap apply, precedence tests)
3. **Task 3: pinned genesis-manifest fingerprint (tdd)** — `37fabb2c0` (test: RED — five boot cases, tamper case fails closed today's unrestricted boot) + `aff042ddb` (feat: GREEN — parse + enforcement + example pin)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/base/bounded_defer.hpp` — `BoundedDeferTracker` (Doxygen file header; std-only includes; `kDefaultDeferWindow` 30 min, `kDefaultMaxTrackedEntries` 4096).
- `src/base/bounded_defer.cpp` — mechanism implementation; `EvictOverCapLocked` mirrors the evidence-cache while-loop shape (single-oldest per insert).
- `src/base/CMakeLists.txt` — `bounded_defer` library (no dependencies) + install.
- `test/src/base/bounded_defer_test.cpp` — 8 tests (255 lines): expiry flip, first-seen anchoring, single-oldest eviction, evicted-redefer freshness, Clear re-anchor, SetWindow scope, defaults, concurrency smoke.
- `test/src/base/CMakeLists.txt` — `addtest(bounded_defer_test …)`.
- `src/account/GeniusNode.hpp` — `NetworkSettings` fields + retained members (`authorization_defer_window_sec_`, `max_new_validators_per_update_`) + `genesis_manifest_fingerprint_` beside the genesis trust members.
- `src/account/GeniusNode.cpp` — LoadNetworkConfig reads (int staging for the size_t cap), InitNetwork retention, cap apply after `Blockchain::New`, LoadSgnsConfig pin parse, pin enforcement between manifest assembly and `TrustStartupController::New`.
- `test/testutil/genius_node_test_access.hpp` — `AuthorizationDeferWindow` / `MaxNewValidatorsPerUpdate` observers (same rationale as `BootstrapBackgroundMultiplier`).
- `test/testutil/local_trust_setup.hpp` — Rule 3 fix: fully qualified `sgns::securecrdt::SecureCrdt` (order-dependence with `securecrdt_test_node.hpp`'s `test::securecrdt` namespace).
- `test/src/account/network_config_precedence_test.cpp` — three tests: defaults-without-keys (1800 s / 10), config-driven override (2400 s / 3), ill-typed fallback (string window / negative cap).
- `test/src/account/burnconfig_policy_e2e_test.cpp` — `GenesisFingerprintPinE2ETest` fixture (temp config dirs, real GeniusNode boots, wait_condition) with the five behavior cases; pre-existing 6 burnconfig tests unchanged and green.
- `example/node_test/sgns_config.json` — `genesis_manifest_fingerprint` field + notice text notes the boot enforcement.

## Decisions Made

- Cap value reaches the state machine through a retained member (the plan sketched `settings.*` at the blockchain-construction site, but `NetworkSettings` is local to `InitNetwork`); retention follows the existing `network_key_` pattern. The defer window is retained the same way so 21-04/21-06 can consume it.
- `Expired()` returns false for untracked keys (never-deferred = not expired); documented in the header contract.
- The restart-conflict compat case uses a same-path/different-trust-scope configuration (net_id 144 fixed path, subnet_id 963 conflicting scope): v1.1 semantics make trust-FIELD conflicts non-fatal by design (persisted wins), so the existing FATAL path at node level is the network-scope mismatch — the test pins exactly that, unchanged by the pin work.
- Example-pin verification is end-to-end (boot the shipped file under enforcement) rather than test-side re-assembly of the manifest — the assembly logic stays one-home in GeniusNode and a stale pin would turn the boot fatal.

## Requirements Status

`requirements: [TRUST-01, TRUST-02, MINT-03]` in the plan frontmatter is intentionally NOT marked complete:

- **TRUST-01** closes in 21-04 when `Consensus.cpp` callers migrate to the verified loader (21-01's recorded rationale stands).
- **TRUST-02** ("admission growth bounds and cross-certificate equivocation detection are AUDITED, with findings documented") — this plan wires the growth-bounds half (D-04 cap config + apply); the audit deliverable and equivocation-detection findings remain.
- **MINT-03** (legacy-v1 mint admission disable) is 21-05/21-06 scope; this plan ships the defer mechanism those plans consume.

Marking any of the three complete now would falsify the REQUIREMENTS.md traceability rows.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] local_trust_setup.hpp include-order collision**
- **Found during:** Task 3 RED build
- **Issue:** `securecrdt::SecureCrdt::Error` inside the header resolves to `sgns::test::securecrdt` (from `securecrdt_test_node.hpp`) when that header is included first — latent order-dependence that broke the burnconfig TU.
- **Fix:** Fully qualify `sgns::securecrdt::SecureCrdt::Error::DUPLICATE_CANDIDATE_APPROVAL`; header is now order-independent.
- **Files modified:** test/testutil/local_trust_setup.hpp
- **Committed in:** 37fabb2c0

**2. [Rule 3 - Blocking] Restart-conflict case reshaped to the real v1.1 semantics**
- **Found during:** Task 3 RED run
- **Issue:** The plan's case-4 wording ("conflicting config → existing FATAL path") assumed config-vs-persisted field conflicts are fatal; in v1.1 they are deliberately non-fatal (persisted wins, `EveryTrustFieldConflictEmitsOneStructuredCriticalAndKeepsHeads`). Also, a different `net_id` changes the network DB path, giving the restarted node a different trust store (fresh boot, no conflict at all).
- **Fix:** The compat test drives the existing node-level FATAL path that does fire: same network path (net_id 144) with a conflicting trust scope (subnet_id 963) → `TrustStateStore::NETWORK_MISMATCH` → controller `New` error → `FATAL_TRUST_MISMATCH`. Unpinned, so the controller's own check is what fires — proving the v1.1 path unchanged.
- **Files modified:** test/src/account/burnconfig_policy_e2e_test.cpp
- **Committed in:** 37fabb2c0

**3. [Change discipline] Task 2 split into format-normalization + behavior commits**
- **Found during:** Task 2 commit review
- **Issue:** The mandatory clang-format pass on GeniusNode.{hpp,cpp} re-indented ~1,400 lines of pre-existing non-format-clean switch layout; committing it with the behavioral change mixed a structural rewrite into one diff (violates "refactor first, then change").
- **Fix:** `7a532a05c` carries the behavior-neutral normalization alone; `90c977eda` carries only the 174-line behavioral diff.
- **Files modified:** src/account/GeniusNode.hpp, src/account/GeniusNode.cpp
- **Committed in:** 7a532a05c / 90c977eda

**4. [Rule 3 - Blocking] Cap value retained through a member**
- **Found during:** Task 2 implementation
- **Issue:** The plan's apply-site sketch referenced `settings.max_new_validators_per_update` at the blockchain-construction case, but `NetworkSettings` is local to `InitNetwork` (which runs in the ctor, before the state machine).
- **Fix:** Retain to `max_new_validators_per_update_` in `InitNetwork` beside `network_bootstrap_peers_` (established retention pattern); apply from the member after `Blockchain::New`. The window is retained identically for its 21-04/21-06 consumers.
- **Files modified:** src/account/GeniusNode.hpp, src/account/GeniusNode.cpp
- **Committed in:** 90c977eda

**5. [Acceptance strengthening] Defaults pinned by new precedence tests (files outside files_modified)**
- **Found during:** Task 2 verification
- **Issue:** Acceptance allowed "asserted in a config-precedence style test or the existing network_config tests still green"; without a pin, the two defaults would be unobservable until 21-04/21-06/21-07.
- **Fix:** Added `GeniusNodeTestAccess::AuthorizationDeferWindow/MaxNewValidatorsPerUpdate` observers (same dormant-value rationale as `BootstrapBackgroundMultiplier`) and three precedence tests: defaults, override, ill-typed/negative fallback.
- **Files modified:** test/testutil/genius_node_test_access.hpp, test/src/account/network_config_precedence_test.cpp
- **Committed in:** 90c977eda

---

**Total deviations:** 4 auto-fixed/blocking + 1 acceptance strengthening
**Impact on plan:** All required for compilability, the plan's own compat contracts, or repo change discipline — no scope creep.

## Issues Encountered

- clang-tidy cannot consume this tree's `compile_commands.json` directly (dual-arch compile commands); used the filtered single-arch DB at /tmp/sgns-tidy-db extended with a `bounded_defer.cpp` entry. New-code findings: bounded_defer.cpp — zero Required-group findings (only the codebase-wide Recommended noise: scoped_lock/const-correctness/k-constant naming, identical to Consensus.hpp/ValidatorRegistry.hpp precedent). GeniusNode.cpp new lines — the pin parse line's `operator[]` warning is the file's universal guarded-read idiom (8 pre-existing siblings in the same function); the enforcement region adds only cognitive-complexity notes attached to the state machine's pre-existing over-threshold finding.
- Hand-transcribing the example's 128-hex trust keys for the fingerprint probe initially dropped one character (CanonicalBytes correctly failed closed) — caught by comparing against the JSON with a script; the probe was never committed and was removed after use.
- The tamper test's RED run takes its full 60 s timeout waiting for a FATAL that never comes (pre-fix behavior); GREEN rejects in ~190 ms.

## Verification

- `ctest -R "bounded_defer|burnconfig_policy_e2e|network_config"` — 4/4 targets green (bounded_defer 8/8 cases; burnconfig_policy_e2e 11/11 incl. the 5 new pin cases; precedence incl. 3 new; private_network).
- Boot-path regression through the changed GeniusNode init code: `multi_node_finality_fault_compatibility_smoke_test` and `policy_lifetime_multi_account_test` — both passed (real multi-node boots; unpinned-warn path exercised by every trust-configured test node).
- clang-format applied to all changed C++ files; clang-tidy clean of new Required-group findings (see Issues Encountered).
- Acceptance greps: `read_seconds("authorization_defer_window_sec"…)` + `read("max_new_validators_per_update"…)` inside LoadNetworkConfig; `SetMaxNewValidatorsPerUpdate( max_new_validators_per_update_ )` after Blockchain::New; `Fingerprint()` compare before `TrustStartupController::New`; example JSON carries `genesis_manifest_fingerprint` = 4f6840f5….

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - all paths wired (tracker is a real mechanism, cap applies through the real setter, pin compares the real fingerprint).

## Threat Flags

None - all five `mitigate` dispositions implemented and test-proven (T-21-0301 pin fail-closed with adversarial+compat quintet; T-21-0302 accept documented as the unpinned-warning residual + test; T-21-0303 FIFO cap 4096 single-oldest eviction + eviction tests; T-21-0304 generous 1800 s default + config override test; T-21-0305 ill-typed/negative cap keeps default 10). No new security surface beyond the plan's model.

## Next Phase Readiness

- 21-04 consumes `authorization_defer_window_sec_` for the ConsensusManager defer-and-hold boundary (member retained; GeniusNodeTestAccess observer available) and migrates the five Consensus.cpp callers + `LoadRegistryByCidCached` to `LoadVerifiedRegistryByCid` (deprecated alias still in place from 21-01).
- 21-06 consumes the same window for TransactionManager mint ingress; both build their verdict policy on `BoundedDeferTracker`.
- 21-07 tests the admission-cap enforcement against `max_new_validators_per_update` (the registry has no cap getter today — 21-07 adds the observable it needs).
- Full-suite run at wave merge remains the plan's stated gate (GeniusNode boot paths are widely consumed).

## Self-Check: PASSED

All 12 key files exist on disk; all 6 task commits (36c607f9c, 38d7f6934, 7a532a05c, 90c977eda, 37fabb2c0, aff042ddb) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-20*
