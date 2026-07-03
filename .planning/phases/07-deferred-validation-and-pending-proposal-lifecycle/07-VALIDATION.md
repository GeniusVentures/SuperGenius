---
phase: 07
slug: deferred-validation-and-pending-proposal-lifecycle
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-06-16
---

# Phase 07 — Validation Strategy

> Per-phase validation contract for deferred consensus validation and pending proposal lifecycle.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test / CTest |
| **Config file** | `test/src/blockchain/CMakeLists.txt`, `test/src/account/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Debug -R 'consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Debug --output-on-failure` |
| **Estimated runtime** | Existing focused tests should stay under 60 seconds; full suite runtime depends on local build state |

---

## Sampling Rate

- **After every task commit:** Run the focused CTest command for the touched subsystem.
- **After every plan wave:** Run both focused pending lifecycle tests.
- **Before `$gsd-verify-work`:** Focused tests and relevant existing consensus/account tests must be green.
- **Max feedback latency:** One task without a focused automated check.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 07-W0-01 | Wave 0 | 0 | PEND-01, PEND-02, PEND-03, PEND-04, PEND-05, PEND-06, PEND-07 | T-07-01 / T-07-02 / T-07-03 | Pending remains local, bounded, idempotent, and dependency-triggered | unit/integration | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | no | pending |
| 07-W0-02 | Wave 0 | 0 | TXSTATE-01, PEND-06 | T-07-04 | Inconclusive expiry does not become proven failure; remote temp state is cleaned | unit/integration | `ctest --test-dir build/OSX/Debug -R transaction_manager_pending_lifecycle_test --output-on-failure` | no | pending |

*Status: pending / green / red / flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — tests for structured pending result, typed dependency indexing, dependency-triggered retry, transient retry, TTL, capacity, cleanup, and idempotent voting.
- [ ] `test/src/account/transaction_manager_pending_lifecycle_test.cpp` — tests for predecessor-certificate pending behavior, local outgoing `UNCONFIRMED`, remote temp entry cleanup, and proven-invalid `FAILED`.
- [ ] Add both test files to the relevant CMake test targets.

---

## Manual-Only Verifications

All phase behaviors should have automated verification. Manual review should only inspect threat-model coverage and plan completeness.

---

## Validation Sign-Off

- [ ] All tasks have automated verify commands or Wave 0 dependencies.
- [ ] Sampling continuity: no three consecutive tasks without automated verify.
- [ ] Wave 0 covers all missing pending lifecycle tests.
- [ ] No watch-mode flags in verification commands.
- [ ] Focused feedback latency remains practical for local development.
- [ ] `nyquist_compliant: true` set in frontmatter.

**Approval:** pending

