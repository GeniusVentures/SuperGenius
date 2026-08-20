# Phase 8: Canonical Slot & Certificate Binding - Research

**Researched:** 2026-08-20  
**Domain:** C++17 consensus slot arbitration and certificate validation  
**Confidence:** HIGH

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Keep `MintTransactionV2::GetSlotID()` as the canonical slot calculation. Its verified chain, token, source transaction, amount, and destination are intentional identity facts; do not remove amount or destination.
- **D-02:** Proposer account, proposal nonce, and other proposal-envelope differences must not change the slot. Competing mint proposals for the same verified burn must therefore contend in the same finality domain.
- **D-03:** The certificate remains bound to its exact winning proposal. A matching slot does not permit a certificate to stand in for a different proposal.
- **D-04:** Certificate acceptance must verify agreement among canonical slot, eventual slot-keyed storage key, certificate payload, and embedded winning proposal before it can finalize or mint.
- **D-05:** Any slot/key/payload/proposal mismatch fails closed: it must not finalize, mint, overwrite a certificate, or unlock local slot state. Do not use a local delivery-source flag as evidence of authority.
- **D-06:** Do not change the Mint slot formula merely to solve publication or vote-lifecycle behavior. Those behaviors are intentionally sequenced into Phases 9-11.

### the agent's Discretion

- Choose the smallest validation helpers and test seams that fit the existing C++17 consensus patterns, provided the locked identity and failure behavior above are preserved.

### Deferred Ideas (OUT OF SCOPE)

- `Bridge Startup Wiring + Mock RPC Endpoints` (`.planning/todos/pending/bridge-startup-wiring-mock-rpc.md`) — low-confidence keyword overlap only; startup wiring and mock RPC endpoints are outside Phase 8's canonical-slot and binding boundary.

## Phase Requirements

| ID | Description | Research Support |
|---|---|---|
| SLOT-01 | Same verified burn shares a canonical slot; different burns do not. | Retain `MintTransactionV2::GetSlotID()` and exercise it through `ConsensusManager::GetSlotKey`. [CITED: src/account/MintTransactionV2.cpp] |
| SLOT-02 | Chain, token, source transaction, amount, and destination remain slot facts; proposer and nonce do not. | The implementation reads only Mint fields and first input/output; proposal envelope fields are absent from it. [CITED: src/account/MintTransactionV2.cpp] |
| SLOT-03 | Certificate remains bound to exact proposal; slot/key/payload mismatches are rejected. | Extend the existing certificate semantic validator/filter boundary with a pure canonical-slot/key binding check before clearing a slot or invoking a certificate handler. [CITED: src/blockchain/Consensus.cpp] |

## Summary

`MintTransactionV2::GetSlotID()` already serializes the required verified identity facts as `mint-v2:<chain>:<token>:<amount>:<destination>:<first-input-tx-hash>`; it has no proposer ID or nonce input. Phase 8 must preserve that formula exactly and prove its behavior through the same `GetSlotKey` dispatch that consensus uses. [CITED: src/account/MintTransactionV2.cpp] [CITED: src/blockchain/Consensus.cpp]

The unsafe seam is later in consensus: `GetSubjectHash()` returns the nonce payload transaction hash, and `SubmitCertificate`, CRDT receipt, recovery, and lookup use `/cert/<subject-hash>`. A competing Mint proposal can therefore share a slot but have a different subject-hash persistence identity. Phase 8 should add the canonical-slot/proposal/key binding primitives and enforce proposal/slot consistency on certificate ingress, while leaving the actual authoritative key migration, writer ownership, and durable voting to Phases 10 and 9 respectively. [CITED: src/blockchain/Consensus.cpp]

