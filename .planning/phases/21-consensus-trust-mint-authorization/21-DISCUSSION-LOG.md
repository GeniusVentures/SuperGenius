# Phase 21: Consensus Trust & Mint Authorization - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-10-19
**Phase:** 21-Consensus Trust & Mint Authorization
**Areas discussed:** Grandfather rule, Legacy-mint disposition, Reject-vs-defer behavior, Admission audit depth, Todo folding

---

## Todo Folding (cross-reference step)

| Todo | Match basis | Selected |
|------|-------------|----------|
| bridge-race fixture (not all 11 nodes mint within 90s) | keyword: mint | |
| Bridge Startup Wiring + Mock RPC Endpoints | keyword: bridge/rpc — already tagged resolves_phase: 24 | |
| Secure trusted-peer genesis configuration | area: security (score 0.2) | ✓ |

**User's choice:** Fold "Secure trusted-peer genesis configuration" only.
**Notes:** bridge-startup-wiring was recommended to stay with Phase 24 (its roadmap tag); bridge-race left as test-infra follow-up.

---

## Grandfather Rule

| Option | Description | Selected |
|--------|-------------|----------|
| Finality-bound | Already-final certificates never re-checked; finality is the proof. Only in-flight/new certificates take the strict verified-registry path. No markers, no migration. | ✓ |
| Upgrade-height cutover | Trust below upgrade height/epoch, strict above. Needs persisted marker + slot→height mapping that doesn't cleanly exist. | |
| Persisted anchors | One-time verification of historical registries at upgrade, storing approved hashes. Strongest, most machinery and risk. | |

**User's choice:** Finality-bound (Recommended)
**Notes:** User first asked for clarification of "sync-breaker" and "grandfathered" — explained concretely (re-validation runs on restart/sync/merge rejecting a node's own accepted history; grandfather clause = old data keeps the old rule). User then selected finality-bound.

---

## Legacy-Mint Disposition

| Option | Description | Selected |
|--------|-------------|----------|
| Hard-disable at admission | New v1 mints rejected at every ingress; existing outputs spendable; no authority machinery. | ✓ |
| Authority-gated acceptance | Only authorized minter may submit v1; builds authority check + misconfiguration risk. | |
| Config-gated disable | Default-off flag gates v1 reception; a flag that must never be on publicly is a footgun. | |

**User's choice:** Hard-disable at admission (Recommended)
**Notes:** None.

---

## Reject-vs-Defer Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Defer with size+time bound | Hold-and-retry as dependencies land; reject on deadline expiry; buffer-full oldest-first eviction backstop. | ✓ |
| Reject immediately | Simplest and memory-safest, but drops legitimate out-of-order traffic — strains the Core Value. | |
| Time-bound only | Deadline-only eviction; burst within window still grows memory unbounded. | |

**User's choice:** Free-text, mapped to "Defer with size+time bound" — "it should accept if it's within a time limit or the buffer is full, however the time has to be considerable so we don't wrongly reject valid late dependencies"
**Notes:** User emphasized the window must be considerable; wrongly rejecting valid late dependencies is the failure mode to avoid.

### Follow-up: Deferral window expression

| Option | Description | Selected |
|--------|-------------|----------|
| Config, ~30 min default | network_config.json beside reconnect knobs; generous default; test/dev override. | ✓ |
| Config, multi-hour default | Maximum late-dependency protection; long-lived buffers. | |
| Depth-based bound | Dep-count/head-depth deadline; clock-skew-immune but harder to reason about. | |

**User's choice:** Config, ~30 min default (Recommended)
**Notes:** None.

---

## Admission Audit Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Audit + cheap guards | Documented audit AND the inexpensive guards it identifies (ACTIVE-addition cap per certified update; equivocation check reusing the slot ledger). | ✓ |
| Audit only | Evidence plus remediation list; policy changes become follow-up work. | |

**User's choice:** Audit + cheap guards (Recommended)
**Notes:** None.

---

## Claude's Discretion

- Verification mechanism for the strict registry path (update-chain replay vs. anchored delta) — replay of existing machinery presumed; researcher validates.
- Ownership/placement of the shared verdict primitive.
- Per-type mint consensus-rule naming (MINT-02).
- Eviction policy details beyond oldest-first.
- Decomposition of the folded trusted-peer-genesis-config hardening.

## Deferred Ideas

None raised — discussion stayed within phase scope. Reviewed-but-not-folded todos are recorded in CONTEXT.md's Deferred section.
