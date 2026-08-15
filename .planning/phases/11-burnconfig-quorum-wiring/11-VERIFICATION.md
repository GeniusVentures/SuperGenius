---
phase: 11-burnconfig-quorum-wiring
verified: 2026-07-27T14:20:00Z
status: gaps_found
score: 3/3 must-haves (BURN-01/02/03) verified; 1 real regression confirmed, partially investigated (one contributing lock-scope hazard fixed), root cause NOT yet fully resolved
overrides_applied: 0
gaps:
  - truth: "No regression in existing stable behavior — multi_account_test remains stable"
    status: failed
    reason: "multi_account_test is now genuinely non-deterministic. Independently reproduced across 3 consecutive local runs on the same build: run 1 = assertion failure (ConfigureConsensus timeout, 'missing validator in registry'), run 2 = clean pass (111s), run 3 = SEGFAULT after 0.42s (crashed before test body ran meaningfully). This is broader than the SUMMARY's disclosed 'thread::join failed: No such process' teardown-only crash — the instability surfaces as at least three distinct symptoms, not one. Confirmed absent on develop before Phase 11 per SUMMARY's own account and not contradicted by this verification."
    artifacts:
      - path: "test/src/multiaccount/multi_account_sync.cpp"
        issue: "Consumer of GeniusNode multi-instance construction/teardown; not itself modified by Phase 11, but its stability depends on GeniusNode's INITIALIZING_TRANSACTIONS sequence which Phase 11 changed"
      - path: "src/account/BurnConfig.cpp"
        issue: "TrySeedGenesisIfEligible performs a real ProposeValue+AddSignature CRDT write synchronously during every GeniusNode construction where the node's address is a trusted peer — new work on the startup/shutdown-sensitive path that did not exist before this phase"
      - path: "src/crdt/impl/crdt_set.cpp"
        issue: "RESOLVED during orchestrator follow-up: PutElems was firing putHookFunc_ while still holding CrdtSet::mutex_. Restructured (commit 1cd35fa6) to release the lock before firing hooks. Reran multi_account_test 4x after this fix: 2/4 STILL FAILED (1 SEGFAULT, 1 assertion failure) — this lead is now exhausted; the lock-scope hazard was a real bug and worth fixing regardless, but is NOT the sole/root cause of the instability."
    missing:
      - "Root-cause the REMAINING multi_account_test instability (lock-scope lead ruled out) to a specific line/mechanism — next step is profiling/instrumenting multi_account_test's specific multi-node construction sequence directly"
      - "A fix or confirmed-safe mitigation, re-verified by running multi_account_test at least 5-10 times consecutively with zero failures"
---

# Phase 11: BurnConfig Quorum Wiring Verification Report

