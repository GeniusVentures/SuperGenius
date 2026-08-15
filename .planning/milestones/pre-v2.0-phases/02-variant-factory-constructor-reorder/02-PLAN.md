---
id: 01
title: "AccountSource variant factory + NodeType enum + config-driven is_full_node_ + ctor reorder"
phase: 2
wave: 1
depends_on: []
requirements: [INTF-01, INTF-02, INTF-03, CFG-02, CFG-03]
files_modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
  - test/src/account/node_type_derivation_test.cpp
  - test/src/account/CMakeLists.txt
autonomous: true
---

# Plan 01: Variant Factory + Constructor Reorder

<objective>
Add the canonical `New(dev_config, AccountSource)` factory with `AccountSource =
std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>`, introduce the
`NodeType` enum + case-insensitive `NodeTypeFromString()` + the `node_type` read in
`LoadSgnsConfig()`, and add a reordered private constructor that creates the account via
`std::visit` **after** `LoadSgnsConfig()` so `is_full_node_` is derived from `node_type_`
(config-driven). `New()` preserves `nullptr`-on-failure.

**Old factories + old private ctor are RETAINED unchanged (CONTEXT D-01)** so the 18
unmigrated call sites keep their param-driven `is_full_node` behavior and the build stays
green. Old-factory deletion + call-site migration is Phase 3 (INTF-04 + MIG-01..04).

The key design resolution: `LoadSgnsConfig()` only **reads** `node_type_` (it does NOT touch
`is_full_node_`). The **new ctor** derives `is_full_node_ = (node_type_ != Light)` after
`LoadSgnsConfig`; the **retained old ctor** keeps the param-driven `is_full_node_`. This lets
both paths coexist without behavior change for existing call sites.

Covers INTF-01 (canonical `New`), INTF-02 (4-source variant, `FromPublicKey` promoted),
INTF-03 (ctor reorder — account after `LoadSgnsConfig`), CFG-02 (`node_type` read + enum),
CFG-03 (derived `is_full_node_`).
</objective>

<implementation_notes>
- **D-02 (case-insensitive):** `NodeTypeFromString` lowercases input before mapping to the enum. Unknown/ill-typed → returns `std::nullopt`; `LoadSgnsConfig` then WARN-logs and leaves `node_type_` at its default (`Light`). Missing key → default `Light` + INFO-log (mirrors the existing `is_processor` default-on-missing pattern).
- **D-03 (variant shapes):** `NewAccount{}; FromPrivateKey{std::string eth_private_key;}; FromMnemonic{std::string mnemonic;}; FromPublicKey{std::string public_address;}`. NOTE: `FromPublicKey.public_address` is passed as the `public_key` arg to `GeniusAccount::NewFromPublicKey(token_id, public_key, full_node)` — the existing internal API uses "public_key" vocabulary for an address-like string; pass the value through, do NOT rename the downstream param.
- **D-04 (nullptr-on-failure):** the new ctor **throws** `std::runtime_error` if `std::visit` returns a null account; `New(dev_config, AccountSource)` wraps `new GeniusNode(...)` in `try`/`catch(...)` → returns `nullptr`.
- **D-05 (ctor body order):** `RotateLogFiles` → `InitOpenSSL` → `InitLoggers` → `LoadSgnsConfig` (sets `node_type_`) → derive `is_full_node_` → `std::visit` creates `account_` (throw if null) → `InitNetwork(40001, is_full_node_)` (default `port_seed`; Phase-1 config layer overrides from `network_config.json`) → rest of today's ctor body unchanged.
- **GeniusAccount factories** (`src/account/GeniusAccount.hpp:59-95`) all take a trailing `bool full_node` — that is why account creation must happen after `is_full_node_` is resolved. `NewFromPublicKey` takes **no base_path**.
- **Two private ctors coexist in Phase 2:** new `(dev_config, AccountSource)` (derived) and retained old `(dev_config, account, autodht, port_seed, is_full_node)` (param-driven). The old one is deleted in Phase 3 alongside the old factories.
- `account_` (hpp:655) and `is_full_node_` (hpp:676) are plain assignable members — the new ctor default-inits them and assigns in the body (they cannot be init-list-initialized since they depend on `LoadSgnsConfig`).
</implementation_notes>

