# Phase 10: Authoritative Slot Certificate Publication - Research

**Researched:** 2026-08-21  
**Domain:** C++17 consensus certificate authority, CRDT-backed durable publication, and transaction-derived lookup migration  
**Confidence:** MEDIUM

## User Constraints (from CONTEXT.md)

### Locked Decisions

#### Publisher selection and authority

- **D-01:** Preserve the existing proposal-derived, deterministic consensus-round aggregator rotation. Phase 10 introduces no new publisher-selection or timeout mechanism.
- **D-02:** Only the locally selected current-round aggregator may enter the authoritative certificate write path. Receiving a certificate through PubSub never grants authority and never writes the CRDT key.
- **D-03:** A non-selected validator that sees quorum retains the evidence but neither writes nor advertises. It waits until a later normal round makes it eligible.
- **D-04:** The first valid certificate persisted for a slot is final. A replay of the same certificate is harmless; a different payload for an occupied slot is a safety conflict and must never overwrite it.

#### Persistence and advertisement

- **D-05:** The selected aggregator validates the certificate, persists `/cert/<slot>`, and only then sends the PubSub notification. A successful durable write result is sufficient; Phase 10 does not add a readback-before-advertise requirement.
- **D-06:** PubSub is best-effort cleanup acceleration, not finality. Publish the full certificate as today, but a failed publish is logged and not retried; normal CRDT replication/recovery is the fallback.
- **D-07:** The publisher has no special completion shortcut. After its write, it follows the same certificate receipt/recovery path as every other node.

#### Failover

- **D-08:** Existing consensus-round rotation is the complete, protocol-visible failover rule. A later round's selected aggregator may publish only when no authoritative slot record exists.
- **D-09:** A successor requires the same fully validated quorum evidence for the exact winning proposal. It must fail closed and wait/retry if it cannot reliably determine whether the slot is already occupied.
- **D-10:** If publishers are unavailable for successive rounds, recovery continues through ordinary rotation with that same validated evidence. No new lease, timeout, or retry cap is introduced.

#### Consumer lookup migration

- **D-11:** Transaction-backed consumers derive the authoritative certificate key directly from the transaction's `GetSlotID()`. No subject-hash-to-slot locator and no subject-hash certificate authority are introduced.
- **D-12:** A caller that retained only a transaction hash retrieves the transaction from CRDT, derives its slot, and then performs authoritative lookup. If it cannot retrieve the transaction, finality is unavailable and normal recovery retries; it never falls back to a subject-hash certificate key.
- **D-13:** Registry-batch identity semantics are not redesigned in this phase. Existing generic `GetSlotKey` behavior remains the integration point for non-transaction subjects using the slot-keyed namespace.

### the agent's Discretion

- The researcher and planner may choose the smallest safe code shape for slot-key creation, datastore collision detection, and migration of internal lookup APIs, provided every decision above and the Phase 10 boundary are preserved.

### Deferred Ideas (OUT OF SCOPE)

- Redesigning registry-batch slot identity beyond its existing generic `GetSlotKey` integration is outside Phase 10.
- Convergent certificate consumption and exactly-once mint application remain Phase 11 work.

### Reviewed Todos (not folded)

- `bridge-startup-wiring-mock-rpc.md` — matched only weakly on “bridge” (score 0.2) and is unrelated to authoritative certificate publication.

## Phase Requirements

| ID | Description | Research Support |
|---|---|---|
| CERT-01 | Authority uses only `/cert/<canonical-slot-id>`. | Replace every legacy certificate-key predicate and lookup with `GetExpectedCertificateSlotKey(certificate)` / a slot-derived helper; `GetSlotKey` already dispatches generic subject identity. [CITED: src/blockchain/Consensus.cpp] |
| CERT-02 | Only the deterministic publisher writes; PubSub receipt never writes. | Keep writing reachable only from the existing `ProcessCertificates` → `GetAggregatorRole` → `SubmitCertificate` path; `HandleCertificate` remains keyless validation/volatile handling only. [CITED: src/blockchain/Consensus.cpp] |
| CERT-03 | Persist before PubSub advertising. | Reorder `SubmitCertificate` so validation and collision-safe `db_->Put` finish before constructing/sending the PubSub envelope. [CITED: src/blockchain/Consensus.cpp] |
| CERT-04 | Round rotation provides failover without competing contents. | Existing `GetAggregatorRole` deterministically selects the sorted active validator at `(proposal hash base index + current round) % validator count`; add occupied-slot checks before each normal-round attempt. [CITED: src/blockchain/Consensus.cpp] |
| COMP-01 | Hash-starting consumers resolve a transaction then derive its slot. | `TransactionManager::FetchTransaction` already loads a serialized transaction from `GlobalDB`; use its `GetSlotID()` before the authoritative certificate lookup. [CITED: src/account/TransactionManager.cpp] |