**Phase Goal:** `BURN_BASIS_POINTS` is a live, quorum-signed CRDT value gated by `TrustedPeerRegistry` membership instead of a compile-time constant, and `TransactionManager` reads a cached value refreshed via callback rather than performing a CRDT read on every `PayEscrow` call.
**Verified:** 2026-07-27
**Status:** gaps_found (functional requirements BURN-01/02/03 pass; one real, independently-reproduced stability regression remains open)
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | BURN-01: `BURN_BASIS_POINTS` is a `TrustedPeerRegistry`-quorum-signed CRDT value, not a hardcoded constant | ✓ VERIFIED | `src/account/BurnConfig.hpp`/`.cpp` — `BurnConfigPayload : ISignedCRDTData` (SerializeToBytes/DeserializeFromBytes/Verify/Apply), `BurnConfig::New` calls `ValidateQuorumThreshold` then delegates ALL propose/sign/quorum work to `SecureCrdt::ProposeValue`/`AddSignature`/`ReadIfQuorum` and `SecureCrdtRegistry::Register` — zero bespoke signature/quorum logic found in `BurnConfig.cpp`. Old `TransactionManager::BURN_BASIS_POINTS` constant confirmed removed (`grep` for `static constexpr uint64_t BURN_BASIS_POINTS  *=` in `TransactionManager.hpp` returns nothing; only `BURN_BASIS_POINTS_DEFAULT` remains as the pre-quorum fallback). |
| 2 | BURN-02: `TransactionManager::PayEscrow` uses a cached in-memory value; no CRDT read on the `PayEscrow` path; cache updates via CRDT-change callback | ✓ VERIFIED | `TransactionManager.hpp:543` declares `std::atomic<uint64_t> burn_basis_points_`; `TransactionManager.cpp:820` — `PayEscrow`'s burn computation reads `burn_basis_points_.load(std::memory_order_relaxed)`, no CRDT/SecureCrdt/GlobalDB symbol anywhere in `PayEscrow`'s body. `TransactionManager::New` (`TransactionManager.cpp:266-271`) registers a weak-captured lambda via `burn_config->RegisterRefreshCallback(...)` that stores into the same atomic — confirmed this is `BurnConfig`'s own callback API, not a raw `GlobalDB::RegisterNewElementCallback` bypassing `BurnConfig` (matches plan's T-11-08 mitigation and the header-forward-declaration-only constraint: `grep` confirms `TransactionManager.hpp` only forward-declares `sgns::account::BurnConfig`, no `#include "account/BurnConfig.hpp"`). |
| 3 | BURN-03: A freshly-seeded genesis node burns exactly 1% (100 basis points) by default, matching pre-milestone behavior | ✓ VERIFIED (real build+test, not just source review) | `GeniusNode::BurnConfig::GENESIS_DEFAULT_BASIS_POINTS = 100`; `test/src/blockchain/node_startup_test.cpp` new test `GenesisNodeDefaultBurnRateIsOnePercent` asserts `GetBurnBasisPoints()==100`/`GetBasisPointsTotal()==10000` against a real, running `GeniusNode` reaching `NodeState::READY` through the actual `INITIALIZING_TRANSACTIONS` production path. **Independently re-run in this verification session:** `ctest -R node_startup_test --output-on-failure` → `Passed 11.12 sec`. |
| 4 | D-03: Auto-signing is strictly limited to the known genesis default (100); no code path auto-signs a proposed *change* | ✓ VERIFIED | `BurnConfig.cpp`'s only `ProposeValue`/`AddSignature` call site is inside `TrySeedGenesisIfEligible()`, gated by (a) `ReadIfQuorum` returning absent (never re-runs once any value confirmed) and (b) the node's own address being in `GetCurrentPeers()`, and the value signed is the hardcoded literal `BurnConfigPayload(GENESIS_DEFAULT_BASIS_POINTS)` — never a value read from CRDT or supplied by a caller. `OnCrdtElementChanged` (the ongoing refresh path) contains zero `ProposeValue`/`AddSignature` calls — confirmed by direct code read, matching the plan's explicit constraint ("Do NOT call TrySeedGenesisIfEligible's propose/sign logic from inside OnCrdtElementChanged"). |
| 5 | D-07: Majority-floor threshold enforcement is a hard, code-enforced invariant for both `TrustedPeerRegistry` and `BurnConfig` | ✓ VERIFIED | `BurnConfig::New` calls `ValidateQuorumThreshold(quorum_threshold, trusted_peer_registry->GetCurrentPeers().size())` as its first action and returns the error immediately (no instance constructed) on failure — confirmed by direct code read of `BurnConfig.cpp:97-102`. `GeniusNode.cpp`'s `INITIALIZING_TRANSACTIONS` case logs+`return`s on either `TrustedPeerRegistry::New` or `BurnConfig::New` construction failure, never falling through to construct `TransactionManager` with a null/invalid `burn_config_`. |
| 6 | No regression in existing stable behavior (multi_account_test unaffected) | ✗ FAILED | See Gaps below — independently reproduced non-deterministic instability across 3 consecutive local runs. |

