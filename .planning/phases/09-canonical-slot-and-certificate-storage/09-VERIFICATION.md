---
status: gaps_found
score: "10/16 must-have truths verified; 7/10 requirements satisfied"
phase: "09-canonical-slot-and-certificate-storage"
timestamp: "2026-07-23T15:27:00Z"
verifier: "gsd-verifier"
---

# Phase 09 Verification

## Verdict

Phase 09 does **not** achieve its goal yet.

The normal-transaction slot contract, candidate-independent mint slot composition,
slot-keyed local certificate writes, verified transaction-hash indexes, compatibility
wrappers, and startup rejection of pre-existing legacy state are implemented and the
focused tests pass. However, three independently confirmed critical defects break the
phase boundary:

1. an external mint can bypass its burn proof with an empty source reference (and can
   trust a receipt for a different transaction);
2. catch-up permanently advances past burns when receipt resolution fails; and
3. replicated certificates are not required to use one canonical byte representation.

Four additional confirmed warnings affect stalled-certificate recovery, storage error
classification, runtime legacy-key admission, and `MintFunds` canonical input handling.
Green focused tests do not cover these paths.

## Verification scope and method

Verification was performed against the current implementation, not the plan summaries.
The following were read and cross-referenced:

- all four `09-0X-PLAN.md` files and all four `09-0X-SUMMARY.md` files;
- `09-REVIEW.md`, `.planning/REQUIREMENTS.md`, and the Phase 9 section of
  `.planning/ROADMAP.md`;
- applicable slot derivation, bridge receipt/index propagation, public-chain validation,
  certificate CRDT filtering/storage, lookup, and consumer code;
- the focused Phase 09 tests and their uncovered paths.

No applicable `AGENTS.md` exists in this checkout. No implementation file was modified.
The pre-existing dirty items listed by the orchestrator remained untouched.

## Goal assessment

| Goal component | Status | Evidence |
|---|---|---|
| One canonical finality identity for normal proposals | Verified | `GeniusTransaction::MakeNonceSlotPreimage` validates a 128-character lowercase hex source and appends unsigned decimal nonce; `GetSlotID` hashes the preimage (`src/account/GeniusTransaction.cpp:122-161`). The registered nonce handler delegates embedded transactions to `GetSlotID` (`src/account/TransactionManager.cpp:176-203`). |
| One candidate-independent identity for a real bridge burn | Gap | The intended preimage uses chain, input hash, and receipt index only (`src/account/MintTransactionV2.cpp:231-273`), but an all-zero input hash is accepted as canonical and malformed `MintFunds` input is converted to an empty `Hash256` (`src/account/TransactionManager.cpp:654-680`). Thus invalid source references do not fail closed. |
| Consensus admission/arbitration uses the slot result | Verified | Local submission and remote handling derive a slot before insertion (`src/blockchain/Consensus.cpp:1651-1679`, `1908-1946`); proposal and slot state are keyed by the derived slot (`651-700`); registered-handler errors propagate without fallback (`3028-3065`). |
| Slot-keyed certificates remain usable by transaction-hash consumers | Mostly verified | Local submission writes `/cert/v2/slot/<slot>` and `/cert/v2/tx/<winner>` in one batch (`src/blockchain/Consensus.cpp:1731-1853`); hash lookup follows the index, delegates to slot lookup, and verifies the winner (`3740-3790`). Actual account consumers call the wrapper (`src/account/TransactionManager.cpp:3910-3934`, `src/account/GeniusInputValidator.cpp:454-476`). Remote canonical-representation and storage-error gaps remain. |

## Requirement traceability

All ten IDs from plan frontmatter are defined in `.planning/REQUIREMENTS.md`, mapped to
Phase 9 in `.planning/ROADMAP.md`, and accounted for below.

| Requirement | Status | Evidence and impact |
|---|---|---|
| SLOT-01 | Satisfied | Slot derivation is result-returning and is used before proposal state insertion, by arbitration state, by full-subject finality lookup, and by certificate storage. |
| SLOT-02 | Satisfied | Normal slots hash canonical source address plus nonce; tests prove deterministic equality and nonce separation (`consensus_slot_key_test`, 11/11 passed). |
| SLOT-03 | Gap | The intended `(source chain, burn hash, receipt-local index)` composition exists, but an all-zero source hash is accepted and malformed mint inputs can be turned into that zero hash instead of being rejected. This is not a canonical external burn identity. |
| SLOT-04 | Satisfied | Token, amount, destination, proposer, proposal ID, proposer nonce, and transaction hash are excluded from mint slot derivation; adversarial equality tests pass. |
| CERT-01 | Gap | Proposal/vote signatures and signed-surface mutation checks exist, but remote validation silently ignores duplicate, wrong-proposal, inactive-validator, and bad-signature votes if quorum remains, and does not bind/derive timestamp. Accepted certificate bytes can therefore contain unvalidated certificate surfaces. |
| CERT-02 | Gap | The authoritative local key is the canonical slot, but remote replicas can accept different protobuf byte encodings for the same semantic certificate. First-writer byte equality can make honest deterministic replay conflict and allow replicas to retain different authoritative bytes for one slot. |
| CERT-03 | Satisfied | A successful local write includes the winner-hash index whose value is only the slot ID, in the same `GlobalDB::Put(vector<DataPair>)` batch as the authoritative certificate. |
| CERT-04 | Satisfied with warning | Hash lookup validates canonical input, validates the index slot, loads via slot lookup, and checks the embedded winning hash. However, all underlying read errors are currently collapsed to `NotFound`. |
| COMP-01 | Satisfied with coverage gap | Both previous-nonce and producer-UTXO production paths call `GetCertificateBySubjectHash`; neither reads `/cert/` directly. The named compatibility test calls the lookup API twice rather than executing either real consumer. |
| COMP-02 | Satisfied with warning | Startup scans `/cert/` before subscriptions/timers/filter registration and rejects non-v2 keys. Runtime replication does not guard the whole namespace, so a node can accept a legacy key and only fail after restart. |

