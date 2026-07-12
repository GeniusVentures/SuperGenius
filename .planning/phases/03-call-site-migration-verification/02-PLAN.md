---
id: 02
title: "Migrate all ~25 GeniusNode call sites to New(dev_config, FromPrivateKey{...}) + helper-written configs"
phase: 3
wave: 2
depends_on: ["01"]
requirements: [MIG-01]
files_modified:
  - example/node_test/NodeExample.cpp
  - test/src/account/account_management_test.cpp
  - test/src/account/network_config_precedence_test.cpp
  - test/src/blockchain/blockchain_genesis_test.cpp
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/node/node_initialization_progress.cpp
  - test/src/processing_multi/processing_multi_test.cpp
  - test/src/processing_nodes/child_tokens_test.cpp
  - test/src/processing_nodes/full_node_test.cpp
  - test/src/processing_nodes/processing_nodes_test.cpp
  - test/src/transaction_sync/migration_sync_test.cpp
  - test/src/transaction_sync/transaction_crash_test.cpp
  - test/src/transaction_sync/transaction_sync_test.cpp
autonomous: true
---

# Plan 02: Call-Site Migration

<objective>
Rewrite every `GeniusNode::NewFromPrivateKey(...)` / old-`New(4-arg)` call site in `example/`
and `test/` to the canonical `New(dev_config, FromPrivateKey{...})` API, writing config files
via the Plan-01 helpers (`WriteNetworkConfig`/`WriteSgnsConfig`) so each site's pre-migration
`auto_dht`/`port_seed`/`is_full_node`/`is_processor` behavior is preserved (CONTEXT D-02, D-04;
MIG-01). `node_type_derivation_test.cpp` already uses the new API — skip it.

This plan depends on Plan 01 (the helpers). Old factories are NOT deleted here — that's Plan 03,
after every site has migrated and the build is green (CONTEXT D-03).
</objective>

<transform_rule>
**Apply uniformly to every call site** (read each file to get its exact current arguments):

For each `sgns::GeniusNode::NewFromPrivateKey( DEV_CONFIG, KEY, AUTODHT, PORT_SEED, IS_FULL_NODE )`
(or old `New(DEV_CONFIG, AUTODHT, PORT_SEED, IS_FULL_NODE)`):

1. **Before the call**, write config files into `DEV_CONFIG.BaseWritePath` (the helpers join the
   path and handle the trailing-slash form tests use):
   - `GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, PORT_SEED, AUTODHT );`
   - `GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath, IS_FULL_NODE ? "Full" : "Light", IS_PROCESSOR );`
     where `IS_PROCESSOR` is the site's existing value (`true` if the test did not previously write
     `sgns_config.json`; otherwise the `is_processor` value the test wrote).
   - **Replace any pre-existing manual `std::ofstream` write of `sgns_config.json`/`network_config.json`
     in the file** with these helper calls (DRY — the helper supersedes inline writes), preserving
     the file's existing `is_processor`/etc. values.
   - The helpers return `outcome::result<void>`; discard with `(void)` or assert success per the
     file's existing style (most tests don't check setup I/O).

2. **Replace the factory call** with the canonical API:
   `NewFromPrivateKey( DEV_CONFIG, KEY, ... )` → `New( DEV_CONFIG, FromPrivateKey{ KEY } )`.
   `KEY` is currently `key.c_str()` / a `const char*` / a string literal — `FromPrivateKey{ KEY }`
   constructs the `std::string` payload from any of these. Use the `std::string` form where the site
   has one (drop `.c_str()`): e.g. `FromPrivateKey{ key }`, `FromPrivateKey{ eth_private_key }`.

**Behavior-preservation mapping (CONTEXT D-04):** `is_full_node=true` → `node_type="Full"`;
`is_full_node=false` → `node_type="Light"`. No site uses `Archive`.
</transform_rule>

