# Phase 1: Config-Driven Settings Foundation - Context

**Gathered:** 2026-07-02
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 1 introduces **config-file reads for network settings** (`auto_dht`, `port_seed`) in `network_config.json`, parsed inside `InitNetwork()`, as an **additive, behavior-preserving layer**. The old factory signatures and all 18 call sites are **untouched** this phase (call-site migration is Phase 3).

**In scope:**
- Read `auto_dht` (bool) and `port_seed` (uint16) from `network_config.json` in `InitNetwork()`
- Config-wins-if-present precedence over the constructor param (param is fallback when key absent)
- Rename `base_port` → `port_seed` throughout the touched code (constructor param, `InitNetwork` signature, `GenerateRandomPort` call, config key)
- Doxygen documentation of port resolution: `pubsub_port` has priority; if undefined, `port_seed` derives the port via `GenerateRandomPort`
- Safe defaults byte-identical to today (`auto_dht=true`, `port_seed=40001`) with WARN-log on unrecognized/ill-typed values
- INFO-log resolved values (match existing `"network_config.json: ..."` logging style)

**Out of scope (moved to Phase 2):**
- `NodeType` enum, `NodeTypeFromString()`, `node_type_` member, and the `node_type` config read (CFG-02) — ALL deferred to Phase 2 alongside the `is_full_node_` derivation (CFG-03)
- Variant factory collapse, constructor reorder, old-factory deletion (Phase 2)
- Call-site migration (Phase 3)

</domain>

<decisions>
## Implementation Decisions

### Config vs Param Precedence
- **D-01:** **Config wins if present; the constructor param is the fallback when the config key is absent.** The config read is **live, not dormant** — a deployment that sets `auto_dht`/`port_seed` in `network_config.json` overrides the param even in Phase 1. Since no existing config file contains these keys today, all current call sites behave identically (param flows through unchanged).
- **D-02 (verification implication):** Phase 1 verification MUST include a test proving the config value overrides the constructor param when the key is present. This guards the precedence rule before Phase 3 migrates call sites away from the param.

### Port Rename & Documentation
- **D-03:** **Rename `base_port` → `port_seed`** throughout the Phase 1 scope: constructor param, `InitNetwork(uint16_t port_seed, ...)` signature, the `GenerateRandomPort(port_seed, account_->GetAddress())` call at `src/account/GeniusNode.cpp:918`, the new config key, and all Doxygen comments. ("Seed" is the accurate term — it seeds deterministic per-address port generation.)
- **D-04:** **Doxygen-document the port resolution priority** at the `InitNetwork` declaration and the port-selection block (`GeniusNode.cpp:911-919`): `pubsub_port` (existing string override) takes priority; if undefined, `port_seed` is used to derive the port via `GenerateRandomPort`. No behavior change — existing override logic at lines 912-914 is preserved.
- **D-05 (alias note for downstream):** REQUIREMENTS.md and ROADMAP.md still say `base_port`; in implementation that is **`port_seed`**. Treat `base_port` ≡ `port_seed` when reading those artifacts.

### Config Key Names
- **D-06:** The two new `network_config.json` keys are **`port_seed`** (numeric `IsUint()`, uint16) and **`auto_dht`** (bool).
- **D-07 (name-mapping note):** The JSON key `auto_dht` maps to the existing C++ member **`autodht_`** (no underscore between "auto" and "dht"). The key uses conventional snake_case; the member keeps its existing spelling. The planner/executor must handle this key→member name difference explicitly (do not rename the member — that widens blast radius).

### Field Type Convention
- **D-08:** `port_seed` is parsed as **numeric `IsUint()`**, NOT the string-then-`std::stoi` style used by the existing `pubsub_port` field (`GeniusNode.cpp:791-805`). Leave `pubsub_port` untouched (string cleanup is out of scope — HARD-01). Add a code comment noting the intentional type divergence.

### node_type / NodeType Timing (roadmap adjustment)
- **D-09:** **Defer ALL node-type work to Phase 2.** The `NodeType` enum, `NodeTypeFromString()`, the `node_type_` member, and the `sgns_config.json` `node_type` read (CFG-02) move to Phase 2, joining the `is_full_node_` derivation (CFG-03). This avoids any "read but not yet effective" / unused-enum state in Phase 1. ROADMAP.md's requirement table is updated accordingly (CFG-02: Phase 1 → Phase 2). The NodeType string form (`"Full"`/`"Light"`/`"Archive"`, case sensitivity) will be decided during Phase 2 discussion.

