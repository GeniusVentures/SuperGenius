---
phase: 10
slug: authoritative-slot-certificate-publication
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-08-21
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through CTest |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` |
| **Full focused build** | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` |
| **Estimated runtime** | ~15 seconds after the build is current |

---

## Sampling Rate

- **After every task commit:** Build both focused targets and run the quick command.
- **After every plan wave:** Run the focused CTest command and `git diff --check`.
- **Before `$gsd-verify-work`:** Focused slot/lifecycle regressions must be green; include the relevant account target when COMP-01 changes it.
- **Max feedback latency:** 60 seconds once build artifacts are current.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| TBD | Planned publication/ingress work | TBD | CERT-01 | T-10-01 | Reject subject-hash authority; accept only validated `/cert/<slot>` records. | component | focused CTest command | ❌ W0 cases | ⬜ pending |
| TBD | Planned publisher work | TBD | CERT-02 | T-10-02 | Non-selected and PubSub-receiving peers never call certificate `Put`. | component | focused CTest command | ❌ W0 cases | ⬜ pending |
| TBD | Planned publisher work | TBD | CERT-03 | T-10-03 | Authoritative slot persistence completes before any advertisement; failed write emits no announcement. | component | focused CTest command | ❌ W0 cases | ⬜ pending |
| TBD | Planned recovery work | TBD | CERT-04 | T-10-04 | Successor uses normal round rotation and never overwrites occupied or indeterminate slots. | component + fault injection | focused CTest command | ❌ W0 cases | ⬜ pending |
| TBD | Planned consumer migration | TBD | COMP-01 | T-10-05 | Consumer fetches transaction, derives `GetSlotID()`, and never falls back to `/cert/<hash>`. | integration/component | focused CTest plus relevant account target | ❌ dedicated cases | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] Extend `ConsensusPendingLifecycleTestAccess` with authoritative-write/announcement observation and `/cert/<slot>` seed/read helpers.
- [ ] Add deterministic selected, non-selected, and successor-round tests without sleeps.
- [ ] Add empty, byte-identical, different-valid, malformed, and unreadable slot-record collision tests.
- [ ] Add transaction lookup coverage for hash → CRDT transaction → `GetSlotID()` → slot certificate and missing transaction fail-closed behavior.
- [ ] Resolve and test the bounded PubSub-failure logging seam, or document why no failure result is observable.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| None | — | All Phase 10 behavior should have deterministic component coverage. | — |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verification or Wave 0 dependencies.
- [ ] Sampling continuity: no 3 consecutive tasks without automated verification.
- [ ] Wave 0 covers all missing references.
- [ ] No watch-mode flags.
- [ ] Feedback latency < 60 seconds once build artifacts are current.
- [ ] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending
