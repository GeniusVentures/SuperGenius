---
plan: 02
phase: 3-call-site-migration-verification
status: code-complete-build-pending
requirements: [MIG-01]
---

# Plan 02 Summary — Call-Site Migration

## What was built
Migrated **all 25 `GeniusNode::NewFromPrivateKey` / old-`New(4-arg)` call sites across 13 files** to `New(dev_config, FromPrivateKey{...})` + helper-written configs (MIG-01). Each site's pre-migration `auto_dht`/`port_seed`/`is_full_node`/`is_processor` behavior preserved via `WriteNetworkConfig`/`WriteSgnsConfig` (`is_full_node=true`→`node_type="Full"`, `false`/defaults→`"Light"`).

**Files (13):** `example/node_test/NodeExample.cpp`; `test/src/{account/account_management_test (3 sites), account/network_config_precedence_test (REFRAMED), blockchain/blockchain_genesis_test, multiaccount/multi_account_sync, node/node_initialization_progress, processing_multi/processing_multi_test (3), processing_nodes/{child_tokens,full_node,processing_nodes}_test (1/1/3), transaction_sync/{transaction_crash (3),transaction_sync (3),migration_sync (2)}_test}.cpp`.

**Reframe (network_config_precedence_test):** its Phase-1 "config overrides param" framing can't survive old-factory deletion (canonical factory has no `autodht`/`port_seed` params) → reframed to "config drives value"; assertions (`EXPECT_FALSE(IsAutodhtEnabled)`, `EXPECT_GE(port, 49999)`) preserved; removed the conflicting local `WriteNetworkConfig` helper.

**DRY:** all manual `std::ofstream` writes of `sgns_config.json`/`network_config.json` in migrated tests replaced by the helper calls.

## Commits
- `refactor(03-02): migrate single-site call sites...` (6 files)
- `refactor(03-02): migrate account_management (3 sites), processing_multi (3), processing_nodes (3); reframe network_config_precedence` (4 files)
- `refactor(03-02): migrate transaction_sync trio...` (3 files)

## Self-Check (static, PASS)
- ✓ MIG-01 grep: `GeniusNode::NewFromPrivateKey`/`NewFromMnemonic` in `example/`+`test/` = **0**; `FromPrivateKey{` count = 27 (25 migrated + 2 in Phase-2 `node_type_derivation_test`).
- ✓ All 13 migrated files call `WriteNetworkConfig`/`WriteSgnsConfig`; no raw `std::ofstream` config writes remain in migrated tests.
- ○ Build + CTest (behavior preservation) pending user environment — the transaction_sync/processing_nodes/account tests are the behavior-regression sentinels.
