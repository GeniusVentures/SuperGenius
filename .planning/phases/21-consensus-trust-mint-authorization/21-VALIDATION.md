---
phase: 21
slug: consensus-trust-mint-authorization
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-10-19
---

# Phase 21 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (GTest) via CTest (`addtest()` helper, cmake/functions.cmake) |
| **Config files** | `test/src/{blockchain,account,transaction_sync,base}/CMakeLists.txt` (`consensus_certificate_test` stays disabled — planner disposition of RESEARCH Pitfall 9: multi-node apparatus + static-helper unit seams cover this phase) |
| **Quick run command** | `ctest --test-dir build/OSX/Release -R "<touched-target>" --output-on-failure` |
| **Full suite command** | `ctest --test-dir build/OSX/Release --output-on-failure` |

---

## Sampling Rate

- After every task commit: quick command for the touched suite + changed-file clang-format/clang-tidy
- After every plan wave: full suite command
- Before `/gsd:verify-work`: full suite green (multi-node suites included)
- Max feedback latency: one quick-suite run (~minutes); multi-node targets are RUN_SERIAL gated (existing properties)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 21-01-T1 | 01 | 1 | TRUST-01 | T-21-0101..04 | Verified/unverified loader split; errc-classified failures; raw core guards recursion | unit (static helper) | `ctest --test-dir build/OSX/Release -R validator_registry` | ⬜ pending |
| 21-01-T2 | 01 | 1 | TRUST-01 | T-21-0101 | Forged chain → permission_denied; missing ancestor → try_again; memo cap 64 oldest-first | unit | `ctest --test-dir build/OSX/Release -R validator_registry_verified_load` | ⬜ new (Wave 0) |
| 21-02-T1 | 02 | 1 | MINT-02 | T-21-0201/03 | Empty source-ref / empty-or-local chain-id mint → reject; local types unaffected | unit (mock transport) | `ctest --test-dir build/OSX/Release -R public_chain_input_validator` | ⬜ pending |
| 21-02-T2 | 02 | 1 | MINT-04 | T-21-0202/04 | Receipt decode bound to mint token/amount/destination; mismatch rejects on honest RPC | unit | same as above | ⬜ pending |
| 21-02-T3 | 02 | 1 | MINT-02, MINT-04 | all T-21-02xx | Adversarial (empty identities, fact mismatches) + compat (matching facts, local types, bogus-topic0 control) | unit | `ctest --test-dir build/OSX/Release -R mint_evidence_binding` | ⬜ new (Wave 0) |
| 21-03-T1 | 03 | 1 | TRUST-01, MINT-03 | T-21-0303 | Bounded defer: first-seen deadline, FIFO cap 4096, oldest-first eviction, injectable clock | unit | `ctest --test-dir build/OSX/Release -R bounded_defer` | ⬜ new (Wave 0) |
| 21-03-T2 | 03 | 1 | TRUST-02, MINT-03 | T-21-0304/05 | Config keys read with safe defaults (1800 s / 10); cap applied to registry | config test | `ctest --test-dir build/OSX/Release -R network_config` | ⬜ pending |
| 21-03-T3 | 03 | 1 | TRUST-01 | T-21-0301/02 | Genesis fingerprint pin: tamper → FATAL_TRUST_MISMATCH; correct pin/unpinned/restart-from-confirmed | boot-path unit | `ctest --test-dir build/OSX/Release -R burnconfig_policy_e2e` | ⬜ pending |
| 21-04-T1 | 04 | 2 | TRUST-01 | T-21-0401..04 | Grandfather probe; classified verdicts; Stalled never memoized; D-03 deadline | integration | `ctest --test-dir build/OSX/Release -R consensus` | ⬜ pending |
| 21-04-T2 | 04 | 2 | TRUST-01 | T-21-0401 | Verified-only cache (cap 64, oldest-first); all raw callers migrated; alias deleted | integration/unit | `ctest --test-dir build/OSX/Release -R "consensus\|multi_account"` | ⬜ pending |
| 21-04-T3 | 04 | 2 | TRUST-01 | T-21-0401..05 | Forged registry CID → Reject, no durable record; restart re-validate; catch-up converges | multi-node integration | `ctest --test-dir build/OSX/Release -R multi_node_finality_fault` | ⬜ new (Wave 0) |
| 21-05-T1 | 05 | 1 | MINT-03 | T-21-0501/03 | Consensus ingress rejects amount>0 v1 mint; zero-value/v2 unaffected | unit | `ctest --test-dir build/OSX/Release -R legacy_mint_admission` | ⬜ new (Wave 0) |
| 21-05-T2 | 05 | 1 | MINT-03 | T-21-0502 | Preflight rejects v1 mint with operation_not_permitted; parser map untouched | unit | same as above | ⬜ pending |
| 21-05-T3 | 05 | 1 | MINT-03 | T-21-0503/04 | Adversarial (self-signed v1 rejected both ingresses) + compat (zero-value, v2, transfer) | unit | same as above | ⬜ new (Wave 0) |
| 21-06-T1 | 06 | 3 | MINT-03 | T-21-0601..05 | Certificate-conditional sync verdict: Stall until cert / Reject on deadline / Accept certified | unit/integration | `ctest --test-dir build/OSX/Release -R "legacy_mint\|transaction_sync"` | ⬜ pending |
| 21-06-T2 | 06 | 3 | MINT-03 | T-21-0602/03 | Stall-resolution negative control; deadline tombstones; migration fillers zero-stall; spendability compat | integration | `ctest --test-dir build/OSX/Release -R "legacy_mint_sync\|migration_sync"` | ⬜ new (Wave 0) |
| 21-07-T1 | 07 | 3 | TRUST-02 | T-21-0701..03 | Audit doc: F1/F2/F3 findings each with evidence + disposition | doc | `grep -c Disposition .planning/notes/2026-10-validator-admission-equivocation-audit.md` | ⬜ new |
| 21-07-T2 | 07 | 3 | TRUST-02 | T-21-0701/02/05 | Cap enforcement (wired value); equivocation reject + durable record; disjoint-signer compat | unit/integration | `ctest --test-dir build/OSX/Release -R validator_admission_equivocation` | ⬜ pending |
| 21-07-T3 | 07 | 3 | TRUST-02 | T-21-0701/02 | Four-case adversarial+compat target with named attacker authorities | unit/integration | same as above | ⬜ new (Wave 0) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] Mapped: TRUST-01 adversarial fixture → 21-01-T2 (unit) + 21-04-T3 (multi-node DAG injection)
- [x] Mapped: MINT-02 adversarial → 21-02-T3; MINT-04 mismatch-binding → 21-02-T3 (honest-RPC mismatch cases)
- [x] Mapped: MINT-03 adversarial+compat → 21-05-T3 (ingress/preflight) + 21-06-T2 (sync: stall-resolution negative control, migration fixture, spendability)
- [x] Mapped: TRUST-02 → 21-07-T3
- [x] Mapped: folded-todo boot tamper tests → 21-03-T3
- [x] Stall-resolves negative control (RESEARCH Validation Wave 0) → 21-06-T2 case 1

*Existing infrastructure covers the framework; new files this phase adds: validator_registry_verified_load_test.cpp, mint_evidence_binding_test.cpp, bounded_defer_test.cpp, legacy_mint_admission_test.cpp, legacy_mint_sync_admission_test.cpp, validator_admission_equivocation_test.cpp, and the audit note.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| (none — all phase behaviors have automated verification paths) | | | |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency acceptable for the suites in scope
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner-signed 2026-10-19 (execution confirms by flipping statuses)