<special_case name="network_config_precedence_test.cpp">
This Phase-1 test was built to prove **"config overrides the constructor param"** (Phase-1 D-01/D-02).
Its two scenes pass `auto_dht`/`port_seed` as PARAMS to the old `NewFromPrivateKey` and assert the
config value wins. Once migrated to `New(dev_config, FromPrivateKey{...})`, **there are no
`auto_dht`/`port_seed` params anymore** — those values come only from config. So a blind transform
would delete the very thing the test asserts against.

**Reframe (not blind migration):** keep the two scenes' assertions (`EXPECT_FALSE(IsAutodhtEnabled())`
for config `auto_dht:false`; `EXPECT_GE(GetPubsubPort(), 49999)` for config `port_seed:49999`), but
reword them from "config overrides the param" to "config drives the value." Each scene writes its
config via `WriteSgnsConfig`/`WriteNetworkConfig` then constructs via `New(dev_config, FromPrivateKey{KEY})`
(no params to override). The tests still prove config-driven `auto_dht`/`port_seed` — which remains
valuable — just without the now-nonexistent "param" side. Update the scene comments accordingly.
</special_case>

---

## Task 1: example/node_test/NodeExample.cpp (1 site)

<task id="1">
<read_first>
- example/node_test/NodeExample.cpp (the call at line ~532: `NewFromPrivateKey( DEV_CONFIG, eth_private_key.c_str(), true, 40101, is_full_node )`; surrounding setup that sets `DEV_CONFIG.BaseWritePath`)
- .planning/phases/03-call-site-migration-verification/01-PLAN.md (helper signatures)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-02 — example reuses the shared statics)
</read_first>

<action>
Apply `<transform_rule>`: before the `NewFromPrivateKey` call, add
`GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/40101, /*auto_dht=*/true );`
and `GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath, is_full_node ? "Full" : "Light", /*is_processor=*/true );`
then replace the call with `GeniusNode::New( DEV_CONFIG, FromPrivateKey{ eth_private_key } )`.
(`eth_private_key` is a `std::string` — drop the `.c_str()`.)
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" example/node_test/NodeExample.cpp` returns `0`
- `grep -c "GeniusNode::New( DEV_CONFIG, FromPrivateKey{" example/node_test/NodeExample.cpp` returns `1`
- `grep -c "WriteNetworkConfig\|WriteSgnsConfig" example/node_test/NodeExample.cpp` returns `2`
- The example builds (`node_test` target, or whichever CMake target builds it)
</acceptance_criteria>
</task>

---

## Task 2: test/src/account/account_management_test.cpp (3 sites; already writes sgns_config.json)

<task id="2">
<read_first>
- test/src/account/account_management_test.cpp (3 `NewFromPrivateKey` calls ~lines 43, 132, 136; existing manual `std::ofstream` writes of `sgns_config.json` with `is_processor` true/false ~lines 36-40, 120-129)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-04)
</read_first>

