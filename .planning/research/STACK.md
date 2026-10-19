# Stack Research — v1.3 Security Remediation & Network Readiness

**Domain:** Security remediation + supply-chain/network-readiness engineering for an existing C++17 CMake/CTest blockchain platform
**Researched:** 2026-10-19
**Confidence:** HIGH for in-tree claims (every one verified against source in this workspace; file paths cited); MEDIUM for the two new external tool versions (verified against official release pages/changelogs via web)

**Bottom line:** This milestone needs **two new external additions** (one GitHub Action, one Android benchmark library — and the latter only if mobile budget proof is done natively rather than via `dumpsys`). Everything else — fuzzing, sanitizers, adversarial regression testing, reorg/confirmation-depth testing, on-chain role audit, NAT traversal — is **already satisfiable in-tree**. The repo's own constraint ("no new dependency unless research establishes a concrete need") holds for all 25 findings except #19.

---

## Recommendation Summary

| Finding(s) | Need | Recommendation | New dep? | Confidence |
|-----------|------|----------------|----------|------------|
| #8, #9 | Fuzz wire/ingestion parsers | **libFuzzer + ASan** — infra already in-tree (`SGNS_FUZZING`, `addfuzztarget`, 3 working harnesses). Extend coverage; do not adopt another engine | No | HIGH (source-verified) |
| #24, #9 | SDK memory-safety testing | GTest (already scaffolded in GeniusSDK `test/CMakeLists.txt`) + **`SANITIZE_CODE=address,undefined`** builds (option already in `CommonCompilerOptions.cmake:90-101`) | No | HIGH (source-verified) |
| #19 | Dependency content-hash verification in CI | **GitHub release-asset SHA-256 `digest` field** via `gh api` + `sha256sum` after each `gh release download` — zero new tooling (gh already authenticated in CI) | No | MEDIUM-HIGH (changelog + API docs) |
| #19 | Release artifact provenance | **`actions/attest-build-provenance@v4.2.2`** pinned by commit SHA — the one recommended new external dependency; official GitHub action, wraps `actions/attest` | **Yes** | MEDIUM (version verified; self-hosted caveat below) |
| #20 | Release gating on green CI | Pure `cmake.yml` restructuring — split a `publish` job with `needs: build`; no tooling | No | HIGH (workflow read) |
| #10 | Reorg/confirmation-depth tests | **Foundry/anvil** — already conditionally integrated (`requires_foundry` ctest label, `test/src/bridge_e2e/anvil_fixture.hpp`); extend, don't add | No | HIGH (source-verified) |
| #12 | On-chain privileged-role audit | **Foundry `cast`** against deployed mainnet addresses — same existing toolchain; operational runbook, not code | No | HIGH |
| #17 | NAT relay/hole-punch | **Complete the fork's existing code** — `thirdparty/libp2p/src/protocol/{relay,holepunch,autonat}/` already present; gap is wiring/completion, not missing library | No | HIGH (source-verified) |
| #18, #25 | Mobile duty-cycle measurement | Platform-native: **androidx.benchmark 1.5.0 / macrobenchmark** in a minimal Android test module, or `dumpsys batterystats` + XCTest metrics with zero libraries | Only if benchmarked via library | MEDIUM |
| All | Adversarial regression suite (attacker authority / expected rejection / actual behavior) | **GTest + CTest** — the established spine; no new framework | No | HIGH |

---

## Recommended Stack

