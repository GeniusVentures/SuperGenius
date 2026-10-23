---
phase: 19
slug: pruned-node-survival-recovery-hardening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-23
---

# Phase 19 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (existing) |
| **Config file** | none — existing targets host new files |
| **Quick run command** | per-task filters: `tx_pruner_test --gtest_filter='Survival*'`, `globaldb_erase_test` selections, `crdt_test` gate selections |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-90s · full suite per baseline |

---

## Sampling Rate

- **After every task commit:** the task's quick filter, double-run for survival suites
- **After every plan wave:** full suite
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~90s

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 19-XX | — | — | GATE-05 | — | consumed-preserving clear + own-replay-proof replay: rebuild never resurrects spent outputs | integration (checkpoint-mismatch fixture) | Survival filter | ❌ W0 | ⬜ pending |
| 19-XX | — | — | SURV-01 | — | restart matrix: prune→restart→READY (real boot leg)→tip sync→spend/receive→nonce chain | integration (reopen + boot matrix) | Survival filter ×2 | ❌ W0 | ⬜ pending |
| 19-XX | — | — | SURV-01 | — | pre-broadcast failure rolls baseline; bounded SYNCING exit → durable nonce, READY | unit (lifecycle access) | lifecycle filter | ❌ W0 | ⬜ pending |
| 19-XX | — | — | SURV-02 | — | tri-state lookup: found/not-found/pruned distinct; hash-arg query surfaces it | unit + integration | lookup filter | ❌ W0 | ⬜ pending |
| 19-XX | — | — | SURV-02 | — | peer refusal: RS_NOT_FOUND observed by requester, failover | 3-node fixture | Redelivery-extended filter | ❌ W0 | ⬜ pending |
| 19-XX | — | — | SURV-03 | — | fresh node bootstraps through pruned mesh (full node rebroadcasts serving heads) | 3-node E2E | mesh filter ×2 | ❌ W0 | ⬜ pending |
| 19-XX | — | — | GATE-05 | — | RetryDueFailedRoots isResolved guard + missing_tx PRUNED consult: zero doomed fetches/requests | unit + integration | audit filters | ❌ W0 | ⬜ pending |

*Task IDs assigned by the planner.*

---

## Wave 0 Requirements

From 19-RESEARCH.md:

- [ ] Survival fixture file: extends TxPrunerTest (prune-state setup via EraseTransactionEntryPruned) + at least one REAL `StartCore` boot leg (research Pitfall 5: READY strategy — pre-seeded local confirmed nonce)
- [ ] 3-node mesh fixture: the 18-01 Redelivery wiring extended; full node rebroadcasts serving heads (routes register from broadcasts)
- [ ] Checkpoint-mismatch trigger path in a fixture (durable root vs computed root divergence) — the D-01 test vehicle
- [ ] Typed pre-broadcast signal seam (TransactionManager::Error extension) — the D-02 half-1 contract

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live pruned-node spot check on staging | SURV-01/02 | Live net | Optional re-run of the phase-18 demo node post-restart: `ds`-equivalent query on a pruned hash → "history pruned"; spend/receive still works |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