<action>
Apply `<transform_rule>` to all 3 call sites. This file already writes `sgns_config.json` with
explicit `is_processor` values — **replace each manual `std::ofstream` write** with
`GeniusNode::WriteSgnsConfig( base, node_type, <that site's is_processor> )` (preserving each
site's `is_processor`), and add `WriteNetworkConfig` for each site's `port_seed`/`auto_dht`
(default `40069`/`false` per the existing call args — read each site's exact values). Replace each
`NewFromPrivateKey(...)` with `New(dev_config, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/account/account_management_test.cpp` returns `0`
- `grep -c "GeniusNode::New(" test/src/account/account_management_test.cpp` returns `3` (the 3 migrated calls)
- No remaining raw `std::ofstream` writing `sgns_config.json` (`grep -c 'ofstream.*sgns_config' ...` returns 0 — superseded by `WriteSgnsConfig`)
- `account_management_test` target builds and the test still passes (existing behavior preserved)
</acceptance_criteria>
</task>

---

## Task 3: test/src/account/network_config_precedence_test.cpp (REFRAME — special case)

<task id="3">
<read_first>
- test/src/account/network_config_precedence_test.cpp (Phase-1 test; 2 scenes using `NewFromPrivateKey` with `auto_dht`/`port_seed` params + asserting config wins via `IsAutodhtEnabled()`/`GetPubsubPort()`)
- .planning/phases/03-call-site-migration-verification/01-PLAN.md (helpers)
- This plan's `<special_case name="network_config_precedence_test.cpp">` block above
</read_first>

<action>
Apply the **reframe** described in `<special_case>`: keep both scenes' assertions, but each scene
now writes its config via the helpers and constructs via `New(MakeDevConfig(base), FromPrivateKey{ TEST_PRIVATE_KEY })`
(no `auto_dht`/`port_seed` params). Replace the manual `WriteNetworkConfig`/`WriteSgnsConfig`-equivalent
`std::ofstream` writes with the helper calls. Reword scene comments from "config overrides param" to
"config drives the value" (the param no longer exists). Assertions unchanged: Scene A
`EXPECT_FALSE(node->IsAutodhtEnabled())` with `WriteSgnsConfig`... actually Scene A asserts `auto_dht`,
so it uses `WriteNetworkConfig(base, /*port_seed=*/40001, /*auto_dht=*/false)`; Scene B asserts
`port_seed`, so it uses `WriteNetworkConfig(base, /*port_seed=*/49999, /*auto_dht=*/false)`. Add
`WriteSgnsConfig(base, "Light", true)` to both (default role; the test doesn't assert node_type).
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/account/network_config_precedence_test.cpp` returns `0`
- Both scenes construct via `New( MakeDevConfig( base ), FromPrivateKey{` (count = 2)
- `WriteNetworkConfig`/`WriteSgnsConfig` calls present (config written via helpers, no raw ofstream)
- `EXPECT_FALSE( node->IsAutodhtEnabled` and `EXPECT_GE( resolved, 49999u` assertions still present
- `network_config_precedence_test` builds and passes
</acceptance_criteria>
</task>

---

## Task 4: test/src/blockchain/blockchain_genesis_test.cpp (1 site)

<task id="4">
<read_first>
- test/src/blockchain/blockchain_genesis_test.cpp (the `NewFromPrivateKey( devConfig, key.c_str(), false, uniquePort, isFullNode )` call ~line 83; its `devConfig`/`isFullNode` setup)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md (D-04 mapping)
</read_first>

<action>
Apply `<transform_rule>`: add `WriteNetworkConfig(devConfig.BaseWritePath, uniquePort, false)` +
`WriteSgnsConfig(devConfig.BaseWritePath, isFullNode ? "Full" : "Light", true)` before the call;
replace with `New(devConfig, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/blockchain/blockchain_genesis_test.cpp` returns `0`
- `grep -c "New( devConfig, FromPrivateKey{" test/src/blockchain/blockchain_genesis_test.cpp` returns `1`
- `blockchain_genesis_test` builds and passes
</acceptance_criteria>
</task>

---

## Task 5: test/src/multiaccount/multi_account_sync.cpp (1 site)

<task id="5">
<read_first>
- test/src/multiaccount/multi_account_sync.cpp (call ~line 110: `NewFromPrivateKey( devConfig, key.c_str(), false, uniquePort, isFullNode )`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>`: `WriteNetworkConfig(devConfig.BaseWritePath, uniquePort, false)` +
`WriteSgnsConfig(devConfig.BaseWritePath, isFullNode ? "Full" : "Light", true)`; replace with
`New(devConfig, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/multiaccount/multi_account_sync.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/multiaccount/multi_account_sync.cpp` returns `1`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 6: test/src/node/node_initialization_progress.cpp (1 site)

<task id="6">
<read_first>
- test/src/node/node_initialization_progress.cpp (call ~line 24; the existing `NewFromPrivateKey` args — note it passes `40069, true` per the Phase-1 read)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` with the site's exact args (`port_seed=40069`, `auto_dht=false`, `is_full_node=true`
→ `node_type="Full"`). Add the two helper writes; replace with `New(dev_config, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/node/node_initialization_progress.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/node/node_initialization_progress.cpp` returns `1`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 7: test/src/processing_multi/processing_multi_test.cpp (3 sites)

<task id="7">
<read_first>
- test/src/processing_multi/processing_multi_test.cpp (3 calls ~lines 70, 74, 78 using `DEV_CONFIG`/`DEV_CONFIG2`/`DEV_CONFIG3`; their `auto_dht`/`port_seed`/`is_full_node` args + any existing config writes)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` to all 3 sites (each has its own `DEV_CONFIGn.BaseWritePath`, `port_seed`,
`auto_dht`, `is_full_node`). Add helper writes per site; replace each `NewFromPrivateKey` with
`New(DEV_CONFIGn, FromPrivateKey{ key })`. Preserve any per-site `is_processor` intent.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/processing_multi/processing_multi_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/processing_multi/processing_multi_test.cpp` returns `3`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 8: test/src/processing_nodes/child_tokens_test.cpp (1 site)

<task id="8">
<read_first>
- test/src/processing_nodes/child_tokens_test.cpp (call ~line 75)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` (args `false, uniquePort, isFullNode`). Add helper writes; replace with
`New(devConfig, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/processing_nodes/child_tokens_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/processing_nodes/child_tokens_test.cpp` returns `1`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 9: test/src/processing_nodes/full_node_test.cpp (1 site)

<task id="9">
<read_first>
- test/src/processing_nodes/full_node_test.cpp (call ~line 49: `NewFromPrivateKey( devConfig, privKey.c_str(), false, port, isFullNode )`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>`. Add helper writes (`port`, `false`, `isFullNode?"Full":"Light"`); replace
with `New(devConfig, FromPrivateKey{ privKey })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/processing_nodes/full_node_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/processing_nodes/full_node_test.cpp` returns `1`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 10: test/src/processing_nodes/processing_nodes_test.cpp (3 sites)

<task id="10">
<read_first>
- test/src/processing_nodes/processing_nodes_test.cpp (3 calls ~lines 49, 62, 67: `node_proc1`/`node_main`/`node_proc2`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` to all 3 sites. Add per-site helper writes; replace each with
`New(devConfign, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/processing_nodes/processing_nodes_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/processing_nodes/processing_nodes_test.cpp` returns `3`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 11: test/src/transaction_sync/migration_sync_test.cpp (2 sites)

<task id="11">
<read_first>
- test/src/transaction_sync/migration_sync_test.cpp (calls ~lines 119, 154 — note line 154 uses unqualified `GeniusNode::NewFromPrivateKey( devConfig, FULL_NODE_KEY, false, unique_port, true )`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` to both sites. `FULL_NODE_KEY` is likely a `const char*` literal —
`FromPrivateKey{ FULL_NODE_KEY }` constructs the std::string. Add helper writes; replace each call.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/transaction_sync/migration_sync_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/transaction_sync/migration_sync_test.cpp` returns `2`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 12: test/src/transaction_sync/transaction_crash_test.cpp (3 sites)

<task id="12">
<read_first>
- test/src/transaction_sync/transaction_crash_test.cpp (3 calls ~lines 74, 78, 100 using `CONFIG1`/`CONFIG2`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` to all 3 sites. Add per-site helper writes (`CONFIGn.BaseWritePath`,
site `port_seed`/`auto_dht`/`is_full_node`); replace each with `New(CONFIGn, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/transaction_sync/transaction_crash_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/transaction_sync/transaction_crash_test.cpp` returns `3`
- target builds and passes
</acceptance_criteria>
</task>

---

## Task 13: test/src/transaction_sync/transaction_sync_test.cpp (3 sites)

<task id="13">
<read_first>
- test/src/transaction_sync/transaction_sync_test.cpp (3 calls ~lines 91, 102, 107: `full_node`/`node_proc1`/`node_proc2`)
- .planning/phases/03-call-site-migration-verification/03-CONTEXT.md
</read_first>

<action>
Apply `<transform_rule>` to all 3 sites. Add per-site helper writes; replace each with
`New(CONFIGn, FromPrivateKey{ key })`.
</action>

<acceptance_criteria>
- `grep -c "NewFromPrivateKey" test/src/transaction_sync/transaction_sync_test.cpp` returns `0`
- `grep -c "FromPrivateKey{" test/src/transaction_sync/transaction_sync_test.cpp` returns `3`
- target builds and passes
</acceptance_criteria>
</task>

---

<threat_model>
**ASVS Level:** 1 (migration of test/example call sites — no new attack surface).

**Threats & mitigations:**
- **T1 — Behavior regression from a botched `node_type` mapping:** a site that passed `is_full_node=true` but whose migration writes `node_type="Light"` (or omits the `WriteSgnsConfig` call) silently flips to a light node. **Mitigation:** the uniform `<transform_rule>` maps `is_full_node ? "Full" : "Light"`; `WriteSgnsConfig` validates the string (`INVALID_NODE_TYPE` on a typo); the full CTest suite (MIG-03, verified in Plan 03) must stay green — a flipped node type would break the transaction_sync/processing_nodes tests that depend on full-node behavior. Severity: MEDIUM before mitigation, LOW after.
- **T2 — Port collision from a missing `WriteNetworkConfig`:** a multi-node test (processing_multi, transaction_sync) that relied on unique `port_seed` params, if migrated without writing `port_seed` to config, would default all nodes to 40001 → port bind conflicts. **Mitigation:** the `<transform_rule>` writes `port_seed` for every site; green CTest confirms. Severity: MEDIUM before, LOW after.

**Block-on threshold (high):** No HIGH-severity threats (T1/T2 are MEDIUM, mitigated by the rule + green-suite gate). Proceed.
</threat_model>

---

<must_haves>
<truths>
- Zero `GeniusNode::NewFromPrivateKey` / old-`New(4-arg)` call sites remain in `example/` or `test/` (all migrated to `New(dev_config, FromPrivateKey{...})`)
- Every migrated site writes `network_config.json` + `sgns_config.json` via the `WriteNetworkConfig`/`WriteSgnsConfig` helpers (Plan 01) with values preserving its pre-migration `auto_dht`/`port_seed`/`is_full_node`/`is_processor`
- `is_full_node=true` → `node_type="Full"`; `is_full_node=false` → `node_type="Light"` at every site
- `network_config_precedence_test` is reframed to "config drives value" (no param side) and still passes
- No raw `std::ofstream` writes of `sgns_config.json`/`network_config.json` remain in migrated tests (superseded by helpers)
- Old factories are NOT touched in this plan (retained; deleted in Plan 03)
- The full CTest suite stays green (no behavior change)
</truths>

<verification>
1. **Grep audit (MIG-01):** `grep -rn "GeniusNode::NewFromPrivateKey(" example/ test/` returns 0; `grep -rn "FromPrivateKey{" example/ test/` returns ~25 (one per migrated site).
2. **Helper usage:** `grep -rln "WriteNetworkConfig\|WriteSgnsConfig" example/ test/` lists all 13 migrated files.
3. **No raw config ofstream in tests:** `grep -rn 'ofstream.*\(sgns_config\|network_config\)' test/src/` returns 0 (all superseded by helpers; `node_type_derivation_test` already uses helpers).
4. **Build + CTest (MIG-03):** full build passes; existing CTest suite green — the transaction_sync/processing_nodes/account tests confirm behavior preservation.
</verification>

<goal_alignment>
MIG-01 (migrate all call sites to `New(dev_config, FromPrivateKey{...})`). This plan delivers the complete migration with behavior preservation via the Plan-01 helpers, leaving only the old-factory deletion (Plan 03) to finish the milestone.
</goal_alignment>