---

## Task 1: Header — NodeType enum, AccountSource variant, new factory/ctor declarations, getters, member

<task id="1">
<read_first>
- src/account/GeniusNode.hpp (enum block 126-162; factories 82-119; getters near GetPubsubPort/IsAutodhtEnabled added in Phase 1; private ctor 686-690; members account_ @655, autodht_ @660, is_full_node_ @676; InitNetwork decl ~728)
- src/account/GeniusAccount.hpp (59-95 — factory signatures the visitor will call)
- .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md (D-02, D-03, D-04, D-05, CF-1..6)
</read_first>

<action>
In `src/account/GeniusNode.hpp`:

1. **Add the `NodeType` enum** immediately after the `Error` enum (before the `#ifdef SGNS_DEBUG` block, ~line 162), co-located with `NodeState`/`Error` per CF-2:
   ```
   /**
    * @brief Deployment node role, read from sgns_config.json ("node_type").
    *        Drives the derived is_full_node_ flag (Full/Archive -> true, Light -> false).
    */
   enum class NodeType : uint8_t
   {
       Full    = 0, ///< Full node (is_full_node_ = true).
       Light   = 1, ///< Light node (is_full_node_ = false). Default on missing/unknown key.
       Archive = 2, ///< Archive node (is_full_node_ = true; behavior identical to Full this milestone).
   };
   ```

2. **Add the `AccountSource` variant + source structs** in the `sgns` namespace, BEFORE the `GeniusNode` class definition (so `New(dev_config, AccountSource)` and call-site `FromPrivateKey{...}` resolve cleanly). Place them after the `DevConfig` / forward-declaration area near the top of the namespace (~line 63, after `class MigrationManager;`):
   ```
   /**
    * @brief Account-creation source for GeniusNode::New(dev_config, AccountSource).
    *        Owned std::string payloads (a std::variant owns its active alternative).
    */
   struct NewAccount {};                                  ///< Generate a new identity.
   struct FromPrivateKey { std::string eth_private_key; }; ///< Restore from an Ethereum hex private key.
   struct FromMnemonic   { std::string mnemonic; };        ///< Restore from a BIP39 mnemonic.
   struct FromPublicKey  { std::string public_address; };  ///< Load from storage by public address (read-only).
   using AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>;
   ```
   (`<variant>` and `<string>` are already transitively included; verify the file compiles.)

3. **Add the new public factory declaration** in the public section (alongside the retained old `New`/`NewFromPrivateKey`/`NewFromMnemonic`), with Doxygen noting it is the canonical entry point and `@return nullptr` on failure:
   ```
   /**
    * @brief Canonical node factory. Account identity is chosen via AccountSource; node role
    *        (is_full_node_) is derived from node_type in sgns_config.json, not a param.
    * @param[in] dev_config Runtime configuration (paths, token, payout data).
    * @param[in] source Account-creation source (NewAccount / FromPrivateKey / FromMnemonic / FromPublicKey).
    * @return Shared node instance after asynchronous DB init is scheduled, or nullptr on
    *         account-restore or initialization failure.
    */
   static std::shared_ptr<GeniusNode> New( const DevConfig  &dev_config,
                                           AccountSource        source );
   ```
   (This overloads `New` — the old `New(autodht, port_seed, is_full_node)` is retained. The overloads are unambiguous because arities/param-types differ: 2 args vs 4 args.)

4. **Add the new private constructor declaration** (alongside the retained old private ctor ~line 686):
   ```
   /**
    * @brief Constructs a node, creating the account from source AFTER LoadSgnsConfig()
    *        resolves node_type_ -> is_full_node_ (the init-order hinge fix, INTF-03).
    *        Throws std::runtime_error on account-restore failure; New() catches -> nullptr.
    */
   GeniusNode( const DevConfig &dev_config, AccountSource source );
   ```

5. **Add the `node_type_` member** next to `is_full_node_` (~line 676):
   ```
   NodeType                                 node_type_ = NodeType::Light; ///< Role from sgns_config.json (default Light).
   ```

6. **Add two read-only test getters** alongside the Phase-1 getters (`GetPubsubPort`/`IsAutodhtEnabled`):
   - `bool IsFullNode() const noexcept;` — exposes the resolved `is_full_node_` (test observable for CFG-03 derivation).
   - `NodeType GetNodeType() const noexcept;` — exposes the resolved `node_type_` (test observable for CFG-02).
