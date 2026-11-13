---
phase: 16
slug: erasure-primitives-bookkeeping-hardening
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-10-15
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test + CTest (C++17, existing repo infrastructure; `addtest` macro, one ctest entry per target) |
| **Config file** | none — existing targets (`crdt_test`, `globaldb_integration_test`) host the new files |
| **Quick run command** | `build/OSX/Release/test_bin/crdt_test --gtest_filter='Erase*'` (after `cmake --build build/OSX/Release --target crdt_test`) |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | quick ~30-60s · full suite per repo baseline (SC-5 wave gate) |

---

## Sampling Rate

- **After every task commit:** Run the quick command (`Erase*` filter + the task's own filter), double-run for erase suites (D-C6 stale-DB discipline)
- **After every plan wave:** Run the full CTest suite (criterion 5: existing suite stays green)
- **Before `/gsd:verify-work`:** Full suite must be green + all grep gates clean
- **Max feedback latency:** ~60 seconds (quick filter)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 16-01-1 | 16-01 | 1 | PRUNE-02 | T-16-01/02 | stages 4 key families, no hooks, no commit | unit (crdt_set_test) | `test_bin/crdt_test --gtest_filter='EraseElementStaging*'` | ❌ W0→16-01-3 | ⬜ pending |
| 16-01-2 | 16-01 | 1 | PRUNE-02 | T-16-01 | public pending/active guard + forwarder (additive) | build + source assertion | `cmake --build ... --target crdt_datastore` + awk visibility check | n/a | ⬜ pending |
| 16-01-3 | 16-01 | 1 | PRUNE-02 | T-16-03 | four-family erase, sibling control, null/abort | unit (fresh RocksDB) | `test_bin/crdt_test --gtest_filter='EraseElementStaging*'` ×2 | ❌ W0 (created here) | ⬜ pending |
| 16-02-1 | 16-02 | 1 | PRUNE-02, GATE-03 | T-16-04/05/07 | stages CID::toBytes() only, never seal, removeNode dormant | build + body-scan + repo greps | awk body scan + `grep -rn '"sealed"' src/` == 0 | n/a | ⬜ pending |
| 16-02-2 | 16-02 | 1 | PRUNE-02 | T-16-06 | plain tx_processed_m erase, no revert/tombstone | build + body-scan | awk body scan (banned tokens == 0) | n/a | ⬜ pending |
| 16-02-3 | 16-02 | 1 | GATE-03 | T-16-04/05 | real-store fixture: block gone, seal retained, interrupt no-op, sibling intact | unit (fresh RocksDB) | `test_bin/crdt_test --gtest_filter='EraseBlockStaging*'` ×2 | ❌ W0 (created here) | ⬜ pending |
| 16-03-1 | 16-03 | 2 | PRUNE-02, GATE-03 | T-16-08..12 | guard ensemble → one batch → one commit → seal re-assert | build + body-scan (behavioral proof in 16-04-2) | awk body scan: 1× commit, 4 guards, 0 tombstones | n/a | ⬜ pending |
| 16-03-2 | 16-03 | 2 | PRUNE-02 | T-16-11 | lifecycle driver: resolve CID → EraseEntry → plain cleanup | build + body-scan | awk body scan (composition present, banned absent) | n/a | ⬜ pending |
| 16-03-3 | 16-03 | 2 | PRUNE-02 | T-16-09 | zero production callers (SC-5 precondition) | build + grep inventory + existing suites | `ctest -R "crdt_test\|globaldb_integration_test"` | ✅ (existing) | ⬜ pending |
| 16-04-1 | 16-04 | 3 | PRUNE-02 | T-16-14 | substrate all-or-nothing + reopen; pending true→false; LRU input | unit (fresh RocksDB + stall) | `test_bin/crdt_test --gtest_filter='EraseAtomicity*:EraseGuard*'` ×2 | ❌ W0 (created here) | ⬜ pending |
| 16-04-2 | 16-04 | 3 | PRUNE-02, GATE-03 | T-16-14 | SC-1 e2e + 3-step crash injection + head/unresolved guard rejections | integration (real pubsub node) | `ctest -R globaldb_integration_test` ×2 | ❌ W0 (created here) | ⬜ pending |
| 16-04-3 | 16-04 | 3 | GATE-03 | T-16-13 | ctest source-scan bans (sealed literal, removeNode dormancy, tombstone tokens) + full suite green | gate test + full suite | `test_bin/crdt_test --gtest_filter='EraseSurfaceGate*'` then full `ctest` | ❌ W0 (created here) | ⬜ pending |

*Task IDs assigned by the planner (2026-10-15). Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

From 16-RESEARCH.md Validation Architecture (Wave 0 gaps) — all resolved into plan tasks:

- [x] New erase-primitives unit fixture file wiring real `RocksdbDatastore` + `MerkleDagServiceImpl` from the temp DB handle → created in **16-02-3**, extended in **16-04-1** (`test/src/crdt/crdt_erase_test.cpp`)
- [x] GlobalDB-level erase integration test file (`globaldb_integration_test` target) → **16-04-2** (`test/src/crdt/globaldb_erase_test.cpp`)
- [x] CMake link dependency → **16-02-3** (`ipfs-lite-cpp::ipfs_datastore_rocksdb` on `crdt_test`) + **16-04-2/3** (integration file, gate compile definition)
- [x] Grep-gate mechanism decision → **ctests source-scan** (`EraseSurfaceGateTest`, 16-04-3) for the two repo-wide bans + function-scoped awk gates in plan verify steps (16-02-1/2, 16-03-1/2) for the tombstone ban — first-of-kind mechanism adopted per the "gates, not promises" project rule

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Zero production behavior change (pruner unwired) | PRUNE-02 | Absence of wiring is a review property | Verify no production call sites added (16-03-3 grep inventories); `removeNode` callers remain test-only |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or declared behavioral-proof linkage (16-02-1/2, 16-03-1/2 are build+scan gated; behavioral proof lands in 16-04-1/2 — declared in plan text)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 60s (quick filter)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