Requirement score: **7/10 satisfied**. SLOT-03, CERT-01, and CERT-02 have
goal-blocking gaps.

## Plan must-have verification

### Plan 09-01 — Canonical slot identity

| Must-have truth | Status | Evidence |
|---|---|---|
| Every accepted consensus subject resolves to one lowercase 64-hex SHA-256 slot ID. | Verified | Canonical preimages are SHA-256 hashed and `GetSlotKey` enforces canonical 64-lowercase-hex output. |
| Same canonical source address and nonce share one normal slot. | Verified | Static derivation and passing normal-slot tests. |
| Same chain, burn hash, and receipt-local index share one mint slot regardless of candidate fields. | Verified | Mint preimage contains only those three components; equality tests vary all candidate-controlled fields. |
| Invalid or noncanonical slot inputs are rejected instead of falling back. | Gap | All-zero burn hashes pass `MintTransactionV2::GetSlotPreimage`; malformed `MintFunds` hashes become an empty input hash, and empty chain IDs become noncanonical `"public"` after the API has begun mutation. |

### Plan 09-02 — Canonical bridge event identity

| Must-have truth | Status | Evidence |
|---|---|---|
| Every live or catch-up burn carries its absolute receipt-local position. | Gap | Live receipt iteration assigns the absolute ordinal and the relayer rejects absence. Catch-up derives the ordinal from the full receipt, but missing/inconsistent receipts are skipped while `process_logs` still returns success and the cursor advances (`bridge_catchup_watcher.cpp:244-362`, `496-511`). |
| Multiple burns in one transaction remain independently mintable. | Verified | Discovery, deduplication, persistence, and UTXO identity use `(tx hash, receipt index)`; focused identity/relayer tests pass. |
| Synthetic outpoint, reservation, rollback, and persistence identity use the receipt index. | Verified | `MintFunds` uses `receipt_log_index` in duplicate checks, persistence key, UTXO construction, reservation, and rollback. |
| Validators accept only when the exact indexed log matches chain, token, amount, and destination. | Gap | Exact indexed-log comparisons exist, but an empty source reference returns `true` before external-chain classification; the returned receipt transaction hash is not checked against the requested source hash (`PublicChainInputValidator.cpp:110-123`, `320-330`, `396-435`). |

### Plan 09-03 — Canonical certificate storage

| Must-have truth | Status | Evidence |
|---|---|---|
| One successful publication makes the slot record and index visible in the same CRDT delta. | Gap | The local batch is atomic, but when registry data is temporarily unavailable, `ValidateCertificate` returns `Stalled`; the delta filter requires `Approve`, removes both elements, and the DAG node is still recorded as processed. The pair is never journaled for retry. |
| Byte-identical replay is idempotent; a different occupied-slot certificate is rejected and diagnosed. | Gap | This works for locally deterministic bytes, but the remote filter accepts noncanonical protobuf bytes for equivalent semantics, allowing semantically identical honest replay to be treated as a conflict. |
| Replicated malformed, partial, or mismatched pairs are rejected before merge. | Gap | Structural partial/mismatch checks pass, but unknown fields, changed timestamp, vote reordering, duplicate votes, and appended ignored invalid votes can be accepted because incoming bytes are never compared with deterministic normalized serialization. |
| Legacy transaction-keyed state blocks startup before side effects. | Verified | `HasCompatibleCertificateState` runs before pubsub subscription, timer start, or filter registration; startup tests pass. |

### Plan 09-04 — Verified compatibility lookups

| Must-have truth | Status | Evidence |
|---|---|---|
| Slot lookup returns only a certificate deriving to the requested slot. | Verified | Canonical input, parse, derived slot, and complete-certificate checks are enforced. |
| Hash lookup verifies index-to-slot and certificate-to-winner links. | Verified | The index is validated, slot lookup is authoritative, and winner hash must equal the request. |
| Losing hash is `NotFound`; a full losing subject sees its finalized slot and winner. | Verified | Implemented by distinct hash and full-subject APIs; compatibility fixture passes. |
| Previous-nonce and producer-UTXO consumers continue retrieving winners by hash. | Verified statically | Both production consumers use the Blockchain hash wrapper. Their Phase 09 tests do not execute the real branches. |

