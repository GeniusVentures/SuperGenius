# Phase 12: Multi-node-finality-fault-proof - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-28
**Phase:** 12-multi-node-finality-fault-proof
**Areas discussed:** Record attribution, teardown completion, evidence gate, observer ownership

---

## Record attribution

| Option | Description | Selected |
|---|---|---|
| Process-bound fingerprint | Run token, observer schema version, and executable identity bind diagnostics to one process. | ✓ |
| Schema version only | Identifies output format but not the binary/process. | |
| Runtime identity only | Uses peer IDs, ports, and roots already in the record. | |

**User's choice:** Require a process-bound fingerprint at test start and normal terminal completion. A missing/mismatched fingerprint is invalid evidence and the run counts only with matching identity, normal GTest completion, and terminal record.

---

## Teardown completion

| Option | Description | Selected |
|---|---|---|
| Explicit post-shutdown terminal record | Scenario writes terminal output after all owned peers release. | ✓ |
| RAII destructor record | Emits automatically while scopes unwind. | |
| Both records | Emits provisional destructor output plus explicit final output. | |

**User's choice:** Explicit terminal record after clean release of all four peer handles. A prevented normal completion emits an `incomplete` record, retained but excluded from the evidence gate.

---

## Evidence gate

| Option | Description | Selected |
|---|---|---|
| Two matching complete failures | Requires identical fully attributed observer-lifecycle failures. | ✓ |
| One diagnosed run | Allows a single run to authorize a repair. | |
| Any mismatched output | Treats the symptom as repair authorization. | |

**User's choice:** Only two matching fully attributed complete failures authorize an observer-only repair. Three complete passes close this scope without repair; a different binary/process requires a clean rebuild and a new three-run gate.

---

## Observer ownership

| Option | Description | Selected |
|---|---|---|
| Scenario-owned, read-only observer | The scenario owns lifecycle; observer only reads and writes diagnostics. | ✓ |
| Shared fixture observer | Observer spans multiple scenarios. | |
| External runner | Harness emits diagnostics outside the process. | |

**User's choice:** The publisher-loss scenario owns diagnostic lifecycle. The observer controls no peer or shutdown work and uses one synchronized, immediately flushed test-owned writer. Its fingerprint contains a run token, schema version, and executable path/size/mtime.

---

## the agent's Discretion

- Choose the minimal test-local representation for the fingerprint, release proof, and synchronized writer.

## Deferred Ideas

- Bridge Startup Wiring + Mock RPC Endpoints — unrelated to finality evidence; deferred.