### the agent's Discretion
- INFO-logging of which port source resolved (`pubsub_port` override vs `port_seed`-derived) is recommended but not mandated by the user — the agent may add it if low-cost. The mandatory part is the Doxygen documentation (D-04).
- Exact wording of log/WARN messages — follow the existing `"network_config.json: ..."` style.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project context
- `.planning/PROJECT.md` — Core value, constraints (C++17, no new deps, deployed-config backward compat), key decisions
- `.planning/REQUIREMENTS.md` — Phase 1 owns CFG-01 + CFG-04 (CFG-02 moved to Phase 2 per D-09). NOTE `base_port` ≡ `port_seed`.
- `.planning/ROADMAP.md` — Phase 1 goal + success criteria; updated to move CFG-02 to Phase 2

### Research (grounded in actual code, produced inline)
- `.planning/research/STACK.md` — std::variant/RapidJSON/enum patterns; §"RapidJSON Config Reading" and §"What NOT to Use" most relevant to Phase 1
- `.planning/research/PITFALLS.md` — P2 (silent default mismatch), P7 (bad config values), P8 (base_port type — now `port_seed`) directly govern Phase 1; see Pitfall→Phase matrix
- `.planning/research/ARCHITECTURE.md` — §"The Critical Finding: Init-Order Chicken-and-Egg" (Phase 2 concern, but Phase 1 must not break the path to it), §"Where things live"

### Codebase maps
- `.planning/codebase/STACK.md` — RapidJSON dependency confirmation
- `.planning/codebase/ARCHITECTURE.md` — config-file layer overview
- `.planning/codebase/CONCERNS.md` — GeniusNode god-class context (stay surgical)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Existing RapidJSON parse pattern** in `InitNetwork()` (`src/account/GeniusNode.cpp:768-919`): `std::ifstream` → `rapidjson::Document::Parse` → `HasMember && IsXxx && GetXxx` guards. Add `auto_dht` and `port_seed` reads directly alongside the existing `pubsub_port`/`upnp_enabled`/`high_water`/`low_water` reads. Do NOT introduce a new parsing utility.
- **Existing logging style**: `node_logger_->info("network_config.json: ...")` / `node_logger_->warn(...)`. Match it exactly.

### Established Patterns
- **Default-on-missing-key is mandatory** — deployed `network_config.json` files lack the new keys. Every read keeps its pre-declared default when `HasMember` is false or the type check fails.
- **`HasMember && IsXxx` before `GetXxx`** — every existing read follows this; never call `GetXxx` unguarded.

### Integration Points
- `GeniusNode::InitNetwork(uint16_t base_port, bool is_full_node)` at `src/account/GeniusNode.cpp:768` (signature becomes `... uint16_t port_seed, ...`) — this is where both new reads land.
- Port selection block at `src/account/GeniusNode.cpp:911-919`: `config_port` (from `pubsub_port`) overrides; else `GenerateRandomPort(port_seed, address)`. Document via Doxygen (D-04); logic unchanged.
- Constructor member-init list at `src/account/GeniusNode.cpp:209` (`autodht_( autodht )`) — unchanged in Phase 1 (the param still feeds it; config-override happens in `InitNetwork`).
- Public API declarations + Doxygen at `src/account/GeniusNode.hpp:82-119, 728` — update param name `base_port` → `port_seed` and add the port-resolution Doxygen note.

</code_context>

<specifics>
## Specific Ideas

- The rename `base_port` → `port_seed` was the user's explicit request — it should be the canonical name going forward (config key, param, docs). Do not keep `base_port` as an alias in code.
- The user emphasized **Doxygen documentation** of the `pubsub_port` > `port_seed` priority relationship — treat clear doc comments as a first-class deliverable, not an afterthought.

</specifics>

<deferred>
## Deferred Ideas

- **`node_type` config read + `NodeType` enum + `is_full_node_` derivation** — deferred to Phase 2 (D-09). The NodeType string form / case sensitivity to be decided in Phase 2 discussion.
- **`pubsub_port` string → numeric cleanup** — HARD-01, future milestone. Phase 1 only adds `port_seed` as numeric and leaves `pubsub_port` as-is.
- **INFO-logging which port source resolved** — recommended but agent-discretion (not user-mandated).
- No scope creep was introduced — all deferred items are either later-phase work or explicit out-of-scope items from REQUIREMENTS.md.

</deferred>

---

*Phase: 1-Config-Driven Settings Foundation*
*Context gathered: 2026-07-02*
