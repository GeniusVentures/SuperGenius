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

> Reconciled by planner 2026-08-31 against final PLAN.md task numbering (8 plans, 4 waves). Requirement IDs are derived (D-01..D-11 / PNET-*) because ROADMAP lists `Requirements: TBD`.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 15-01-01 | 01 | 1 | D-10 (env) | T-15-SC | build bound to dev_pnets install; existing pnet handshake test green | integration | `ctest --test-dir build/OSX/Release -R pubsub_counts --output-on-failure` | ✅ (extend run) | ⬜ pending |
| 15-01-02 | 01 | 1 | D-10 | — | owner confirms implementation base + identity encoding before Wave 2 | checkpoint | blocking decision (no automated) | — | ⬜ pending |
| 15-01-03 | 01 | 1 | D-01, D-02 / PNET-CFG | T-15-01, T-15-02 | malformed/all-zero `private_network_id` rejected at load; absent = public; public id only in logs | unit | `ctest --test-dir build/OSX/Release -R network_config_private_network --output-on-failure` | ❌ W0 | ⬜ pending |
| 15-02-01 | 02 | 1 | D-04 / PNET-REG | T-15-04 | PeerRegistry interface compiles; cached-only resolution documented | build | `ninja -C build/OSX/Release securecrdt trustedpeer` | ❌ W0 | ⬜ pending |
| 15-02-02 | 02 | 1 | D-04 / PNET-REG | T-15-05 | SecureCrdtRegistryEntry carries explicit registry authority; regex contract byte-stable | unit | `ctest -R securecrdt` (regression) | ✅ | ⬜ pending |
| 15-02-03 | 02 | 1 | D-04, D-05 / PNET-REG | T-15-06 | per-key signer set resolves from associated registry; TPR logic unchanged | unit | `ctest -R peer_registry` | ❌ W0 | ⬜ pending |
| 15-03-01 | 03 | 2 | D-03, D-06 / PNET-NETREG | T-15-07..10 | TPR-majority bootstrap; double quorum floor; cached-only signer resolution | build+unit | `ninja -C build/OSX/Release networkregistry` | ❌ W0 | ⬜ pending |
| 15-03-02 | 03 | 2 | D-03, D-06 / PNET-NETREG | T-15-07..09 | under-signed bootstrap never confirms; no unilateral admission; no raw key bytes in records | unit | `ctest -R network_registry` | ❌ W0 | ⬜ pending |
| 15-04-01 | 04 | 2 | D-07 / PNET-GATE | T-15-11..13 | allow-list predicate at all peer-aware intercept stages; deny wins; raw-stage pass-through | unit (vendored) | vendored `gossip_pubsub_test` + install grep | ❌ W0 | ⬜ pending |
| 15-04-02 | 04 | 2 | D-07 / PNET-GATE | T-15-11 | GossipPubSub surface installed; SGNUS still builds against refreshed install | integration | `grep SetMembershipAllowList <install>/include/... && ninja -C build/OSX/Release pubsub_counts_test` | ❌ W0 | ⬜ pending |
| 15-05-01 | 05 | 3 | D-06, D-07 / PNET-GATE | T-15-15..18 | private node binds registry membership to gossip host; fail-closed on empty registry | integration | `ctest -R "trustedpeer|securecrdt"` + grep gates | ✅ (wiring) | ⬜ pending |
| 15-05-02 | 05 | 3 | D-07 / PNET-GATE | T-15-15, T-15-16 | same-PSK-not-in-registry rejected; empty-membership fail-closed; runtime admission works | integration | `ctest -R pubsub_counts` (new `PrivateNetworkMembershipGating`) | ✅ (extend) | ⬜ pending |
| 15-06-01 | 06 | 2 | D-08 / PNET-SCOPE | T-15-19..21 | scope helpers; public output byte-identical; no SetNetworkId | unit | `ninja -C build/OSX/Release processing transaction_manager` + grep | ✅ (source) | ⬜ pending |
| 15-06-02 | 06 | 2 | D-02, D-08 / PNET-SCOPE | T-15-19 | node threads private scope into keys/topics/chain-ids; public node unchanged | integration | `ctest -R "startup|node"` | ✅ | ⬜ pending |
| 15-06-03 | 06 | 2 | D-08 / PNET-SCOPE | T-15-19, T-15-20 | public golden strings pinned; distinct networks disjoint | unit | `ctest -R task_keys_scope` | ❌ W0 | ⬜ pending |
| 15-07-01 | 07 | 4 | D-09 / PNET-VAL | T-15-22, T-15-23 | instance-scoped identifiers; public strings byte-stable | unit | `ctest -R "blockchain|validator|genesis|multi_account"` | ✅ | ⬜ pending |
| 15-07-02 | 07 | 4 | D-09 / PNET-VAL | T-15-22 | Blockchain/GossipNode scope threading; all call sites compile | integration | `ctest -R "startup|node|blockchain"` | ✅ | ⬜ pending |
| 15-07-03 | 07 | 4 | D-09 / PNET-VAL | T-15-22, T-15-23 | three-way disjoint scope identifiers | unit | `ctest -R validator_registry_scope` | ❌ W0 | ⬜ pending |
| 15-08-01 | 08 | 5 | D-11 / PNET-PROC | T-15-25..27 | processing host Noise-only + pnet + gater; errors not exceptions | unit+integration | `grep -c Plaintext == 0 && ctest -R processing` | ✅ (source) | ⬜ pending |
| 15-08-02 | 08 | 5 | D-11 / PNET-PROC | T-15-25, T-15-27 | injector builds public/pnet hosts; invalid key throws eagerly; gater rejects non-member | unit | `ctest -R processing_core_gating` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

*Every automated command runs under 300s per the sampling contract; the 15-01-02 checkpoint is the only manual gate (decision, not verification).*

---

## Wave 0 Requirements

- [ ] Reconfigure `build/OSX/Release` against `/Users/henriqueklein/gnus/3rdparty/build/OSX/Release` — **15-01 Task 1**
- [ ] New test suite `network_config_private_network_test` (config identity) — **15-01 Task 3**
- [ ] New test suite `peer_registry_test` (registry association) — **15-02 Task 3**
- [ ] New test suite `network_registry_test` (bootstrap/self-governance/secret exclusion) — **15-03 Task 2**
- [ ] Vendored allow-list gater tests + reinstall — **15-04 Tasks 1-2**
- [ ] Extend `test/src/pubsub_counts/pubsub_counts.cpp` with the D-07 membership layer — **15-05 Task 2**
- [ ] New test suite `task_keys_scope_test` — **15-06 Task 3**
- [ ] New test suite `validator_registry_scope_test` — **15-07 Task 3**
- [ ] New test suite `processing_core_gating_test` — **15-08 Task 2**

*Existing infrastructure (CTest) covers framework needs; new suites are content, not framework. Each Wave-0 scaffold is created inside its owning task (tdd-style behavior-first where applicable) so no task lacks an automated gate.*

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
