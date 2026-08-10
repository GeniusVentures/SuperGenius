---
id: 01
title: "GeniusNode config-write helper statics (WriteNetworkConfig / WriteSgnsConfig)"
phase: 3
wave: 1
depends_on: []
requirements: [MIG-02]
files_modified:
  - src/account/GeniusNode.hpp
  - src/account/GeniusNode.cpp
autonomous: true
---

# Plan 01: Config-Write Helper Statics

<objective>
Add the two shared `static` helper methods on `GeniusNode` that every migrated call site
(tests + example) will call to materialize `network_config.json` and `sgns_config.json`
before constructing a node via `New(dev_config, AccountSource)`. This is MIG-02 and the
foundation for all call-site migration (Plan 02).

Per CONTEXT D-01: `WriteNetworkConfig` writes `port_seed`+`auto_dht`; `WriteSgnsConfig` writes
`node_type`+`is_processor` and **validates `node_type`** via `NodeTypeFromString` (case-insensitive,
Phase 2 D-02), returning `Error::INVALID_NODE_TYPE` on an unrecognized string. Both return
`outcome::result<void>`. Per CONTEXT D-01 the user accepted the GeniusNode god-class growth
(`.planning/codebase/CONCERNS.md`) in exchange for DRY + no new file.
</objective>

---

## Task 1: Declare the two static helpers + INVALID_NODE_TYPE in the header

<task id="1">
<read_first>
- src/account/GeniusNode.hpp (Error enum block ending TRANSACTION_FAILED=15; NodeType enum added in Phase 2 right after Error; public static factories region ~82-119; getters ~175-200)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-01 — exact signatures, validation contract)
- .planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md (CF-2: node_type/is_processor keys; NodeType enum)
</read_first>

<action>
In `src/account/GeniusNode.hpp`:

1. **Extend the `Error` enum** with one new constant after `TRANSACTION_FAILED = 15`:
   `INVALID_NODE_TYPE = 16, ///< sgns_config.json node_type string was not Full/Light/Archive.`
   (Phase 2 added the `NodeType` enum immediately AFTER `Error`; the new constant goes INSIDE `Error`, before its closing `};`.)

2. **Declare the two public static helpers** in the public section (place them alongside the canonical `New(dev_config, AccountSource)` factory declared in Phase 2, or near the `Get*` getters). Exact signatures:
   ```
   /**
    * @brief Writes a minimal network_config.json for test/example setup.
    * @param[in] base_path Directory whose network_config.json will be (over)written (dev_config.BaseWritePath).
    * @param[in] port_seed Numeric port seed (Phase-1 network_config.json key "port_seed").
    * @param[in] auto_dht  Whether DHT discovery is enabled (key "auto_dht").
    * @return Failure on file I/O error; success otherwise. Truncates/rewrites the file (deterministic minimal config).
    */
   static outcome::result<void> WriteNetworkConfig( const std::string &base_path,
                                                    uint16_t            port_seed,
                                                    bool                auto_dht );

   /**
    * @brief Writes a minimal sgns_config.json for test/example setup; validates node_type.
    * @param[in] base_path Directory whose sgns_config.json will be (over)written.
    * @param[in] node_type Role string — validated case-insensitively (Full/Light/Archive); any other value returns INVALID_NODE_TYPE.
    * @param[in] is_processor Whether processing services run (key "is_processor").
    * @return Error::INVALID_NODE_TYPE on an unrecognized node_type; failure on I/O error; success otherwise.
    */
   static outcome::result<void> WriteSgnsConfig( const std::string &base_path,
                                                 const std::string &node_type,
                                                 bool               is_processor );
   ```
   (`outcome::result` is already used throughout this header — no new include.) Ensure `<fstream>`/`<filesystem>` availability is consistent with the rest of the header (the implementation in GeniusNode.cpp will use boost::filesystem + std::ofstream, both already used in the .cpp).
</action>

<acceptance_criteria>
- `grep -n "INVALID_NODE_TYPE = 16" src/account/GeniusNode.hpp` matches once (inside the `Error` enum, before its `};`)
- `grep -c "static outcome::result<void> WriteNetworkConfig(" src/account/GeniusNode.hpp` returns 1
- `grep -c "static outcome::result<void> WriteSgnsConfig(" src/account/GeniusNode.hpp` returns 1
- Both declarations are in the `public:` section (before the `private:` ctor/members)
</acceptance_criteria>
</task>

---

## Task 2: Define the two helpers in the implementation

<task id="2" depends_on="1">
<read_first>
- src/account/GeniusNode.cpp (anon-namespace `NodeTypeFromString` from Phase 2 — `WriteSgnsConfig` reuses it; existing `LoadSgnsConfig`/`InitNetwork` show the JSON key spellings: `port_seed`, `auto_dht`, `node_type`, `is_processor`; boost::filesystem + std::ofstream already used)
- src/account/GeniusNode.hpp (after Task 1 — the two declarations + INVALID_NODE_TYPE)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-01)
</read_first>

