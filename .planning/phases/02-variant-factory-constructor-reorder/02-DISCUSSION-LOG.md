# Phase 2: Variant Factory + Constructor Reorder - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `02-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-07-02
**Phase:** 2-Variant Factory + Constructor Reorder
**Mode:** discuss (advisor, full_maturity calibration). Subagent runtime unavailable (`no such column: replacement_seq`) → advisor research (comparison tables) produced inline by the orchestrator instead of via `gsd-advisor-researcher` agents.
**Areas discussed:** Build-green sequencing (P2/P3 boundary), `node_type` string form & parsing, `AccountSource` variant struct shapes, `New()` failure semantics

---

## Build-green sequencing (P2 delete vs P3 migrate)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Roadmap-faithful (broken intermediate) | P2 deletes old factories, accepts broken-build intermediate; P3 migrates & restores green | |
| B. Defer deletion to P3 | P2 adds new factory+enum+reorder, KEEPS old factories; P3 deletes them as first migration step. Every phase green; needs ROADMAP edit (INTF-04 → P3) | ✓ |
| C. Fold migration into P2 | P2 = new factory + enum + reorder + migrate all 18 callers + delete old; P3 shrinks to test-helper + grep | |

**User's choice:** B. Defer deletion to P3.
**Notes:** Honors the milestone's "tests stay green / no behavior change" non-functional constraint over strict phase-boundary fidelity. Old factories are not "shims" — they remain the real API until Phase 3 removes them atomically with the call-site rewrites. Required traceability edit (INTF-04: Phase 2 → Phase 3 in ROADMAP + REQUIREMENTS) applied.

---

## `node_type` string form & parsing (NodeTypeFromString)

| Option | Description | Selected |
|--------|-------------|----------|
| A. Exact-case + WARN-default | "Full"/"Light"/"Archive" exact-case; unknown/ill-typed → WARN + default Light | |
| B. Case-insensitive + WARN-default | Accept any case → normalize; unknown → WARN + default Light | ✓ |
| C. Exact-case + reject unknown | Exact-case; unknown → WARN + refuse to start | |

**User's choice:** B. Case-insensitive + WARN-default.
**Notes:** Operator-forgiveness over audit-strictness — a casing typo in a deployed `sgns_config.json` must not silently mis-default. `NodeTypeFromString` lowercases input before mapping; stored `node_type_` is always a clean enum value; INFO-log echoes the normalized form.

---

## `AccountSource` variant struct shapes

| Option | Description | Selected |
|--------|-------------|----------|
| A. Owned std::string payloads | NewAccount{}; FromPrivateKey{std::string}; FromMnemonic{std::string}; FromPublicKey{std::string public_address} | ✓ |
| B. const char* payloads | Match today's const char* factory params (dangling-pointer risk in a stored variant) | |
| C. std::string_view payloads | Non-owning; same lifetime risk as const char* if captured | |

**User's choice:** A. Owned std::string payloads.
**Notes:** Safety/idiom over matching legacy param types. `FromPublicKey` carries a **public_address** (consumed like the existing internal `NewFromPublicKey` / `AddAccountWithKey`); `TokenID` and other fields come from `dev_config`, not the variant.

---

## `New()` failure semantics

| Option | Description | Selected |
|--------|-------------|----------|
| A. Preserve nullptr-on-failure | Ctor throws on account-restore failure; New() wraps `new GeniusNode(...)` in try/catch → returns nullptr | ✓ |
| B. Let ctor exceptions propagate | Ctor throws, New() lets it propagate (breaks the nullable API contract) | |
| C. outcome::result return | New() returns outcome::result<shared_ptr<GeniusNode>> (biggest API churn) | |

**User's choice:** A. Preserve nullptr-on-failure.
**Notes:** Preserves the existing public contract so callers' nullptr-checks keep working → minimal Phase 3 churn → honors "no behavior change." Consistent with the ctor's existing throw behavior for loggers/network failures.

---

## the agent's Discretion

- Exact WARN/INFO message wording for `node_type` parsing (behavior fixed by D-02; wording flexible — follow existing `LoadSgnsConfig` style).
- `NodeTypeFromString` placement (anonymous namespace at top of `GeniusNode.cpp`, alongside `NodeStateToString`/`GenerateRandomPort`).
- Optional Doxygen note on `New()` documenting the nullptr-on-failure contract.

## Deferred Ideas

- Old-factory deletion + 18-call-site migration → Phase 3 (INTF-04 + MIG-01..04).
- `NodeType` enum downstream propagation (PROP-01) / distinct Archive-vs-Full behavior (PROP-02) → future milestone.
- `New()` → `outcome::result` API (rejected option C this phase) → future consideration.
- `pubsub_port` string→numeric cleanup (HARD-01) → future milestone.
