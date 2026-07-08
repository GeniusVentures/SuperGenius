---
id: 01
title: "Config-driven network settings (auto_dht, port_seed) + base_port rename"
phase: 1
wave: 1
depends_on: []
requirements: [CFG-01, CFG-04]
files_modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
  - test/src/account/network_config_precedence_test.cpp
  - test/src/account/CMakeLists.txt
autonomous: true
---

# Plan 01: Config-Driven Network Settings Foundation

<objective>
Introduce config-file reads for `auto_dht` (bool) and `port_seed` (uint16, numeric
`IsUint()`) from `network_config.json` inside `GeniusNode::InitNetwork()`, as an
**additive, behavior-preserving layer**. Config wins if the key is present; the
constructor param is the fallback when the key is absent (D-01). Rename `base_port` →
`port_seed` throughout the touched code (D-03). Doxygen-document the port-resolution
priority (`pubsub_port` > `port_seed`-derived) (D-04). Add a precedence test proving the
config value overrides the constructor param (D-02). Old factory signatures (types) and
all 18 call sites are untouched — only param *names* and docs change.

Covers CFG-01 (read `auto_dht` + `base_port`≡`port_seed` from network_config.json, numeric
`IsUint()`) and CFG-04 (safe defaults on missing key, byte-identical to today: `auto_dht=true`,
`port_seed=40001`; WARN-log unrecognized/ill-typed values).
</objective>

<implementation_notes>
- **D-07 name mapping:** JSON key `auto_dht` maps to existing C++ member `autodht_`
  (different spelling). Do NOT rename the member.
- **D-08 type divergence:** `port_seed` uses numeric `IsUint()`; existing `pubsub_port`
  uses string-then-`std::stoi`. Leave `pubsub_port` untouched. Add a code comment noting
  the intentional divergence.
- **Live override:** `autodht_` is initialized from the param in the constructor init-list
  (`GeniusNode.cpp:209`). The config override MUST reassign `autodht_` inside `InitNetwork`
  after reading `auto_dht` so a deployment setting the key takes effect in Phase 1 (D-01).
- `base_port` appears ONLY in `src/account/GeniusNode.{hpp,cpp}` (verified repo-wide). No
  call site in `example/` or `test/` references the identifier by name.
- `GenerateRandomPort` is in an anonymous namespace (`GeniusNode.cpp:46`) — internal
  linkage; the test cannot call it directly and must compare resolved ports across two
  same-private-key constructions instead.
</implementation_notes>

---

## Task 1: Rename `base_port` → `port_seed` and add Doxygen/getters in the header

<task id="1">
<read_first>
- src/account/GeniusNode.hpp (current declarations + Doxygen at lines 72-119, 678-690, 722-728; getters near 171-189; members autodht_ @660, pubsubport_ @675)
- .planning/phases/01-config-driven-settings-foundation/01-CONTEXT.md (decisions D-03, D-04, D-07)
</read_first>

<action>
In `src/account/GeniusNode.hpp`:

1. **Rename the parameter** `base_port` → `port_seed` in all five declarations (types and
   default values unchanged — only the parameter *name*):
   - `New(...)` at line 84: `uint16_t port_seed = 40001,`
   - `NewFromPrivateKey(...)` at line 101: `uint16_t port_seed = 40001,`
   - `NewFromMnemonic(...)` at line 118: `uint16_t port_seed = 40001,`
   - Private constructor at line 689: `uint16_t port_seed,`
   - `InitNetwork(...)` at line 728: `bool InitNetwork( uint16_t port_seed, bool is_full_node );`

2. **Update every `@param[in] base_port` Doxygen line** to `@param[in] port_seed` at lines
   76, 92, 109, 683, 724.

3. **Expand the `InitNetwork` Doxygen block** (currently lines 722-727) to document the
   port-resolution priority per D-04. Keep existing text; append a `@par Port resolution`
   paragraph (Doxygen `\par` or `@par`): `pubsub_port` (string override from
   `network_config.json`) takes priority; if undefined, `port_seed` derives the listening
   port via `GenerateRandomPort(port_seed, account_address)`. Add `@param[in] port_seed`
   describing it as the deterministic per-address port seed (fallback when `pubsub_port`
   is absent; overridable by the `port_seed` key in `network_config.json`).

