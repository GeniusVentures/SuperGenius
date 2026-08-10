---
phase: quick-260704-hfi
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
  - example/node_test/NodeExample.cpp
  - test/src/processing_nodes/full_node_test.cpp
  - test/src/processing_nodes/processing_nodes_test.cpp
  - test/src/transaction_sync/migration_sync_test.cpp
autonomous: true
requirements: []
must_haves:
  truths:
    - "GeniusNode construction no longer reads sgns_config.json from disk"
    - "Tests construct an SgnsConfig directly in C++ (no file I/O) to control is_processor"
    - "NodeExample.cpp still loads sgns_config.json from BaseWritePath before constructing GeniusNode"
    - "All existing factory callers compile and tests pass"
  artifacts:
    - path: "src/account/GeniusNode.hpp"
      provides: "SgnsConfig struct definition with defaults"
      contains: "struct SgnsConfig"
    - path: "src/account/GeniusNode.hpp"
      provides: "LoadSgnsConfig free/static function declaration taking BaseWritePath"
      contains: "LoadSgnsConfig"
  key_links:
    - from: "example/node_test/NodeExample.cpp"
      to: "GeniusNode::LoadSgnsConfig"
      via: "static call before NewFromPrivateKey"
      pattern: "GeniusNode::LoadSgnsConfig"
    - from: "test/src/processing_nodes/full_node_test.cpp"
      to: "SgnsConfig"
      via: "struct literal with isProcessor = false"
      pattern: "SgnsConfig"
---

<objective>
Extract LoadSgnsConfig() from GeniusNode's private members into a standalone SgnsConfig struct + static loader, so is_processor (and other sgns_config.json values) are supplied to the constructor rather than read from disk inside it. Tests can then dependency-inject these values without writing per-node sgns_config.json files.

Purpose: Eliminate filesystem side-effects during GeniusNode construction so tests are deterministic and hermetic.
Output: Refactored GeniusNode API (3 factory overloads), SgnsConfig struct, NodeExample caller updated, 3 test files migrated.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@src/account/GeniusNode.hpp
@src/account/GeniusNode.cpp
@example/node_test/NodeExample.cpp
@example/node_test/sgns_config.json

<interfaces>
<!-- Current factory signatures (src/account/GeniusNode.hpp lines 90-127): -->
static std::shared_ptr<GeniusNode> New( const DevConfig &dev_config,
                                        bool autodht = true,
                                        uint16_t base_port = 40001,
                                        bool is_full_node = false );
static std::shared_ptr<GeniusNode> NewFromPrivateKey( const DevConfig &dev_config,
                                                      const char *eth_private_key,
                                                      bool autodht = true,
                                                      uint16_t base_port = 40001,
                                                      bool is_full_node = false );
static std::shared_ptr<GeniusNode> NewFromMnemonic( const DevConfig &dev_config,
                                                    const std::string &mnemonic,
                                                    bool autodht = true,
                                                    uint16_t base_port = 40001,
                                                    bool is_full_node = false );

<!-- Current private constructor (lines 720-724): -->
GeniusNode( const DevConfig &dev_config,
            std::shared_ptr<GeniusAccount> account,
            bool autodht,
            uint16_t base_port,
            bool is_full_node );

<!-- LoadSgnsConfig body (GeniusNode.cpp lines 271-332): reads write_base_path_ + "/sgns_config.json",
     sets: version::SetNetworkId(net_id), isprocessor_, subnet_id_, bootstrap_fullnodes_,
     Blockchain::SetAuthorizedFullNodeAddress(addr). -->

<!-- Members populated by LoadSgnsConfig (GeniusNode.hpp): -->
// bool isprocessor_ (line 681, defaults true)
// uint16_t subnet_id_ (line 703, defaults 0)
// std::vector<std::string> bootstrap_fullnodes_ (line 704)

<!-- SgnsConfig.json schema (example/node_test/sgns_config.json): -->
// { "net_id": uint, "is_processor": bool, "subnet_id": uint,
//   "bootstrap_fullnodes": [string], "authorized_full_node": string }
</interfaces>
</context>

<tasks>

<task type="auto" tdd="false">
  <name>Task 1: Introduce SgnsConfig struct + static LoadSgnsConfig, thread values through constructor and factories</name>
  <files>src/account/GeniusNode.hpp, src/account/GeniusNode.cpp</files>
  <behavior>
    - SgnsConfig is an aggregate struct with the five fields and working defaults (isProcessor=true, netId=144, subnetId=0, empty bootstrap list, empty authorized address).
    - GeniusNode::LoadSgnsConfig(const std::string& base_write_path) is a static (or free) function returning SgnsConfig; it performs the same JSON parsing and the same logging via a spdlog logger. Side effects on globals (version::SetNetworkId, Blockchain::SetAuthorizedFullNodeAddress) must STILL happen during loading so NodeExample behavior is unchanged.
    - The private GeniusNode constructor takes an additional SgnsConfig parameter and uses it to populate isprocessor_, subnet_id_, and bootstrap_fullnodes_ instead of calling LoadSgnsConfig().
    - The three public factories (New, NewFromPrivateKey, NewFromMnemonic) each gain an optional trailing SgnsConfig parameter defaulting to std::nullopt. When the caller does not supply one, the factory invokes GeniusNode::LoadSgnsConfig(dev_config.BaseWritePath) itself before constructing the node — preserving current behavior for NodeExample.cpp and any caller that does not pass an explicit config.
  </behavior>
  <action>
