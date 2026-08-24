---
phase: 11-convergent-certificate-consumption-mint-recovery
verified: 2026-08-24T18:01:58Z
status: passed
score: 4/4 must-haves verified
overrides_applied: 0
re_verification:
  previous_verdict: gap-closure-required
  previous_score: 2/4
  gaps_closed:
    - "A byte-identical certificate replay is harmless, while different certificate contents for an occupied slot use the deterministic lower serialized SHA-256 selection and surface a different-Mint quorum as consensus equivocation."
    - "A durably accepted certificate causes its embedded winning Mint transaction at most once per node, including duplicate delivery and restart."
  gaps_remaining: []
  regressions: []
---

# Phase 11: Convergent Certificate Consumption & Mint Recovery Verification Report

**Phase Goal:** Every node converges on one accepted slot certificate and applies its exact certified mint at most once across duplicate delivery and crash recovery.
**Verified:** 2026-08-24T18:01:58Z
**Status:** passed
**Re-verification:** Yes — after 11-04 gap closure

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Local completion, PubSub, CRDT synchronization, and restart recovery all pass certificates through one idempotent acceptance path for the same canonical slot. | ✓ VERIFIED | `CertificateReceived` only stalls existing certificate-work; recovery rereads and validates durable `/cert/<slot>` before dispatch. No-handler and handler-error branches remain stalled; registration triggers the same recovery after releasing the handler lock. `HandleCertificate` does not write the CRDT key. |
| 2 | A byte-identical certificate replay is harmless, while occupied-slot representations converge through one deterministic authority selection and a different exact Mint quorum is surfaced as a consensus fault. | ✓ VERIFIED | `SubmitCertificate` and `FilterCertificate` both call `SerializedCertificateHash` and retain the lower lowercase SHA-256 serialized value. Filter rejects a losing remote candidate before CRDT apply. Same-Mint alternate certificate bytes do not enter the critical branch; verified different Mint V2 transaction hashes do. This is correctly a validator-equivocation diagnostic, not a bridge rollback claim. |
| 3 | A durably accepted certificate causes its embedded winning Mint transaction at most once per node, including after duplicate delivery or restart. | ✓ VERIFIED | Normal Phase 9 flow persists/re-announces exactly one signed vote per honest validator and only its deterministic winner forms 2-of-3. Certificate-first handling selects tracked state, then exact CRDT bytes, then only the exact embedded fallback, and requires `CertificateMatchesTransaction` before the shared confirmation lifecycle. Mint UTXOs are outpoint-idempotent; successful repeats are terminal no-ops. |
| 4 | Recovery durably distinguishes certified, applying, and applied Mint work (or an equivalent atomic boundary), so a crash neither repeats the Mint effect nor silently loses certified work. | ✓ VERIFIED | A Mint remains `VERIFYING` while `ParseTransaction` persists its effects; `/bridge/executed/<chain>:<source>` is persisted next; only then is it `CONFIRMED`. Persistence errors return to the certificate-work journal, while already-durable outpoints are safe replay progress. |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `src/blockchain/Consensus.cpp` | One shared durable certificate recovery path and convergent occupied-slot ingress | ✓ VERIFIED | Substantive recovery, vote, filtering, and dispatch logic; manually traced into startup/timer/handler-registration and CRDT callback paths. |
| `src/account/TransactionManager.cpp` / `.hpp` | Exact certificate-bound Mint selection and completion boundary | ✓ VERIFIED | CRDT-first candidate lookup, exact-binding guard, and UTXO → marker → `CONFIRMED` ordering are in production code and exercised by the account target. |
| `src/account/UTXOManager.cpp` / `.hpp` | Durable idempotent Mint effects | ✓ VERIFIED | Outpoint insertions are serialized and rollback their in-memory insertion on snapshot-write failure; an existing outpoint represents durable progress. |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Honest one-vote/quorum, occupied-slot ordering, equivocation, and timer teardown regressions | ✓ VERIFIED | 2,469-line substantive target; focused 5-test run and full CTest wrapper both passed. |
| `test/src/account/transaction_manager_certificate_fallback_test.cpp` | Exact winner, duplicate/restart, and marker-gap recovery | ✓ VERIFIED | 1,124-line substantive target; CTest passed. |
| `test/src/account/utxo_manager_test.cpp` | Concurrent UTXO persistence regression | ✓ VERIFIED | 577-line substantive target; CTest passed. |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| `ProcessDueVoteWork` | `PersistOrLoadExactActiveVote` | Persist exact vote before announcement and `SubmitVote` | ✓ WIRED | The persisted vote is loaded/created before `active_vote_announcements_for_test_` and `SubmitVote`. |
| `SubmitCertificate` / `FilterCertificate` | `/cert/<canonical-slot>` | Shared serialized SHA-256 ordering | ✓ WIRED | Both use `SerializedCertificateHash`; local submit avoids a higher-hash write and remote filtering returns an empty vector before applying a higher-hash candidate. |
| `FilterCertificate` | CRDT immutable merge | Valid equal/lower value reaches existing `PutConvergentImmutable` / `CrdtSet::SetValue` path | ✓ WIRED | The CRDT implementation keeps the lower SHA-256 value at convergent immutable priority. |
| `RegisterCertificateHandler` | `RecoverPendingCertificateWork` | Call after handler-map lock release | ✓ WIRED | Recovery dispatch and registration cannot deadlock on the handler map. |
| `RecoverPendingCertificateWork` | `TransactionManager::OnConsensusCertificate` | Registered nonce handler returns persistence errors to stalled work | ✓ WIRED | Recovery only marks work done after handler success; account marker-failure regression drives callback → durable readback → handler. |
| `ChangeTransactionState(CONFIRMED)` | UTXOs then bridge marker then terminal map entry | `ParseTransaction` → `PersistBridgeExecutedMarker` → `CONFIRMED` | ✓ WIRED | Direct production ordering; no second Mint lifecycle or authority exists. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| --- | --- | --- | --- | --- |
| Certificate recovery | Slot work and certificate bytes | Journal entry → durable `/cert/<slot>` readback → validation → subject handler | Yes | ✓ FLOWING |
| Certificate-first confirmation | Candidate transaction | Tracked transaction → exact monitored-network CRDT transaction → exact certificate-embedded fallback | Yes; every candidate is intrinsic-hash checked and certificate-bound | ✓ FLOWING |
| Mint completion | UTXO outpoints and bridge marker | RocksDB-backed UTXO operations → existing bridge marker write → terminal transaction state | Yes; errors stall work and a retry uses durable outpoints | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Build lifecycle target | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 4` | Completed successfully. | ✓ PASS |
| One-vote/quorum, ordering, equivocation, and Close race | `consensus_pending_lifecycle_test --gtest_filter='...5 focused tests...' --gtest_color=no` | 5/5 passed in 18.87s. | ✓ PASS |
| Full lifecycle CTest wrapper | `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` | 1/1 passed in 43.75s. The previously reported non-completion was not reproduced. | ✓ PASS |
| Build account/UTXO targets | `cmake --build build/OSX/Release --target transaction_manager_certificate_fallback_test utxo_manager_test --parallel 4` | Completed successfully. | ✓ PASS |
| Exact winner plus marker/UTXO recovery | `ctest --test-dir build/OSX/Release -R '^(transaction_manager_certificate_fallback_test|utxo_manager_test)$' --output-on-failure` | 2/2 passed in 33.75s. | ✓ PASS |

### Probe Execution

No phase-declared or conventional `scripts/*/tests/probe-*.sh` probes exist. This C++ phase has focused CTest/GoogleTest behavior evidence instead.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| CERT-05 | 11-01, 11-02, 11-04 | All ingress/restart paths converge through one idempotent canonical-certificate path; an occupied-slot different Mint quorum is a safety fault. | ✓ SATISFIED | Shared durable recovery, receive-only PubSub, identical CRDT ordering at local/remote ingress, and explicit verified-Mint equivocation diagnostic. |
| MINT-01 | 11-02, 11-03, 11-04 | The certified winning Mint applies at most once per node under duplicate delivery/restart. | ✓ SATISFIED | Honest 2-of-3 proof yields one normal winner; exact winner binding plus outpoint/terminal idempotence covers delivery and recovery. A second overlapping quorum requires validator equivocation and is not represented as Mint recovery. |
| MINT-02 | 11-03 | Recovery cannot lose or repeat a Mint effect. | ✓ SATISFIED | Persisted UTXOs precede marker and terminal state; marker failure remains stalled and replays without a second effect. |

No Phase 11 requirements are orphaned from its plans.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | ---: | --- | --- | --- |
| — | — | No Phase 11-introduced `TBD`, `FIXME`, `XXX`, stub, empty handler, or hardcoded-output pattern found. Existing nearby TODOs predate Phase 11. | ℹ️ Info | None. |

### Disconfirmation Checks

- The earlier failure theory—an occupied slot must never select a different serialized certificate—does not match the user-locked CRDT contract. Current code intentionally chooses the lower serialized SHA-256 value in both ingress paths; it does not claim to reverse a previously applied Mint after a real overlapping-quorum fault.
- The test-only equivocation fixture deliberately double-signs one validator. The normal three-validator test does not manufacture that fault, persists one vote per node, and cannot form a loser quorum.
- The timer teardown/concurrent `Close()` concern is covered by `TimerWorkCanReleaseTheLastExternalManagerOwner` and `ConcurrentCloseCallsTransferTimerOwnershipOnlyOnce`; both passed in the focused run and the entire lifecycle CTest target completed.

### Human Verification Required

None. The phase delivers protocol/storage behavior with runnable focused regression coverage; no visual or external-service UAT remains.

## Conclusion

The two prior gaps are closed under the locked protocol semantics. Normal validators can produce only the deterministic same-slot Mint quorum; remote and local certificate ingress use the identical deterministic serialized-value choice; true overlapping-quorum Mint conflicts are observable consensus faults rather than bridge recovery. Existing certificate recovery and UTXO → marker → `CONFIRMED` behavior remains connected and passed its focused regressions. No new authority, journal, CRDT merge implementation, schema, receiver-side PubSub write, or alternate Mint-consumption path was introduced by 11-04.

---

_Verified: 2026-08-24T18:01:58Z_
_Verifier: the agent (gsd-verifier)_
