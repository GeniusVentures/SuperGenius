---
phase: 13
slug: close-v1-1-trusted-peer-genesis-quorum-policy-and-production
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-08-11
---

# Phase 13 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest through repository `addtest(...)`; CTest 3.31.4 |
| **Config file** | `test/src/CMakeLists.txt` plus subsystem `CMakeLists.txt` files |
| **Quick run command** | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt_candidate|genesis_manifest|quorum_policy|trust_state_store|trustedpeerregistry|burnconfig'` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'` |
| **Estimated runtime** | Quick unit sampling under 30 seconds per target; focused phase suite approximately 2–5 minutes |

---

## Sampling Rate

- **After every task commit:** Build affected target(s), then run the smallest mapped unit/integration binary.
- **After every plan wave:** Run the focused phase CTest regex.
- **Before `$gsd-verify-work`:** Run the full phase regex, repeat the policy-lifetime/account-switch E2E at least five consecutive times, and confirm no HIGH security finding remains.
- **Max feedback latency:** 30 seconds for codec/policy/store unit tasks; longer startup/E2E targets run at wave boundaries.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 13-01-01 | 01 | 1 | D-02, D-04, BOOT-02 | T13-01 | Canonical manifest rejects empty, duplicate, malformed, and tampered inputs and produces stable fingerprints | unit | `build/OSX/Release/test_bin/genesis_manifest_test` | ❌ W0 | ⬜ pending |
| 13-01-02 | 01 | 1 | D-05..D-08, POLICY-01, VALID-01 | T13-05 | Current policy exclusively authorizes valid hash-linked successors with exact membership/burn floors | unit | `build/OSX/Release/test_bin/quorum_policy_test` | ❌ W0 | ⬜ pending |
| 13-01-03 | 01 | 1 | D-14, D-15, BOOT-04 | T13-06, T13-07, T13-11 | Durable head wins restart races and rejects corruption, rollback, forks, and failed commits | unit | `build/OSX/Release/test_bin/trust_state_store_test` | ❌ W0 | ⬜ pending |
| 13-02-01 | 02 | 2 | D-09..D-12 | T13-03, T13-04 | Only current peers can introduce bounded content-addressed candidates; signatures bind exact bytes | integration | `build/OSX/Release/test_bin/securecrdt_candidate_test` | ❌ W0 | ⬜ pending |
| 13-02-02 | 02 | 2 | D-12 | T13-06 | Concurrent candidates coexist but exactly one durable successor activates and the loser stays stale | concurrency integration | `build/OSX/Release/test_bin/securecrdt_candidate_race_test` | ❌ W0 | ⬜ pending |
| 13-03-01 | 03 | 3 | D-03, BOOT-01, BOOT-03 | T13-01, T13-02 | One-shot tool signs/submits/confirms genesis without argv, logs, or account persistence leaking the key | integration | `build/OSX/Release/test_bin/trust_genesis_tool_test` | ❌ W0 | ⬜ pending |
| 13-03-02 | 03 | 3 | D-13, D-14 | T13-07, T13-08 | Fresh node remains restricted until confirmation; confirmed restart ignores conflicting trust JSON | startup E2E | `build/OSX/Release/test_bin/trust_first_boot_e2e_test && build/OSX/Release/test_bin/trust_restart_test` | ❌ W0 | ⬜ pending |
| 13-03-03 | 03 | 3 | D-10, D-15, TEST-01 | T13-03, T13-07, T13-08 | Altered peers/bootstrapper/thresholds, manifest mismatch, rollback, and forks never replace LKG state | multi-node E2E | `build/OSX/Release/test_bin/trust_tamper_e2e_test` | ❌ W0 | ⬜ pending |
| 13-04-01 | 04 | 3 | D-11 | T13-04 | Candidate reception does not sign; explicit local approval contributes one deduplicated signature | integration | `build/OSX/Release/test_bin/operator_approval_test` | ❌ W0 | ⬜ pending |
| 13-04-02 | 04 | 3 | BURN-01..03, TEST-01 | T13-09 | Confirmed live burn successor changes actual `PayEscrow`; stale-policy candidate cannot update burn | account integration | `build/OSX/Release/test_bin/burnconfig_policy_e2e_test` | ❌ W0 | ⬜ pending |
| 13-04-03 | 04 | 3 | D-16, BURN-02 | T13-10 | `SelectAccount()` preserves policy objects/callback count and later burn updates reach the replacement manager | multiaccount E2E | `build/OSX/Release/test_bin/policy_lifetime_multi_account_test` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/trustedpeer/genesis_manifest_test.cpp` — canonical vectors, validation, fingerprint tampering.
- [ ] `test/src/trustedpeer/quorum_policy_test.cpp` — exact formulas, bounds, versions, and current-policy authorization.
- [ ] `test/src/trustedpeer/trust_state_store_test.cpp` — durable load/commit, corruption, rollback, and fault injection.
- [ ] `test/src/securecrdt/securecrdt_candidate_test.cpp` — candidate authorization, addressing, signature binding, and quorum.
- [ ] `test/src/securecrdt/securecrdt_candidate_race_test.cpp` — barrier-driven simultaneous quorum winner.
- [ ] `test/src/trustedpeer/trust_genesis_tool_test.cpp` — one-shot key lifecycle and submit/confirm behavior.
- [ ] `test/src/startup/trust_first_boot_e2e_test.cpp` and `test/src/startup/trust_restart_test.cpp` — startup restriction and persisted restart authority.
- [ ] `test/src/startup/trust_tamper_e2e_test.cpp` — altered config/manifest, rollback, and fork rejection.
- [ ] `test/src/trustedpeer/operator_approval_test.cpp` — explicit approval only and signature deduplication.
- [ ] `test/src/account/burnconfig_policy_e2e_test.cpp` — actual `PayEscrow` effect and stale-policy rejection.
- [ ] `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` — node-scoped callback/cache lifetime.
- [ ] Register each target in the existing subsystem `CMakeLists.txt`; no test framework installation is required.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Final trusted-peer address collection | D-01, D-04, BOOT-01 | Real project identities and trusted communication channels cannot be inferred by automated tests | Ask each trusted participant for their public key through the agreed trusted channel; enter only public keys; compare the tool's canonical ordered list and fingerprint before confirming |
| Ephemeral private-key destruction ceremony | D-03, T13-02 | Host filesystems, backups, swap, and operator handling extend beyond process-level tests | Use a restricted key file or stdin, verify successful genesis confirmation, remove the source key material and backups according to the runbook, and retain only public bootstrap identity plus audit evidence |

---

## Validation Sign-Off

- [x] All anticipated tasks have automated verification or explicit Wave 0 dependencies.
- [x] Sampling continuity: no three consecutive implementation tasks lack automated verification.
- [x] Wave 0 names every currently missing test reference.
- [x] No watch-mode flags are used.
- [x] Unit feedback latency target is under 30 seconds; startup/E2E tests are reserved for wave gates.
- [x] `nyquist_compliant: true` is set in frontmatter.

**Approval:** strategy approved for planning 2026-08-11; Wave 0 implementation pending