## Summary

Phase 10 is a narrow migration of certificate authority, not a new consensus protocol. `GetSlotKey(certificate.proposal())` already produces the generic canonical slot, `GetExpectedCertificateSlotKey` already formats `/cert/<slot>`, and `GetAggregatorRole` already implements the locked round rotation. The existing code instead publishes first and writes `/cert/<subject-hash>` afterwards; CRDT filter/recovery and all public lookup helpers still enforce that legacy key. [CITED: src/blockchain/Consensus.cpp]

Use one private authoritative-slot helper family in `ConsensusManager`: derive key only from the embedded proposal, validate the exact certificate, inspect an existing slot value, accept byte-identical replay, reject a different valid payload as a conflict, and fail closed on an indeterminate read. Keep `SubmitCertificate` callable only from the selected-aggregator path; successful durable `db_->Put` precedes best-effort full-certificate PubSub. Then migrate read paths so a transaction hash is first resolved to its stored transaction and `GetSlotID()`, never used as a `/cert/` suffix. [CITED: src/blockchain/Consensus.cpp] [CITED: src/account/TransactionManager.cpp]

**Primary recommendation:** Implement the namespace/ingress/lookup migration together in `ConsensusManager` and `Blockchain`, then migrate transaction consumers and registry-batch lookup in one follow-up plan; do not add a locator, publisher lease, receiver-side write, or CRDT/persistence redesign. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|---|---|---|---|
| Canonical slot/key derivation | API / Backend | Database / Storage | `ConsensusManager::GetSlotKey` derives the identity from the embedded proposal, and `/cert/` is only a datastore prefix. [CITED: src/blockchain/Consensus.cpp] |
| Publisher eligibility and normal-round failover | API / Backend | — | `GetAggregatorRole` owns ordered-validator selection and round calculation. [CITED: src/blockchain/Consensus.cpp] |
| Durable authoritative record | Database / Storage | API / Backend | `GlobalDB::Put` writes CRDT data; consensus must validate and choose when that operation is permitted. [CITED: src/crdt/globaldb/globaldb.cpp] |
| PubSub notification | API / Backend | PubSub transport | `ConsensusManager::Publish` sends the full `ConsensusMessage`; it is not the durable certificate store. [CITED: src/blockchain/Consensus.cpp] |
| Transaction-hash consumer lookup | API / Backend | Database / Storage | Transaction code loads the transaction from CRDT and owns the concrete `GeniusTransaction::GetSlotID()` dispatch. [CITED: src/account/TransactionManager.cpp] [CITED: src/account/GeniusTransaction.hpp] |

## Standard Stack

### Core