</action>

<acceptance_criteria>
- `grep -n "enum class NodeType" src/account/GeniusNode.hpp` matches once (after `enum class Error`)
- `grep -c "struct FromPrivateKey\|struct FromMnemonic\|struct FromPublicKey\|struct NewAccount" src/account/GeniusNode.hpp` returns 4
- `grep -n "using AccountSource" src/account/GeniusNode.hpp` matches once
- `grep -n "static std::shared_ptr<GeniusNode> New( const DevConfig  &dev_config," src/account/GeniusNode.hpp` matches the new 2-arg overload (in addition to the retained old 4-arg `New`)
- `grep -n "GeniusNode( const DevConfig &dev_config, AccountSource source )" src/account/GeniusNode.hpp` matches once (new private ctor; the old `(dev_config, account, autodht, port_seed, is_full_node)` ctor declaration is still present)
- `grep -n "node_type_ = NodeType::Light" src/account/GeniusNode.hpp` matches once (member declaration)
- `grep -c "IsFullNode() const noexcept\|GetNodeType() const noexcept" src/account/GeniusNode.hpp` returns 2
- The retained old factories (`New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`) and old private ctor are STILL present (D-01 — unchanged)
</acceptance_criteria>
</task>

---

## Task 2: NodeTypeFromString + node_type read in LoadSgnsConfig + getter definitions

<task id="2" depends_on="1">
<read_first>
- src/account/GeniusNode.cpp (anon namespace 45-83 incl. NodeStateToString @61; LoadSgnsConfig body ~251+ with the is_processor default-on-missing pattern; Phase-1 getters GetPubsubPort/IsAutodhtEnabled)
- src/account/GeniusNode.hpp (after Task 1 — NodeType enum, node_type_ member, new getter decls)
- .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md (D-02, CF-1)
</read_first>

<action>
In `src/account/GeniusNode.cpp`:

1. **Add `NodeTypeFromString` in the anonymous namespace** (45-83, alongside `NodeStateToString`). Pure function, internal linkage; returns `std::optional<sgns::GeniusNode::NodeType>` (`<optional>` include if not present). Case-insensitive via lowercase-normalize (D-02):
   ```
   std::optional<sgns::GeniusNode::NodeType> NodeTypeFromString( std::string_view s )
   {
       std::string lower;
       lower.reserve( s.size() );
       for ( char c : s ) { lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) ); }
       if ( lower == "full" )    return sgns::GeniusNode::NodeType::Full;
       if ( lower == "light" )   return sgns::GeniusNode::NodeType::Light;
       if ( lower == "archive" ) return sgns::GeniusNode::NodeType::Archive;
       return std::nullopt;
   }
   ```
   (`<cctype>` for `std::tolower` — verify included.) Do NOT log here (no logger in anon namespace); `LoadSgnsConfig` logs.

2. **Add the `node_type` read to `LoadSgnsConfig()`**, placed alongside the existing `is_processor` read (the closest analog — it has an explicit else-default + INFO-log). Insert after the `is_processor` block. **Critical: set `node_type_` ONLY — do NOT assign `is_full_node_` here** (the new ctor derives it; the retained old ctor keeps its param value):
   ```
   if ( config_json.HasMember( "node_type" ) && config_json["node_type"].IsString() )
   {
       const auto parsed = NodeTypeFromString( config_json["node_type"].GetString() );
       if ( parsed )
       {
           node_type_ = *parsed;
           node_logger_->info( "sgns_config.json: node_type={}",
                               *parsed == NodeType::Full ? "Full"
                               : *parsed == NodeType::Archive ? "Archive" : "Light" );
       }
       else
       {
           node_type_ = NodeType::Light; // default on unrecognized value
           node_logger_->warn( "sgns_config.json: node_type '{}' unrecognized, defaulting to Light",
                               config_json["node_type"].GetString() );
       }
   }
   else
   {
       node_type_ = NodeType::Light; // default on missing key
       node_logger_->info( "sgns_config.json: node_type not set, defaulting to Light" );
   }
   ```

