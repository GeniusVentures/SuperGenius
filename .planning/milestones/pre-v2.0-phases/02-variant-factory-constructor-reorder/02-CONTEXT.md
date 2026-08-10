# Phase 2: Variant Factory + Constructor Reorder - Context

**Gathered:** 2026-07-02
**Status:** Ready for planning
**Mode:** discuss (advisor, full_maturity calibration; subagent runtime unavailable → advisor research done inline)

<domain>
## Phase Boundary

Phase 2 collapses the three overloaded `GeniusNode` factories into a single
`New(dev_config, AccountSource)` and reorders the private constructor so the account is
created **after** `LoadSgnsConfig()` resolves `node_type_` → `is_full_node_` (the init-order
hinge from `research/ARCHITECTURE.md`). It introduces the `NodeType` enum + `NodeTypeFromString()`
+ the `node_type` config read, and derives `is_full_node_` from `node_type_`.

**In scope (this phase):**
- New public factory `New(const DevConfig &dev_config, AccountSource source)` as the canonical entry point
- `AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>` with the four owned-`std::string` struct shapes (D-03)
- Private constructor signature changes to `(dev_config, AccountSource)`; account created via `std::visit` **after** `LoadSgnsConfig()` in the ctor body (D-04 / INTF-03)
- `NodeType` enum co-located with `NodeState`/`Error` at `GeniusNode.hpp` (~line 129); `NodeTypeFromString()` case-insensitive parser (D-02)
- `node_type` read added to `LoadSgnsConfig()` (`sgns_config.json`); `is_full_node_` derived from `node_type_` (Full/Archive → true, Light → false), set once, immutable
- `New()` preserves nullptr-on-failure semantics (D-04)

