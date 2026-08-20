# Phase 8: Canonical Slot & Certificate Binding - Pattern Map

**Mapped:** 2026-08-20  
**Files analyzed:** 4 planned changes; 1 canonical reference-only source  
**Analogs found:** 4 / 4

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/blockchain/Consensus.hpp` | service header / internal API | event-driven, CRDT ingress | its existing private certificate/slot declarations | exact |
| `src/blockchain/Consensus.cpp` | consensus service | event-driven, CRDT + pub-sub | existing `ValidateCertificate`, `FilterCertificate`, `CertificateReceived`, `HandleCertificate` | exact |
| `test/src/blockchain/consensus_slot_key_test.cpp` | unit test | transform | existing Mint-v2 `GetSlotKey` fixture | exact |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | component test | event-driven / CRDT | existing CRDT fixture and friend access seam | exact |

`src/account/MintTransactionV2.cpp` is a **reference-only** file for this phase: preserve `GetSlotID()` exactly; do not change its formula.

## Pattern Assignments

### `src/blockchain/Consensus.hpp` (service header, event-driven)

**Analog:** private certificate and slot API around `ConsensusManager::HandleCertificate`, `GetSlotKey`, `ValidateCertificateBestProposal`, and `ClearProposalSlot` ([`Consensus.hpp`](../../../../src/blockchain/Consensus.hpp):641-725).

Add only a narrow private binding helper/declaration (and, if needed, an expected future slot-key calculation helper) beside these existing methods. Keep test-only access via the already declared friend accessors ([`Consensus.hpp`](../../../../src/blockchain/Consensus.hpp):541-543); extend `ConsensusPendingLifecycleTestAccess` rather than making consensus internals public.

### `src/blockchain/Consensus.cpp` (consensus service, event-driven / CRDT ingress)

**Analog 1 — canonical slot derivation:** `GetSlotKey` ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2527-2549) dispatches a subject-type handler, then falls back to `ComputeSubjectId`; all new binding code must derive the slot from `certificate.proposal()` through this method rather than accepting a caller-supplied slot.

```cpp
if ( it != slot_key_handlers_.end() )
{
    return it->second( subject );
}

auto subject_id = ComputeSubjectId( subject );
return subject_id.has_value() ? subject_id.value() : proposal.proposal_id();
```

**Analog 2 — exact certificate/proposal validation:** extend the existing semantic validation sequence in `ValidateCertificate` ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2131-2215). It already rejects absent/mismatched embedded proposals and recomputes the proposal ID before tallying votes; the Phase 8 binding guard belongs in this fail-closed path or in a shared helper called by every ingress path.

```cpp
const auto &proposal = certificate.proposal();
if ( proposal.proposal_id() != certificate.proposal_id() )
{
    return Check::Reject;
}
// ... CheckProposal(proposal)
const auto computed_id = CreateProposalId( proposal );
if ( computed_id != certificate.proposal_id() )
{
    return Check::Reject;
}
```

Use `Check::Reject` for identity/binding mismatches (not `Stalled`, which is reserved here for unavailable registry data at lines 2161-2169). Never relax exact proposal-ID/payload binding merely because two proposals resolve to one slot.

**Analog 3 — apply the same guard at both CRDT ingress stages:** the filter parses and rejects before accepting an element ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2023-2047); the callback validates before registry finalization, handler dispatch, journal completion, or dependency wake-up ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2050-2128). Call the shared binding predicate in both locations, using `element.key()`/`key` only as the supplied storage-key evidence.

```cpp
if ( ValidateCertificate( certificate ) == Check::Reject )
{
    return std::vector<crdt::pb::Element>{};
}
```

```cpp
if ( certificate_check == Check::Reject )
{
    return;
}

registry_->OnFinalizedCertificate( certificate );
// registered certificate handler and dependency wake-up follow
```

**Analog 4 — validation must precede destructive slot effects:** `HandleCertificate` validates, resolves state, checks the best proposal, then clears all contenders ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2372-2407). Phase 8's binding failure must return before `CreateProposalState`, `ClearProposalSlot`, registry finalization, handler invocation, and any mint-capable downstream effect.

```cpp
if ( !ValidateCertificateBestProposal( proposal_state, certificate ) )
{
    return;
}

ClearProposalSlot( certificate.proposal() );
```

`ClearProposalSlot` erases every matching proposal and the slot state ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2478-2524), so it is not an acceptable cleanup path for a rejected certificate.

**Legacy namespace boundary:** `SubmitCertificate` still writes `"/cert/" + GetSubjectHash(...)` ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):1548-1578), and lookup does the same ([`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):3135-3163). Preserve both. A Phase 8 helper may calculate/test the future `"/cert/" + GetSlotKey(certificate.proposal())` binding, but must not move storage/lookup authority to that key.