### Core Technologies (new, external — the short list)

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| `actions/attest-build-provenance` | v4.2.2 (latest, Aug 2026; from v4 a thin wrapper on `actions/attest`) | Generate signed SLSA-style provenance attestations for release tarballs in `cmake.yml` (#19) | GitHub-official, actively maintained, works with the existing `gh release upload` flow; consumers verify with `gh attestation verify` (already-available gh CLI ≥2.49). Conservative choice over the heavier `slsa-framework/slsa-github-generator` |
| androidx.benchmark (incl. macrobenchmark module) | 1.5.0 stable (2026-10-07) | Measured CPU/duty-cycle budgets for the Android worker profile (#18, #25) | Google's standard, widely adopted measurement library; produces reproducible metric reports from instrumentation tests against a release build. Only needed if budget proof is done via library — the zero-dependency `dumpsys batterystats` path is an acceptable first rung |

### In-Tree Capabilities That Already Cover a Need (verified — do NOT re-add)

| Capability | Where It Lives (verified) | Covers Finding(s) |
|-----------|--------------------------|-------------------|
| libFuzzer + ASan harness infra | `fuzz/CMakeLists.txt` (`addfuzztarget` targets + CTest smoke runs `-max_total_time=60` + seed corpora); `cmake/functions.cmake:64-71` (`-fsanitize=fuzzer,address -g -O1`); gate `test/CMakeLists.txt:9-12` (`option(SGNS_FUZZING ... OFF)`, Clang-only warning) | #8, #9 — harnesses exist for `decode_log_data`, `parse_burn_event_values`, `mint_transaction_deserialize`; the pattern is proven, extend it |
| Generic sanitizer builds | `build/CommonCompilerOptions.cmake:90-101` — `SANITIZE_CODE` maps to clang `-fsanitize=` **and** MSVC `/fsanitize=` | #24, #9 — ASan/UBSan SDK and ingestion builds need zero build-system work |
| GTest + CTest + `addtest`/`addtest_part` | `cmake/functions.cmake`; `test/` tree; GeniusSDK `test/CMakeLists.txt` already references `TransactionDataTest`, `TransactionBlocksTest`, `GeniusSDKTest` — **the .cpp files are missing** (matches finding #24's "only a CMake file") | Adversarial regression suite for all findings — write tests, not infrastructure |
| Foundry/anvil E2E fixtures | `test/src/bridge_e2e/` (`anvil_fixture.hpp`, `bridge_anvil_e2e_test.cpp`, `bridge_anvil_catchup_e2e_test.cpp`, `bridge_race/`), `requires_foundry` ctest label excluded in CI (`.github/workflows/cmake.yml:604`) | #10 (reorg/depth tests via anvil), #12 (`cast call`/storage inspection of deployed roles) |
| Circuit-relay / hole-punch / autonat protocol code | `thirdparty/libp2p/src/protocol/holepunch/` (client, server, msg processors, .proto), `src/protocol/relay/` (relay.cpp, relay_msg_processor.cpp), `src/protocol/autonat/`; disabled at the ipfs-pubsub integration layer (`ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp`) | #17 — finish/enable existing code; DCUtR protocol machinery is not absent |
| UPnP NAT traversal | `gnus_upnp` (thirdparty; `upnp_enabled` already in `network_config.json`) | #17 — the static-port-forward half of NAT readiness |
| EVM RPC client stack | `evmrelay/` (go-ethereum integration) + `src/api/` JSON-RPC/WebSocket transport | #10, #11 — confirmation-depth checks and content-corroborated RPC compare agreeing block/event data through existing clients; no new web3 library |
| libp2p fork GTest suite | `thirdparty/libp2p/test/libp2p/` (per-protocol dirs; existing `varint_prefix_reader_test.cpp`, `multi/utils/uvarint_test.cpp`) | #8 — length-prefix cap tests belong beside these; fork has its own CMake `addtest` heritage |
| GeniusSDK budget/lifecycle knobs | `GeniusSDK/src/android/background_config.h` (`battery_not_low`, `network_required`, `idle_only`, `thermal_check_enabled`, `inference_idle_timeout_seconds` — parsed at `GeniusSDKInit()` from `background_config.json`) | #25 — the adaptive-budget control surface exists; what's missing is enforcement wiring + demonstrated recovery tests |
| gh CLI (authenticated) | `GH_TOKEN: ${{ secrets.GNUS_TOKEN_1 }}` used throughout `cmake.yml`; `gh release download`/`create`/`upload` already core steps | #19, #20 — digest fetch (`gh api --jq .digest`), `gh attestation verify`, and release gating need no new CLI |

### Supporting Libraries (only where a concrete gap exists)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `actions/attest` | current (v4.x line) | Direct attestations (SBOM, build provenance variants) if a form `attest-build-provenance` doesn't cover is needed later | Only if #19 scope grows beyond release-tarball provenance |
| Battery Historian (Google) | current web tool | Visualize `dumpsys batterystats` bugreport dumps for the #25 budget evidence | Zero-install analysis aid; not a build dependency |
| XCTest performance metrics (`XCTCPUMetric`, `XCTMemoryMetric`, `XCTOSSignpostMetric`) | current Xcode (built-in) | iOS duty-cycle/lifecycle-recovery measurement | iOS half of #25; needs only an Xcode test target hosting the SDK |

## Installation / Wiring (this repo's shape — not npm)

```bash
# Fuzzing (already wired; nothing to install — Clang toolchain only)
cmake -S build/OSX -B build/OSX/Fuzz -DCMAKE_BUILD_TYPE=Debug -DSGNS_FUZZING=ON ...
ctest -R '_smoke'                       # 60s-per-harness smoke runs

# SDK memory-safety builds (existing option)
cmake ... -DSANITIZE_CODE=address,undefined   # clang; MSVC: =address via /fsanitize

# Mobile budget measurement (Android module, if library route chosen)
# androidx.benchmark 1.5.0 via the module's gradle dependencies block
```

CI wiring points in `.github/workflows/cmake.yml` (all verified against the current file):

1. **Digest verification (#19):** immediately after the `Download thirdparty release` / `Download zkLLVM release` steps (~lines 318-347), add `gh api repos/GeniusVentures/thirdparty/releases/tags/$tag --jq '.assets[] | select(.name=="'$FILE_NAME'") | .digest'` and compare against `shasum -a 256` of the extracted tarball. Digests exist for assets uploaded after June 2025; the `digest` field format is `sha256:<hex>`. Note: the API digest proves the artifact matches what was uploaded; the review-time trust anchor is the pinned tag — record expected digests in a committed manifest when thirdparty re-cuts releases.
2. **Release gating (#20):** today `Create release tag` (~line 539) and `Upload file` (~line 560) run before `Run tests` (~line 597). Restructure: build job uploads the tarball as a workflow artifact; a separate `publish` job with `needs: build` (every matrix leg) and `if: github.ref_name == 'main' || ...` creates the tag, attests, and uploads. This also removes the current delete-and-recreate-release dance.
3. **Attestation (#19):** in `publish`, before/with `gh release upload`: `uses: actions/attest-build-provenance@<v4.2.2 commit SHA>` with `subject-name: "${FILE_NAME}"`, `path-to-artifact: <tarball>`; job needs `permissions: id-token: write, attestations: write, contents: write`. Consumers verify: `gh attestation verify <downloaded.tar.gz> --repo GeniusVentures/SuperGenius`.
4. **Pin actions by commit SHA:** `cmake.yml` currently pins `actions/checkout@v7`, `actions/cache@v6`, `actions/upload-artifact@v7`, `dorny/paths-filter@v3`, `actions/setup-node@v4` by tag. Convert to full-SHA pins (GitHub's own supply-chain guidance) — mechanical, no behavior change.
5. **Fuzz CI leg:** add a Linux (clang, AlmaLinux container already pins clang via `COMPILER_PIN`) and/or macOS matrix entry with `-DSGNS_FUZZING=ON` running the `_smoke` ctests; Windows `cl.exe` has no libFuzzer, so fuzz targets stay off the MSVC legs.

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| libFuzzer (in-tree) | AFL++ | Only if a target needs byte-oriented persistent fuzzing libFuzzer can't reach — it doesn't; AFL++ would add a second engine + CI infra for zero coverage gain over the proven `addfuzztarget` pattern |
| libFuzzer | cargo-fuzz | Never here — it's Rust-only; the libp2p fork and SDK are C++ (only wallet-core is Rust, consumed as a prebuilt) |
| `actions/attest-build-provenance` | `slsa-framework/slsa-github-generator` | If the project later wants BYO-signing-key SLSA L3+ provenance or container-image provenance as a first-class artifact; the generator is heavier and its runner model fits GitHub-hosted builders best, which this repo's self-hosted fleet isn't |
| `actions/attest-build-provenance` | In-toto/cosign hand-rolled | Never — re-implements what the official action does, violating the conservative-vendor rule |
| androidx.benchmark + dumpsys | Commercial mobile profiling (e.g. vendor APM) | Never this milestone — new vendor surface, no added assurance for a duty-cycle budget check |
| Foundry/anvil (existing) | Hardhat/ethers reorg harness | Never — the bridge E2E suite already standardizes on Foundry; a second EVM toolchain would duplicate fixture logic (DRY violation) |
| ASan/UBSan (`SANITIZE_CODE`) | Valgrind | Only for leak-only checks where ASan can't run (e.g. some platforms); ASan is faster, already integrated, and CI-friendly |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| OSS-Fuzz enrollment (now) | Requires fully open-source build integration plus 90-day disclosure cadence the remediation timeline shouldn't inherit; the findings need targeted harnesses this quarter | In-tree libFuzzer smoke + periodic long runs on a self-hosted Linux leg |
| Any new C++ testing framework (Catch2, doctest, Boost.Test expansion) | GTest is the established framework wired into `addtest`, CTest, and both sibling repos; a second framework fragments the adversarial-suite discipline | GTest/CTest everywhere, including GeniusSDK's missing test sources |
| New EVM/web3 C++ client library for #10/#11 | `evmrelay` (go-ethereum) + existing JSON-RPC stack already fetch blocks/events; a new client duplicates wire-schema facts | Extend `PublicChainInputValidator` against existing clients |
| A new NAT/traversal library (or switching to go-libp2p/rust-libp2p) | The fork already vendors relay/holepunch/autonat implementations; go-libp2p has the most mature DCUtR but adopting it is a platform rewrite | Complete and enable the fork's existing protocols; UPnP already covers static mappings |
| Step-security `harden-runner` or monitoring agents (now) | New vendor surface in every job for marginal gain this milestone | SHA-pinned actions + digest verification + attestations first; revisit if the threat model grows |
| Bleeding-edge clang (nightly) for fuzzing | Conservative-vendor rule; distro clang (EL8 image) + Xcode clang both ship stable libFuzzer | The toolchains CI already pins |

## Stack Patterns by Variant

**If a fuzz target lives in the libp2p fork (#8 varint/uvarint/Kademlia readers):**
- Add the harness inside the fork's own tree (`thirdparty/libp2p/test/`), copying the `addfuzztarget` pattern from SuperGenius `cmake/functions.cmake:64` (the fork has separate CMake helpers; keep one copy per repo — do not cross-include build logic between repos).
- Reason: the fork builds standalone with its own GTest suite; harnesses must regenerate when the fork rebuilds in the thirdparty pipeline.

**If release publishing must stay a single job (minimal #20 change):**
- Move `Create release tag` + `Upload file` below `Run tests` with `if: success() && env.RELEASE == 'true'` — the two-line variant of the gate.
- Reason: acceptable first rung; the separate `publish` job is still preferred because it also gates on other matrix legs.

**If mobile budget proof is needed without any new library (#25 minimal path):**
- Android: instrumentation-free `dumpsys batterystats --reset` → run worker workload → `bugreport` → Battery Historian; assert duty-cycle thresholds in a scripted CTest/CI step.
- iOS: XCTest `measure(metrics:)` with built-in CPU/memory metrics in a minimal test host.
- Reason: zero new dependencies; androidx.benchmark 1.5.0 is the upgrade path when reproducible CI-grade metrics are required.

**If Windows must run sanitized tests (#24 on MSVC):**
- `SANITIZE_CODE=address` already maps to `/fsanitize=address` (MSVC) — but no libFuzzer: keep fuzz targets Linux/macOS-only; run GTest suites sanitized on Windows.

## Version Compatibility

| Component | Compatible With | Notes |
|-----------|----------------|-------|
| libFuzzer harnesses | Clang (any modern; CI pins clang on Linux, Xcode clang on macOS) | `SGNS_FUZZING=ON` explicitly warns and expects Clang (`test/CMakeLists.txt:11-12`); `cl.exe` cannot build them — exclude from MSVC matrix legs |
| `actions/attest-build-provenance@v4.2.2` | GitHub-hosted AND self-hosted runners | Works on this repo's self-hosted fleet, but provenance does not record runner identity (community discussion #205663) — attestation proves source→artifact binding, not runner integrity; document this residual in the release process |
| Release-asset `digest` API field | Assets uploaded after June 2025 | Older thirdparty/zkLLVM releases lack digests; re-upload or fall back to committed checksum manifests |
| `gh attestation verify` | gh CLI ≥ ~2.49 | Runners already run current gh; verify locally before relying on it in scripts |
| androidx.benchmark 1.5.0 | Android Gradle Plugin current stable, minSdk per docs (macrobenchmark measurement needs API 29+ device/emulator for full features) | Host module required — see gap note below |
| Foundry/anvil | Current `foundryup` install | Tests already probe PATH at runtime and `GTEST_SKIP` when absent (`requires_foundry`); keep that pattern for new reorg tests |

## Sources

In-tree (HIGH confidence, read directly):
- `/Users/henriqueklein/gnus/SuperGenius/fuzz/CMakeLists.txt`, `fuzz/fuzz_mint_transaction_deserialize.cpp`, `fuzz/corpus/*`
- `/Users/henriqueklein/gnus/SuperGenius/cmake/functions.cmake:64` (`addfuzztarget`), `test/CMakeLists.txt:9-12` (`SGNS_FUZZING`)
- `/Users/henriqueklein/gnus/SuperGenius/build/CommonCompilerOptions.cmake:90-101` (`SANITIZE_CODE`)
- `/Users/henriqueklein/gnus/SuperGenius/.github/workflows/cmake.yml` (download steps 318-347, release-before-tests ordering 539-564 vs 597, requires_foundry 604)
- `/Users/henriqueklein/gnus/thirdparty/libp2p/src/protocol/{relay,holepunch,autonat}/`, `thirdparty/libp2p/test/`, `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp`
- `/Users/henriqueklein/gnus/GeniusSDK/test/CMakeLists.txt`, `GeniusSDK/src/android/background_config.h`, `GeniusSDK/build/` (mirrors SuperGenius platform-CMake layout)
- `.planning/codebase/STACK.md` (2026-05-25 inventory: Boost 1.85, GTest, RocksDB, libp2p v0.1.2, gnus_upnp, etc.)

External (verified via official pages/changelogs):
- [actions/attest-build-provenance releases](https://github.com/actions/attest-build-provenance/releases) — v4.2.2 latest; v4+ wraps `actions/attest` — MEDIUM
- [GitHub changelog: releases expose digests for release assets](https://github.blog/changelog/2025-06-03-releases-now-expose-digests-for-release-assets) and [community discussion #23512](https://github.com/orgs/community/discussions/23512) — sha256 `digest` field — MEDIUM-HIGH
- [gh attestation verify manual](https://cli.github.com/manual/gh_attestation_verify), [GitHub artifact-attestations docs](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds) — MEDIUM
- [Artifact attestations & self-hosted runners (community discussion #205663)](https://github.com/orgs/community/discussions/205663) — runner-identity caveat — MEDIUM
- [androidx Benchmark releases](https://developer.android.com/jetpack/androidx/releases/benchmark) — 1.5.0 stable, 2026-10-07 — MEDIUM
- [libp2p hole-punching spec](https://github.com/libp2p/specs/blob/master/connections/hole-punching.md) — DCUtR reference for completing the fork's implementation — HIGH (spec), fork-completeness assessment is in-tree evidence

## Gaps to Address at Phase Level

- **Android instrumentation host module:** no Gradle app module exists in GeniusSDK or this repo to host androidx.benchmark/instrumentation tests (the Unity wrapper lives in a separate UnityGeniusSDK repo). A phase owning #18/#25 must decide: minimal `test/android/` host module (new but platform-standard) vs. zero-dep `dumpsys` scripting. Not a library choice — an infrastructure placement decision.
- **thirdparty release digest backfill:** whether GeniusVentures/thirdparty and zkLLVM releases get re-cut so `digest` fields exist, or expected hashes are committed as manifests — a release-engineering decision upstream of the CI change.
- **Foundry on CI runners:** `requires_foundry` tests currently skip everywhere in CI (PATH probe). #10 reorg tests only gate releases if a runner installs Foundry — decide which leg(s) own it.

---
*Stack research for: v1.3 Security Remediation & Network Readiness (SuperGenius + libp2p fork + GeniusSDK)*
*Researched: 2026-10-19*
