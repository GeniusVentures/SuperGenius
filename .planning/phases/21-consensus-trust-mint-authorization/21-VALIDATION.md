---
phase: 21
slug: consensus-trust-mint-authorization
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-10-19
---

# Phase 21 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (GTest) via CTest |
| **Config file** | `test/src/blockchain/CMakeLists.txt` (note: `consensus_certificate_test` currently commented out — see RESEARCH.md open question 4) |
| **Quick run command** | `ctest --test-dir build/<platform>/<config> -R "blockchain|transaction" --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/<platform>/<config> --output-on-failure` |
| **Estimated runtime** | plan-wave dependent; full suite is the pre-verification gate |

---

## Sampling Rate

- **After every task commit:** Run the quick command for the touched suite
- **After every plan wave:** Run the full suite command
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** one quick-suite run (~minutes)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| (filled at planning — RESEARCH.md § Validation Architecture carries the requirement→test map this table draws from) | | | | | | | | | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] Adversarial+compat test pairs for TRUST-01 (forged registry CID rejected / pre-upgrade cert re-validates)
- [ ] Adversarial tests for MINT-02 (fail-open chain-id cases rejected where today's path accepts)
- [ ] Adversarial+compat pair for MINT-03 (new self-signed v1 mint rejected / migration + historical sync survive)
- [ ] MINT-04 mismatch-binding test (honest RPCs, mismatched amount/destination rejected)

*Existing infrastructure covers the framework; these are the new test files/stubs this phase adds.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| (none anticipated — all phase behaviors have automated verification paths) | | | |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency acceptable for the suites in scope
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
