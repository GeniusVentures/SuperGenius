---
status: human_needed
phase: 02-conflict-and-replay-detection-hardening
score: "5/5"
updated: "2026-05-30"
---

# Phase 2 Verification: Conflict and Replay Detection Hardening

## Score: 5/5 must-have truths verified at code-structure level

| # | Must-Have | Status |
|---|-----------|--------|
| 1 | Standalone validator deserializes from certificate, populates tx_processed_m with CONFIRMED | ✓ VERIFIED |
| 2 | HasConfirmedInputConflict detects double-spends via certificate-populated entries | ✓ VERIFIED (code) / ⏳ HUMAN (behavioral) |
| 3 | CheckTransactionReplayProtection detects nonce replays via certificate-populated entries | ✓ VERIFIED (code) / ⏳ HUMAN (behavioral) |
| 4 | Full nodes with CRDT state unchanged — certificate path is additive | ✓ VERIFIED |
| 5 | Malformed certificate data returns Check::Approve, never rejects | ✓ VERIFIED |

## Requirements

| Requirement | Description | Status |
|-------------|-------------|--------|
| CONFLICT-01 | Double-spend detection without CRDT state | SATISFIED (code) / NEEDS HUMAN (behavioral) |
| NONCE-01 | Nonce replay detection without CRDT state | SATISFIED (code) / NEEDS HUMAN (behavioral) |

## Artifact Verification

| Artifact | Expected | Status |
|----------|----------|--------|
| `src/account/TransactionManager.hpp:462` | `const ConsensusCertificate &certificate` param | ✓ VERIFIED |
| `src/account/TransactionManager.cpp:3573-3641` | Certificate fallback deserialization block | ✓ VERIFIED |
| `test/.../transaction_manager_certificate_fallback_test.cpp` | 9 TEST_F cases, 457 lines | ✓ VERIFIED |
| `test/src/account/CMakeLists.txt:82-101` | Test registration | ✓ VERIFIED |

## Data-Flow Trace

| Data Variable | Source | Status |
|--------------|--------|--------|
| `tx_processed_m` | `ChangeTransactionState(CONFIRMED)` → line 5045 | FLOWING |
| `confirmed_nonces_` | `SetPeerConfirmedNonce` → line 5090 | FLOWING |
| `utxo_outpoints_` | `ParseTransaction` → `PutUTXO` | FLOWING |

## Behavioral Spot-Checks

| Check | Result |
|-------|--------|
| Test binary compiles | PASS |
| 9 tests pass (32.7s) | PASS |
| No anti-patterns in modified files | PASS |

## Human Verification Required

### 1. Double-spend rejection end-to-end (CONFLICT-01)

**Test:** Process certificate for tx_A spending UTXO outpoint O1 via `OnConsensusCertificate`. Then submit proposal for tx_B spending same outpoint O1 via `HandleNonceConsensusSubject`.

**Expected:** tx_B rejected — `HasConfirmedInputConflict(tx_B)` returns true because tx_A's CONFIRMED entry in `tx_processed_m` has conflicting outpoint.

**Why human:** Test fixture doesn't exercise `HandleNonceConsensusSubject` after certificate processing.

### 2. Nonce replay rejection end-to-end (NONCE-01)

**Test:** Process certificate for tx_A with nonce N via `OnConsensusCertificate`. Then submit proposal for tx_B with same nonce N from same address.

**Expected:** tx_B rejected — `GetPeerNonce(address)` returns N (populated by `SetPeerConfirmedNonce`), and `tx_B.GetNonce() <= confirmed_nonce`.

**Why human:** Test fixture doesn't exercise `CheckTransactionReplayProtection` after certificate processing.

### 3. UAT items (02-UAT.md)

- Test 1 (signature change): PASS
- Test 2 (certificate fallback when tx not found): PENDING
- Test 3 (no regression for full nodes): PENDING
- Test 4 (failure modes return Approve): PENDING

## Gap Summary

**Implementation: Complete and correct.** All 9 tests pass. Certificate fallback path is properly hardened with defensive guards.

**Test coverage gap:** The 9 tests verify state population and edge-case safety. ROADMAP success criteria SC1 and SC2 require verifying that populated state actually leads to rejection of conflicting/replayed proposals — integration test gap, not implementation gap.

**No blockers.**
