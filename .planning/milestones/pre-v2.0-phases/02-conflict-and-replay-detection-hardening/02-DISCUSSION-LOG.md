# Phase 2: Conflict and Replay Detection Hardening - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-28
**Phase:** 02-conflict-and-replay-detection-hardening
**Areas discussed:** Double-spend detection, Nonce replay protection, Certificate store lookup efficiency, VerifyParameters integration

---

## Double-Spend Detection

| Option | Description | Selected |
|--------|-------------|----------|
| Certificate UTXO deltas on proto | Add consumed_utxos/created_utxos to Certificate proto, validator replays headers | |
| Certificate-derived outpoint index | Build unordered_map<OutpointKey, tx_hash> from certificate stream | |
| **Certificate-driven UTXO state** | Deserialize tx from certificate's embedded proposal, populate UTXO set via ParseTransaction — existing infrastructure handles the rest | ✓ |

**User's choice:** The user identified that the certificate already carries `ConsensusCertificate.proposal.subject.transaction_data`. Phase 1 embedded it; Phase 2 should use it to populate the UTXO set so `VerifyParameters`, `HasConfirmedInputConflict`, and `CheckTransactionReplayProtection` work naturally.

**Notes:** The approach eliminates the need for a separate certificate-derived index. One change in `OnConsensusCertificate` — deserialize from embedded bytes when `GetTransactionByHash` returns null — feeds the entire existing validation pipeline. UTXO state is populated from certificates, not from CRDT transaction sync.

---

## Nonce Replay Protection

| Option | Description | Selected |
|--------|-------------|----------|
| Embed confirmed_nonce in NonceSubject | Add proto field, proposer declares view | |
| **Certificate-derived nonce index** | Build unordered_map<Address, max_nonce> from certificate stream | ✓ |
| Lazy certificate scan | O(n) per validation | |
| Hybrid hint + index | Embedded field as fast path, index as fallback | |
| Certificate-driven UTXO state | Same approach as double-spend — populate state, existing checks work | ✓ |

**User's choice:** User initially selected certificate-derived nonce index, then the discussion converged on the unified approach: populate UTXO state from certificates so `CheckTransactionReplayProtection`'s certificate chain check (already working for standalone validators) and `GetPeerNonce` nonce window check both work.

**Notes:** `CheckTransactionReplayProtection` already has two layers: (1) certificate chain check via `blockchain_->GetCertificateBySubjectHash(previous_hash)` — works for standalone validators, (2) CRDT nonce window via `account_m->GetPeerNonce` — currently bypassed for standalone validators (returns true on error). Once UTXO state is populated from certificates, layer 2 also works.

---

## VerifyParameters Integration

| Option | Description | Selected |
|--------|-------------|----------|
| **Process certificates into UTXO state** | `OnConsensusCertificate` → `ChangeTransactionState(CONFIRMED)` → `ParseTransaction` → `PutUTXO`/`ConsumeUTXOs` | ✓ |
| Skip UTXO existence verification | Accept double-spend detection only | |

**User's choice:** User identified that `VerifyParameters` (UTXOManager.cpp:473) checks outpoint existence and state against `utxo_outpoints_`, which is only populated by `PutUTXO`/`ConsumeUTXOs` called during `ParseTransaction`. For standalone validators, certificate processing must trigger this chain so `VerifyParameters` works.

**Notes:** `PutUTXO` and `ConsumeUTXOs` are currently only called when the peer receives the transaction via CRDT sync. The certificate path bypassed this entirely. The fix makes the certificate path feed the UTXO state, closing the gap.

---

## the agent's Discretion

- Error handling for certificate deserialization failures
- Whether to insert deserialized tx into `tx_processed_m` before or after `ChangeTransactionState`
- Logging and metrics for certificate-based validation path

## Deferred Ideas

None.