Define `struct SgnsConfig` in the `sgns` namespace in GeniusNode.hpp (near the DevConfig typedef, lines 49-59). Use aggregate-style members with defaults:

  - `bool isProcessor = true;`
  - `uint16_t netId = 144;`          // matches current default logged in LoadSgnsConfig
  - `uint16_t subnetId = 0;`
  - `std::vector<std::string> bootstrapFullnodes;`
  - `std::string authorizedFullNode;`  // empty means "not set"

Add a Doxygen header. Do NOT use designated initializers (project rule: C++17 only, MSVC C7555).

Replace the private member declaration `void LoadSgnsConfig();` (line 736) with:

    static SgnsConfig LoadSgnsConfig( const std::string &base_write_path );

Keep the implementation in GeniusNode.cpp at the same site (lines 271-332). Convert it to a static function returning SgnsConfig. Behavior must remain identical to today:
  - Open `base_write_path + "/sgns_config.json"`. On missing/invalid file, log the same info message and return a default-constructed SgnsConfig.
  - Parse all five fields. STILL call `version::SetNetworkId(net_id)` and `Blockchain::SetAuthorizedFullNodeAddress(addr)` as side effects (these were global side effects in the original; NodeExample relies on them). Populate the returned SgnsConfig with the parsed values.
  - Use `spdlog::info(...)` / `spdlog::warn(...)` directly (the function is no longer a member, so it has no `node_logger_`). NodeExample.cpp already links spdlog; this keeps logging behavior identical.