4. **Add two public read-only getters** near the existing getters (e.g., after
   `GetInitializationStatus` / alongside `GetAddress`), matching the existing declaration
   style (lower_case name, `const noexcept`):
   - `uint16_t GetPubsubPort() const noexcept;`  — exposes resolved `pubsubport_` (test observable for D-02)
   - `bool IsAutodhtEnabled() const noexcept;`   — exposes resolved `autodht_`   (test observable for D-02)

   These are pure read accessors with no behavior change; they mirror existing accessors
   like `GetAddress()` and `GetState()`.
</action>

<acceptance_criteria>
- `grep -c "base_port" src/account/GeniusNode.hpp` returns `0`
- `grep -c "port_seed" src/account/GeniusNode.hpp` returns `>= 5`
- `grep -c "@param\[in\] port_seed" src/account/GeniusNode.hpp` returns `>= 5`
- `grep -n "GetPubsubPort() const noexcept" src/account/GeniusNode.hpp` matches once
- `grep -n "IsAutodhtEnabled() const noexcept" src/account/GeniusNode.hpp` matches once
- The `InitNetwork` Doxygen block contains the substring `pubsub_port` and the substring `GenerateRandomPort`
- Factory parameter *types* unchanged: `New` still declares `bool autodht = true, uint16_t port_seed = 40001, bool is_full_node = false` (only the second param's name changed)
</acceptance_criteria>
</task>

---

## Task 2: Add config reads, live override, rename, and Doxygen in the implementation

<task id="2" depends_on="1">
<read_first>
- src/account/GeniusNode.cpp (InitNetwork parse block lines 768-866; port-selection block 911-919; constructor 200-238; factory wrappers 134-195; anonymous GenerateRandomPort 46-60; existing logging style e.g. line 802, 881)
- src/account/GeniusNode.hpp (after Task 1 — new getter declarations + renamed signatures)
- .planning/phases/01-config-driven-settings-foundation/01-CONTEXT.md (D-01, D-06, D-07, D-08)
</read_first>

<action>
In `src/account/GeniusNode.cpp`:

1. **Define the two new getters** added in Task 1 (place near other getter definitions, or
   directly above `InitNetwork`). Match existing getter style:
   - `uint16_t GeniusNode::GetPubsubPort() const noexcept { return pubsubport_; }`
   - `bool GeniusNode::IsAutodhtEnabled() const noexcept { return autodht_; }`

2. **Rename `base_port` → `port_seed`** at every occurrence in this file (verified
   repo-wide: this is the complete set):
   - Private constructor signature line 203: `uint16_t port_seed,`
   - Constructor body line 235: `if ( !InitNetwork( port_seed, is_full_node_ ) )`
   - `New` wrapper line 134 (param) and line 141 (forwarding arg)
   - `NewFromPrivateKey` wrapper line 155 (param) and line 162 (forwarding arg)
   - `NewFromMnemonic` wrapper line 176 (param) and line 190 (`new GeniusNode( ..., port_seed, ... )`)
   - `InitNetwork` signature line 768: `bool GeniusNode::InitNetwork( uint16_t port_seed, bool is_full_node )`
   - `GenerateRandomPort` call line 918: `pubsubport_ = GenerateRandomPort( port_seed, account_->GetAddress() );`
   - Do NOT touch `GenerateRandomPort`'s own parameter `base` at line 47 (out of scope; it is a free-function-local name).

3. **Add the two config reads** inside `InitNetwork`, within the
   `if ( !config_json.HasParseError() && config_json.IsObject() )` block (after the
   existing `low_water` read around line 832, before the reconnect-config section at 835).
   Replicate the EXACT existing `HasMember && IsXxx` pattern. Concrete reads:

   - **`port_seed` (numeric, D-06/D-08):** Add immediately after the `low_water` block:
     ```
     // NOTE: port_seed is read as numeric IsUint(), intentionally diverging from the
     // legacy string-based pubsub_port read above (see HARD-01 / CONTEXT D-08).
     if ( config_json.HasMember( "port_seed" ) )
     {
         if ( config_json["port_seed"].IsUint() )
         {
             port_seed = static_cast<uint16_t>( config_json["port_seed"].GetUint() );
             node_logger_->info( "network_config.json: port_seed overridden to {}", port_seed );
         }
         else
         {
             node_logger_->warn( "network_config.json: port_seed is not a uint, using default/param {}", port_seed );
         }
     }
     ```
     (This reassigns the `port_seed` parameter — the live override per D-01. The parameter
     is a mutable local; no new local variable is needed.)

   - **`auto_dht` (bool, D-06/D-07):** Add directly after the `port_seed` read:
     ```
     if ( config_json.HasMember( "auto_dht" ) )
     {
         if ( config_json["auto_dht"].IsBool() )
         {
             autodht_ = config_json["auto_dht"].GetBool();  // JSON key auto_dht -> member autodht_ (D-07)
             node_logger_->info( "network_config.json: auto_dht overridden to {}", autodht_ );
         }
         else
         {
             node_logger_->warn( "network_config.json: auto_dht is not a bool, using default/param {}", autodht_ );
         }
     }
     ```
     (Reassigns the `autodht_` member — the live override. Constructor already initialized
     `autodht_` from the param at line 209, so absent/ill-typed key preserves today's value.)

4. **Add an inline comment at the port-selection block** (lines 911-919) documenting the
   priority per D-04. The logic is UNCHANGED — only a comment is added. Example:
   ```
   // Port resolution priority (Doxygen: see InitNetwork declaration):
   //   1. pubsub_port (string override from network_config.json) -> config_port
   //   2. else: port_seed (param or network_config.json "port_seed") derives the port
   //      via GenerateRandomPort(port_seed, account address).
   ```
</action>

<acceptance_criteria>
- `grep -c "base_port" src/account/GeniusNode.cpp` returns `0`
- `grep -n 'HasMember( "port_seed" )' src/account/GeniusNode.cpp` matches once
- `grep -n 'config_json\["port_seed"\].IsUint()' src/account/GeniusNode.cpp` matches once
- `grep -n 'HasMember( "auto_dht" )' src/account/GeniusNode.cpp` matches once
- `grep -n 'config_json\["auto_dht"\].IsBool()' src/account/GeniusNode.cpp` matches once
- `grep -n "autodht_ = config_json" src/account/GeniusNode.cpp` matches once (the live override assignment)
- `grep -n "port_seed = static_cast<uint16_t>" src/account/GeniusNode.cpp` matches once
- The `pubsub_port` string read (lines ~791-805) is UNCHANGED: `grep -c 'config_json\["pubsub_port"\].IsString' src/account/GeniusNode.cpp` returns `1`
- `GenerateRandomPort( port_seed, account_->GetAddress() )` appears at the port-selection block (line ~918)
- Full project builds: `cmake --build <build_dir> --target genius_node` exits 0 (run via the project's standard build command)
</acceptance_criteria>
</task>

---

## Task 3: Add the config-precedence test (D-02) and register it in CMake

<task id="3" depends_on="2">
<read_first>
- test/src/account/account_management_test.cpp (the harness MODEL: temp dir via boost::dll::program_location(), writes a JSON config to BaseWritePath with std::ofstream, constructs via NewFromPrivateKey(dev_config, key, autodht, port, is_full_node))
- test/src/account/CMakeLists.txt (addtest + target_link_libraries genius_node + Apple -force_load pattern)
- src/account/GeniusNode.hpp (new GetPubsubPort() / IsAutodhtEnabled() getters from Task 1)
- src/account/GeniusNode.cpp (InitNetwork reads + GenerateRandomPort anonymous-namespace caveat at line 46)
- .planning/phases/01-config-driven-settings-foundation/01-CONTEXT.md (D-01, D-02)
</read_first>

<action>
1. **Create `test/src/account/network_config_precedence_test.cpp`** modeled on
   `account_management_test.cpp`'s setup. The test proves BOTH new config keys override the
   constructor param (D-02), using assertions that do NOT depend on the internal RNG range
   of the anonymous `GenerateRandomPort`:

   - Use a per-test temp directory under
     `boost::dll::program_location().parent_path() / "nc_precedence_test"`, removing it
     first (wrap in try/catch like the model test). Use the SAME Ethereum private key
     (`"90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa"`) for every
     construction so the account address — and therefore `GenerateRandomPort`'s seed — is
     identical across scenes.

   - **Scene A — `auto_dht` config overrides the param (crisp boolean):** Write
     `network_config.json` with `{"auto_dht": false}`. Construct via
     `GeniusNode::NewFromPrivateKey(dev_config, key, /*autodht=*/true, /*port_seed=*/40001,
     /*is_full_node=*/true)`. Assert `EXPECT_FALSE( node->IsAutodhtEnabled() )`. (Param was
     `true`; config `false` must win → the getter reads `false`. This directly proves D-01
     for the `auto_dht` axis with no RNG coupling.)

   - **Scene B — `port_seed` config overrides the param (same-address comparison):** Use
     the same private key in two constructions so `GenerateRandomPort(seed, address)` is
     deterministic:
       * Construct `nodeParamOnly` with NO `port_seed` key in `network_config.json` (file
         absent or empty `{}`) and param `port_seed = 49999`. Record
         `uint16_t baseline = nodeParamOnly->GetPubsubPort();`
       * Construct `nodeConfigOverride` with `network_config.json` containing
         `{"port_seed": 49999}` but param `port_seed = 40001` (a different value). Record
         `uint16_t overridden = nodeConfigOverride->GetPubsubPort();`
       * Assert `EXPECT_EQ( baseline, overridden )`. Reasoning: both resolve through
         `GenerateRandomPort(49999, sameAddress)`; if the config key did NOT override,
         `overridden` would flow through `GenerateRandomPort(40001, sameAddress)` and
         differ. (Range-overlap caveat: choose `49999` and `40001` so the
         `[base, base+300]` ranges `[49999,50299]` and `[40001,40301]` do not overlap,
         making the equality assertion unambiguous. Construct sequentially and let each
         node leave scope / be destroyed before the next to avoid resource/port conflicts.)

   - Use a `::testing::Test` fixture with `SetUp`/`TearDown` (or per-scene test bodies) and
     the project's existing includes:
     `#include "account/GeniusNode.hpp"`, `#include "account/TokenID.hpp"`,
     `#include <boost/dll/runtime_symbol_info.hpp>`, `#include <boost/filesystem.hpp>`,
     `#include <fstream>`, `#include <gtest/gtest.h>`. Construct `dev_config` with the same
     aggregate initializer shape as the model test:
     `{ "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), path.generic_string() + '/' }`.

2. **Register the test** in `test/src/account/CMakeLists.txt` by appending a block copied
   from the `account_management_test` registration, changing only the target name to
   `network_config_precedence_test` and the source to
   `network_config_precedence_test.cpp`. Include the Apple `-force_load` /
   MSVC `/WHOLEARCHIVE` / Linux `--whole-archive` linker block exactly as the model does
   (required for the `genius_node` object library).
</action>

<acceptance_criteria>
- `test/src/account/network_config_precedence_test.cpp` exists and `#include`s `"account/GeniusNode.hpp"`
- The test file contains a scene asserting `EXPECT_FALSE( ... IsAutodhtEnabled() )` against a config where `"auto_dht": false` overrides an `autodht=true` param
- The test file contains a scene asserting `EXPECT_EQ( baseline, overridden )` comparing a no-`port_seed`-key param `49999` construction against a config-`port_seed`-`49999` + param `40001` construction, both with the same private key
- `grep -n "network_config_precedence_test" test/src/account/CMakeLists.txt` matches at least twice (addtest + target name)
- `test/src/account/CMakeLists.txt` contains a `-force_load` linker-options block for `network_config_precedence_test` (Apple branch)
- After configure+build, `ctest -R network_config_precedence_test --output-on-failure` exits 0 (test passes)
- The full existing suite stays green: `ctest --output-on-failure` shows no NEW failures attributable to this change (default/missing-key behavior is byte-identical)
</acceptance_criteria>
</task>

---

<threat_model>
**ASVS Level:** 1 (config-input surface)

**Assets:** node listening-port selection, DHT-discovery enable flag.

**Threats & mitigations:**
- **T1 — Malformed/ill-typed config value (e.g., `port_seed: "abc"`, `port_seed: -1`,
  `port_seed: 99999`, `auto_dht: "true"`):**
  - Mitigation: every new read is guarded by `HasMember && IsXxx` before `GetXxx`
    (replicates the existing mandatory pattern). Ill-typed values are WARN-logged and the
    pre-declared default/param value is retained — no crash, no silent corruption.
    Severity: LOW (graceful degrade).
- **T2 — Out-of-range `port_seed` (e.g., value > 65535 after cast, or producing a derived
  port outside the valid TCP range):**
  - Mitigation: `IsUint()` bounds the JSON value to `[0, 2^32-1]` before the
    `static_cast<uint16_t>`; the cast intentionally wraps to uint16 as the field type
    dictates. `GenerateRandomPort` adds a small `[0,300]` offset. NOTE (residual): a value
    like 65500 could derive a port > 65535 via the +300 offset and wrap; this is a
    pre-existing property of `GenerateRandomPort` (unchanged this phase) and is not
    introduced by this additive read. Severity: LOW (pre-existing).
- **T3 — Config precedence ambiguity (operator expects param to win but config silently
  overrides):**
  - Mitigation: D-01 makes precedence explicit and documented (config wins). INFO-logging
    of the resolved override value surfaces the behavior at startup. The D-02 test locks
    the precedence contract before Phase 3 migrates call sites. Severity: INFORMATIONAL.
- **T4 — Untrusted config file path traversal / arbitrary file read:**
  - Out of scope: `config_path` is built from `write_base_path_` (a trusted operator-controlled
    `dev_config.BaseWritePath`), not user input. No new path handling introduced. Severity: N/A.

**Block-on threshold (high):** No HIGH-severity threats identified. Proceed.
</threat_model>

---

<must_haves>
<truths>
- `auto_dht` and `port_seed` are read from `network_config.json` inside `InitNetwork()` using RapidJSON `HasMember && IsXxx` guards
- `port_seed` is parsed as numeric `IsUint()` (NOT the string style of `pubsub_port`); `pubsub_port` read is unchanged
- Config value wins when the key is present; the constructor param is the fallback when the key is absent (live override, not dormant)
- Defaults on missing/ill-typed keys are byte-identical to today (`autodht_=true` from param, `port_seed=40001` from param) with WARN-log on ill-typed values
- `base_port` is renamed to `port_seed` throughout `src/account/GeniusNode.{hpp,cpp}`; no `base_port` identifier remains in those files
- `pubsub_port` > `port_seed`-derived port-resolution priority is Doxygen-documented
- A test proves config `port_seed` and config `auto_dht` each override the constructor param
- The three old factory signatures (parameter TYPES) and all 18 call sites are otherwise untouched
</truths>

<verification>
1. **Build:** full project builds with no new warnings (standard project build command).
2. **Identifier audit:** `grep -rn "base_port" src/account/` returns zero matches.
3. **Read audit:** `grep -n 'HasMember( "port_seed" )\|HasMember( "auto_dht" )' src/account/GeniusNode.cpp` returns two matches; each is followed by an `IsUint()` / `IsBool()` type guard.
4. **Precedence test:** `ctest -R network_config_precedence_test` passes.
5. **Regression:** `ctest` shows no new failures; existing tests (e.g., `account_management_test`, `node_initialization_progress`, `utxo_manager_test`) remain green — default/missing-key behavior is unchanged.
6. **Doxygen:** the `InitNetwork` declaration comment and the port-selection block comment both describe the `pubsub_port` > `port_seed` priority.
</verification>

<goal_alignment>
Phase goal: "Introduce config-file reads for network settings as an additive,
behavior-preserving layer." This plan delivers exactly that — two new reads, a rename for
clarity, documentation of the resolution priority, and a test locking the precedence
contract. Node-type work (CFG-02/CFG-03) is explicitly deferred to Phase 2 (D-09) and is
not touched here.
</goal_alignment>
