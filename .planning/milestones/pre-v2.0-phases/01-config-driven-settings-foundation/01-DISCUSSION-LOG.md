# Phase 1: Config-Driven Settings Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-02
**Phase:** 1-Config-Driven Settings Foundation
**Areas discussed:** Param vs config precedence, base_port & pubsub_port coexistence, node_type effectiveness in Phase 1, key names & enum strings

---

## Param vs config precedence

| Option | Description | Selected |
|--------|-------------|----------|
| Config wins if present | Config key overrides param when present; param is fallback when absent. No existing config has keys → behavior preserved. | |
| Param always wins (dormant config) | Param always wins in Phase 1; config read dormant until Phase 2. | (initially) |
| Config always wins | Param ignored entirely; breaks tests passing custom ports. | |

**User's choice:** Config wins (corrected from initial "Param always wins"). The user clarified: "Actually the config wins."
**Notes:** Decision refined to: config wins if present; param is fallback when the key is absent. Implication flagged: Phase 1 verification must include a test proving config overrides the param (D-01, D-02).

---

## base_port & pubsub_port coexistence

| Option | Description | Selected |
|--------|-------------|----------|
| Log + document, keep override | Keep pubsub_port-wins logic; add INFO log of source; Doxygen comment documenting base_port=seed, pubsub_port=override. | ✓ (refined) |
| WARN on both-set conflict | WARN-log when both set; pubsub_port still wins. | |
| Minimal: add read only | Add base_port read; leave relationship undocumented. | |

**User's choice:** Log + document, with a rename and Doxygen emphasis.
**Notes:** The user redirected: "The base_port naming should actually be port_seed. It should be documented (ideally in doxygen) that pubsub_port has priority and if not defined the port_seed will be used to derive it." This produced D-03 (rename base_port→port_seed) and D-04 (Doxygen priority docs). INFO-logging which source resolved left as agent-discretion.

---

## node_type effectiveness in Phase 1

| Option | Description | Selected |
|--------|-------------|----------|
| Read + log + 'not yet effective' notice | Read node_type in Phase 1, log it, note it takes effect next phase. | |
| Read + log normally | Read and log as live config; no special notice. | |
| Defer node_type read to Phase 2 | Move CFG-02 next to CFG-03; Phase 1 = network_config only + NodeType enum (unused). | ✓ (then refined) |

**User's choice:** Defer node_type read to Phase 2.
**Notes:** Follow-up question resolved the sub-ambiguity: the NodeType enum itself also moves to Phase 2 (no unused-enum dead code). Result: ALL node-type work (enum, read, derivation) is Phase 2 (D-09). ROADMAP.md and REQUIREMENTS.md updated to move CFG-02 from Phase 1 → Phase 2. NodeType string form / case sensitivity deferred to Phase 2 discussion.

---

## Key names & enum strings

| Option | Description | Selected |
|--------|-------------|----------|
| port_seed + autodht | port_seed matches renamed param/member; autodht matches autodht_ member. | |
| port_seed + auto_dht | auto_dht uses conventional snake_case for the JSON key; slight mismatch with autodht_ member. | ✓ |

**User's choice:** port_seed + auto_dht.
**Notes:** Key→member name mismatch flagged for the planner: JSON key `auto_dht` maps to C++ member `autodht_` (do not rename the member — D-07). The NodeType string-form portion of this area moved to Phase 2 with the enum.

---

## the agent's Discretion

- INFO-logging which port source resolved (pubsub_port override vs port_seed-derived) — recommended, not user-mandated (D-04 mandates the Doxygen docs; logging is extra).
- Exact wording of log/WARN messages — follow existing `"network_config.json: ..."` style.

## Deferred Ideas

- node_type config read + NodeType enum + is_full_node_ derivation → Phase 2 (D-09).
- NodeType string form / case sensitivity → Phase 2 discussion.
- pubsub_port string → numeric cleanup → HARD-01, future milestone.
