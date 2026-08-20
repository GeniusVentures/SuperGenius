---
phase: 9
slug: durable-one-vote-finality
status: ready
nyquist_compliant: true
wave_0_complete: false
created: 2026-08-20
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through CTest |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |
| **Estimated runtime** | Focused suite: under 30 seconds after build; full suite: environment-dependent |

---

## Sampling Rate

- **After every task commit:** build `consensus_pending_lifecycle_test` and `consensus_slot_key_test`, then run the quick command.
- **After every plan wave:** run the full CTest command when the runner permits; report any environment limitation rather than claiming a result.
- **Before `$gsd-verify-work`:** focused suite and `git diff --check` must be green.
- **Max feedback latency:** one focused test build/run per task.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | VOTE-01–03 | T-09-01–03 | Test seam exposes deterministic window, direct local-store failures, exact announcement, and restart inspection. | test infrastructure | focused consensus CTest | ✅ | ⬜ pending |
| 09-01-02 | 01 | 1 | VOTE-01–03 | T-09-01–03 | Freeze/ranking, durable-before-first-publication, exact replay, collision/corruption, and expiry retain the no-revote lock. | deterministic lifecycle | focused consensus CTest | ✅ | ⬜ pending |
| 09-02-01 | 02 | 2 | VOTE-04 | T-09-04 | Only successful post-commit legacy-key readback, binding revalidation, `Check::Approve`, and same-slot match release the record. | durable-ingress lifecycle | focused consensus CTest | ✅ | ⬜ pending |
| 09-02-02 | 02 | 2 | VOTE-04 | T-09-04 | Callback-before-commit, readback-race, malformed/rejected/stalled/different-slot paths retain the record; later committed same-slot readback releases it. | adversarial lifecycle | focused consensus CTest | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — extend the existing test accessor with deterministic active-vote clock, store-failure, recovery, and exact-announcement observation.
- [ ] `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — retain the `MemorySecureStorage` fixture setup before signing-account construction.
- [ ] No framework installation is required; existing CMake/CTest infrastructure covers this phase.

---

## Manual-Only Verifications

All Phase 9 behaviors have automated focused regression coverage. Full-suite execution is required when the runner can complete it; a runner timeout is an environment limitation, not proof of success.

---

## Validation Sign-Off

- [x] All planned tasks have an automated verification route.
- [x] Sampling continuity: no three consecutive tasks lack focused automated verification.
- [x] Existing infrastructure covers all requirements; Wave 0 only extends the current test seam.
- [x] No watch-mode flags.
- [x] Security regressions cover corrupt records, same-slot replacement, deadline replay, and undurable certificate receipt.
- [x] `nyquist_compliant: true` set in frontmatter.

**Approval:** approved 2026-08-20