Update the private constructor signature (line 720) to accept `const SgnsConfig &sgns_config` as the LAST parameter (after `is_full_node`). In the constructor body:
  - Remove the call `LoadSgnsConfig();` at line 253.
  - Initialize `isprocessor_ = sgns_config.isProcessor;`, `subnet_id_ = sgns_config.subnetId;`, and `bootstrap_fullnodes_ = sgns_config.bootstrapFullnodes;` in the member initializer list (preferred per project rules) or at the top of the body BEFORE InitNetwork runs (bootstrap_fullnodes_ is consumed by InitNetwork/InitBootstrapReconnect).
  - Do NOT re-call `version::SetNetworkId` or `Blockchain::SetAuthorizedFullNodeAddress` here — the loader already did them. (NodeExample path: loader is invoked by the factory. Test path: tests that inject SgnsConfig directly are not expected to set these globals, which matches today's behavior where tests that skip the file also skip those side effects.)

Update the three factories (GeniusNode.cpp lines 146, 167, 193) to accept `std::optional<SgnsConfig> sgns_config = std::nullopt` as the FINAL parameter. Inside each factory, before constructing the GeniusNode:

    const SgnsConfig resolved = sgns_config.value_or( LoadSgnsConfig( dev_config.BaseWritePath ) );

Then pass `resolved` to the constructor. This preserves existing behavior for every current caller (NodeExample.cpp and any test that does not pass an explicit config) while allowing tests to pass `SgnsConfig{ /* isProcessor = */ false, ... }`.

Update the factory declarations in GeniusNode.hpp (lines 90, 106, 123) to match: add the trailing `std::optional<SgnsConfig> sgns_config = std::nullopt` parameter and `#include <optional>` is already present (line 16).

Keep `isprocessor_`, `subnet_id_`, `bootstrap_fullnodes_` as private members — only their initialization source changes.

Do NOT change any other GeniusNode method. Do NOT touch LoadCrdtConfig or other config loaders.
  </action>
  <verify>
    <automated>cd build/OSX/Debug && cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5 && ninja genius_node_test 2>&1 | tail -20</automated>
  </verify>
  <done>
    - GeniusNode.hpp declares `struct SgnsConfig` and `static SgnsConfig LoadSgnsConfig(const std::string&)`.
    - Private constructor accepts SgnsConfig; no LoadSgnsConfig() call inside constructor body.
    - Three factories accept trailing `std::optional<SgnsConfig> = std::nullopt`.
    - `genius_node_test` target compiles cleanly with zero warnings.
    - grep confirms no remaining `void LoadSgnsConfig();` member declaration: `grep -c "void LoadSgnsConfig" src/account/GeniusNode.hpp` returns 0.
  </done>
</task>

<task type="auto" tdd="false">
  <name>Task 2: Update NodeExample.cpp caller and migrate the 3 test files that write sgns_config.json</name>
  <files>example/node_test/NodeExample.cpp, test/src/processing_nodes/full_node_test.cpp, test/src/processing_nodes/processing_nodes_test.cpp, test/src/transaction_sync/migration_sync_test.cpp</files>
  <action>
NodeExample.cpp (line 531-532): leave the factory call as-is. The default `std::nullopt` triggers the factory to call `LoadSgnsConfig(gGeniusNodeConfig.BaseWritePath)` internally, so production behavior (reads sgns_config.json from BaseWritePath) is unchanged. No code change required here — verify by reading the call site.

For each of the 3 test files, replace the "write sgns_config.json to disk" idiom with an explicit SgnsConfig passed to the factory. Pattern (full_node_test.cpp lines 43-52, processing_nodes_test.cpp lines 48-54, migration_sync_test.cpp lines 126-132 and 160-166):

  - DELETE the `std::filesystem::create_directories(...)` + `std::ofstream configFile(...)` + `configFile << R"({"is_processor": false})"` blocks. Leave any `create_directories` calls that establish the BaseWritePath itself (tests still need the directory for databases/logs) — only remove the sgns_config.json file-writing portion.
  - DELETE the comments `// is_processor is now read exclusively from sgns_config.json (defaults to true).` (these become stale).
  - Pass an explicit SgnsConfig as the new trailing factory argument:

        sgns::SgnsConfig non_processor_config;
        non_processor_config.isProcessor = false;
        auto node = GeniusNode::NewFromPrivateKey( devConfig, privKey.c_str(), false, port, isFullNode, non_processor_config );

    Do NOT use designated initializers (project rule). Use named field assignment.

For migration_sync_test.cpp there are TWO call sites (instance creation ~line 134 AND full-node creation ~line 169): migrate both. Both want `isProcessor = false`.

After migration, the test source files must contain ZERO occurrences of the string `sgns_config.json`. Verify with grep before declaring done.
  </action>
  <verify>
    <automated>cd build/OSX/Debug && ninja genius_node_test full_node_test processing_nodes_test migration_sync_test 2>&1 | tail -20 && grep -rn "sgns_config.json" test/src/processing_nodes/full_node_test.cpp test/src/processing_nodes/processing_nodes_test.cpp test/src/transaction_sync/migration_sync_test.cpp | wc -l | tr -d ' '</automated>
  </verify>
  <done>
    - All 4 binaries compile cleanly.
    - grep for `sgns_config.json` across the 3 migrated test files returns 0.
    - NodeExample.cpp call site is unchanged (still relies on default file-based load).
  </done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 3: Run the 3 migrated tests and confirm they still pass</name>
  <what-built>
    GeniusNode now accepts an injected SgnsConfig so tests no longer write sgns_config.json files. The 3 migrated tests construct non-processor nodes via the new optional parameter.
  </what-built>
  <how-to-verify>
    From `build/OSX/Debug`, run the three migrated test binaries (use `test-unlocked` if keychain prompts appear, though these tests use MemorySecureStorage so should not prompt):

    1. `./full_node_test`
    2. `./processing_nodes_test`
    3. `./migration_sync_test`

    Expected: all three pass with the same assertions as before the refactor. If any test fails, capture the assertion text and the failing node's state (GetInitializationStatus()) for diagnosis.
  </how-to-verify>
  <resume-signal>Type "approved" if all three pass, or paste the failing assertion(s).</resume-signal>
</task>

</tasks>

<verification>
- `ninja` builds the four affected binaries (genius_node_test, full_node_test, processing_nodes_test, migration_sync_test) cleanly.
- `grep -rn "sgns_config.json" test/src/` returns matches ONLY in test files NOT migrated by this plan (other tests that still use the file-based default are untouched and unaffected — they will continue to work via the factory's `std::nullopt` default path).
- The three migrated tests pass.
- NodeExample.cpp is byte-identical to its pre-refactor state (production path preserved).
</verification>

<success_criteria>
- SgnsConfig struct exists in GeniusNode.hpp with the five documented fields and safe defaults.
- LoadSgnsConfig is a static member (or free function) taking BaseWritePath, returning SgnsConfig, preserving the global side effects (version::SetNetworkId, Blockchain::SetAuthorizedFullNodeAddress) for the file-based path.
- GeniusNode constructor accepts SgnsConfig and no longer reads sgns_config.json.
- The three public factories accept an optional trailing SgnsConfig (default std::nullopt → file-based load).
- Three test files migrated: no sgns_config.json file writes, is_processor injected via struct.
- NodeExample.cpp unchanged.
- All migrated tests pass.
</success_criteria>

<output>
Create `.planning/quick/260704-hfi-need-to-refactor-the-config-loader-it-re/260704-hfi-SUMMARY.md` when done
</output>
