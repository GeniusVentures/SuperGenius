---
phase: 15
slug: private-networks-consume-privatenetworkid-identity-and-bind
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-31
---

# Phase 15 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest over existing `test/src/*` suites (GoogleTest-style binaries, per existing `pubsub_counts`) |
| **Config file** | root `CMakeLists.txt` + `test/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R <suite> --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | ~300 seconds |

> **Wave 0 precondition:** existing `build/OSX/{Release,Debug}` dirs are stale (configured against the old keyless third-party tree). Reconfigure against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` before any test run (research finding — environment blocker).

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build/OSX/Release -R <suite> --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build/OSX/Release --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 300 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 15-01-01 | 01 | 1 | TBD | T-15-01 | pnet handshake rejects mismatched PSK/network identity at connection upgrade | integration | `ctest ... -R pnet` | ❌ W0 | ⬜ pending |
| 15-01-02 | 01 | 1 | TBD | — | `private_network_id` parsed from license NFT and propagated as identity | unit | `ctest ... -R license` | ⬜ pending | ⬜ pending |
| 15-02-01 | 02 | 2 | TBD | T-15-02 | gater fails closed while NetworkRegistry bootstrap unconfirmed | unit+integration | `ctest ... -R gater` | ❌ W0 | ⬜ pending |
| 15-02-02 | 02 | 2 | TBD | — | chain topics and CRDT keys isolated per network (no cross-network read/write) | integration | `ctest ... -R registry` | ⬜ pending | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

> Task IDs above are placeholders pending PLAN.md task numbering — planner must reconcile this map with final plan tasks. Requirements column reads TBD because ROADMAP lists `Requirements: TBD`; coverage derives from CONTEXT.md decisions (D-01…D-11).

---

## Wave 0 Requirements

- [ ] Reconfigure `build/OSX/Release` against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release`
- [ ] New test suite(s) for pnet/gater gating behavior (stubs for the connection-upgrade rejection cases)
- [ ] Extend `test/src/pubsub_counts/pubsub_counts.cpp` pattern for multi-network isolation tests

*Existing infrastructure (CTest) covers framework needs; new suites are content, not framework.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| None anticipated | — | — | All gating/isolation behaviors should be automatable via local multi-node test binaries |

*All phase behaviors have automated verification (target state; planner to confirm no residual manual items).*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 300s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
