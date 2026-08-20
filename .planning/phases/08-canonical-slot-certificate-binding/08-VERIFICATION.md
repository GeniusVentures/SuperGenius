---
phase: 08-canonical-slot-certificate-binding
verified: 2026-08-20T15:58:02Z
status: passed
score: 6/6 must-haves verified
overrides_applied: 0
---

# Phase 8: Canonical Slot & Certificate Binding — Verification Report

**Phase Goal:** Validators and receivers recognize all competing proposals for one verified external burn as one finality domain while preserving the certificate's exact winning-proposal binding.

**Verified:** 2026-08-20T15:58:02Z  
**Status:** passed  
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | D-01 / SLOT-01: The existing Mint canonical slot retains chain, token, source transaction, amount, and destination; distinct verified burns differ. | VERIFIED | [`MintTransactionV2::GetSlotID`](../../../../src/account/MintTransactionV2.cpp) constructs `mint-v2:` from exactly those fields (lines 212–234), unchanged from `develop`. Production registration routes nonce subjects through deserialization to that method in [`TransactionManager.cpp`](../../../../src/account/TransactionManager.cpp) lines 169–184. The slot test mutates each fact independently and asserts a different slot (lines 213–241). |
| 2 | D-02 / SLOT-02: Proposal ID, proposer, and nonce cannot change a Mint slot. | VERIFIED | `MintSlotIgnoresProposalEnvelope` changes all three and asserts equality through the registered slot-handler/`GetSlotKey` path ([`consensus_slot_key_test.cpp`](../../../../test/src/blockchain/consensus_slot_key_test.cpp) lines 194–211). `ConsensusManager::GetSlotKey` dispatches that handler rather than using the proposal envelope (lines 2589–2610). |
| 3 | D-03 / SLOT-03: A certificate is bound to its exact embedded winning proposal, not merely to a shared slot. | VERIFIED | [`ValidateCertificate`](../../../../src/blockchain/Consensus.cpp) rejects missing proposal IDs, certificate/embedded-proposal ID mismatch, registry mismatch, invalid subject/signature, and a recomputed proposal-ID mismatch before registry/tally work (lines 2160–2226). |
| 4 | D-04: Key-aware CRDT ingress checks the supplied legacy `/cert/<subject-hash>` evidence; keyless PubSub ingress does not invent a key; the future slot key remains non-authoritative. | VERIFIED | `FilterCertificate` and `CertificateReceived` call `ValidateLegacyCertificateKey` before accepting/dispatching (lines 2040–2107); the helper derives it from the embedded proposal (lines 2260–2268). `HandleCertificate` is keyless and validates independently (lines 2434–2444). `GetExpectedCertificateSlotKey` has no production caller beyond the test seam, so it is only the Phase-8 predicate, not authority/persistence (lines 2270–2277). The lifecycle test covers matching legacy key, mismatch rejection, and valid keyless handling (lines 585–642). |
| 5 | D-05: Invalid or mismatched certificates fail closed before finality/mint-capable state effects, including when the registry is unavailable. | VERIFIED | The review-blocker fix `35daf3d0` places all intrinsic checks before `LoadRegistryByCid` (lines 2173–2237). `HandleCertificate` now accepts only `Check::Approve` before `FetchProposalState`, `CreateProposalState`, or `ClearProposalSlot` (lines 2438–2468); CRDT receipt returns/stalls before handlers on `Reject`/`Stalled` (lines 2101–2116). The unavailable-registry malformed-certificate regression preserves the tracked proposal and records zero handler calls ([`consensus_pending_lifecycle_test.cpp`](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp) lines 645–718). |
| 6 | D-06: The change is identity/binding only; it does not implement durable vote locks, slot-key authority/writer selection, or mint recovery. | VERIFIED | The source delta is limited to two binding helpers, their ingress guards, and focused tests. `SubmitCertificate` still persists only `/cert/<subject-hash>` (lines 1539–1577); `GetExpectedCertificateSlotKey` is test-only. No Phase 9 RocksDB vote state, Phase 10 publisher/authoritative namespace, or Phase 11 idempotent mint recovery code appears in the source diff. |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `test/src/blockchain/consensus_slot_key_test.cpp` | Canonical Mint identity regressions | VERIFIED | Substantive registered-handler tests cover same burn, changed burn facts, and envelope independence. |
| `src/blockchain/Consensus.hpp` | Private binding/key contract | VERIFIED | Private static helpers are declared and exercised through the existing friend test seam. |
| `src/blockchain/Consensus.cpp` | Fail-closed ingress validation | VERIFIED | Helpers are wired into CRDT filter, CRDT callback, certificate submission, certificate validation, and keyless handling. |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Production-ingress binding regressions | VERIFIED | Uses a real signing registry/manager and in-memory secure storage; covers valid and rejected CRDT/keyless paths plus the unavailable-registry regression. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Slot test | `MintTransactionV2::GetSlotID` | Registered nonce slot handler | WIRED | The test handler deserializes the embedded transaction and invokes `GetSlotID` (lines 134–152); the production handler uses the identical flow. |
| Certificate validation | Embedded proposal | Recomputed proposal ID and canonical slot | WIRED | `ValidateCertificate` validates the embedded proposal and invokes `ValidateCertificateBinding`; the latter derives its key only with `GetSlotKey(certificate.proposal())`. |
| CRDT/PubSub ingress | Binding validation | Before handler/finality/cleanup effects | WIRED | `FilterCertificate`, `CertificateReceived`, and `HandleCertificate` all guard their later effects. |

The automated key-link checker reported two false negatives caused by an over-restrictive multiline/escaped PLAN regex; manual source tracing above verifies both links.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Canonical slots and certificate ingress binding | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key\|pending_lifecycle)_test' --output-on-failure` | 2/2 passed in 6.80s | PASS |
| Scope and whitespace sanity | `git diff --check HEAD --` (phase source/test files); `git diff --quiet develop...HEAD -- src/account/MintTransactionV2.cpp` | Clean; Mint slot implementation unchanged | PASS |

### Requirements Coverage

| Requirement | Source Plan | Status | Evidence |
|---|---|---|---|
| SLOT-01 | 08-01 | SATISFIED | Production slot registration plus same-burn and changed-fact test coverage. |
| SLOT-02 | 08-01 | SATISFIED | Envelope-independence regression and unchanged `GetSlotID` formula. |
| SLOT-03 | 08-01 | SATISFIED | Intrinsic exact-proposal validation, legacy-key compatibility validation, keyless acceptance, and no-cleanup unavailable-registry regression. |

No Phase-8 requirements are orphaned from the plan.

### Anti-Patterns Found

No Phase-introduced debt markers or stubs found. The only `TODO` in the reviewed `Consensus.cpp` predates Phase 8 (`52fc1c253`) and is outside the changed region.

### Probe Execution

No Phase-8 probe scripts are declared or present; focused CTest coverage is the runnable validation contract.

## Conclusion

Phase 8 achieves its bounded goal. It establishes and verifies canonical Mint slot identity and exact certificate/proposal binding, including the follow-up fail-closed ordering fix. The still-legacy subject-hash certificate persistence is deliberate Phase-8 compatibility behavior; slot-keyed authority, durable one-vote finality, and mint-recovery work remain explicitly deferred to Phases 9–11.

---

_Verified: 2026-08-20T15:58:02Z_  
_Verifier: the agent (gsd-verifier)_