**Score:** 5/6 truths verified (BURN-01/02/03 and D-03/D-07 all hold); 1 truth fails (pre-existing test stability).

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/account/BurnConfig.hpp`/`.cpp` | `BurnConfigPayload` + `BurnConfig` wrapper, SecureCrdt-transported | ✓ VERIFIED | Exists, substantive, matches plan spec exactly; compiles as part of `genius_node` (confirmed via successful `node_startup_test` run against this build) |
| `src/securecrdt/QuorumThresholdValidation.hpp` | `ValidateQuorumThreshold` majority-floor helper | ✓ VERIFIED | Present, used by both `TrustedPeerRegistry::New` and `BurnConfig::New` |
| `src/account/TransactionManager.hpp`/`.cpp` | Cached atomic burn rate, `BurnConfig` forward-decl only, no CRDT read on `PayEscrow` | ✓ VERIFIED | Confirmed via direct code read (see truths 1-2 evidence) |
| `src/account/GeniusNode.cpp` (`INITIALIZING_TRANSACTIONS`) | Constructs `SecureCrdt`→`TrustedPeerRegistry`→`BurnConfig`→`TransactionManager` in order, halts on failure | ✓ VERIFIED | `GeniusNode.cpp:731-775` — exact sequence confirmed, with `node_logger_->error(...); return;` guards on both fallible constructions |
| `example/node_test/sgns_config.json` | Two new quorum-threshold fields | ✓ VERIFIED | `trusted_peer_quorum_threshold: 2`, `burn_config_quorum_threshold: 2` present |
| `test/src/blockchain/node_startup_test.cpp` | End-to-end BURN-03 regression test | ✓ VERIFIED, and re-run successfully in this session | `ctest -R node_startup_test` → Passed |
| `src/crdt/impl/crdt_set.cpp` | Foundational post-commit hook-firing fix (Plan 01) | ⚠️ VERIFIED-WITH-CONCERN | Fix is logically correct for its stated purpose (hooks now see committed data, fixing the previously-broken quorum-confirmation callback path) — confirmed by reading the diff. However, it introduces a new lock-scope characteristic: `PutElems` now fires `putHookFunc_` while still holding `CrdtSet::mutex_` (the single-key `SetValue` overload does NOT take this lock at all — pre-existing asymmetry, not new). This is a plausible (not confirmed) contributing factor to the reproduced instability; see Gaps. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `GeniusNode.cpp` | `TransactionManager::New` | `burn_config_->GetCachedBasisPoints(), burn_config_` trailing args | ✓ WIRED | `GeniusNode.cpp:774-775` |
| `TransactionManager.cpp` | `BurnConfig.hpp` | `burn_config->RegisterRefreshCallback(...)` | ✓ WIRED | `TransactionManager.cpp:266` |
| `TrustedPeerRegistry.cpp`/`BurnConfig.cpp` | `QuorumThresholdValidation.hpp` | `ValidateQuorumThreshold` call at start of `New()` | ✓ WIRED | Confirmed in both files |
| `BurnConfig.cpp` | `SecureCrdt.hpp` | `ProposeValue`/`AddSignature`/`ReadIfQuorum`, no bespoke logic | ✓ WIRED | Confirmed, no parallel verification logic found |

### Behavioral Spot-Checks / Real Test Execution

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Real GeniusNode reaches READY with default 1% burn rate | `ctest -R node_startup_test --output-on-failure` | `Passed 11.12 sec` | ✓ PASS |
| multi_account_test stability (run 1) | `ctest -R multi_account_test --output-on-failure` | `MultiAccountTest.NodeConsensusBatch5Test` FAILED: `ConfigureConsensus` timeout (50s), "missing validator in registry" | ✗ FAIL |
| multi_account_test stability (run 2) | same, immediate re-run | `Passed 111.69 sec` | ✓ PASS |
| multi_account_test stability (run 3) | same, immediate re-run | `***Exception: SegFault 0.42 sec` — crashed almost immediately, before test body progressed | ✗ FAIL (worse than SUMMARY's disclosed symptom) |

**This independently confirms the regression is real, and is more severe/varied than the single symptom disclosed in 11-02-SUMMARY.md** (which described only a teardown-time `thread::join failed: No such process` after all assertions passed). This verification session observed three different failure modes across three runs: a genuine assertion/timeout failure, a clean pass, and an early segfault — evidence of a broader non-determinism than "crashes only during clean teardown."

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| BURN-01 | 11-01-PLAN | `BURN_BASIS_POINTS` as quorum-signed CRDT value | ✓ SATISFIED | See Truth 1 |
| BURN-02 | 11-02-PLAN | Cached value, no CRDT read on `PayEscrow`, callback refresh | ✓ SATISFIED | See Truth 2 |
| BURN-03 | 11-02-PLAN | Genesis default 1% preserved | ✓ SATISFIED | See Truth 3, real test re-run |

No orphaned requirements found for this phase.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | No TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER markers found in `BurnConfig.hpp/.cpp`, `TransactionManager.hpp/.cpp` (burn-related sections), or `GeniusNode.cpp` (burn-related sections) | — | None — code is complete, no debt markers |
| `src/crdt/impl/crdt_set.cpp` | 671-728 (`PutElems`) | Hook invocation now occurs while holding `CrdtSet::mutex_`; the single-key `SetValue` overload's equivalent hook call does not hold this lock | ⚠️ Warning | Not a debt marker, but an asymmetry/reentrancy risk worth explicit review in the regression follow-up — flagged, not confirmed root cause |

### Human Verification Required

None — all must-haves for this phase are either mechanically verifiable via source inspection or via real, reproducible test execution, which was performed in this session.

## Gaps Summary

**Functional correctness (BURN-01/02/03) is genuinely and thoroughly proven**, both by direct source-code inspection (matching the plan's exact design — no CRDT read on `PayEscrow`, all propose/sign/quorum delegated to `SecureCrdt`, genesis auto-sign strictly scoped to the known default per D-03, majority-floor enforced per D-07 as a hard construction-time failure with no silent clamping) and by this session's own re-execution of `node_startup_test`, which brings up a real `GeniusNode` through the actual production `INITIALIZING_TRANSACTIONS` path and confirms the default 1% burn rate end-to-end. The SUMMARY's claims here are corroborated, not merely trusted.

**The `multi_account_test` regression is real, independently confirmed, and is broader than what was disclosed.** Three consecutive runs against the exact same build produced three distinct outcomes: a genuine assertion failure (consensus-configuration timeout + "missing validator in registry" — a logic-adjacent symptom, not just a teardown crash), a clean pass, and an early segfault. The SUMMARY's honest disclosure of a `thread::join failed` teardown crash is credible but incomplete — this verification found the instability manifests in at least one additional way (an outright test-assertion failure during the run, and a very early segfault) that was not mentioned. This does not change the verdict that the regression is real and pre-existing-code-adjacent (not a BurnConfig functional defect), but it does mean the actual blast radius of "how unstable is multi-node GeniusNode construction now" is larger than the SUMMARY implied.

The `crdt_set.cpp` fix itself is architecturally sound for its stated purpose and does not appear to reintroduce the original bug (hooks now correctly observe post-commit state). It did introduce a new characteristic — hook invocation while holding `CrdtSet::mutex_` inside `PutElems` — that this verification flagged as a plausible contributing factor to reentrancy-style instability, since `BurnConfig`'s refresh callback chain re-enters `SecureCrdt`/`GlobalDB` machinery synchronously from within that same call stack now that `BurnConfig`'s genesis auto-seed runs on every applicable node construction.

**Orchestrator follow-up on this specific lead:** acted on the verifier's own recommendation immediately — restructured `PutElems` to release `mutex_` (via a scoped block) before firing post-commit hooks, so a hook re-entering `CrdtSet`/`GlobalDB` synchronously no longer does so while this instance's own lock is held. Rebuilt and reran `multi_account_test` 4 consecutive times on the fixed build: **2/4 still failed** (1 SEGFAULT, 1 assertion failure) — confirming this genuine improvement (worth keeping regardless) does NOT fully resolve the instability. The root cause is deeper than the lock-scope hazard alone; something else in the increased per-node CRDT/registration workload (or a separate concurrency issue entirely) is still at play. All of this project's own suites (`securecrdt`/`trustedpeer`/`burnconfig`/`multisig`/`node_startup_test`, 12/12) remain green after this additional fix.

## Verdict

**Conditional pass — BURN-01/02/03 are genuinely achieved; the phase should NOT be blocked from being marked functionally complete, but the multi_account_test regression is real, confirmed NOT fully resolved by the lock-scope fix attempted during this closeout, and must be tracked as a dedicated follow-up, not silently deferred.**

Reasoning:
- The regression is in a **different test's shutdown/multi-instance-construction path**, not in BurnConfig/TransactionManager's own behavior — `burnconfig_test`, `trustedpeerregistry_*` tests, and `node_startup_test` (the direct BURN-01/02/03 proof points) all pass reliably, including after the additional lock-scope fix.
- The regression is **more serious than "flaky teardown noise"**: reproduced failure modes include a genuine assertion failure mid-test (missing validator in registry, consensus configuration timeout) and an early segfault, not just a teardown-only crash.
- **This is a real regression introduced by Phase 11's GeniusNode wiring** (constructing `SecureCrdt`/`TrustedPeerRegistry`/`BurnConfig`, including a genesis auto-seed CRDT write, on every applicable node's startup) — it did not occur on `develop` before this phase. It is not being characterized as a pre-existing, unrelated issue.
- One concrete lead (hook-under-lock reentrancy in `PutElems`) was investigated, fixed as a genuine improvement, and empirically ruled out as the SOLE cause via 4 fresh test runs post-fix.
- Recommend: **do not block Phase 12** on this, but **do require a dedicated, tracked follow-up phase/issue** to continue root-causing the remaining instability (a fresh profiling/instrumentation pass on `multi_account_test`'s specific multi-node construction sequence is the logical next step, since the lock-scope lead has now been exhausted).

**Scope update (2026-07-28, during Phase 12 verification):** Phase 12's verifier, while independently re-running tests (not just trusting the executor's single-run claim), found `blockchain_genesis_test` also intermittently aborts (1/3 reruns) — the same class of multi-node CRDT/GlobalDB instability documented above, not a new/separate regression, and not caused by Phase 12's actual code change (a pure signature-verify callee substitution touching neither test's exercised path). Per user decision, this is folded into this same tracked item rather than opened as a separate one — the dedicated follow-up's scope is hereby broadened to cover both `multi_account_test` and `blockchain_genesis_test` as symptoms of one underlying multi-node instability class. Both tests are also independently pre-filed as known-failing on GitHub (`#302` and `#301` respectively, filed 2026-06-01, predating this milestone), consistent with this being a pre-existing, cross-cutting issue rather than something newly introduced.