### `test/src/blockchain/consensus_slot_key_test.cpp` (unit test, transform)

**Analog:** the static friend accessor plus registered nonce slot handler ([`consensus_slot_key_test.cpp`](../../../../test/src/blockchain/consensus_slot_key_test.cpp):28-145). Tests build a Mint-v2 protobuf, route it through `TransactionManager::DeSerializeEmbeddedTransaction`, and observe the real `GetSlotID()` via `GetSlotKey`; copy this seam rather than testing a duplicated slot formula.

```cpp
auto tx = TransactionManager::DeSerializeEmbeddedTransaction(
    nonce.value().transaction() );
if ( tx.has_value() )
{
    return tx.value()->GetSlotID();
}
```

Extend the existing paired-proposal assertions ([`consensus_slot_key_test.cpp`](../../../../test/src/blockchain/consensus_slot_key_test.cpp):156-208): hold the Mint facts and burn input fixed while varying `proposal_id`, `proposer_id`, and nonce; assert one slot. Add negative controls for chain, token, amount, destination, and source burn hash; assert different slots. Do **not** modify `MintTransactionV2::GetSlotID()`.

### `test/src/blockchain/consensus_pending_lifecycle_test.cpp` (component test, event-driven / CRDT)

**Analog:** the active CRDT-backed fixture and private accessor ([`consensus_pending_lifecycle_test.cpp`](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp):33-225, 250-346). Extend this accessor with only the minimal Phase 8 seams needed to seed/check proposal slot state and invoke the private certificate ingress path. It already observes `proposals_`, `slot_states_`, `certificates_pending_`, and can call private processing methods.

```cpp
static bool HasProposal( const std::shared_ptr<ConsensusManager> &manager,
                         const std::string &proposal_id )
{
    return manager && manager->proposals_.find( proposal_id ) != manager->proposals_.end();
}

static void ProcessCertificates( const std::shared_ptr<ConsensusManager> &manager )
{
    manager->ProcessCertificates();
}
```

Use this active target for negative ingress tests: create a valid certificate, mutate an embedded proposal/ID or bind it to a mismatching canonical slot/key, then assert the target proposal and slot state remain and that any registered certificate handler/finality counter is untouched. Build a real manager with `MakeRegistry`/`MakeManager`; do not enable the currently commented-out `consensus_certificate_test` CMake target just for Phase 8 ([`CMakeLists.txt`](../../../../test/src/blockchain/CMakeLists.txt):14-21, 36-45).

## Shared Patterns

### Canonical Mint identity

**Source:** [`MintTransactionV2.cpp`](../../../../src/account/MintTransactionV2.cpp):212-234  
**Apply to:** all Phase 8 slot derivation and slot tests

```cpp
key += chain_id_;
key += kSeparator;
key += token_id_.ToHex();
key += kSeparator;
key += std::to_string( GetAmount() );
// then first destination and first input burn hash when present
```

This is locked canonical behavior. Proposal envelope values are absent; amount and destination are intentionally included.

### Fail-closed effects ordering

**Sources:** [`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):2023-2128, 2372-2524  
**Apply to:** CRDT filter, CRDT callback, pub-sub `HandleCertificate`

Perform parse → semantic/exact-proposal validation → Phase-8 canonical binding validation → existing state/effect logic. On mismatch, return reject/no-op before `OnFinalizedCertificate`, handler/wake-up, `MarkDone`, `CreateProposalState`, or `ClearProposalSlot`.

### Subject hash is not canonical slot

**Sources:** [`Consensus.cpp`](../../../../src/blockchain/Consensus.cpp):545-577, 1548-1565, 3135-3163  
**Apply to:** helper names and tests

For nonce subjects, `GetSubjectHash` returns the nonce `tx_hash`; `GetSlotKey` can instead return Mint's canonical slot. Keep these names/values separate, retain current `/cert/<subject-hash>` persistence and lookup, and make any future `/cert/<slot>` calculation non-authoritative in this phase.

## No Analog Found

| File / concern | Role | Data Flow | Planner direction |
|---|---|---|---|
| strict certificate storage-key ↔ canonical-slot predicate | validation helper | CRDT ingress | No current strict slot-key certificate validator exists. Add the smallest private pure helper based on `GetSlotKey(certificate.proposal())`; exercise it through the active CRDT fixture, but do not activate Phase 10 key migration. |

## Metadata

**Analog search scope:** `src/account`, `src/blockchain`, `test/src/blockchain`  
**Files scanned:** 8  
**Pattern extraction date:** 2026-08-20