**Out of scope (this phase — explicit):**
- **Old factory deletion (`INTF-04`) is DEFERRED to Phase 3** (D-01). The old `New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic` STAY in Phase 2 so the build stays green; Phase 3 deletes them as the first step of call-site migration.
- Migrating the 18 existing call sites (`example/node_test/` + `test/src/`) → Phase 3 (MIG-01..04)
- Propagating `NodeType` enum downstream into `UTXOManager`/`TransactionManager`/`MigrationManager`/`GeniusAccount` (they keep the **bool** unchanged — criterion #5; PROP-01 is a future milestone)
- Distinct `Archive` vs `Full` runtime behavior (both → `is_full_node_=true`; PROP-02 future)

</domain>

<decisions>
## Implementation Decisions

### Build-green sequencing (P2 ↔ P3 boundary)
- **D-01:** **Defer old-factory deletion to Phase 3.** Phase 2 introduces `New(dev_config, AccountSource)` + the enum + the constructor reorder, but **KEEPS** the three old factories (`New(autodht, port_seed, is_full_node)`, `NewFromPrivateKey`, `NewFromMnemonic`) so the build compiles and the existing 18 call sites stay green. Phase 3 deletes the old factories **as its first migration step** (atomically with the call-site rewrites that make deletion safe). The old factories are NOT "deprecated shims" — they are the still-real API until Phase 3 removes them, so this does not violate the "no compatibility shim" out-of-scope rule.
  - **Required traceability edit (already applied):** `INTF-04` moved Phase 2 → Phase 3 in `REQUIREMENTS.md` and `ROADMAP.md` coverage table; Phase 2 success-criterion #4 ("Old factories deleted") is re-scoped to "new factory is canonical; old factories retained for Phase 3 deletion."

### `node_type` parsing (CFG-02 / NodeTypeFromString)
- **D-02:** **Case-insensitive** matching for `NodeTypeFromString()`. Accept `"Full"`/`"full"`/`"FULL"` (and likewise Light/Archive) → normalize to the `NodeType` enum value. Unknown or ill-typed value → WARN-log (matching the existing `LoadSgnsConfig` style, e.g. `"sgns_config.json: node_type 'X' unrecognized, defaulting to Light"`) and default to `NodeType::Light`. Missing key → default `Light` with INFO-log (mirrors the existing `is_processor` default-on-missing pattern in `LoadSgnsConfig`). The stored `node_type_` is always a clean enum value; INFO-log echoes the **normalized** form, not the raw input.

### `AccountSource` variant design (INTF-02)
- **D-03:** Owned `std::string` payloads (a `std::variant` owns its active alternative — `const char*`/`std::string_view` are dangling-pointer footguns once the variant is stored/passed). Structs defined in `GeniusNode.hpp` alongside the `NodeType` enum:
  ```cpp
  struct NewAccount {};                                  // generate a new identity
  struct FromPrivateKey { std::string eth_private_key; };
  struct FromMnemonic   { std::string mnemonic; };
  struct FromPublicKey  { std::string public_address; }; // 0x... address (consumed like AddAccountWithKey)
  ```
  `FromPublicKey` carries a **public_address** (the existing internal `NewFromPublicKey` + `AddAccountWithKey` consume address-like strings). `TokenID` and other `dev_config` fields are NOT part of the variant — they come from `dev_config`.

### `New()` failure semantics (ctor owns account creation)
- **D-04:** **Preserve nullptr-on-failure.** Once account creation moves into the private ctor (via `std::visit`), a restore failure (e.g. bad mnemonic/key) throws inside the ctor. `New(dev_config, AccountSource)` wraps `new GeniusNode(...)` in `try`/`catch (...)` and returns `nullptr` on any ctor failure — preserving the existing public contract (`@return shared_ptr, nullptr on failure`) so callers' nullptr-checks keep working and Phase 3 call-site migration has minimal semantics churn. This is consistent with the ctor's existing throw behavior ("Could not configure loggers" / "Network initialization error").

### Constructor reorder mechanism (INTF-03) — research-prescribed, locked here
- **D-05:** Follow `research/ARCHITECTURE.md` §"The Critical Finding: Init-Order Chicken-and-Egg". Private ctor `(dev_config, AccountSource)` body order:
  1. `RotateLogFiles` / `InitOpenSSL` / `InitLoggers` (unchanged from today)
  2. `LoadSgnsConfig()` — now also resolves `node_type_` → `is_full_node_` (Full/Archive → true, Light → false)
  3. `account_ = std::visit(visitor, source)` — creates the account **with `is_full_node_` already known** (the hinge fix)
  4. `InitNetwork(port_seed, is_full_node_)` (unchanged) and the rest of today's ctor body
  `node_type_` is a new private member (near `is_full_node_` at `GeniusNode.hpp:662`); `is_full_node_` becomes **derived** (no longer ctor-init-list-initialized). The ctor now owns account creation (previously the public factory did) — this centralizes config resolution and is the accepted trade-off per research.

### Carrying forward from Phase 1 / prior decisions (locked, do not re-ask)
- **CF-1:** `node_type` lives in `sgns_config.json`, read in `LoadSgnsConfig()` (REQ CFG-02). Default `"Light"` on missing key (REQ CFG-04).
- **CF-2:** `NodeType` enum co-located with `NodeState`/`Error` at `GeniusNode.hpp:~129` (REQ CFG-02).
- **CF-3:** `is_full_node_` derived from `node_type_` (Full/Archive → true, Light → false), set exactly once during construction, immutable afterward, **no setter** (REQ CFG-03). Downstream consumers receive the bool unchanged.
- **CF-4:** No compatibility shim / deprecated wrapper (Out of Scope). The retained-old-factory decision (D-01) is NOT a shim — see D-01.
- **CF-5:** `port_seed` / `auto_dht` config-driven layer from Phase 1 stays in place; `InitNetwork(port_seed, is_full_node_)` signature is unchanged this phase.
- **CF-6:** Coding conventions — C++17, `snake_case_` private members, `std::shared_ptr` factory pattern, RapidJSON `HasMember && IsXxx` config parsing, Doxygen `@param` on public API, spdlog `node_logger_->info/warn` (see `.planning/codebase/CONVENTIONS.md`).

### the agent's Discretion
- Exact WARN/INFO message wording for `node_type` parsing — follow the existing `LoadSgnsConfig` `"sgns_config.json: ..."` style (D-02 mandates the normalize + WARN-default behavior; wording is flexible).
- Whether to add a brief Doxygen note on `New()` documenting the nullptr-on-failure contract (recommended but not mandated).
- Internal helper placement for `NodeTypeFromString` (free function in anonymous namespace at top of `GeniusNode.cpp`, matching `NodeStateToString`/`GenerateRandomPort`) — follow the existing local-helper style.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Architecture / init-order (THE key reference for the reorder)
- `.planning/research/ARCHITECTURE.md` §"The Critical Finding: Init-Order Chicken-and-Egg" — prescribes the ctor reorder, the `std::visit`-after-`LoadSgnsConfig` mechanism, the new ctor signature, and the phased build order. **Read first.**
- `.planning/codebase/ARCHITECTURE.md` — config-file layer overview + component boundaries (GeniusNode god-class facade).

### Prior-phase decisions
- `.planning/phases/01-config-driven-settings-foundation/01-CONTEXT.md` — D-09 deferred all node-type work to Phase 2 and flagged the string-form/case-sensitivity decision (resolved here as D-02). Also the source of the `port_seed` rename context that Phase 2 builds on.

### Requirements & project context
- `.planning/REQUIREMENTS.md` — Phase 2 owns INTF-01, INTF-02, INTF-03, CFG-02, CFG-03 (INTF-04 moved to Phase 3 per D-01). NOTE `base_port` ≡ `port_seed`.
- `.planning/ROADMAP.md` — Phase 2 goal + success criteria; Phase 3 migration scope.
- `.planning/PROJECT.md` — Key Decisions table (variant factory, derived bool, no shim, defaults).

### Conventions
- `.planning/codebase/CONVENTIONS.md` — naming, error handling, Doxygen, logging.
- `.planning/codebase/CONCERNS.md` — GeniusNode god-class context (stay surgical).
- `.planning/research/PITFALLS.md` — P2 (silent default mismatch), P7 (bad config values) govern the `node_type` read.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`LoadSgnsConfig()`** (`src/account/GeniusNode.cpp`, ~line 251) — already parses `sgns_config.json` via the same `HasMember && IsXxx` RapidJSON pattern as `InitNetwork`. The `node_type` read + `NodeTypeFromString` call land directly alongside `is_processor`/`net_id`/`subnet_id`. The `is_processor` block is the closest analog (explicit else-default + INFO-log) — model `node_type` on it.
- **`GeniusAccount` factories** (`src/account/GeniusAccount.hpp:59-102`) — `New`/`NewFromPrivateKey`/`NewFromPublicKey`/`NewFromMnemonic` return `shared_ptr` (nullptr on failure). The ctor's `std::visit` visitor dispatches to these. `TokenID` (first param) comes from `dev_config`, not the variant.
- **Current private ctor** (`src/account/GeniusNode.cpp:200-238`) — the body to reorder. Today it init-lists `is_full_node_(is_full_node)` and `autodht_(autodht)`; after reorder `is_full_node_` is derived in the body, `autodht_` still init-listed (Phase 1 config-override applies in `InitNetwork`).
- **Anonymous-namespace helpers** at top of `GeniusNode.cpp` (`GenerateRandomPort`, `NodeStateToString`) — `NodeTypeFromString` belongs here (internal linkage, same style).

### Established Patterns
- **RapidJSON guard chain** — `HasMember && IsXxx` before `GetXxx`, default-on-missing, INFO-log resolved / WARN-log ill-typed. Both `node_type` (Phase 2) reads follow this exactly (Phase 1 established it for `port_seed`/`auto_dht`).
- **Factory nullable contract** — `static std::shared_ptr<...> New(...)` with `@return nullptr on failure`; callers check. D-04 preserves this.
- **spdlog style** — `node_logger_->info("sgns_config.json: ...")` / `node_logger_->warn(...)`.

### Integration Points
- `New(dev_config, AccountSource)` — new public factory (`GeniusNode.hpp`, alongside the retained old factories).
- Private ctor `GeniusNode(dev_config, AccountSource)` — replaces `(dev_config, account, autodht, port_seed, is_full_node)`.
- New private member `node_type_` near `is_full_node_` (`GeniusNode.hpp:662`).
- `std::visit` visitor in the ctor body creates `account_` by dispatching to the `GeniusAccount` factories (needs `is_full_node_` and `dev_config` token id).

</code_context>

<specifics>
## Specific Ideas

- The user explicitly chose **operator-forgiveness** for `node_type` (case-insensitive, D-02) over strict exact-case — prioritize not bricking a deployed node on a casing typo. Unknown values still default safely to Light.
- The user explicitly chose **green-build invariant** (D-01) over roadmap-faithful phase boundaries — the milestone's "tests stay green / no behavior change" constraint outranks keeping INTF-04 in Phase 2. This is why old factories are retained this phase.
- `AccountSource` structs use owned `std::string` (D-03) — the user picked safety/idiom over matching the legacy `const char*` param types. Call sites in Phase 3 will read `FromPrivateKey{ std::move(key) }`-style.
- `New()` nullptr-on-failure (D-04) is non-negotiable for minimizing Phase 3 churn — do not "improve" it to `outcome::result` this milestone.

</specifics>

<deferred>
## Deferred Ideas

- **Old-factory deletion + 18-call-site migration** — Phase 3 (INTF-04 + MIG-01..04). D-01 sequences deletion into P3 as the first migration step.
- **`NodeType` enum propagation downstream** (`TransactionManager` 60+ `full_node_m` refs, `UTXOManager`, `MigrationManager`, `GeniusAccount`) — future milestone (PROP-01). This milestone keeps the derived bool at the GeniusNode boundary.
- **Distinct `Archive` vs `Full` runtime behavior** — future (PROP-02). Both → `is_full_node_=true` for now.
- **`New()` → `outcome::result<shared_ptr>` API** — a more correct long-term error surface, but bigger churn than this refactor targets (rejected for Phase 2 as D-04 option C).
- **`pubsub_port` string→numeric cleanup** — HARD-01, future milestone.
- No scope creep was introduced during discussion — all deferred items are either later-phase work or explicit out-of-scope items.

</deferred>

---

*Phase: 2-Variant Factory + Constructor Reorder*
*Context gathered: 2026-07-02*
