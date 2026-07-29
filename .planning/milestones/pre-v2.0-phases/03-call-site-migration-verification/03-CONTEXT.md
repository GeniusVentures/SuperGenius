# Phase 3: Call-Site Migration + Verification - Context

**Gathered:** 2026-07-03
**Status:** Ready for planning
**Mode:** discuss (advisor, full_maturity calibration; subagent runtime unavailable → advisor research done inline)

<domain>
## Phase Boundary

Phase 3 deletes the 3 old `GeniusNode` factories + the old private ctor (INTF-04, moved here
from Phase 2 per `02-CONTEXT.md` D-01), migrates every `NewFromPrivateKey` / old-`New(4-arg)`
call site to `New(dev_config, FromPrivateKey{...})`, adds a shared config-write helper, and
verifies the full build + CTest suite is green with no stale old-factory references.

**In scope (this phase):**
- Add two shared `static` helper methods on `GeniusNode`: `WriteNetworkConfig` + `WriteSgnsConfig` (D-01).
- Migrate **all ~26 call sites** (1 in `example/node_test/NodeExample.cpp`, ~25 across `test/src/`) from `NewFromPrivateKey(dev_config, key, autodht, port_seed, is_full_node)` / old-`New(...)` → `New(dev_config, FromPrivateKey{...})`, writing config files via the helpers for each site's `auto_dht`/`port_seed`/`node_type`/`is_processor` values (D-03, D-04).
- Delete the 3 old factories (`New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`) **and** the old private ctor `(dev_config, account, autodht, port_seed, is_full_node)` as the **final** commit, after all migrations + a green build (D-03).
- Verify: full build passes; CTest green; no behavior change; grep confirms no stale references (MIG-04).

**Out of scope (this phase):**
- `NodeType` enum propagation downstream (PROP-01) — future milestone.
- Distinct `Archive` vs `Full` runtime behavior (PROP-02) — future.
- `pubsub_port` string→numeric cleanup (HARD-01) — future milestone.
- Any new node capabilities — pure migration + cleanup.

**Scope correction (important for the planner):** the ROADMAP estimated "18 call sites"; the
actual count is **~26** `GeniusNode::NewFromPrivateKey` / old-`New(4-arg)` call sites across
**14 files** (1 example + ~25 test). `NewFromMnemonic` has **0** call sites in `example/` or
`test/` (it is still deleted as a dead factory — INTF-04 deletes all three). The planner MUST
enumerate every site; a partial migration would leave dangling references that fail MIG-04.

</domain>

<decisions>
## Implementation Decisions

### Shared config-write helper (MIG-02) — design
- **D-01:** Two `static` methods on `GeniusNode`, both returning `outcome::result<void>`:
  - `static outcome::result<void> WriteNetworkConfig( const std::string &base_path, uint16_t port_seed, bool auto_dht );` — writes `{base_path}/network_config.json` with keys `port_seed` (numeric) and `auto_dht` (bool) — the keys the Phase-1 `InitNetwork` layer reads.
  - `static outcome::result<void> WriteSgnsConfig( const std::string &base_path, const std::string &node_type, bool is_processor );` — writes `{base_path}/sgns_config.json` with `node_type` (string) and `is_processor` (bool).
  - **`WriteSgnsConfig` validates `node_type`** by calling `NodeTypeFromString` (Phase-2 anon-namespace helper; case-insistent per D-02 of Phase 2). If `NodeTypeFromString` returns `std::nullopt`, `WriteSgnsConfig` returns failure (a new `GeniusNode::Error::INVALID_NODE_TYPE = 16`). On success it writes the (validated) `node_type` string as-is — the runtime `LoadSgnsConfig` parser is case-insensitive, so any-case input is acceptable; canonicalizing is the agent's discretion.
  - **Location:** public static methods on `GeniusNode` (declared in `GeniusNode.hpp`, defined in `GeniusNode.cpp`). The user explicitly accepted the god-class growth (`.planning/codebase/CONCERNS.md` flags GeniusNode as a god-class) in exchange for DRY + no new file. Both tests and `NodeExample.cpp` already link `genius_node`, so no new CMake wiring is needed beyond the target they already use.
  - Add `INVALID_NODE_TYPE = 16` to the `GeniusNode::Error` enum (Phase 2 added the `NodeType` enum after `Error`; the new constant extends `Error`).

### Example app strategy (NodeExample.cpp)
- **D-02:** `example/node_test/NodeExample.cpp` **reuses the shared `GeniusNode::WriteNetworkConfig` / `WriteSgnsConfig` statics** (same helpers the tests use) — DRY, no test-only/inline split. The example calls them at startup before `New(dev_config, FromPrivateKey{...})`, converting its current `NewFromPrivateKey(DEV_CONFIG, key, /*autodht=*/true, /*port_seed=*/40101, is_full_node)` into helper calls (`auto_dht=true`, `port_seed=40101`, `node_type = is_full_node ? "Full" : "Light"`) + the new factory.