**RESOLVED (2026-07-28, via `/gsd:debug` session `multi-node-crdt-instability`):** Root-caused via a fresh debug session rather than re-chasing the lock-scope lead. `GeniusNode::WriteNetworkConfig()` (`GeniusNode.cpp:201-214`) was documented to disable UPnP for tests/examples but never actually wrote the `upnp_enabled` key — every test-created `GeniusNode` (including both flaky tests) therefore defaulted to `upnp_enabled=true` and performed real, blocking SSDP/UPnP network discovery during construction. A preserved macOS crash report confirmed this real network I/O crashed on a heap-corruption bug in the vendored `gnus_upnp` library (`sgns::upnp::parseSSDPResponse` → `InitUPNP` → `GeniusNode` constructor), and its timing variance was implicated in the broader flakiness. Fixed by having `WriteNetworkConfig()` write `"upnp_enabled": false` as its own doc comment already claimed it did. Verified via 10/10 + 10/10 clean `multi_account_test`/`blockchain_genesis_test` reruns. Full session record: `.planning/debug/resolved/multi-node-crdt-instability.md`. The `CrdtSet::PutElems` lock-scope fix (`1cd35fa6`) remains a separate, genuine improvement, kept as-is alongside this fix.

---

_Verified: 2026-07-27_
_Verifier: Claude (gsd-verifier)_
