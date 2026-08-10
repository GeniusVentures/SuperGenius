# Phase 3: Call-Site Migration + Verification - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `03-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-07-03
**Phase:** 3-Call-Site Migration + Verification
**Mode:** discuss (advisor, full_maturity calibration). Subagent runtime unavailable (`no such column: replacement_seq`) → advisor comparison tables produced inline.
**Areas discussed:** Shared config-write helper (MIG-02), Example app config strategy, Migration + deletion ordering, `node_type` preservation in migrated tests (+ 2 targeted follow-ups: helper location, node_type param type)

---

## Shared config-write helper (MIG-02)

| Option | Description | Selected |
|--------|-------------|----------|
| A. One header-only fn | `WriteNodeConfigs(base, auto_dht, port_seed, is_full_node, is_processor)` writes both files; derives node_type from is_full_node | |
| B. Two separate functions | `WriteNetworkConfig` + `WriteSgnsConfig` (more flexible, more verbose) | ✓ |
| C. Fluent builder | `NodeConfigBuilder{}...Write(base)` | |
| D. Inline per-test (no helper) | — (violates MIG-02) | |

**User's choice:** B. Two separate functions.
**Notes:** Combined with the location follow-up (below), these become two **static methods on `GeniusNode`** returning `outcome::result<void>`, reused by tests + example.

---

## Example app (NodeExample.cpp) config strategy

| Option | Description | Selected |
|--------|-------------|----------|
| A. Inline at startup | Example writes config via ofstream (~6 lines); test helper stays test-only | |
| B. Shared src/ helper | Move helper to shared location; tests + example both reuse (DRY, but adds a config-writer to production code) | ✓ |
| C. Example-local lambda | Functionally A, wrapped | |

**User's choice:** B. Shared src/ helper.
**Notes:** Accepted the production-utility trade-off for DRY. Resolved in the follow-up to "static methods on GeniusNode" (no new file).

---

## Migration + deletion ordering

| Option | Description | Selected |
|--------|-------------|----------|
| A. Migrate-then-delete | Migrate all ~26 sites + write configs, verify green, THEN delete old factories + old ctor as the final commit | ✓ |
| B. Delete-then-migrate | Delete first → ~26 dangling refs → broken-build window | |

**User's choice:** A. Migrate-then-delete.
**Notes:** The only option preserving the green-build invariant. Old factories + old private ctor die in the final commit.

---

## `node_type` preservation in migrated tests

| Option | Description | Selected |
|--------|-------------|----------|
| A. Helper derives node_type | Pass old is_full_node; helper writes Full/Light automatically | |
| B. Explicit per-test | Each test writes node_type (Full/Light) explicitly | ✓ |
| C. Default Light everywhere | Tests needing Full set it explicitly (silent-behavior-change risk) | |

**User's choice:** B. Explicit per-test.
**Notes:** Each migrated site states `"Full"`/`"Light"` directly. The executor applies the behavior-preservation mapping (`is_full_node=true`→`"Full"`, `false`→`"Light"`) at migration time.

---

## Follow-up: helper location in src/

| Option | Description | Selected |
|--------|-------------|----------|
| A. New src/account/NodeConfigWriter | New file in src/account/; keeps GeniusNode god-class from growing | |
| B. Static methods on GeniusNode | No new file; grows the god-class (CONCERNS.md flags it) | ✓ |
| C. New src/config/ module | Generic separation; over-scoped | |

**User's choice:** B. Static methods on GeniusNode.
**Notes:** Accepted the god-class growth for DRY + no new file/CMake target.

---

## Follow-up: WriteSgnsConfig node_type param type

| Option | Description | Selected |
|--------|-------------|----------|
| A. NodeType enum | Type-safe; helper serializes (adds NodeTypeToString) | |
| B. Raw string + outcome failure on wrong string | Stringly-typed but validated; typo → loud failure | ✓ |

**User's choice:** String + outcome failure on a wrong string.
**Notes:** `WriteSgnsConfig` validates via `NodeTypeFromString` (case-insensitive, Phase 2 D-02); unrecognized → new `GeniusNode::Error::INVALID_NODE_TYPE = 16`. Surfaces config typos at setup time rather than silently defaulting to Light.

---

## the agent's Discretion

- Whether `WriteSgnsConfig` canonicalizes the validated `node_type` (writes `"Full"` for input `"full"`) or writes input as-is.
- Whether the helpers truncate-and-rewrite or merge (truncate-and-rewrite is the test-helper contract).

## Deferred Ideas

- `NodeType` downstream propagation (PROP-01), Archive-vs-Full behavior (PROP-02) → future milestone.
- `New()` → `outcome::result` API → future.
- `pubsub_port` numeric cleanup (HARD-01) → future.
- Extracting config-write helpers out of the GeniusNode god-class into a dedicated `NodeConfigWriter` → future cleanup milestone (this phase keeps them as statics per D-01).