| Library / facility | Version | Purpose | Why Standard |
|---|---|---|---|
| Existing `ConsensusManager` + protobuf certificate types [VERIFIED: codebase grep] | project-local | Exact proposal/certificate validation, slot derivation, and publisher rotation | It already validates the proposal binding and quorum and provides the current publication seam. [CITED: src/blockchain/Consensus.cpp] |
| Existing `crdt::GlobalDB` [VERIFIED: codebase grep] | project-local | Replicated `/cert/<slot>` record | This is the current persistence interface and delegates writes to `CrdtDatastore::PutKey`. [CITED: src/crdt/globaldb/globaldb.cpp] |
| Existing RocksDB through `GlobalDB` [VERIFIED: codebase grep] | project-local | Existing direct local-vote state and CRDT backing store | Phase 10 needs no new persistence dependency. [CITED: src/crdt/globaldb/globaldb.hpp] |
| Existing GTest/CTest lifecycle fixture [VERIFIED: codebase grep] | project-local | Deterministic consensus/CRDT regression tests | `consensus_pending_lifecycle_test` exposes friends, a real temporary CRDT/RocksDB fixture, and forced timer seams. [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |

### Supporting

| Facility | Purpose | When to Use |
|---|---|---|
| `outcome::result<T>` [VERIFIED: codebase grep] | Fail-closed error propagation | Every parse, validation, read, collision, and write branch. [CITED: src/blockchain/Consensus.cpp] |
| `TransactionManager::FetchTransaction` [VERIFIED: codebase grep] | CRDT transaction retrieval from a known transaction key | Hash-only lookup callers must retrieve the transaction before deriving `GetSlotID()`. [CITED: src/account/TransactionManager.cpp] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|---|---|---|
| Generic slot record | Subject-hash `/cert/<hash>` authority | Rejected by D-11/D-12 and CERT-01; the hash identifies a proposal/transaction, not the shared finality domain. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] |
| Existing round rotation | Publisher lease/health timeout | Rejected by D-01/D-08/D-10; ordinary rounds are the full failover rule. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] |
| Selected publisher only | Receiver-side CRDT write or delivery-source authority | Rejected by D-02 and the milestone scope. [CITED: .planning/REQUIREMENTS.md] |

**Installation:** None — the phase must use existing C++17, CRDT, RocksDB, PubSub, and validator facilities. [CITED: .planning/REQUIREMENTS.md]

## Package Legitimacy Audit

No external package installation is permitted or needed for this phase; the package legitimacy gate is not applicable. [CITED: .planning/REQUIREMENTS.md]

## Architecture Patterns

### System Architecture Diagram

```text
quorum evidence for exact winning proposal
        |
        v
ProcessCertificates / current normal round
        |
        +--> GetAggregatorRole(proposal, registry)
        |      +--> not selected: retain state; no CRDT write; no PubSub
        |      `--> selected:
        |              validate exact certificate + derive /cert/<slot>
        |              |
        |              +--> slot lookup indeterminate --> fail closed; next normal round retries
        |              +--> existing identical payload --> idempotent completion; no overwrite
        |              +--> existing different payload --> safety conflict; no overwrite/advertise
        |              `--> absent --> GlobalDB::Put(/cert/<slot>) succeeds
        |                              |
        |                              v
        |                      full certificate PubSub announcement (best effort)
        |
        v
CRDT merge callback (pre-commit notification only)
        --> mark stalled work --> committed db_->Get --> validate slot key
        --> Phase-9 lock release / existing certificate receipt-recovery path

hash-only consumer --> load transaction from CRDT --> transaction.GetSlotID()
        --> authoritative /cert/<slot> lookup --> valid certificate or pending/retry
```

The diagram intentionally keeps certificate completion on the existing callback/journal/recovery path: the callback runs before CRDT batch commit, so Phase 10 must only change the key predicate there, not use it as publisher authority or a durable shortcut. [CITED: src/crdt/impl/crdt_set.cpp] [CITED: src/blockchain/Consensus.cpp]

### Recommended Project Structure

```text
src/blockchain/
├── Consensus.hpp                 # slot-keyed certificate predicates and slot lookup API
├── Consensus.cpp                 # publish ordering, occupancy checks, filter/recovery migration
├── Blockchain.hpp                # slot-aware certificate façade
├── impl/Blockchain.cpp           # façade forwarding
└── ValidatorRegistry.cpp/.hpp    # registry batch: generic slot-derived certificate lookup
src/account/
├── TransactionManager.cpp        # retrieve hash-addressed transaction, then derive GetSlotID
└── GeniusInputValidator.cpp      # same producer transaction-to-slot lookup
test/src/blockchain/
└── consensus_pending_lifecycle_test.cpp # authority, order, collision, and recovery regressions
```

### Pattern 1: One authoritative key predicate for every CRDT certificate ingress

**What:** Replace `ValidateLegacyCertificateKey` with an authoritative predicate that derives `GetExpectedCertificateSlotKey(certificate)` and requires the supplied key to equal it. Apply it in `FilterCertificate`, `RecoverPendingCertificateWork`, finalized-slot detection, and direct slot lookup. [CITED: src/blockchain/Consensus.cpp]