3. **Define the two new getters** (near the Phase-1 getter definitions):
   - `bool GeniusNode::IsFullNode() const noexcept { return is_full_node_; }`
   - `NodeType GeniusNode::GetNodeType() const noexcept { return node_type_; }`
</action>

<acceptance_criteria>
- `grep -n "NodeTypeFromString" src/account/GeniusNode.cpp` matches the definition (in the anon namespace, before `namespace sgns`)
- `grep -n 'HasMember( "node_type" )' src/account/GeniusNode.cpp` matches once (inside LoadSgnsConfig)
- `grep -n 'config_json\["node_type"\].IsString()' src/account/GeniusNode.cpp` matches once
- `grep -c "node_type_ = NodeType::Light" src/account/GeniusNode.cpp` returns 2 (unknown-value default + missing-key default)
- `grep -n "IsFullNode() const noexcept { return is_full_node_" src/account/GeniusNode.cpp` matches once
- `grep -n "GetNodeType() const noexcept { return node_type_" src/account/GeniusNode.cpp` matches once
- LoadSgnsConfig does NOT assign `is_full_node_` — `grep -n "is_full_node_ =" src/account/GeniusNode.cpp` returns no matches inside LoadSgnsConfig (derivation happens only in the new ctor, Task 3)
</acceptance_criteria>
</task>

---

## Task 3: New private ctor (reordered) + new public New() factory

<task id="3" depends_on="2">
<read_first>
- src/account/GeniusNode.cpp (current private ctor body 200-260 incl. init-list 205-220 and the post-InitNetwork body; old factories 132-197 calling `new GeniusNode(dev_config, account, autodht, port_seed, is_full_node)` + BeginDBInitialization; GeniusAccount factory signatures)
- src/account/GeniusNode.hpp (after Task 1 — new ctor + new New() declarations, node_type_ member)
- src/account/GeniusAccount.hpp (59-95 — New/NewFromPrivateKey/NewFromMnemonic/NewFromPublicKey signatures, all taking trailing `bool full_node`; NewFromPublicKey takes NO base_path)
- .planning/research/ARCHITECTURE.md §"The Critical Finding: Init-Order Chicken-and-Egg" (the prescribed reorder)
- .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md (D-04, D-05)
</read_first>

<action>
In `src/account/GeniusNode.cpp`, ADD (do not modify the retained old ctor/factories):

1. **New private constructor `GeniusNode(dev_config, AccountSource source)`** — placed alongside the old ctor. Init-list omits `account_` and `is_full_node_` (they are assigned in the body after LoadSgnsConfig); init `autodht_( true )` (Phase-1 config layer overrides from `network_config.json`). Replicate the old ctor's other init-list members (`write_base_path_`, `io_`, `io_work_guard_`, `isprocessor_( true )`, `dev_config_`, `processing_channel_topic_`, etc.) exactly. Body order (D-05):
   - `RotateLogFiles( write_base_path_ )`; `InitOpenSSL()`; `InitLoggers( write_base_path_ )` (throw `"Could not configure loggers"` on failure — same as today).
   - `node_logger_->info( version... )`.
   - `LoadSgnsConfig()` — sets `node_type_` (Task 2).
   - `is_full_node_ = ( node_type_ != NodeType::Light );` — **the derivation (CFG-03).**
   - Account creation via `std::visit` (a generic-lambda visitor with `if constexpr`, capturing `this`):
     ```
     account_ = std::visit(
         [this]( auto &&src ) -> std::shared_ptr<GeniusAccount> {
             using T = std::decay_t<decltype(src)>;
             if constexpr ( std::is_same_v<T, NewAccount> )
                 return GeniusAccount::New( dev_config_.TokenID, write_base_path_, is_full_node_ );
             else if constexpr ( std::is_same_v<T, FromPrivateKey> )
                 return GeniusAccount::NewFromPrivateKey( dev_config_.TokenID, src.eth_private_key.c_str(), write_base_path_, is_full_node_ );
             else if constexpr ( std::is_same_v<T, FromMnemonic> )
                 return GeniusAccount::NewFromMnemonic( dev_config_.TokenID, src.mnemonic, write_base_path_, is_full_node_ );
             else if constexpr ( std::is_same_v<T, FromPublicKey> )
                 return GeniusAccount::NewFromPublicKey( dev_config_.TokenID, src.public_address, is_full_node_ );
         },
         source );
     if ( !account_ ) { throw std::runtime_error( "Account creation failed" ); } // D-04: New() catches -> nullptr
     ```
     (`<variant>` and `<type_traits>` — verify included.) NOTE `FromPrivateKey.eth_private_key` is `std::string`; `GeniusAccount::NewFromPrivateKey` takes `const char*` → `.c_str()`. `FromPublicKey.public_address` → `NewFromPublicKey`'s `public_key` (`std::string_view`) arg.
   - `InitNetwork( 40001, is_full_node_ )` (throw `"Network initialization error"` on failure — same as today). `40001` is the default `port_seed`; the Phase-1 config layer overrides it from `network_config.json` when `"port_seed"` is present.
   - The remainder of today's ctor body (from `node_logger_->debug("Account Address ...)` onward: io_threads_ setup, etc.) — copy verbatim from the old ctor.

