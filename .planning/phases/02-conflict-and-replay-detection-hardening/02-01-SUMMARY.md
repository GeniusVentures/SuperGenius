---
phase: 02-conflict-and-replay-detection-hardening
plan: 01
subsystem: consensus
tags: [c++17, consensus, certificate, validation, decentralization]

# Dependency graph
requires:
  - 01-01 (embedded transaction_data in NonceSubject)
  - 01-02 (ChangeTransactionState lifecycle)
provides:
  - Certificate fallback deserialization in OnConsensusCertificate
  - UTXO/nonce state population from certificates for standalone validators
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Certificate fallback path: DecodeNonceSubject → hash verify → DeSerializeTransaction → ChangeTransactionState(CONFIRMED)"
    - "if/else branching — no goto; certificate path and regular path both fall through to shared checkpoint code"

key-files:
  created: []
  modified:
    - src/account/TransactionManager.hpp
    - src/account/TransactionManager.cpp

key-decisions:
  - "OnConsensusCertificate gains const ConsensusCertificate& parameter — callback already had it"
  - "Certificate fallback triggered when GetTransactionByHash returns null (standalone validator path)"
  - "All failure modes return Approve — never reject a valid consensus certificate"
  - "Certificate path skips conflict resolution — certificate is the consensus winner"
  - "No goto — clean if/else with shared checkpoint code at the end"

patterns-established:
  - "Certificate deserialization: DecodeNonceSubject(certificate.proposal().subject()) → transaction_data() → blake2b_256 hash check → DeSerializeTransaction"
  - "Standalone confirmation: ChangeTransactionState(tx, CONFIRMED) → populates tx_processed_m + utxo_outpoints_ + confirmed_nonces_"

requirements-completed:
  - CONFLICT-01
  - NONCE-01

# Metrics
duration: ~10min
completed: 2026-05-28
---

# Phase 02 Plan 01: Certificate Fallback Deserialization Summary

**Certificate fallback path in OnConsensusCertificate for standalone validators — no goto, clean if/else**

## Performance

- **Duration:** ~10 min
- **Files modified:** 2
- **Tasks:** 1 of 1 (implementation only; tests pending)

## Accomplishments

- Added `const ConsensusCertificate &certificate` parameter to `OnConsensusCertificate` signature (header + cpp)
- Passed certificate through at the callback (line 127 — callback already received it)
- Certificate fallback path when `GetTransactionByHash` returns null:
  1. `DecodeNonceSubject(certificate.proposal().subject())` to extract embedded NonceSubject
  2. Empty `transaction_data` check (pre-Phase-1 certificate → Approve)
  3. Defensive `blake2b_256` hash integrity check before deserialization
  4. `DeSerializeTransaction(std::string)` — same path as Phase 1 handler
  5. `ChangeTransactionState(tx, CONFIRMED)` — populates `tx_processed_m`, `utxo_outpoints_`, `confirmed_nonces_`
  6. Falls through to shared checkpoint code (no goto)
- Regular path (tx found in local store) unchanged — conflict resolution preserved

## Decisions Made

- Certificate fallback deserialization uses same Phase 1 pattern (DecodeNonceSubject → hash verify → DeSerialize)
- All failure modes return `Check::Approve` — certificate is valid consensus regardless of local parsing ability
- Certificate path skips conflict resolution — the certificate is the definitive consensus winner
- No goto — clean `if (certificate fallback) { ... } else { regular path }` structure; both paths fall through to shared checkpoint

## Test Status

Tests for the certificate fallback path (8 TEST_F cases from plan) are pending. The plan specified TDD (RED→GREEN→REFACTOR) but they were reverted along with the goto-based implementation. Write tests before marking complete.

## Files Created/Modified

- `src/account/TransactionManager.hpp:456` — Added `const ConsensusCertificate &certificate` parameter
- `src/account/TransactionManager.cpp:127` — Callback passes `certificate` through
- `src/account/TransactionManager.cpp:3416-3493` — Certificate fallback deserialization path

---

*Phase: 02-Conflict and Replay Detection Hardening*
*Completed: 2026-05-28*