**When to use:** Any path that receives or reads a key/value certificate record. Keyless PubSub remains a non-authoritative full-certificate notification and must not invent a write key. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

```cpp
// Recommended shape; slot helper already exists. [CITED: src/blockchain/Consensus.cpp]
const auto key = GetExpectedCertificateSlotKey(certificate);
if (key.empty() || stored_key != key || ValidateCertificate(certificate) != Check::Approve) {
    return outcome::failure(std::errc::invalid_argument);
}
```

### Pattern 2: Check occupied state before an authorized write

**What:** In `SubmitCertificate`, after validation and selected-aggregator gating, derive the slot key and read the current value. If absent, write exactly once; if bytes equal, return idempotent success; if a parsed, valid different certificate occupies the key, report a safety conflict and do not publish; if the read/parse/validation status is indeterminate, return failure and keep proposal state for a later ordinary round. [ASSUMED]

**Why:** `GlobalDB::Put` creates a CRDT add delta without an existence predicate, while `CrdtSet::SetValue` accepts an equal-priority different value by storing it; a simple `Put` is not an immutable first-write operation. [CITED: src/crdt/impl/crdt_datastore.cpp] [CITED: src/crdt/impl/crdt_set.cpp]

**Implementation note:** A local pre-read does not supply distributed compare-and-set semantics. The planner must keep this as a protocol safety risk and add a test proving the chosen collision guard rejects an already visible different slot payload; no existing `GlobalDB` API provides atomic “put if absent.” [CITED: src/crdt/globaldb/globaldb.hpp] [ASSUMED]

### Pattern 3: Persist before best-effort advertisement

**What:** Serialize and persist `/cert/<slot>` before calling `Publish(message)`. On a persistence failure return an error and retain quorum evidence. After a successful write, invoke PubSub exactly once; the publisher must still wait for normal CRDT callback/readback recovery instead of directly clearing its slot or vote lock. [CITED: src/blockchain/Consensus.cpp] [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

**Important transport limitation:** `ConsensusManager::Publish` calls `pubsub_->Publish(...)` whose currently visible interface returns no status, then returns success after serialization. Therefore the existing code cannot observe or log a transport-send failure, even though D-06 requires failed publishes to be logged without retry. [CITED: src/blockchain/Consensus.cpp]

### Pattern 4: Derive slot from a retrieved transaction, never the certificate hash key

**What:** Provide a lookup API that accepts either a known slot or a `GeniusTransaction`; callers with only a transaction hash load `GetTransactionPath(hash)` from `GlobalDB`, deserialize it, call `GetSlotID()`, then load `/cert/<slot>`. [CITED: src/account/TransactionManager.cpp] [CITED: src/account/GeniusTransaction.hpp]

**When to use:** All transaction-backed `Blockchain::CheckCertificate`, `Blockchain::GetCertificateBySubjectHash`, replay protection, confirmation/state transitions, and input validation call sites. Registry batch code must instead use the generic certificate’s embedded proposal through `GetSlotKey`, preserving D-13. [CITED: src/account/TransactionManager.cpp] [CITED: src/account/GeniusInputValidator.cpp] [CITED: src/blockchain/ValidatorRegistry.cpp]

### Anti-Patterns to Avoid

- **Publishing before CRDT persistence:** Current `SubmitCertificate` does this; a successful notification is not durable authority. [CITED: src/blockchain/Consensus.cpp]
- **Calling `SubmitCertificate` from PubSub receipt:** `HandleCertificate` must remain a receiver/validation path; D-02 forbids receivers from obtaining write authority. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]
- **Retaining `ValidateLegacyCertificateKey`:** It explicitly binds `/cert/<subject-hash>` and would reject the Phase-10 authoritative record. [CITED: src/blockchain/Consensus.cpp]
- **Clearing proposal or active-vote state immediately after the local write:** Phase 9 requires a committed callback/recovery readback before lock release. [CITED: src/blockchain/Consensus.cpp] [CITED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md]
- **Adding a hash-to-slot authority locator:** D-11/D-12 prohibit it; retrieve the transaction and derive `GetSlotID()`. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---|---|---|---|
| Quorum and signature verification | New certificate verifier | `ValidateCertificate` / `TallyVotes` | Existing code checks embedded proposal identity, registry snapshot, signatures, weights, and quorum. [CITED: src/blockchain/Consensus.cpp] |
| Publisher election/failover | Lease, heartbeat, or timeout manager | `GetAggregatorRole` + normal consensus round | This is the locked protocol-visible selection rule. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] |
| Slot identity | Bridge-only key calculation | `GetSlotKey` / transaction `GetSlotID()` | Existing dispatch is generic and Phase 8 fixed the Mint slot semantics. [CITED: src/blockchain/Consensus.cpp] [CITED: src/account/MintTransactionV2.cpp] |
| Restart/recovery completion | Local-publisher completion branch | Existing certificate work journal and `RecoverPendingCertificateWork` | The callback is deliberately pre-commit and recovery reads committed data. [CITED: src/blockchain/Consensus.cpp] |