Must-have score: **10/16 verified**.

## Independent review-finding disposition

| Finding | Disposition | Verification impact |
|---|---|---|
| CR-001 empty-source mint proof bypass / receipt-hash mismatch | Confirmed critical | Breaks exact external burn proof and fail-closed canonical source handling; impacts SLOT-03 and Plan 09-01/09-02 must-haves. |
| CR-002 catch-up cursor advances after receipt failure | Confirmed critical | Matching burns can be permanently omitted; breaks the “every catch-up burn” must-have. |
| CR-003 noncanonical replicated certificate bytes | Confirmed critical | Breaks canonical authoritative representation and replay convergence; impacts CERT-01/CERT-02 and Plan 09-03 must-haves. |
| WR-001 stalled registry permanently drops certificate delta | Confirmed warning | Breaks publication visibility/recovery on normal registry/certificate reordering. |
| WR-002 every storage read error maps to `NotFound` | Confirmed warning | `storage::DatabaseError` distinguishes `NOT_FOUND`, `CORRUPTION`, `IO_ERROR`, timeout, shutdown, etc., but both lookup APIs return `NotFound` for every `db_->Get` error. |
| WR-003 legacy namespace guarded only at startup | Confirmed warning | Runtime filters match only `/cert/v2/...`; default-accepted `/cert/<hash>` data can poison the next restart. |
| WR-004 empty-chain `MintFunds` creates `"public"` mint | Confirmed warning | `"public"` cannot pass numeric mint slot validation, but the API can reserve/enqueue before slot derivation fails. |
| IN-001 named consumer tests do not execute consumers | Confirmed info | Production call sites are correct by inspection, but regression coverage does not prove their winning/missing/corrupt/operational-error branches. |

## Automated checks

### Build

The following targets built successfully from the current checkout:

- `consensus_slot_key_test`
- `bridge_event_identity_test`
- `bridge_relayer_test`
- `public_chain_mint_validation_test`
- `consensus_certificate_store_test`
- `certificate_compatibility_test`
- `transaction_manager_pending_lifecycle_test`
- `consensus_pending_lifecycle_test`
- `crdt_test`
- `bridge_anvil_catchup_e2e_test`

### Test execution

| Check | Result |
|---|---|
| `consensus_slot_key_test --gtest_brief=1` | PASS, 11/11 |
| `bridge_event_identity_test --gtest_brief=1` | PASS, 4/4 |
| `bridge_relayer_test --gtest_brief=1` | PASS, 27/27 |
| `public_chain_mint_validation_test --gtest_brief=1` | PASS, 4/4 |
| `consensus_certificate_store_test --gtest_brief=1` | PASS, 11/11 |
| `certificate_compatibility_test --gtest_brief=1` | PASS, 11/11 |
| `transaction_manager_pending_lifecycle_test --gtest_brief=1` | PASS, 3/3 |
| `consensus_pending_lifecycle_test --gtest_brief=1` | PASS, 7/7 |
| `crdt_test --gtest_filter='*AtomicTransaction*' --gtest_brief=1` | PASS, 3/3 |
| `bridge_anvil_catchup_e2e_test --gtest_brief=1` | Environment-blocked: fixture found no available TCP port in 18545-18644; all 3 tests skipped and the binary exited 1 |
| `git diff --check` | PASS |
| `git diff --check c852bf9f^..HEAD` | PASS |

The focused suite totals **81 passing tests** plus **3 passing CRDT atomic-transaction
tests**. It does not contain regressions for the confirmed critical paths.

## Gaps to close

1. Reject zero, empty, malformed, and noncanonical external mint source hashes before
   any reservation or enqueue; classify local-chain bypass explicitly; verify returned
   receipt `transactionHash` equals the requested hash.
2. Make catch-up receipt-resolution failure fail the chunk and preserve its cursor for
   retry; do not retain transient receipt failure across retries.
3. Define and enforce canonical replicated certificate bytes: reject unknown fields and
   ignored/duplicate votes, canonicalize vote ordering, derive redundant fields, and
   compare incoming bytes with deterministic serialization before merge.
4. Preserve stalled atomic certificate pairs for dependency retry rather than deleting
   them from a CRDT node that is then recorded as processed.
5. Distinguish underlying `NOT_FOUND` from integrity and operational database errors in
   both typed lookup APIs.
6. Guard the complete runtime `/cert/` namespace against non-v2 keys.
7. Exercise the real previous-nonce and producer-UTXO consumers with winning, absent,
   corrupt, and operational-error lookup outcomes.

## Human verification

No human verification is required to determine this status: the goal-blocking gaps are
conclusive from reachable source paths. The Anvil fixture should be rerun in an
environment with an available configured port after the code gaps are fixed, but a green
external E2E run would not refute the current `gaps_found` verdict.

