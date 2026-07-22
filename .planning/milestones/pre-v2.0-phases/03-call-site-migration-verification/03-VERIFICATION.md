---
status: passed
phase: 3-call-site-migration-verification
verified: 2026-07-03
verifier: inline (orchestrator) — gsd-verifier subagent unavailable in this environment
score: all gates satisfied (static + user-confirmed build/test green)
approved_by_user: 2026-07-03
---

> **Status updated to `passed` on user approval (2026-07-03).** Full build + CTest suite run
> green in the user's environment. Several execution-round fixes were applied before approval:
> (1) helpers now `create_directories` on the parent path (processing_nodes_test had run with
> default config because its per-node dir didn't exist when the ofstream wrote); (2) helpers
> **truncate** (write exactly `port_seed`+`auto_dht` / `node_type`+`is_processor`) per design
> review — merge was reverted as over-engineering; (3) NodeExample uses the **shipped**
> `example/node_test/sgns_config.json` (with `node_type` added) instead of `WriteSgnsConfig`,
> and the dead `is_full_node` CLI flag was removed; (4) all redundant manual `ofstream` config
> writes removed — every config write flows through the helpers; (5) corrected 3 wrong
> `is_processor` values; (6) added the missing `INVALID_NODE_TYPE` case to the Error switch
> (`-Wswitch`). Compile + full CTest green confirmed by the operator.

# Phase 3 Verification — Call-Site Migration + Verification

**Phase goal:** Delete old factories (INTF-04), migrate all call sites to `New(dev_config, AccountSource)`,
add a shared config-write helper, verify build+suite green with no stale references. This is the
milestone's closing phase.

## Verification method
`gsd-verifier` could not be spawned (`no such column: replacement_seq` subagent infrastructure
error, same as Phases 1-2). Verification performed inline against the post-commit source and the
phase `must_haves`. All static gates PASS conclusively; the build/test execution is environment-blocked.

## must_haves — goal-backward check

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | All ~25 `NewFromPrivateKey`/old-`New(4-arg)` call sites migrated to `New(dev_config, FromPrivateKey{...})` | ✓ Verified | `grep GeniusNode::NewFromPrivateKey\|NewFromMnemonic` in example/test = 0; 27 `FromPrivateKey{` constructions |
| 2 | Shared helper writes minimal `network_config.json` + `sgns_config.json` (MIG-02) | ✓ Verified | `GeniusNode::WriteNetworkConfig` + `WriteSgnsConfig` statics present; all 13 migrated files call them; no raw ofstream config writes remain |
| 3 | `is_full_node` → `node_type` mapping preserves behavior (`true`→Full, `false`→Light) | ✓ Verified (static) | each site passes explicit node_type per CONTEXT D-04; `WriteSgnsConfig` validates via `NodeTypeFromString` |
| 4 | Old factories + old private ctor deleted (INTF-04); no stale references (MIG-04) | ✓ Verified | `grep NewFromPrivateKey\|NewFromMnemonic` in src/example/test = 0; all `GeniusNode::New(` are canonical 2-arg; old 4-arg overload + old ctor gone (header + impl) |
| 5 | Canonical `New(dev_config, AccountSource)` is the sole public factory; new `(dev_config, AccountSource)` ctor sole private ctor | ✓ Verified | 1 canonical `New` decl+def; 1 new ctor decl+def (full `std::visit` body intact); no duplicates |
| 6 | Full build + CTest green, no behavior change (MIG-03) | ○ Pending build | not executed in-session (build/local unprimed, `_THIRDPARTY_BUILD_DIR` external) |
| 7 | `network_config_precedence_test` reframed to "config drives value" (no param side) and still asserts config-driven behavior | ✓ Verified (static) | both scenes construct via canonical `New`; assertions `EXPECT_FALSE(IsAutodhtEnabled)` + `EXPECT_GE(port, 49999)` preserved; ○ run pending build |

## Requirements coverage
- **MIG-01** (migrate all call sites): ✓ all 25 sites across 13 files migrated.
- **MIG-02** (shared config-write helper): ✓ `WriteNetworkConfig` + `WriteSgnsConfig`.
- **INTF-04** (delete old factories): ✓ all 3 + old private ctor removed.
- **MIG-04** (no stale references): ✓ grep clean.
- **MIG-03** (build + suite green): ○ pending user environment.

## human_verification
Run in the user's build environment to close verification (and the milestone):
1. **Full build:** `cmake --build <build-dir> -j` — expected zero errors. The genius_node lib + all test targets + the example must compile (no dangling old-factory references).
2. **Full CTest suite:** `ctest --test-dir <build-dir> --output-on-failure` — expected all green. Behavior-regression sentinels: `account_management_test`, `transaction_sync_test`, `transaction_crash_test`, `processing_multi_test`, `processing_nodes_test`, `full_node_test`, `child_tokens_test`, `multi_account_sync`, `blockchain_genesis_test`, `node_initialization_progress`, `migration_sync_test`, plus `network_config_precedence_test` (reframed) and `node_type_derivation_test`.
3. **MIG-04 grep (re-confirm):** `grep -rn "GeniusNode::NewFromPrivateKey\|GeniusNode::NewFromMnemonic" src/ example/ test/ | grep -v GeniusAccount` → expect 0 output.

## Blockers
- **Build environment not primed for in-session verification** — `build/local` has no compiled artifacts; `_THIRDPARTY_BUILD_DIR` externally provided. A from-scratch compile of this project is beyond session scope (same as Phases 1-2). `../thirdparty/build/OSX` is populated, so the user's environment can build it.

Environment limitation, **not** a code defect. Static evidence: all 25 call sites use the canonical factory with helper-written configs; old factories fully removed; `std::ofstream` config writes eliminated. Compile risk is low — but **the build/test must run in the user's environment to close verification.**

## Recommendation
Mark the phase **complete** once the user confirms the full build + CTest suite green. Then run `/gsd-complete-milestone` — this is the GeniusNode construction-refactor milestone's final phase. If any test fails, route to `/gsd-plan-phase 3 --gaps` with the failure output (most likely cause: a missed `node_type`/`port_seed` value at a specific call site — the per-file commits make bisection straightforward).