### Migration + deletion ordering
- **D-03:** **Migrate-all-then-delete.** Rewrite every ~26 call site (tests + example) to `New(dev_config, FromPrivateKey{...})` + helper-written configs first; verify the full build + CTest suite is green; **then** delete the 3 old factories + the old private ctor as the **final** atomic commit. This keeps the build green throughout (the "tests stay green" invariant). The MIG-04 grep (`no NewFromPrivateKey / NewFromMnemonic / old New(autodht, port_seed, is_full_node) references in src/, example/, test/`) passes only after the final deletion commit.

### `node_type` handling in migrated tests
- **D-04:** **Explicit per-test `node_type`** (no auto-derivation from `is_full_node` inside the helper). Each migrated call site calls `GeniusNode::WriteSgnsConfig(base, "<node_type>", is_processor)` with a literal `"Full"` / `"Light"` string chosen to preserve that site's pre-migration behavior:
  - pre-migration `is_full_node=true`  → `node_type="Full"`
  - pre-migration `is_full_node=false` → `node_type="Light"`
  - (No call site uses `Archive`; if one did, it would map to `"Archive"` with `is_full_node=true` semantics.)
  The helper validates the string (D-01); a typo returns `INVALID_NODE_TYPE` at setup time, surfacing immediately rather than silently defaulting to Light.

### Carrying forward from Phase 1 / Phase 2 (locked, do not re-ask)
- **CF-1:** The 3 old factories were **retained through Phase 2** (02-CONTEXT D-01) and are **deleted here** (INTF-04) as the final step, after all call sites migrate.
- **CF-2:** Config-driven knobs: `auto_dht`/`port_seed` from `network_config.json` (Phase 1, `InitNetwork`); `node_type`/`is_processor` from `sgns_config.json` (Phase 2, `LoadSgnsConfig`). Defaults: `auto_dht=true`, `port_seed=40001`, `node_type=Light`, `is_processor=true`.
- **CF-3:** `AccountSource = std::variant<NewAccount, FromPrivateKey{std::string}, FromMnemonic{std::string}, FromPublicKey{std::string}>`; canonical factory `New(dev_config, AccountSource)`; `nullptr` on failure (Phase 2 D-04).
- **CF-4:** No compatibility shim / deprecated wrapper (Out of Scope). All migration is direct.
- **CF-5:** `NodeTypeFromString` is case-insensitive (Phase 2 D-02) — `WriteSgnsConfig` validation reuses it. `node_type_` defaults `Light`; unknown values WARN-default to Light at runtime (but the helper rejects unknowns at write time per D-01/D-04).
- **CF-6:** Conventions — C++17, `outcome::result<void>`, `std::shared_ptr` factory pattern, RapidJSON `HasMember && IsXxx`, Doxygen `@param`, spdlog `node_logger_->info/warn`, CTest/`addtest` + `genius_node` link + Apple `-force_load` (see `.planning/codebase/CONVENTIONS.md`, `TESTING.md`).

### the agent's Discretion
- Whether `WriteSgnsConfig` canonicalizes the validated `node_type` (writes `"Full"` even if input was `"full"`) or writes the input string as-is. Either is correct (the runtime parser is case-insensitive); canonicalizing yields cleaner files.
- Exact WARN/INFO log wording for the new helpers — none required (they are setup utilities, not runtime config reads), but a debug log on write is acceptable.
- Whether `WriteNetworkConfig`/`WriteSgnsConfig` truncate-and-rewrite or merge with existing file contents — truncate-and-rewrite is the test-helper contract (deterministic minimal files); document this in the Doxygen.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Prior-phase decisions (the construction API being migrated TO)
- `.planning/phases/02-variant-factory-constructor-reorder/02-CONTEXT.md` — `AccountSource` variant shapes (D-03), `New(dev_config, AccountSource)` canonical factory, `nullptr`-on-failure (D-04), old-factory deletion deferred to Phase 3 (D-01 there → this phase).
- `.planning/phases/01-config-driven-settings-foundation/01-CONTEXT.md` — `auto_dht`/`port_seed` config keys + defaults; `base_port` ≡ `port_seed`.

### Requirements & project context
- `.planning/REQUIREMENTS.md` — Phase 3 owns INTF-04 (moved here per 02-CONTEXT D-01), MIG-01, MIG-02, MIG-03, MIG-04.
- `.planning/ROADMAP.md` — Phase 3 goal + success criteria (note: "18 call sites" is an underestimate — actual ~26; see domain).
- `.planning/PROJECT.md` — Key Decisions (no shim; single `New` variant; defaults).