<action>
In `src/account/GeniusNode.cpp`, define the two static methods (place them near the canonical `New(dev_config, AccountSource)` factory added in Phase 2). Use `boost::filesystem::path(base_path) / "<file>"` for path joining (handles trailing-slash `BaseWritePath` cleanly) and `std::ofstream` to write minimal JSON. `WriteSgnsConfig` validates by calling the anon-namespace `NodeTypeFromString(node_type)`:

- **`WriteNetworkConfig`**: write `{"port_seed": <port_seed>, "auto_dht": <auto_dht>}` to `network_config.json`. Return failure if `std::ofstream` fails to open.
- **`WriteSgnsConfig`**: call `NodeTypeFromString(node_type)`; if it returns `std::nullopt`, return `GeniusNode::Error::INVALID_NODE_TYPE`. Otherwise write `{"node_type": "<node_type>", "is_processor": <is_processor>}` to `sgns_config.json`. (Write the `node_type` string as-passed — the runtime `LoadSgnsConfig` parser is case-insensitive, so any-case input reads back correctly.) Return failure if `std::ofstream` fails to open.

   Use `OUTCOME_TRY` / the project's outcome error-return convention. The error value for a bad node_type is `Error::INVALID_NODE_TYPE` (a `GeniusNode::Error` enum value, returned via the outcome result).
</action>

<acceptance_criteria>
- `grep -n "outcome::result<void> GeniusNode::WriteNetworkConfig(" src/account/GeniusNode.cpp` matches once
- `grep -n "outcome::result<void> GeniusNode::WriteSgnsConfig(" src/account/GeniusNode.cpp` matches once
- `grep -n "NodeTypeFromString( node_type )" src/account/GeniusNode.cpp` matches once (inside WriteSgnsConfig validation)
- `grep -n "Error::INVALID_NODE_TYPE" src/account/GeniusNode.cpp` matches once (the return on unrecognized node_type)
- Both helpers write the four canonical keys: `grep -c '"port_seed"\|"auto_dht"' src/account/GeniusNode.cpp` ≥ existing + new; `WriteSgnsConfig` writes `"node_type"` and `"is_processor"` (verify by reading the definition)
- Full project builds (`genius_node` target) with no new warnings
</acceptance_criteria>
</task>

---

<threat_model>
**ASVS Level:** 1 (test/example config-writing surface; not a runtime attack surface — operators ship config files in deployment, these helpers are setup utilities).

**Threats & mitigations:**
- **T1 — Path traversal via `base_path`:** the helpers join `base_path` to a fixed filename. `base_path` is `dev_config.BaseWritePath` (operator-controlled, not external user input). No new path handling introduced. Severity: N/A.
- **T2 — Truncate-and-rewrite clobbers an existing operator config:** the helpers overwrite `network_config.json`/`sgns_config.json`. Accepted: they are test/example setup utilities producing deterministic minimal files; the Doxygen (Task 1) documents the truncate-and-rewrite contract. A deployed node does NOT call these. Severity: LOW.
- **T3 — Invalid `node_type` silently written then mis-defaulted at runtime:** mitigated by `WriteSgnsConfig` validating via `NodeTypeFromString` and returning `INVALID_NODE_TYPE` (a typo fails loudly at setup, not silently at read). Severity: LOW after mitigation.

**Block-on threshold (high):** No HIGH-severity threats. Proceed.
</threat_model>

---

<must_haves>
<truths>
- `GeniusNode::WriteNetworkConfig(base, port_seed, auto_dht)` and `GeniusNode::WriteSgnsConfig(base, node_type, is_processor)` exist as public static `outcome::result<void>` methods
- `WriteSgnsConfig` validates `node_type` via `NodeTypeFromString` and returns `Error::INVALID_NODE_TYPE` on an unrecognized string
- The four config keys written match what `InitNetwork`/`LoadSgnsConfig` read: `port_seed`, `auto_dht`, `node_type`, `is_processor`
- `Error::INVALID_NODE_TYPE = 16` is added to the `Error` enum
</truths>

<verification>
1. **Build:** `genius_node` target compiles.
2. **Decl audit:** both static declarations present in the header public section; `INVALID_NODE_TYPE = 16` inside `Error`.
3. **Validation audit:** `WriteSgnsConfig` calls `NodeTypeFromString` and returns `INVALID_NODE_TYPE` on `std::nullopt`.
4. (Behavioral) A caller passing `WriteSgnsConfig(base, "Garbage", true)` receives `INVALID_NODE_TYPE`; passing `"Full"` succeeds and writes the file. (Exercised end-to-end in Plan 02's migrated tests.)
</verification>

<goal_alignment>
MIG-02 (shared test config-write helper writing network_config.json + sgns_config.json). This plan delivers exactly that, as two reusable statics on GeniusNode per CONTEXT D-01, enabling Plan 02's migration.
</goal_alignment>