**Key insight:** This phase is safe only when authority is decided before a write, while all nodes—including the writer—obtain completion from the same subsequently committed CRDT record. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

## Common Pitfalls

### Pitfall 1: Mechanical key rename leaves authority split

**What goes wrong:** `SubmitCertificate` writes `/cert/<slot>` but filter/recovery/finalized-slot lookup still validate or scan `/cert/<subject-hash>`, causing legitimate authoritative records to stall and legacy records to remain effective. [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Make one slot-key predicate the only certificate-record predicate and migrate `FilterCertificate`, `RecoverPendingCertificateWork`, `HasAcceptedCertificateForSlot`, and public lookup helpers in the same implementation task. [ASSUMED]

### Pitfall 2: Equal-priority CRDT collision overwrites the apparent first record

**What goes wrong:** Multiple CRDT elements can exist for a key, and the stored visible value is updated when incoming priority is equal and value bytes differ. [CITED: src/crdt/impl/crdt_set.cpp]

**How to avoid:** Gate every writer by deterministic role and visible occupancy; treat an occupied different certificate as a conflict. Flag the lack of distributed conditional put explicitly; do not claim that a pre-read alone proves first-writer finality. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] [ASSUMED]

### Pitfall 3: Reintroducing callback-time completion

**What goes wrong:** The CRDT callback is invoked from the set write path before the batch commit, so locally received callback data may not be readable yet. [CITED: src/crdt/impl/crdt_set.cpp] [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Preserve Phase 9’s `MarkSeen`/`MarkStalled` then post-commit `db_->Get` recovery sequencing; alter only the key binding used by that sequence. [CITED: src/blockchain/Consensus.cpp]

### Pitfall 4: Hash-only lookup silently falls back to legacy authority

**What goes wrong:** Current `GetCertificateBySubjectHash`, `CheckCertificateForSubject`, `Blockchain` forwarding methods, `TransactionManager`, `GeniusInputValidator`, and `ValidatorRegistry` directly append a subject hash to `/cert/`. [CITED: src/blockchain/Consensus.cpp] [CITED: src/blockchain/impl/Blockchain.cpp] [CITED: src/account/TransactionManager.cpp] [CITED: src/account/GeniusInputValidator.cpp] [CITED: src/blockchain/ValidatorRegistry.cpp]

**How to avoid:** For a transaction hash, fetch and deserialize the transaction, derive `GetSlotID()`, and fail/pending when retrieval is unavailable. For registry batches, derive from the embedded certificate proposal using generic `GetSlotKey`; do not redesign batch identity. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]

### Pitfall 5: Testing PubSub failure that the current API cannot surface

**What goes wrong:** `ConsensusManager::Publish` reports success after calling a `void` transport method, so a unit test cannot inject or assert an error without a new observable transport seam. [CITED: src/blockchain/Consensus.cpp]

**How to avoid:** Resolve this D-06/API tension before implementation: either confirm “failed publish” means serialization failure only, or authorize a narrow injectable/returning wrapper around the transport. Do not implement a retry loop. [ASSUMED]

## Code Examples

### Authoritative slot lookup after transaction retrieval

```cpp
// Shape required by D-11/D-12. [CITED: src/account/TransactionManager.cpp]
auto tx = TransactionManager::FetchTransaction(db, TransactionManager::GetTransactionPath(tx_hash));
if (tx.has_error() || !tx.value()) {
    return outcome::failure(std::errc::resource_unavailable_try_again);
}
return GetCertificateBySlot(tx.value()->GetSlotID());
```