**Primary recommendation:** Preserve `GetSlotID`; add one fail-closed certificate-binding validation seam in `ConsensusManager`, with a separately testable expected future key `/cert/<GetSlotKey(certificate.proposal())>`, but do not switch existing persistence/lookup to that key in Phase 8.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|---|---|---|---|
| Canonical Mint slot derivation | API / Backend | — | The transaction value object owns the verified Mint facts used by consensus. [CITED: src/account/MintTransactionV2.cpp] |
| Contender grouping and winner bookkeeping | API / Backend | — | `ConsensusManager` derives a slot key and stores per-slot proposal state. [CITED: src/blockchain/Consensus.cpp] |
| Certificate/proposal/slot binding | API / Backend | Database / Storage | Semantic validation belongs in consensus; CRDT key validation is an ingress check, not an authority rule. [CITED: src/blockchain/Consensus.cpp] |
| Future certificate record namespace | Database / Storage | API / Backend | `/cert/<canonical-slot-id>` is a later Phase 10 authority/publication change; Phase 8 defines the expected identity only. [CITED: .planning/phases/08-canonical-slot-certificate-binding/08-CONTEXT.md] |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---|---|---|---|
| C++17 standard library [VERIFIED: codebase grep] | project C++17 | Value comparison, strings, containers, locking | Existing consensus implementation uses C++17 STL and no new dependency is required. [CITED: src/blockchain/Consensus.hpp] |
| Protobuf consensus messages [VERIFIED: codebase grep] | project-generated | Exact proposal/certificate payload and deterministic field binding | `ConsensusCertificate` embeds its `ConsensusProposal`; existing validation recomputes the proposal ID and tallies signed votes. [CITED: src/blockchain/Consensus.cpp] |
| `outcome::result<T>` [VERIFIED: codebase grep] | project-local | Fail-closed error propagation | Existing slot, subject, and persistence paths use `outcome::result` for invalid data. [CITED: src/blockchain/Consensus.cpp] |

**Installation:** None — Phase 8 requires no external packages. [VERIFIED: codebase grep]

## Architecture Patterns

### System Architecture Diagram

```text
MintTransactionV2 embedded in nonce subject
  -> TransactionManager slot-key handler
  -> MintTransactionV2::GetSlotID()  [preserve bytes/semantics]
  -> ConsensusManager::GetSlotKey()
  -> slot_states_[canonical_slot] contender arbitration
  -> ConsensusCertificate (exact embedded proposal + votes)
  -> certificate semantic validation + Phase-8 binding helper
       ├─ mismatch: reject; do not clear slot / invoke handler
       └─ match: existing finality ingress
  -> Phase 9 vote lock | Phase 10 `/cert/<slot>` publishing | Phase 11 minting
```

The current `ClearProposalSlot` removes every tracked proposal in the same slot and erases the in-memory slot state. Binding validation must occur before that call; it must not be used as a durable-finality or vote-lock lifecycle operation. [CITED: src/blockchain/Consensus.cpp]

### Recommended Project Structure

```text
src/blockchain/
├── Consensus.hpp          # Declare narrowly scoped certificate/slot binding helpers
└── Consensus.cpp          # Implement and call helpers at certificate ingress seams

test/src/blockchain/
├── consensus_slot_key_test.cpp              # Pure Mint slot invariants
└── consensus_pending_lifecycle_test.cpp     # CRDT-backed manager/accessor negative ingress checks
```

### Pattern 1: Derive slot from the embedded winning proposal

**What:** Compute the canonical slot only by passing the certificate's embedded proposal through `GetSlotKey`; never accept a caller-provided slot as authoritative. [CITED: src/blockchain/Consensus.cpp]

**When to use:** Every new certificate-binding helper and every CRDT-key check. [ASSUMED]

**Example:**

```cpp
// Source: src/blockchain/Consensus.cpp (existing GetSlotKey/ValidateCertificate pattern)
const auto canonical_slot = GetSlotKey(certificate.proposal());
const auto expected_key = std::string{CERTIFICATE_BASE_PATH_KEY} + canonical_slot;
if (canonical_slot.empty() || storage_key != expected_key) {
    return Check::Reject;
}
```

The example is a Phase-10 activation shape, not permission to move current storage in Phase 8. Phase 8 should expose/test the expected-key calculation and validate proposal/slot agreement; the current CRDT data is still keyed by subject hash. [CITED: src/blockchain/Consensus.cpp]

### Pattern 2: Keep payload validation ahead of destructive local cleanup

**What:** Run `ValidateCertificate`, exact proposal-ID recomputation, and the new binding check before `ClearProposalSlot`, `registry_->OnFinalizedCertificate`, or a registered certificate handler. [CITED: src/blockchain/Consensus.cpp]

**Anti-Patterns to Avoid**

- **Changing `GetSlotID()` to omit amount or destination:** Violates the locked identity contract. [CITED: .planning/phases/08-canonical-slot-certificate-binding/08-CONTEXT.md]
- **Treating a common slot as interchangeable certificate content:** A certificate's embedded proposal ID, signature, registry data, and votes remain exact-payload binding. [CITED: src/blockchain/Consensus.cpp]
- **Switching `/cert/<subject-hash>` writes/reads in this phase:** That is Phase 10's publication and consumer migration scope. [CITED: .planning/ROADMAP.md]
- **Clearing slot state when validation fails:** `ClearProposalSlot` erases all contenders for that slot, so failure must return before it. [CITED: src/blockchain/Consensus.cpp]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---|---|---|---|
| Proposal identity | A second proposal hash format | Existing `CreateProposalId` plus `CheckProposal` | Existing validation recomputes and compares the proposal ID and verifies its signature. [CITED: src/blockchain/Consensus.cpp] |
| Certificate quorum/signature checks | Custom certificate verifier | Existing `ValidateCertificate`/`TallyVotes` | It validates embedded proposal correspondence, registry snapshot, signatures, and quorum. [CITED: src/blockchain/Consensus.cpp] |
| Slot dispatch | A bridge-only consensus engine | Existing static slot-key handler registry | `GetSlotKey` already dispatches the nonce subject to embedded transaction `GetSlotID()`. [CITED: test/src/blockchain/consensus_slot_key_test.cpp] |