2. **New public factory `New(dev_config, AccountSource source)`** — placed alongside the retained old factories:
   ```
   std::shared_ptr<GeniusNode> GeniusNode::New( const DevConfig &dev_config, AccountSource source )
   {
       try
       {
           auto instance = std::shared_ptr<GeniusNode>( new GeniusNode( dev_config, source ) );
           if ( instance ) { instance->BeginDBInitialization(); }
           return instance;
       }
       catch ( ... ) //NOLINT(bugprone-empty-catch)
       {
           return nullptr; // D-04: preserve nullptr-on-failure contract
       }
   }
   ```
</action>

<acceptance_criteria>
- `grep -n "GeniusNode::GeniusNode( const DevConfig &dev_config, AccountSource source )" src/account/GeniusNode.cpp` matches once (the new ctor; the retained old ctor signature is still present)
- `grep -n "is_full_node_ = ( node_type_ != NodeType::Light )" src/account/GeniusNode.cpp` matches once (the derivation, inside the new ctor)
- `grep -n "account_ = std::visit(" src/account/GeniusNode.cpp` matches once
- `grep -c "std::is_same_v<T, NewAccount>\|std::is_same_v<T, FromPrivateKey>\|std::is_same_v<T, FromMnemonic>\|std::is_same_v<T, FromPublicKey>" src/account/GeniusNode.cpp` returns 4 (the visitor branches)
- `grep -n 'throw std::runtime_error( "Account creation failed" )' src/account/GeniusNode.cpp` matches once (D-04 throw)
- `grep -n "GeniusNode::New( const DevConfig &dev_config, AccountSource source )" src/account/GeniusNode.cpp` matches once (the new factory; retained old factories still present)
- `grep -n "catch ( ... )" src/account/GeniusNode.cpp` matches at least once inside the new factory body, returning `nullptr`
- The retained old factories (`New(dev_config, autodht, port_seed, is_full_node)` @132, `NewFromPrivateKey` @152, `NewFromMnemonic` @173) and old private ctor @200 are UNCHANGED — diff shows only additions, no removals
- Full project builds (genius_node target) — run via the project's standard build command
</acceptance_criteria>
</task>

---

## Task 4: node_type derivation test + CMake registration

<task id="4" depends_on="3">
<read_first>
- test/src/account/account_management_test.cpp (harness MODEL: temp dir via boost::dll::program_location(), writes JSON config to BaseWritePath via std::ofstream, constructs via a factory, uses `using namespace sgns;`)
- test/src/account/network_config_precedence_test.cpp (Phase 1 model — same harness, immediate post-construction assertion, separate temp dir per scene)
- test/src/account/CMakeLists.txt (addtest + target_link_libraries genius_node + Apple -force_load whole-archive block)
- src/account/GeniusNode.hpp (after Task 1 — AccountSource structs, IsFullNode() getter, new New(dev_config, AccountSource))
- .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md (D-02 case-insensitivity, D-04 nullptr-on-failure)
</read_first>

