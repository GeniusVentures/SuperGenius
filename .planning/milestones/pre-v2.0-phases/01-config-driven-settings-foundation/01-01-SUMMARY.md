---
plan: 01
phase: 1-config-driven-settings-foundation
status: code-complete-build-pending
committed: 2026-07-02
requirements: [CFG-01, CFG-04]
---

# Plan 01 Summary — Config-Driven Network Settings Foundation

## What was built

An **additive, behavior-preserving** config-read layer for `GeniusNode::InitNetwork()`:
`auto_dht` (bool) and `port_seed` (uint16, numeric `IsUint()`) are now read from
`network_config.json`. Config wins when the key is present; the constructor param is the
fallback (CONTEXT D-01). `base_port` was renamed to `port_seed` throughout the touched code.
The `pubsub_port` > `port_seed`-derived port-resolution priority is now Doxygen-documented.
A precedence test proves each new key overrides the constructor param (D-02).

Old factory **signatures (types)** and all 18 call sites are untouched — only parameter
*names* and docs changed. No behavior change for default or pre-existing config files.

## Key files

**Modified:**
- `src/account/GeniusNode.hpp` — renamed `base_port`→`port_seed` (3 public factories, private ctor, `InitNetwork`); expanded `InitNetwork` Doxygen with port-resolution priority; added `GetPubsubPort()` and `IsAutodhtEnabled()` read-only test getters.
- `src/account/GeniusNode.cpp` — added `port_seed` (numeric `IsUint`) + `auto_dht` (bool) RapidJSON reads in `InitNetwork()` with live override of the `port_seed` param and `autodht_` member; default-on-missing + WARN-log ill-typed + INFO-log resolved; inline comment documenting port priority at the selection block; defined the two getters; renamed `base_port`→`port_seed` throughout.

**Created:**
- `test/src/account/network_config_precedence_test.cpp` — two scenes proving config overrides param: `AutoDhtConfigOverridesParam` (`EXPECT_FALSE(IsAutodhtEnabled())` with config `auto_dht:false` vs param `true`) and `PortSeedConfigOverridesParam` (`EXPECT_GE(GetPubsubPort(), 49999)` with config `port_seed:49999` vs param `40001`; ranges `[40001,40301]` vs `[49999,50299]` don't overlap).
- `test/src/account/CMakeLists.txt` — registered `network_config_precedence_test` via `addtest` + `genius_node` link + Apple `-force_load` whole-archive block (mirrors the adjacent `account_management_test` registration).

## Commits

- `7c761947` refactor(01-01): rename `base_port`→`port_seed` in GeniusNode.hpp + Doxygen + getters
- `00b94d6b` feat(01-01): read `auto_dht`/`port_seed` from network_config.json in `InitNetwork`
- `302549d3` test(01-01): add `network_config_precedence_test` proving config overrides param

## Deviations

- **Test assertion for `port_seed` uses `EXPECT_GE(resolved, 49999u)` (single construction per scene) rather than the `EXPECT_EQ(baseline, overridden)` two-construction form in the plan's acceptance criteria.** Rationale: the two-construction comparison would bind the same derived port twice in one process (node1 then node2 with the same private key → same `GenerateRandomPort` output), risking TIME_WAIT/reuse flakiness. The single-construction range-non-overlap proof is equally rigorous (param-only range `[40001,40301]` cannot reach `≥49999`) and avoids second-port-bind flakiness. The `auto_dht` scene uses the crisp `EXPECT_FALSE` boolean as originally planned. The must_have truth ("a test proves config port_seed and auto_dht each override the constructor param") is fully satisfied.

## Self-Check

Static verification (all PASS — grep/code-review based, no compile required):
- ✓ `base_port` identifier count: 0 in both `GeniusNode.hpp` and `GeniusNode.cpp`
- ✓ `port_seed` read: `HasMember("port_seed") && IsUint()` present; override `port_seed = static_cast<uint16_t>(...)` present
- ✓ `auto_dht` read: `HasMember("auto_dht") && IsBool()` present; override `autodht_ = config_json["auto_dht"].GetBool()` present (JSON key `auto_dht` → member `autodht_`, per D-07)
- ✓ `pubsub_port` string read unchanged (`IsString()` still present, untouched)
- ✓ Defaults preserved: both reads are inside the `HasMember` guard; missing key → param/ctor value retained; ill-typed → WARN-log + retain
- ✓ Port-resolution priority Doxygen-documented on `InitNetwork` declaration + inline comment at the selection block
- ✓ Factory signatures (parameter **types**) unchanged — only param names changed; no call site in `example/` or `test/` referenced `base_port` by name (verified repo-wide)
- ✓ Test file modeled on the proven `account_management_test.cpp` harness; CMake registration mirrors the adjacent block

**NOT verified (environment-blocked):**
- ✗ Full project compile (`genius_node` target + new test) — `build/local` has no compiled artifacts and `_THIRDPARTY_BUILD_DIR` is not set in-repo (externally provided by the user's build script). A from-scratch build of this project is beyond session scope.
- ✗ `ctest -R network_config_precedence_test` — pending the compile above.

The C++ edits follow the existing RapidJSON `HasMember && IsXxx` pattern byte-for-byte and the CMake edit mirrors the adjacent `account_management_test` registration, so compile risk is low — but **the build/test must be run in the user's environment to close verification.**

## Build/test commands (for the user's environment)

```bash
# Configure from the platform entry (adjust _THIRDPARTY_BUILD_DIR to your thirdparty install,
# e.g. ../thirdparty/build/OSX/Release). Use the same invocation you normally use.
cmake -S build/OSX -B build/local -D_THIRDPARTY_BUILD_DIR=<your-thirdparty-build-dir> -DCMAKE_BUILD_TYPE=Debug

# Build just the new test target (pulls in genius_node):
cmake --build build/local --target network_config_precedence_test -j

# Run the precedence test:
ctest --test-dir build/local -R network_config_precedence_test --output-on-failure

# (Regression) full account-test subset to confirm no behavior change:
ctest --test-dir build/local -R 'account_management_test|utxo_manager_test|node_initialization_progress' --output-on-failure
```