### Codebase maps
- `.planning/codebase/CONCERNS.md` — GeniusNode god-class context (D-01 grows it modestly; user-accepted trade-off).
- `.planning/codebase/CONVENTIONS.md` — naming, `outcome::result`, Doxygen, CTest/`addtest`.
- `.planning/codebase/TESTING.md` — test harness patterns (the `network_config_precedence_test` / `node_type_derivation_test` from Phases 1-2 are the closest models for config-writing + new-factory use).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`GeniusNode::New(dev_config, AccountSource)`** (Phase 2) — the canonical factory every site migrates to. `FromPrivateKey{ std::string }` is the variant alternative for private-key restore (the dominant case across all ~26 sites).
- **`GeniusNode::NodeTypeFromString`** (Phase 2, anon namespace in `GeniusNode.cpp`) — `WriteSgnsConfig` validation reuses it (same translation unit, so the anon-namespace function is reachable from the `WriteSgnsConfig` definition).
- **Established config-write pattern** — `test/src/account/account_management_test.cpp:36-40` writes `sgns_config.json` via `std::ofstream`; `network_config_precedence_test.cpp` / `node_type_derivation_test.cpp` (Phases 1-2) write both files. The new `GeniusNode::Write*Config` statics centralize this.
- **`GeniusNode::Error`** enum (Phase 2 added `NodeType` after it) — extend with `INVALID_NODE_TYPE = 16`.

### Established Patterns
- **`outcome::result<void>`** for fallible setup/write operations — the helpers return it; tests/example may `OUTCOME_TRY` or assert on success.
- **RapidJSON `HasMember && IsXxx`** for config reads (unchanged this phase — the helpers write JSON via `std::ofstream` string literals, not RapidJSON; reads stay as-is).

### Integration Points
- Every `NewFromPrivateKey(dev_config, key, autodht, port_seed, is_full_node)` call site → `WriteNetworkConfig(base, port_seed, auto_dht)` + `WriteSgnsConfig(base, node_type, is_processor)` + `New(dev_config, FromPrivateKey{ key })`. The `dev_config.BaseWritePath` is the `base` passed to both helpers (that's where `InitNetwork`/`LoadSgnsConfig` read from).
- Files with call sites (enumerate in the plan): `example/node_test/NodeExample.cpp`; `test/src/{multiaccount/multi_account_sync, processing_multi/processing_multi_test, transaction_sync/{transaction_crash_test,transaction_sync_test,migration_sync_test}, processing_nodes/{child_tokens_test,full_node_test,processing_nodes_test}, account_creation/account_creation_test, blockchain/blockchain_genesis_test, node/node_initialization_progress, account/{network_config_precedence_test,account_management_test}}.cpp`. (The Phase-2 `node_type_derivation_test.cpp` already uses the new API — skip it.)

</code_context>

<specifics>
## Specific Ideas

- The user explicitly chose **two separate static methods on `GeniusNode`** (not a combined function, not a new file) — accepting the god-class growth flagged in `CONCERNS.md` in exchange for DRY across tests + example and no new file/CMake target.
- The user explicitly chose **string `node_type` + outcome failure on a wrong string** (not the type-safe enum param) — the helper validates via `NodeTypeFromString` and fails loudly on a typo, rather than silently defaulting. This surfaces config mistakes at test-setup time.
- The user explicitly chose **explicit per-test `node_type`** (not helper auto-derivation from `is_full_node`) — each migrated site states `"Full"`/`"Light"` directly. The behavior-preservation mapping (`is_full_node=true`→`"Full"`, `false`→`"Light"`) is applied at migration time by the executor, not hidden in the helper.
- The user explicitly chose **migrate-then-delete** — the old factories + old private ctor die in the final commit, only after every site has migrated and the suite is green.

</specifics>

<deferred>
## Deferred Ideas

- `NodeType` enum propagation into `TransactionManager`/`UTXOManager`/`MigrationManager`/`GeniusAccount` (PROP-01) — future milestone.
- Distinct `Archive` vs `Full` runtime behavior (PROP-02) — future.
- `New()` → `outcome::result<shared_ptr>` API (considered and deferred in Phase 2) — future.
- `pubsub_port` string→numeric cleanup (HARD-01) — future milestone.
- Extracting the config-write helpers out of the GeniusNode god-class into a dedicated `NodeConfigWriter` (CONCERNS-driven refactor) — a future cleanup milestone; this phase keeps them as GeniusNode statics per D-01.
- No scope creep was introduced during discussion — all deferred items are future-phase or explicit out-of-scope.

</deferred>

---

*Phase: 3-Call-Site Migration + Verification*
*Context gathered: 2026-07-03*