**Key insight:** Phase 8 needs an additive identity/binding guard around existing consensus primitives, not a new finality protocol. [VERIFIED: codebase grep]

## Common Pitfalls

### Pitfall 1: Confusing the subject hash with the canonical slot

**What goes wrong:** `GetSubjectHash()` for a nonce subject returns `tx_hash`, whereas Mint contention uses `GetSlotID()` via `GetSlotKey`. [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Make helper names and tests explicit (`subject_hash` versus `canonical_slot`); derive the latter from the embedded proposal. Keep legacy subject-hash storage behavior untouched until Phase 10. [CITED: src/blockchain/Consensus.cpp]

### Pitfall 2: Validation after lifecycle mutation

**What goes wrong:** `HandleCertificate` calls `ClearProposalSlot` after validation; the function deletes all proposal state for the slot. [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Binding failures must return before cleanup, finalized-registry notification, handler dispatch, or any mint-capable downstream path. [CITED: src/blockchain/Consensus.cpp]

### Pitfall 3: Widening Phase 8 into vote durability or certificate authority

**What goes wrong:** `SlotState::voted_proposal_ids` and `slot_states_` are in-memory only and are erased by `ClearProposalSlot`; they cannot be the Phase 9 durable lock. [CITED: src/blockchain/Consensus.hpp] [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Phase 8 may add test accessors/binding helpers only. Do not add RocksDB vote records, publisher selection, CRDT write rules, persistence-before-advertisement, or mint idempotency. [CITED: .planning/ROADMAP.md]

## Code Examples

### Existing canonical slot dispatch

```cpp
// Source: test/src/blockchain/consensus_slot_key_test.cpp
auto tx = TransactionManager::DeSerializeEmbeddedTransaction(nonce.value().transaction());
return tx.has_value() ? tx.value()->GetSlotID()
                      : subject.account_id() + ":" + std::to_string(nonce.value().nonce());
```

### Existing exact proposal binding

```cpp
// Source: src/blockchain/Consensus.cpp
if (proposal.proposal_id() != certificate.proposal_id()) return Check::Reject;
if (CreateProposalId(proposal) != certificate.proposal_id()) return Check::Reject;
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|---|---|---|---|
| `/cert/<subject-hash>` persistence | Future `/cert/<canonical-slot-id>` authority | Phase 10 (planned) | Phase 8 must make the slot/key agreement testable without implementing this migration. [CITED: .planning/ROADMAP.md] |

**Deprecated/outdated:** The prior bridge-only identity proposal is superseded; the milestone decision keeps the current Mint slot facts. [CITED: .planning/research/SUMMARY.md]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|---|---|---|
| A1 | Phase 8 can expose a future-key calculation without changing current CRDT persistence behavior. | Architecture Patterns | Planner must confirm exact helper visibility/call sites against the intended Phase-8 acceptance interpretation. |

## Resolved Ingress Contract

`HandleCertificate` has no datastore key, while the CRDT filter/callback currently receive
the legacy `/cert/<subject-hash>` key. Phase 8 therefore applies exact
certificate/proposal/canonical-slot validation at every ingress, but validates storage-key
evidence only at key-aware CRDT ingress:

- A keyless ingress must accept a valid, exactly bound certificate; it cannot reject merely
  because no datastore key was supplied.
- A key-aware CRDT ingress validates the supplied key against the current legacy subject-hash
  namespace and the exact embedded proposal. That key is compatibility evidence only, never
  canonical-slot authority.
- Phase 8 exposes and tests deterministic canonical-slot/expected-future-slot-key calculation,
  but does not use `/cert/<canonical-slot-id>` for acceptance, persistence, lookup, or writer
  selection. Those changes remain Phase 10.

This avoids both an always-reject guard and an accidental mixed authority model.

## Validation Architecture

### Test Framework

| Property | Value |
|---|---|
| Framework | GoogleTest via CTest [VERIFIED: codebase grep] |
| Config file | `test/src/blockchain/CMakeLists.txt` [CITED: test/src/blockchain/CMakeLists.txt] |
| Quick run command | `ctest --test-dir build -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` [ASSUMED] |
| Full suite command | `ctest --test-dir build --output-on-failure` [ASSUMED] |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|---|---|---|---|---|
| SLOT-01 | Same Mint facts/same burn input produce one `GetSlotKey`; changed burn input produces another. | unit | `ctest --test-dir build -R consensus_slot_key_test --output-on-failure` | ✅ `consensus_slot_key_test.cpp` [CITED: test/src/blockchain/consensus_slot_key_test.cpp] |
| SLOT-02 | Vary proposal ID, proposer account, and nonce while keeping all Mint facts fixed; slot is unchanged; vary each required Mint fact as negative controls. | unit | same | ❌ Wave 0 extension [ASSUMED] |
| SLOT-03 | Reject certificate with altered embedded proposal ID/payload or supplied canonical-slot/key disagreement; assert no slot cleanup/handler invocation. | component | `ctest --test-dir build -R consensus_pending_lifecycle_test --output-on-failure` | ✅ fixture/access seam; ❌ cases [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` [ASSUMED]
- **Per wave merge:** `ctest --test-dir build --output-on-failure` [ASSUMED]
- **Phase gate:** Full suite green before `$gsd-verify-work`. [ASSUMED]

### Wave 0 Gaps

- [ ] Extend `test/src/blockchain/consensus_slot_key_test.cpp` with envelope-independence and required-fact mutation controls. [ASSUMED]
- [ ] Extend the CRDT-backed `consensus_pending_lifecycle_test.cpp` test-access seam to inspect slot state/handler side effects around rejected certificate binding. [ASSUMED]
- [ ] If the disabled `consensus_certificate_test` fixture is re-enabled instead, preserve its `base_crdt_test` linkage and use it for real `SubmitCertificate`/CRDT callback coverage. [CITED: test/src/blockchain/CMakeLists.txt]

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---|---|---|
| V2 Authentication | yes | Verify proposal and vote signatures using existing `GeniusAccount::VerifySignature`. [CITED: src/blockchain/Consensus.cpp] |
| V3 Session Management | no | No user/session boundary is introduced. [VERIFIED: codebase grep] |
| V4 Access Control | yes | Certificate handler side effects only follow approved semantic/binding validation. [CITED: src/blockchain/Consensus.cpp] |
| V5 Input Validation | yes | Parse protobuf, validate subject and exact proposal ID, and fail closed on binding mismatch. [CITED: src/blockchain/Consensus.cpp] |
| V6 Cryptography | yes | Reuse existing signature verification and SHA-256 proposal IDs; do not implement cryptographic primitives. [CITED: src/blockchain/Consensus.cpp] |

### Known Threat Patterns for consensus certificates

| Pattern | STRIDE | Standard Mitigation |
|---|---|---|
| Certificate embeds a different proposal than its proposal ID | Tampering | Recompute proposal ID, verify proposal signature, and reject mismatches. [CITED: src/blockchain/Consensus.cpp] |
| Same-slot payload substitutes for winning proposal | Tampering | Require exact embedded proposal/certificate ID and canonical-slot derivation from that proposal. [CITED: src/blockchain/Consensus.cpp] |
| Invalid certificate causes local contender cleanup | Denial of service | Run all binding checks before `ClearProposalSlot`. [CITED: src/blockchain/Consensus.cpp] |

## Sources

### Primary (HIGH confidence)

- [src/account/MintTransactionV2.cpp](../../../../src/account/MintTransactionV2.cpp) - canonical slot construction.
- [src/blockchain/Consensus.cpp](../../../../src/blockchain/Consensus.cpp) - slot arbitration, certificate validation/storage/filtering, cleanup, and recovery.
- [test/src/blockchain/consensus_slot_key_test.cpp](../../../../test/src/blockchain/consensus_slot_key_test.cpp) - existing slot dispatch fixture.
- [test/src/blockchain/consensus_pending_lifecycle_test.cpp](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp) - CRDT fixture and private test-access seam.
- [Phase context](08-CONTEXT.md) and [roadmap](../../ROADMAP.md) - locked scope and phase boundaries.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all required primitives already exist in source. [VERIFIED: codebase grep]
- Architecture: HIGH - concrete source seams and lifecycle order are directly observable. [VERIFIED: codebase grep]
- Pitfalls: HIGH - subject-hash persistence and in-memory cleanup are concrete current behavior. [VERIFIED: codebase grep]

**Research date:** 2026-08-20  
**Valid until:** 2026-09-19