<action>
1. **Create `test/src/account/node_type_derivation_test.cpp`** modeled on `network_config_precedence_test.cpp`'s harness. Two scenes exercising the NEW factory (not the retained old ones):

   - Shared helper: per-scene temp dir under `boost::dll::program_location().parent_path()`, removed first; `MakeDevConfig(base)` returning `{ "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base.generic_string() + '/' }`; `WriteSgnsConfig(base, json)` writing `sgns_config.json`. Use the SAME valid private key (`"90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa"`) across scenes.

   - **Scene A — config-driven derivation + case-insensitivity (D-02/CFG-03):** Write `sgns_config.json` with `{"node_type":"full"}` (lowercase — proves case-insensitive parse). Construct via `GeniusNode::New( MakeDevConfig(base), FromPrivateKey{ TEST_PRIVATE_KEY } )`. Assert `ASSERT_NE(node, nullptr)` then `EXPECT_TRUE( node->IsFullNode() )` and `EXPECT_EQ( node->GetNodeType(), NodeType::Full )`. Rationale: the new ctor derives `is_full_node_ = (node_type_ != Light)`; `"full"` (lowercase) must parse to `Full` → `is_full_node_=true`. (Constructs a full node; assert immediately after `New()` returns — `is_full_node_` is set in the ctor body before `New()` returns. Call `Blockchain::SetAuthorizedFullNodeAddress(node->GetAddress())` for harness parity.)

   - **Scene B — nullptr-on-failure (D-04):** Write an empty/default `sgns_config.json` (`{}`). Construct via `GeniusNode::New( MakeDevConfig(base), FromPrivateKey{ "not-a-valid-hex-key" } )`. Assert `EXPECT_EQ( node, nullptr )`. Rationale: `GeniusAccount::NewFromPrivateKey` returns nullptr for an invalid key → the visitor returns nullptr → the new ctor throws `"Account creation failed"` → `New()`'s `try/catch(...)` returns nullptr (D-04).

   Includes: `"account/GeniusNode.hpp"`, `"account/TokenID.hpp"`, `"blockchain/Blockchain.hpp"`, `<boost/dll/runtime_symbol_info.hpp>`, `<boost/filesystem.hpp>`, `<fstream>`, `<gtest/gtest.h>`. `using namespace sgns;` so `FromPrivateKey{...}` and `NodeType::Full` resolve.

2. **Register the test** in `test/src/account/CMakeLists.txt` by appending a block mirroring the `network_config_precedence_test` registration (from Phase 1): `addtest(node_type_derivation_test node_type_derivation_test.cpp)` + `target_link_libraries(... genius_node)` + the MSVC `/WHOLEARCHIVE` / Apple `-force_load` / Linux `--whole-archive` link-options block.
</action>

<acceptance_criteria>
- `test/src/account/node_type_derivation_test.cpp` exists and `#include`s `"account/GeniusNode.hpp"`
- The test constructs via the NEW factory `GeniusNode::New( MakeDevConfig(...), FromPrivateKey{ ... } )` in both scenes (NOT the retained old factories)
- Scene A uses `{"node_type":"full"}` (lowercase) and asserts `EXPECT_TRUE( node->IsFullNode() )` AND `EXPECT_EQ( node->GetNodeType(), NodeType::Full )`
- Scene B asserts `EXPECT_EQ( node, nullptr )` for an invalid private key
- `grep -n "node_type_derivation_test" test/src/account/CMakeLists.txt` matches at least twice (addtest + target name)
- `grep -n "node_type_derivation_test PUBLIC -force_load" test/src/account/CMakeLists.txt` matches once (Apple whole-archive block)
- After configure+build, `ctest -R node_type_derivation_test --output-on-failure` exits 0
- The retained old factories still serve the 18 existing call sites unchanged (existing tests `account_management_test`, `node_initialization_progress`, `utxo_manager_test` remain green — they use the retained old factories with param-driven `is_full_node`)
</acceptance_criteria>
</task>

---

<threat_model>
**ASVS Level:** 1 (config-input + construction-failure surface)

**Assets:** node role (`is_full_node_`), account identity, node startup reliability.