### Existing deterministic publisher selection

```cpp
// Existing source: src/blockchain/Consensus.cpp
const auto round = GetCurrentRound(proposal.timestamp());
const auto index = (base_index + round) % ordered.size();
return ordered[index] == account_address_ ? AggregatorRole::CurrentAggregator
                                          : AggregatorRole::ActiveButNotAggregator;
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|---|---|---|---|
| `/cert/<subject-hash>` write/lookup and legacy-key predicate | `/cert/<canonical-slot>` sole authority and transaction-derived lookup | Phase 10 (planned) | One shared slot prevents competing envelopes from creating separate certificate authority. [CITED: .planning/ROADMAP.md] |
| Publish then write | Write then best-effort full PubSub announcement | Phase 10 (planned) | PubSub becomes acceleration only; CRDT is the durable finality path. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] |
| Phase-9 read-only scan of accepted legacy records | Direct authoritative slot-key lookup | Phase 10 (planned) | Existing fail-closed no-revote seam loses its expensive legacy scan without weakening committed recovery. [CITED: src/blockchain/Consensus.cpp] [CITED: .planning/phases/09-durable-one-vote-finality/09-RESEARCH.md] |

**Deprecated/outdated:** `ValidateLegacyCertificateKey`, `/cert/<subject-hash>` as authority, and direct hash-suffix certificate reads are all incompatible with CERT-01/COMP-01. [CITED: src/blockchain/Consensus.cpp] [CITED: .planning/REQUIREMENTS.md]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|---|---|---|
| A1 | A centralized `ConsensusManager` helper can safely serialize local read/compare/write decisions, but it cannot by itself add distributed CAS to `GlobalDB`. | Pattern 2 | Concurrent publishers may still cause CRDT value replacement; protocol-level finality needs an explicit resolution. |
| A2 | The minimal safe collision behavior for this phase is visible occupancy detection plus conflict rejection, with stronger distributed immutability deferred only if existing CRDT facilities cannot support it. | Pattern 2 / Open Questions | D-04 could remain unprovable under partition/concurrent writes. |
| A3 | D-06 needs an observable PubSub failure seam or clarification because current `Publish` cannot report a transport error. | Pattern 3 / Pitfall 5 | Required logging/negative test may be impossible or misleading. |
| A4 | A new slot-aware public API plus retained compatibility method implementation can migrate all current internal callers without a broad refactor. | Pattern 4 | API migration may require more consumers or a different ownership boundary. |

## Open Questions

1. **How is “first valid certificate persisted is final” enforced against concurrent writes?**
   - What we know: `GlobalDB` offers `Put` and `Get` but no exposed atomic conditional put, and `CrdtSet` can store a different equal-priority value. [CITED: src/crdt/globaldb/globaldb.hpp] [CITED: src/crdt/impl/crdt_set.cpp]
   - What's unclear: Whether the protocol’s deterministic round/availability assumptions rule out an old and successor publisher racing, or whether an existing lower-layer conditional primitive can be exposed within scope. [ASSUMED]
   - Recommendation: Make this a planner checkpoint: inspect the CRDT priority/merge contract and choose a demonstrably no-overwrite mechanism before claiming D-04 complete. A pre-read must fail closed but is not a distributed CAS. [ASSUMED]

2. **How can PubSub failure be logged under D-06?**
   - What we know: the current consensus wrapper invokes `pubsub_->Publish` and returns success; no status is checked. [CITED: src/blockchain/Consensus.cpp]
   - What's unclear: Whether the dependency exposes another status-returning interface outside the checked-in headers. [ASSUMED]
   - Recommendation: Confirm with the project owner or dependency source before planning a test. If no status exists, record the limitation rather than fabricating a failure branch; do not add retries. [ASSUMED]

3. **Which hash-based registry/batch calls must migrate in Phase 10?**
   - What we know: `ValidatorRegistry::LoadCertificateBySubjectHash` is used during batch creation and update verification. [CITED: src/blockchain/ValidatorRegistry.cpp]
   - What's unclear: D-13 preserves generic `GetSlotKey` semantics, while these paths retain batch subject hashes and do not have a `GeniusTransaction`. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md]
   - Recommendation: Keep the batch subject-hash list as identity metadata, but replace direct hash-key lookup by scanning/deriving the certificate’s generic slot or introduce a generic subject-to-slot API that cannot become a subject-hash authority. [ASSUMED]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|---|---|---|---|---|
| CMake | build focused tests | ✓ | 3.31.4 | — [VERIFIED: local build probe] |
| CTest | focused regression run | ✓ | 3.31.4 | — [VERIFIED: local build probe] |
| `build/OSX/Release` test registration | Phase validation | ✓ | `consensus_slot_key_test`, `consensus_pending_lifecycle_test` registered | — [VERIFIED: local build probe] |
| Existing CRDT/RocksDB/PubSub fixture | consensus lifecycle tests | ✓ | project build dependency | — [VERIFIED: local build probe] |

**Missing dependencies with no fallback:** None identified. [VERIFIED: local build probe]

## Validation Architecture

### Test Framework

| Property | Value |
|---|---|
| Framework | GoogleTest through CTest [VERIFIED: codebase grep] |
| Config file | `test/src/blockchain/CMakeLists.txt` [CITED: test/src/blockchain/CMakeLists.txt] |
| Quick run command | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` [VERIFIED: local build probe] |
| Full focused build | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` [VERIFIED: local build probe] |

Baseline: the focused CTest command passed on 2026-08-21 (2/2 tests). [VERIFIED: local build probe]

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|---|---|---|---|---|
| CERT-01 | Slot-key filter/recovery/lookup reject legacy subject-hash keys and accept only expected `/cert/<slot>`. | component | focused CTest command | ✅ fixture; ❌ Phase-10 cases [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |
| CERT-02 | Only forced current-round aggregator calls authoritative write; keyless `HandleCertificate` and non-aggregator paths observe no `Put`/advertisement. | component | focused CTest command | ✅ fixture/accessor; ❌ cases [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |
| CERT-03 | A successful slot write precedes PubSub invocation; write failure emits no announcement; local writer completion still waits for callback/recovery. | component | focused CTest command | ✅ lifecycle fixture; ❌ write/order seam [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |
| CERT-04 | Later normal-round successor sees absent slot and can publish same exact certificate; occupied identical is replay-safe; occupied different fails closed. | component + fault injection | focused CTest command | ✅ timer/round accessor; ❌ occupancy/failure cases [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp] |
| COMP-01 | Hash-only transaction consumer fetches transaction, derives slot, then finds authoritative record; missing transaction never falls back to `/cert/<hash>`. | integration/component | focused CTest plus relevant account target | ❌ dedicated cases [CITED: src/account/TransactionManager.cpp] |

### Sampling Rate

- **Per task commit:** build both focused targets, then run the focused CTest command. [VERIFIED: local build probe]
- **Per wave merge:** focused CTest command plus `git diff --check`. [ASSUMED]
- **Phase gate:** all focused slot/lifecycle regressions green; add the relevant transaction/validator target when COMP-01 changes their implementation. [ASSUMED]

### Wave 0 Gaps

- [ ] Extend `ConsensusPendingLifecycleTestAccess` with an observable authoritative-write/announcement seam and a way to seed/read `/cert/<slot>` values. [ASSUMED]
- [ ] Add deterministic role/round tests for non-selected, selected, and successor paths without sleeps. [CITED: test/src/blockchain/consensus_pending_lifecycle_test.cpp]
- [ ] Add collision tests for empty, byte-identical, different-valid, malformed, and unreadable slot records. [ASSUMED]
- [ ] Add transaction lookup tests covering hash → CRDT transaction → `GetSlotID()` → slot certificate and missing transaction fail-closed behavior. [ASSUMED]
- [ ] Decide whether a transport-status test seam is in scope for D-06. [ASSUMED]

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---|---|---|
| V2 Authentication | Yes | Verify the registry snapshot, proposal signatures, and vote quorum through existing certificate validation. [CITED: src/blockchain/Consensus.cpp] |
| V3 Session Management | No | Node certificate authority is protocol/validator based rather than browser/session state. [CITED: .planning/PROJECT.md] |
| V4 Access Control | Yes | Permit authoritative write only after `GetAggregatorRole` returns `CurrentAggregator`. [CITED: src/blockchain/Consensus.cpp] |
| V5 Input Validation | Yes | Parse certificate, recompute binding, require expected slot key, and fail closed on invalid/stalled data. [CITED: src/blockchain/Consensus.cpp] |
| V6 Cryptography | Yes | Reuse signed proposal/vote verification and existing hash/slot calculation; do not hand-roll cryptography. [CITED: src/blockchain/Consensus.cpp] |

### Known Threat Patterns for consensus certificate publication

| Pattern | STRIDE | Standard Mitigation |
|---|---|---|
| Non-selected peer or PubSub receiver writes the record | Elevation / Tampering | Gate the only write path by deterministic `CurrentAggregator`; receipt never calls `Put`. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] |
| Malformed or wrong-slot CRDT certificate enters recovery | Tampering / DoS | Validate key-to-embedded-proposal slot before recovery effects; retain stalled work on failed readback. [CITED: src/blockchain/Consensus.cpp] |
| Different certificate replaces occupied slot | Tampering | Reject conflict and do not overwrite; resolve the missing conditional-write guarantee before sign-off. [CITED: .planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md] [ASSUMED] |
| Callback triggers early finality | Replay / DoS | Keep Phase-9 pre-commit callback journal behavior and post-commit readback. [CITED: src/blockchain/Consensus.cpp] |

## Sources

### Primary (HIGH confidence)

- [src/blockchain/Consensus.cpp](../../../../src/blockchain/Consensus.cpp) — publisher role/round rotation, current publication ordering, key predicates, filter/callback/recovery, slot scan, and hash lookup.
- [src/blockchain/Consensus.hpp](../../../../src/blockchain/Consensus.hpp) — certificate, slot, active-vote, and recovery interfaces/state.
- [src/crdt/globaldb/globaldb.cpp](../../../../src/crdt/globaldb/globaldb.cpp), [src/crdt/impl/crdt_datastore.cpp](../../../../src/crdt/impl/crdt_datastore.cpp), and [src/crdt/impl/crdt_set.cpp](../../../../src/crdt/impl/crdt_set.cpp) — `Put` delegation and CRDT merge/value collision semantics.
- [src/account/TransactionManager.cpp](../../../../src/account/TransactionManager.cpp), [src/account/GeniusInputValidator.cpp](../../../../src/account/GeniusInputValidator.cpp), and [src/account/GeniusTransaction.hpp](../../../../src/account/GeniusTransaction.hpp) — transaction retrieval, consumer call sites, and `GetSlotID` contract.
- [src/blockchain/ValidatorRegistry.cpp](../../../../src/blockchain/ValidatorRegistry.cpp) — registry batch hash-based lookup call sites.
- [test/src/blockchain/consensus_pending_lifecycle_test.cpp](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp) and [test/src/blockchain/CMakeLists.txt](../../../../test/src/blockchain/CMakeLists.txt) — focused fixtures and registered targets.
- [Phase 10 locked context](10-CONTEXT.md), [requirements](../../REQUIREMENTS.md), and [roadmap](../../ROADMAP.md) — scope, decisions, and acceptance contract.

### Secondary (MEDIUM confidence)

- None — no web-derived dependency claims are needed for this existing-stack phase.

### Tertiary (LOW confidence)

- The four implementation choices recorded in the Assumptions Log, especially the absence of a distributed conditional-write primitive and the needed PubSub status seam.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — the phase uses only inspected project facilities. [VERIFIED: codebase grep]
- Architecture: MEDIUM — key migration/role/recovery seams are concrete, but distributed no-overwrite semantics need resolution. [CITED: src/crdt/impl/crdt_set.cpp] [ASSUMED]
- Pitfalls: HIGH — current publish-before-write, legacy lookup, pre-commit callback, and CRDT replacement behavior were inspected directly. [CITED: src/blockchain/Consensus.cpp] [CITED: src/crdt/impl/crdt_set.cpp]

**Research date:** 2026-08-21  
**Valid until:** 2026-09-20 for internal-code findings; revisit the two open implementation tensions before execution. [ASSUMED]
