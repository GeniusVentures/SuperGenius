---
phase: 06
slug: network-voting-weight-classes-tier-2
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-06-19
---

# Phase 06 — Validation Strategy (Slot-Based RPC-Hash Voting)

> Per-phase validation contract. Maps the slot-based model (D-01..D-10) to test coverage.

---
## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (gtest) via CMake `enable_testing()` |
| **Config file** | `test/src/blockchain/CMakeLists.txt` |
| **Quick run command** | `cd build/OSX/Debug && ninja consensus_slot_quorum_test validator_registry_promotion_test && ctest -R "ConsensusSlotQuorum|ValidatorRegistryPromotion" --output-on-failure` |
| **Full suite command** | `cd build/OSX/Debug && ninja && ctest -j8 --output-on-failure` |
| **Estimated runtime** | ~15 seconds (quick), ~120 seconds (full suite) |

---
## Sampling Rate

- **After every task commit:** Run quick run command
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green (CLAUDE.md shared-library rule for consensus changes)
- **Max feedback latency:** 30 seconds

---
## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Command | Status |
|---------|------|------|-------------|------------|-----------------|-----------|---------|--------|
| 06-01-01 | 01 | 1 | REQ-SLOT-01 | T-06-01 / V4,V6 | ConsensusVote proto extended with 3 slot hashes (tags 6/7/8); backward-compatible | unit | `ctest -R ConsensusProtoSlot` | ⬜ pending |
| 06-01-02 | 01 | 1 | REQ-SLOT-02, REQ-SLOT-06 | T-06-02 / V4 | HashDirectApiEndpoint returns 32-byte hash for paid endpoints; HashPublicEndpoint returns empty-vector when absent | unit | `ctest -R PublicChainInputValidator` | ⬜ pending |
| 06-01-03 | 01 | 1 | REQ-SLOT-01, REQ-SLOT-02, REQ-SLOT-06 | T-06-03 / V4,V5 | PopulateVoteSlotHashes: DIRECT_API fills slot_0; PUBLIC fills slots 1-2; abstainers leave empty | unit | `ctest -R ConsensusSlotQuorum.GeniusNodePopulate` | ⬜ pending |
| 06-02-01 | 02 | 2 | REQ-SLOT-02..04 | T-06-04 / V4 | Slot 0=50%, slots 1-2=25% with ≥2-validator dedup; cumulative >75% threshold | unit | `ctest -R ConsensusSlotQuorum` | ⬜ pending |
| 06-02-02 | 02 | 2 | REQ-SLOT-05 | T-06-05 / V4,V5 | EvaluateQuorum dispatches both TallyVotes and HandleVote; both agree | unit | `ctest -R ConsensusSlotQuorum.BothSitesAgree` | ⬜ pending |
| 06-03-01 | 03 | 3 | REQ-REPUT-01 | T-06-08 / V4 | REGULAR→FULL promotion via ApplyVoteEffects when weight ≥ threshold and penalty low | unit | `ctest -R ValidatorRegistryPromotion` | ⬜ pending |
| 06-04-01 | 04 | 4 | REQ-SLOT-02..04, REQ-DETERM-01 | T-06-09 / V4,V6 | Golden CONTEXT D-06 example (500+50+0 vs 900); dedup; deterministic across registry snapshots | integration | `ctest -R ConsensusSlotQuorum` | ⬜ pending |
| 06-04-02 | 04 | 4 | REQ-REPUT-01, REQ-DETERM-01 | T-06-10 / V4 | FULL promotion full pipeline + penalty gate blocks promotion | integration | `ctest -R ValidatorRegistryPromotion` | ⬜ pending |
| 06-04-03 | 04 | 4 | all | — | Full regression: all consensus + account tests green | regression | `ninja && ctest -j8 --output-on-failure` | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---
## Wave 0 Requirements

- [ ] `test/src/blockchain/consensus_slot_quorum_test.cpp` — NEW (covers REQ-SLOT-02..05, REQ-DETERM-01; includes golden D-06 example)
- [ ] `test/src/blockchain/validator_registry_promotion_test.cpp` — NEW (covers REQ-REPUT-01)
- [ ] `test/src/blockchain/CMakeLists.txt` — add both test executables via `add_subdirectory()`
- [ ] Extend `ConsensusManagerTestAccess` if `EvaluateQuorum` helper needs friendship for test access