**Threats & mitigations:**
- **T1 — Malformed/unknown `node_type` value** (e.g., `"fulll"`, `"FULLNODE"`, empty string): `NodeTypeFromString` returns `std::nullopt`; `LoadSgnsConfig` WARN-logs and defaults `node_type_` to `Light`. Graceful degrade; a misconfigured node starts as Light (least-privilege). Severity: LOW.
- **T2 — Null `account_` dereference** if `std::visit` returns nullptr (account-restore failure) and the explicit `if (!account_) throw` guard were missing: would crash at the next `account_->GetAddress()` dereference. **Mitigation (mandatory):** the new ctor throws `std::runtime_error("Account creation failed")` immediately after `std::visit` when `account_` is null (D-04). Severity before mitigation: HIGH (crash/DoS); after mitigation: LOW (translated to `nullptr` return).
- **T3 — `New()` swallowing unexpected exceptions** via `catch(...)` → could mask programming errors as benign `nullptr`. Accepted trade-off (D-04) to preserve the nullable contract callers rely on. Mitigation: the ctor's existing targeted throws (`"Could not configure loggers"`, `"Network initialization error"`, `"Account creation failed"`) are the expected failure paths; unexpected exceptions propagating to `catch(...)` indicate a bug to investigate via logs. Severity: INFORMATIONAL.
- **T4 — Variant type-confusion** (visitor missing a branch): `std::visit` throws `std::bad_variant_access` if the visitor is ill-formed; the generic-lambda-with-`if constexpr` covers all 4 alternatives. A future 5th `AccountSource` alternative added without a visitor branch would compile (no `else`) and return nullptr → caught by T2's guard → throws → `nullptr`. Severity: LOW.

**Block-on threshold (high):** T2 is HIGH before mitigation; the mandatory throw guard (Task 3 action) reduces it to LOW. No HIGH-severity threats remain after the guard. Proceed.
</threat_model>

---

<must_haves>
<truths>
- `New(dev_config, AccountSource)` is the canonical factory; `AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>` with owned-`std::string` payloads (D-03)
- `NodeType` enum co-located with `NodeState`/`Error` in `GeniusNode.hpp`; `NodeTypeFromString` is case-insensitive, defaulting to `Light` on unknown/missing (D-02)
- `LoadSgnsConfig()` reads `node_type` from `sgns_config.json` into `node_type_`; it does NOT assign `is_full_node_`
- The new private ctor creates the account via `std::visit` AFTER `LoadSgnsConfig()` and derives `is_full_node_ = (node_type_ != Light)` (INTF-03 / CFG-03) — the init-order hinge is resolved
- `New(dev_config, AccountSource)` returns `nullptr` on account-restore/initialization failure (D-04): ctor throws, factory catches
- The retained old factories + old private ctor keep their param-driven `is_full_node` behavior unchanged (D-01) so the 18 unmigrated call sites are unaffected
- Downstream consumers (`UTXOManager`, `TransactionManager`, `MigrationManager`, `GeniusAccount`) receive the bool unchanged (criterion #5)
</truths>

<verification>
1. **Build:** full project builds (genius_node target) with no new warnings.
2. **Identifier audit:** `grep -rn "NodeTypeFromString" src/account/` returns the definition + the LoadSgnsConfig call; `grep -rn "std::visit" src/account/GeniusNode.cpp` returns the visitor.
3. **Derivation audit:** `grep -n "is_full_node_ = ( node_type_ != NodeType::Light )" src/account/GeniusNode.cpp` matches exactly once (new ctor); `grep -n "is_full_node_ =" src/account/GeniusNode.cpp` shows NO match inside `LoadSgnsConfig`.
4. **Retention audit (D-01):** the old factories + old private ctor are still present and unchanged — `git diff` shows only ADDITIONS in GeniusNode.{hpp,cpp} (no deletions).
5. **New-factory test:** `ctest -R node_type_derivation_test` passes (derivation + case-insensitivity + nullptr-on-failure).
6. **Regression (no behavior change for existing call sites):** `ctest -R 'account_management_test|node_initialization_progress|utxo_manager_test'` stays green — these use the retained old factories whose param-driven `is_full_node` path is untouched.
</verification>

<goal_alignment>
Phase goal: collapse to `New(dev_config, AccountSource)`, reorder the ctor (account after `LoadSgnsConfig`), add `NodeType` + `node_type` read, derive `is_full_node_`. This plan delivers exactly that — the new canonical factory + reordered ctor + enum + config read + derivation, while retaining the old factories (D-01) so Phase 2 stays build-green and behavior-preserving for existing call sites. Old-factory deletion + 18-call-site migration is Phase 3.
</goal_alignment>
