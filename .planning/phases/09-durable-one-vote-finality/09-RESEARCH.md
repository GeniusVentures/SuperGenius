# Phase 9: Durable One-Vote Finality - Research

**Researched:** 2026-08-20  
**Domain:** C++17 consensus-slot arbitration, local RocksDB vote durability, restart recovery  
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** The first locally validated proposal for a canonical slot opens a fixed two-second contention window.
- **D-02:** Only proposals fully validated by the deadline are eligible. A late, invalid, or still-stalled proposal cannot delay the window, change its winner, or trigger a vote in that attempt. If the window has no eligible candidate, it creates no vote lock; a proposal may begin a fresh window only after it becomes valid.
- **D-03:** Freeze the eligible set at the deadline. Use the existing deterministic order—lexicographically lowest transaction hash, then lowest proposal ID—to select the winner. The transaction hash is only a tie-break and never changes the canonical slot or certificate-to-proposal binding.
- **D-04:** Apply this arbitration generically to every consensus subject with a canonical slot key, not just bridge Mints. Ignore votes for non-winning and late proposals for finality.
- **D-05:** Store one generic RocksDB-backed consensus-vote record per canonical slot. It is local persistence, not CRDT state, and must not use a bridge-specific namespace. The exact storage prefix follows established storage conventions.
- **D-06:** Before any broadcast, the record must contain the canonical slot, selected full proposal, exact serialized and signed vote, and an absolute acceptance deadline. If persistence cannot complete, emit no vote and leave no usable in-memory vote state.
- **D-07:** An existing same-slot record is idempotent only for the exact stored vote. A different proposed vote is rejected without overwriting the record or broadcasting a replacement.
- **D-08:** On restart, while before the recorded deadline, automatically re-announce only the exact stored signed vote. A failed initial send likewise retries that exact vote on bounded backoff.
- **D-09:** After its deadline, stop re-announcing the vote but retain the lock. Expiry never authorizes a second vote for the slot.
- **D-10:** Delete the local vote record only after durable acceptance of a certificate for the same canonical slot. The accepted certificate may name a different winning proposal from this validator's vote; same-slot finality prevents future voting. Never delete on receipt or parse alone.

### the agent's Discretion
- Choose the smallest C++17 data encoding, RocksDB prefix, timer/test seams, and bounded retry parameters consistent with the fixed two-second window and the persisted absolute deadline.
- Preserve existing generic consensus behavior outside the new slot-arbitration and vote-lock contract. Do not introduce new dependencies or a bridge-special case.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within Phase 9. Slot-keyed certificate publication/failover remains Phase 10, and unified certificate consumption plus exactly-once mint recovery remain Phase 11.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|---|---|---|
| VOTE-01 | Bounded contention and deterministic winner selection per slot. | `ContinueProposalAfterSubject`, `SlotState`, `IsBetterProposal`, and the timer are the direct seam. [VERIFIED: codebase grep] |
| VOTE-02 | Durable selected proposal, signed vote material, and deadline before broadcast. | `GlobalDB::GetDataStore()` exposes local `storage::rocksdb`; its normal path uses synchronous writes. [VERIFIED: codebase grep] |
| VOTE-03 | Restart recovery and exact-vote-only re-announcement. | The manager starts a round timer at construction and already performs recovery work from that loop; add active-vote recovery to the same lifecycle. [VERIFIED: codebase grep] |
| VOTE-04 | Delete the lock only after durable same-slot certificate acceptance. | Certificate ingress and volatile `ClearProposalSlot` are distinct seams; persistent vote deletion must be gated at durable certificate acceptance, not volatile cleanup. [VERIFIED: codebase grep] |
</phase_requirements>

## Summary

`ConsensusManager::ContinueProposalAfterSubject` currently installs/updates the slot's best proposal and immediately creates and publishes a self-vote. `SlotState::voted_proposal_ids` is only process memory, so a better proposal can be selected before the current code's immediate vote and every restart loses the lock. [VERIFIED: codebase grep]

Use a generic per-slot active-vote record in the existing local RocksDB datastore, keyed as `"/consensus/vote/" + canonical_slot`. Store a small protobuf envelope containing the slot, the complete proposal bytes, the exact serialized `ConsensusVote` bytes, and a Unix-millisecond acceptance deadline. Persist it synchronously before the first vote announcement; read/parse/validate it on manager startup; and re-announce its stored vote on a bounded cadence only while the deadline remains in the future. This is local storage, not a `GlobalDB::Put` CRDT write. [VERIFIED: codebase grep] [ASSUMED]

The contention state must be changed from “current best plus already-voted proposal IDs” to “window deadline, eligible proposals, frozen winner, and active/released lock state.” A candidate enters only after subject validation returns `Approve`; first such candidate sets a two-second local monotonic deadline; candidates that complete validation after the deadline are excluded; and only the frozen winner can create, tally, or finalize votes. Existing `IsBetterProposal` already implements the required generic ordering: nonce-subject transaction hash first and proposal ID on a hash tie, otherwise proposal ID. [VERIFIED: codebase grep]

**Primary recommendation:** Add the active-vote record and a deadline-driven `ProcessDueVoteWork` private path to `ConsensusManager`; retain the existing `IsBetterProposal` comparison unchanged, and make every initial/recovery vote flow through one persist-or-load-exact-record gate. [VERIFIED: codebase grep] [ASSUMED]

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|---|---|---|---|
| Candidate validation and two-second eligibility window | API / Backend (ConsensusManager) | — | Subject validation reaches `ContinueProposalAfterSubject`; that method owns `proposals_` and `slot_states_`. [VERIFIED: codebase grep] |
| Deterministic winner selection and incoming-vote gating | API / Backend (ConsensusManager) | PubSub transport | `IsBetterProposal`, `HandleVote`, and slot state are local consensus responsibilities; PubSub only carries envelopes. [VERIFIED: codebase grep] |
| Active-vote durability and restart read | Database / Storage (local RocksDB) | API / Backend | `GlobalDB::GetDataStore()` returns the underlying local RocksDB, while `GlobalDB::Put` is a CRDT operation and is expressly out of scope. [VERIFIED: codebase grep] |
| Exact-vote re-announcement | API / Backend (ConsensusManager timer) | PubSub transport | `StartRoundTimer` already drives deferred work and `SubmitVote` is the outbound boundary. [VERIFIED: codebase grep] |
| Lock release after durable certificate acceptance | Database / Storage | API / Backend | Certificate receipt must be validated and known durable before deleting the local vote key; simple parse/volatile slot cleanup is insufficient. [VERIFIED: codebase grep] [ASSUMED] |

## Standard Stack

### Core

| Library / facility | Version | Purpose | Why Standard |
|---|---:|---|---|
| Existing generated protobuf (`Consensus.proto`) | repository-generated | Encode the small local active-vote envelope and preserve nested consensus objects. | `ConsensusProposal`, `ConsensusVote`, and `ConsensusMessage` are already protobuf types and are serialized throughout consensus. [VERIFIED: codebase grep] |
| Existing `storage::rocksdb` | repository dependency | Synchronous local put/get/remove of the lock record. | `rocksdb::create(path)` configures `WriteOptions::sync = true`; `GlobalDB` exposes that datastore with `GetDataStore()`. [VERIFIED: codebase grep] |
| C++17 chrono/thread/condition variable | C++17 | Monotonic local window timing, absolute persisted deadline, and bounded retry scheduling. | `ConsensusManager` already owns a timer thread, `timer_cv_`, and chrono configuration. [VERIFIED: codebase grep] |
| Existing gtest/CTest consensus fixture | repository test stack | Focused slot, durability, and restart regression coverage. | `consensus_pending_lifecycle_test` provides manager test access, a CRDT/RocksDB fixture, and an in-memory secure-storage factory. [VERIFIED: codebase grep] |

### Supporting

| Facility | Purpose | When to Use |
|---|---|---|
| `base::Buffer` / `crdt::GlobalDB::Buffer` | Convert protobuf bytes and string keys to the local storage API. | Every active-vote record read/write/delete. [VERIFIED: codebase grep] |
| `outcome::result<T>` | Propagate encode, parse, and RocksDB errors fail-closed. | All persistence, recovery, and release helpers. [VERIFIED: codebase grep] |
| `MemorySecureStorage` | Avoid OS keychain state in signing tests. | Before constructing test signing accounts in the existing lifecycle fixture. [VERIFIED: codebase grep] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|---|---|---|
| Direct local RocksDB active-vote record | `GlobalDB::Put` under a CRDT key | Rejected: `GlobalDB::Put` is replicated CRDT state, while D-05 requires local persistence only. [VERIFIED: codebase grep] |
| Small protobuf record | Ad hoc byte packing | Rejected: the existing proposal/vote are protobuf; custom framing would add parsing and forward-compatibility risk without a dependency benefit. [VERIFIED: codebase grep] [ASSUMED] |
| Persisted serialized vote bytes | Re-sign/recreate a vote on restart | Rejected: `CreateVote` timestamps and signs a new object, which violates D-07/D-08 exact-vote recovery. [VERIFIED: codebase grep] |
| Certificate parse/arrival releases lock | Durable accepted-certificate path releases lock | Rejected: current `HandleCertificate` accepts a PubSub message and calls volatile `ClearProposalSlot`; it does not establish the required durable finality boundary. [VERIFIED: codebase grep] |

**Installation:** No package installation is permitted or needed. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md]

## Package Legitimacy Audit

Not applicable: Phase 9 must introduce no external packages. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md]

## Architecture Patterns

### System Architecture Diagram

```text
validated proposal
      |
      v
ContinueProposalAfterSubject
      |
      +--> durable accepted certificate already exists for slot? --> yes: reject/no vote
      |
      v
slot window [first valid -> first-valid + 2s]
      |                        ^
      | candidates validate <= deadline only
      v                        |
deadline reached / candidate sees deadline elapsed
      |
      v
freeze eligible set --IsBetterProposal--> one winner
      |
      v
CreateVote once
      |
      v
local RocksDB /consensus/vote/<slot>
  record: slot + proposal bytes + exact vote bytes + absolute deadline
      |                  |
      | persist fails     +--> existing exact record: reuse only its stored vote
      v
no broadcast, no active in-memory vote
      |
      +--> persist succeeds --> SubmitVote/Publish --> retry exact stored vote while before deadline

durably accepted same-slot certificate
      |
      v
validate certificate and compare GetSlotKey(certificate.proposal())
      |
      v
delete local active-vote record; clear volatile slot bookkeeping
```

### Recommended Project Structure

```text
src/blockchain/
├── Consensus.hpp                 # private record/config/state/helper declarations
├── Consensus.cpp                 # window, RocksDB record, recovery, certificate-release flow
└── impl/proto/Consensus.proto    # local protobuf ActiveVoteRecord schema (if existing proto generation is used)

test/src/blockchain/
├── consensus_pending_lifecycle_test.cpp  # deterministic internal lifecycle/restart/failure tests
└── CMakeLists.txt                         # only if an additional focused target is required
```

### Pattern 1: Freeze before a vote becomes usable

**What:** Treat `ContinueProposalAfterSubject` as admission to a slot window, not authorization to self-vote. It may update the pre-deadline candidate set using the existing comparator, but it must never create a vote until the fixed deadline freezes the set. [VERIFIED: codebase grep] [ASSUMED]

**When to use:** Every subject type because `GetSlotKey` already has generic handler/fallback behavior and `IsBetterProposal` already has a generic non-nonce fallback. [VERIFIED: codebase grep]

**Implementation guidance:**

```cpp
// Source: existing ConsensusManager state/timer pattern. [VERIFIED: codebase grep]
// Use steady_clock only for the live two-second window.
if (slot.window_deadline == steady_clock::time_point{}) {
    slot.window_deadline = now + std::chrono::seconds(2);
}
if (now <= slot.window_deadline && !slot.frozen) {
    AdmitEligibleCandidateLocked(proposal); // delegates to unchanged IsBetterProposal
}
if (now >= slot.window_deadline) {
    FreezeAndStartVoteForWinner(slot_key);
}
```

Use a persisted `system_clock`/Unix-millisecond deadline for the vote's acceptance/re-announcement bound because a `steady_clock::time_point` cannot survive restart. Keep those clock domains explicit; do not serialize a monotonic time point. [ASSUMED]

### Pattern 2: Persist or load exact record under one slot mutex

**What:** Under `proposals_mutex_`, first read `"/consensus/vote/" + GetSlotKey(proposal)` from `db_->GetDataStore()`. A missing key may be written once; an existing key must decode and match the proposed complete record exactly; mismatch returns failure without mutation or broadcast. [VERIFIED: codebase grep] [ASSUMED]

**Why:** `rocksdb::put` replaces a key and does not offer compare-and-set in this wrapper. The manager already serializes its proposal/slot maps with `proposals_mutex_`, so the read/compare/write decision belongs in that critical section for this single-manager lifecycle. [VERIFIED: codebase grep] [ASSUMED]

**Implementation guidance:**

```cpp
// Source: GlobalDB::GetDataStore + storage::rocksdb APIs. [VERIFIED: codebase grep]
ActiveVoteRecord record;
record.set_canonical_slot(slot_key);
record.set_proposal_bytes(Serialize(proposal));
record.set_vote_bytes(Serialize(vote));
record.set_acceptance_deadline_ms(deadline_ms);

auto existing = store->get(KeyForSlot(slot_key));
if (existing.has_value()) {
    return ExistingRecordIsByteExact(existing.value(), record)
             ? UseStoredExactVote(existing.value())
             : outcome::failure(std::errc::operation_not_permitted);
}
BOOST_OUTCOME_TRY(store->put(KeyForSlot(slot_key), Serialize(record)));
return UseStoredExactVote(record);
```

The persisted byte payload must retain `vote_bytes` rather than reconstructing a new `ConsensusVote` with `CreateVote`; a fresh vote receives a fresh timestamp/signature. Validate parsed stored proposal/vote fields before use (slot matches `GetSlotKey(proposal)`, vote proposal/voter/approval fields are coherent, and signature verification succeeds) and fail closed on corrupt storage. [VERIFIED: codebase grep] [ASSUMED]

### Pattern 3: Retry by replay, never replacement

**What:** On initialization, enumerate the local `/consensus/vote/` prefix, validate each record, install an in-memory lock for every valid record, and schedule/attempt only the stored vote while `now_ms < acceptance_deadline_ms`. On expiry remove no record and create no new vote. [VERIFIED: codebase grep] [ASSUMED]

**When to use:** At startup and on each timer wake for unfinalized records. `ConsensusManager::New` starts the timer before normal proposal processing and already calls `RecoverPendingCertificateWork`; add active-vote recovery before or alongside that recovery path. [VERIFIED: codebase grep] [ASSUMED]

The current `GossipPubSub::Publish` call is wrapped by `ConsensusManager::Publish`, which always returns success after calling the transport. Therefore the safe bounded-backoff design is to re-announce the stored vote at every retry cadence before the deadline, rather than trying to infer a transport acknowledgement that this API does not expose. [VERIFIED: codebase grep] [ASSUMED]

### Pattern 4: Release only at a durable same-slot acceptance boundary

**What:** Split volatile `ClearProposalSlot` from persistent-record removal. Call a `ReleaseActiveVoteForAcceptedSlot` helper only after certificate validation/acceptance has succeeded in the durable certificate ingress path, and compare `GetSlotKey(certificate.proposal())` to the active-record key—not proposal ID or subject hash. [VERIFIED: codebase grep] [ASSUMED]

**When to use:** The registered CRDT certificate callback (`CertificateReceived`) is the closest current post-storage ingress; PubSub-only `HandleCertificate`, `FilterCertificate`, malformed records, rejected certificates, and stalled certificates must never delete the vote record. [VERIFIED: codebase grep] [ASSUMED]

The current certificate authority is still the legacy `/cert/<subject-hash>` storage/lookup path; Phase 8 deliberately made `/cert/<slot>` only a future validation predicate. Phase 9 must not publish or make authoritative a slot certificate key. To keep post-restart voting fail-closed after an active record is released, introduce a read-only “accepted certificate for canonical slot” query over existing accepted certificate values (or preserve an equivalent local finalized-slot marker) and have Phase 10 replace its lookup implementation with the authoritative slot key. This is the one planning-level integration point that must be explicit. [VERIFIED: codebase grep] [ASSUMED]

### Anti-Patterns to Avoid

- **Immediate self-vote on each newly best proposal:** Current behavior makes `voted_proposal_ids` a proposal-level memory set rather than a slot-level durable lock; replace it with deadline freeze then one record. [VERIFIED: codebase grep]
- **Re-signing during recovery:** Changes the timestamp and signature and violates the exact-vote rule. [VERIFIED: codebase grep]
- **Writing the active vote through `GlobalDB::Put`:** Replicates local safety state as CRDT data and violates D-05. [VERIFIED: codebase grep]
- **Deleting the record in `ClearProposalSlot` or `HandleCertificate`:** Both are volatile/currently PubSub-adjacent paths; neither alone proves durable acceptance. [VERIFIED: codebase grep]
- **Letting current “best” change after freeze:** Candidate admission after deadline must be rejected even if the timer tick runs later. [ASSUMED]
- **Counting votes before the winner is frozen:** Store/defer them or ignore them until the frozen winner is known; a vote for an early/nonwinning proposal must not contribute to finality. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md] [ASSUMED]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---|---|---|---|
| Durable local KV store | A file, JSON journal, or bridge-specific DB | Existing `storage::rocksdb` via `GlobalDB::GetDataStore()` | Existing storage returns `outcome` errors and uses synchronous writes in its path-based factory. [VERIFIED: codebase grep] |
| Signed vote reconstruction | A second signing/retry protocol | Stored serialized `ConsensusVote` bytes | Exact signed material must survive restart; `CreateVote` creates a new timestamped vote. [VERIFIED: codebase grep] |
| Proposal ranking | Mint-specific ranking branch | Existing `IsBetterProposal` | It already applies transaction-hash/proposal-ID order and generic proposal-ID fallback. [VERIFIED: codebase grep] |
| Background scheduling | A second thread | Existing `StartRoundTimer` plus a due-vote-work helper | The manager already owns cancellation, wakeup, and lifecycle teardown for a timer thread. [VERIFIED: codebase grep] |
| Isolated signing storage | OS keychain setup in tests | `MemorySecureStorage` factory in the lifecycle fixture | Phase 8 established this pattern to avoid platform storage/teardown instability. [VERIFIED: .planning/phases/08-canonical-slot-certificate-binding/08-01-SUMMARY.md] |

**Key insight:** The phase is a state-machine correction at existing consensus boundaries, not a new protocol or persistence subsystem. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md]

## Common Pitfalls

### Pitfall 1: Race between candidate arrival and timer close

**What goes wrong:** A proposal validated after the two-second deadline is added because the timer did not run at the exact deadline. [ASSUMED]

**Why it happens:** The existing timer has a minimum 500 ms cadence, so timer wake timing alone cannot define eligibility. [VERIFIED: codebase grep]

**How to avoid:** Record the monotonic deadline at the first valid proposal and enforce it during candidate admission as well as in due-work processing; freeze while holding `proposals_mutex_`. [VERIFIED: codebase grep] [ASSUMED]

**Warning signs:** A test can force `now > deadline`, submit a better candidate, and observe its proposal ID replace the frozen winner. [ASSUMED]

### Pitfall 2: Persistence failure leaves a usable volatile vote

**What goes wrong:** Signing, setting `voted_proposal_ids`, or self-handling occurs before the RocksDB write result is checked. A subsequent retry then treats the vote as cast although no recovery record exists. [ASSUMED]

**Why it happens:** Current code adds the proposal ID to `voted_proposal_ids` before `CreateVote`/`SubmitVote`, and no durable write exists. [VERIFIED: codebase grep]

**How to avoid:** Build candidate vote bytes locally, write/check the record, then install usable lock state and publish. On any write/serialization failure erase candidate-only state and do not call `SubmitVote` or `HandleVote`. [VERIFIED: codebase grep] [ASSUMED]

**Warning signs:** A fault-injected put failure produces a local self-vote, a slot `voted` marker, or any persisted record. [ASSUMED]

### Pitfall 3: Expiry is mistaken for authorization to vote again

**What goes wrong:** Expiration cleanup deletes the record or `SlotState`, so a late/different proposal gets a replacement vote. [ASSUMED]

**Why it happens:** Existing pending-proposal expiry invokes `ClearProposalSlot`, and current `ClearProposalSlot` removes the entire volatile slot map. [VERIFIED: codebase grep]

**How to avoid:** Expiry changes only re-announcement scheduling. Keep the on-disk record and an in-memory locked/finalized state until accepted same-slot certificate release; do not reuse pending-proposal TTL cleanup for an active vote. [VERIFIED: codebase grep] [ASSUMED]

**Warning signs:** After forcing deadline expiry, a competing candidate causes a new call to `CreateVote` or replaces the stored record. [ASSUMED]

### Pitfall 4: Certificate handling releases on the wrong identity or too early

**What goes wrong:** A certificate with a different proposal ID but the same canonical slot fails to release, or an unpersisted/invalid/other-slot certificate deletes a lock. [ASSUMED]

**Why it happens:** Existing paths mix subject-hash legacy lookup, PubSub `HandleCertificate`, CRDT callback receipt, and volatile `ClearProposalSlot`. [VERIFIED: codebase grep]

**How to avoid:** Validate first; only in durable accepted ingress derive the slot from `certificate.proposal()` with `GetSlotKey`; delete only the matching active-vote key; then clear volatile state. Retain the active record on parse/reject/stall/other-slot events. [VERIFIED: codebase grep] [ASSUMED]

**Warning signs:** A same-slot certificate using a different proposal leaves a record, or a malformed/other-slot certificate removes one. [ASSUMED]

### Pitfall 5: Tests hide timing and transport behavior

**What goes wrong:** Sleeping two seconds in gtest produces flaky tests, while `Publish` looks successful even though it exposes no acknowledgement. [VERIFIED: codebase grep] [ASSUMED]

**How to avoid:** Extend the existing friend test-access class with narrow clock/due-work and active-vote persistence/publication observability. Use forced deadlines and explicit `ProcessDueVoteWork` calls; test repeated exact replay rather than a non-existent PubSub acknowledgement. [VERIFIED: codebase grep] [ASSUMED]

## Code Examples

### Existing deterministic ranking that must remain unchanged

```cpp
// Source: src/blockchain/Consensus.cpp::IsBetterProposal [VERIFIED: codebase grep]
if (cand_hash == curr_hash) {
    return candidate.proposal_id() < current.proposal_id();
}
return BestHash(curr_hash, cand_hash) == cand_hash;
```

### Existing durable local storage shape

```cpp
// Source: src/blockchain/ValidatorRegistry.cpp and storage/rocksdb/rocksdb.cpp
// [VERIFIED: codebase grep]
crdt::GlobalDB::Buffer key;
key.put(std::string("/consensus/vote/") + slot);
crdt::GlobalDB::Buffer value;
value.put(serialized_record);

auto put_result = db_->GetDataStore()->put(key, value);
if (put_result.has_error()) {
    return outcome::failure(put_result.error());
}
```

### Existing exact-vote signing boundary

```cpp
// Source: src/blockchain/Consensus.cpp::CreateVote / SubmitVote [VERIFIED: codebase grep]
auto vote = CreateVote(winner.proposal_id(), account_address_, true, signer_);
// Phase 9 inserts: serialize + persist-or-load-exact record here.
// Only then may SubmitVote(stored_vote) run.
```

## State of the Art

| Old Approach | Current Phase 9 Approach | When Changed | Impact |
|---|---|---|---|
| Immediate self-vote for whichever proposal is currently best | Fixed two-second eligible-set freeze, then one durable vote | Phase 9 | Removes proposal-arrival-order self-vote replacement. [VERIFIED: codebase grep] [ASSUMED] |
| Volatile `voted_proposal_ids` | Per-slot local RocksDB active-vote record | Phase 9 | Allows exact restart recovery and preserves the lock after expiry. [VERIFIED: codebase grep] [ASSUMED] |
| PubSub certificate handling clears volatile slot state | Only durable accepted same-slot certificate may remove active record | Phase 9 | Prevents receipt/parse failure from unlocking the validator. [VERIFIED: codebase grep] [ASSUMED] |

**Deprecated/outdated:** `SlotState::voted_proposal_ids` as the authority for whether the validator has voted is insufficient after this phase; it may remain only as derived transient bookkeeping, never as the durable decision source. [VERIFIED: codebase grep] [ASSUMED]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|---|---|---|
| A1 | A new local protobuf `ActiveVoteRecord` is the smallest safe encoding and can be added to the existing consensus proto generation without a build-system change. | Standard Stack / Patterns | Planner may need a separate local record codec or generated-source task. |
| A2 | `"/consensus/vote/" + canonical_slot` is the correct generic local prefix. | Summary / Patterns | Prefix may conflict with an undocumented storage convention; verify with maintainers/codebase search before implementation. |
| A3 | A single manager mutex is sufficient for record create-if-absent in the supported one-validator-process model. | Pattern 2 | Multiple concurrently running processes against one RocksDB could race and require a stronger storage primitive. |
| A4 | The current CRDT callback is the appropriate present durable-acceptance hook once validation succeeds. | Pattern 4 | Callback ordering may not itself prove the required persistence boundary; implementation must validate this against `CrdtDatastore` callback order. |
| A5 | A read-only scan of accepted legacy `/cert/` records, or an equivalent local finalized marker, is required to preserve post-restart no-revote after active-record deletion until Phase 10's authoritative slot key exists. | Pattern 4 | Omitting it can let a restarted node vote in a finalized slot whose winning proposal has a different subject hash. |
| A6 | Re-announcing every bounded retry interval is the safe interpretation of send failure because the current PubSub `Publish` path has no acknowledgement result. | Pattern 3 | Network layer may later expose delivery status; then retries can be reduced but must remain exact-vote only. |

## Open Questions

1. **What exact event proves the current legacy certificate record is durably accepted?**
   - What we know: `FilterCertificate` validates incoming CRDT elements; `CertificateReceived` performs validation/handler work after callback registration, whereas `HandleCertificate` is a keyless PubSub path that clears volatile slot state. [VERIFIED: codebase grep]
   - What's unclear: The inspected files do not prove the precise write-versus-callback ordering inside `CrdtDatastore`. [VERIFIED: codebase grep]
   - Recommendation: Planner should include a code-reading task that confirms callback ordering, then place vote-record deletion only after that proven boundary plus `Check::Approve`; never at `HandleCertificate`. [ASSUMED]

2. **How does a restart block a slot after the vote record is correctly deleted?**
   - What we know: Phase 8 preserves current certificate persistence/lookup at `/cert/<subject-hash>` and makes `/cert/<slot>` a future predicate, while Phase 9 requires deletion after same-slot certificate finality. [VERIFIED: .planning/phases/08-canonical-slot-certificate-binding/08-01-SUMMARY.md]
   - What's unclear: A different winning proposal can have the same canonical slot but a different subject hash, so current direct lookup is not enough by itself. [VERIFIED: codebase grep] [ASSUMED]
   - Recommendation: Implement a generic read-only same-slot accepted-certificate query over current records (or a local finalized marker with equivalent restart semantics), explicitly marked as a Phase 10 migration seam and not as new certificate authority. [ASSUMED]

3. **How will persistence failure and exact publication be observed deterministically in a focused unit test?**
   - What we know: The existing lifecycle test already has friend access, a real temporary RocksDB, and manager constructors; `Publish` wraps a void PubSub publish call and returns success after invocation. [VERIFIED: codebase grep]
   - What's unclear: There is no current storage-failure or outbound-message observation hook. [VERIFIED: codebase grep]
   - Recommendation: Add the narrowest private/test-access seam for a local active-vote store and/or announcement counter, keeping production calls routed to `GetDataStore()` and `SubmitVote`. [ASSUMED]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|---|---|---|---|---|
| Existing CMake Release build tree | Build focused consensus tests | ✓ | `build/OSX/Release` | — [VERIFIED: local build probe] |
| CTest targets | Focused validation | ✓ | `consensus_slot_key_test`, `consensus_pending_lifecycle_test` listed | — [VERIFIED: local build probe] |
| Local RocksDB through CRDT fixture | Durability/restart tests | ✓ | repository fixture | — [VERIFIED: codebase grep] |
| In-memory secure storage | Signing tests | ✓ | `MemorySecureStorage` | — [VERIFIED: codebase grep] |

**Missing dependencies with no fallback:** None. [VERIFIED: local build probe]

**Missing dependencies with fallback:** None. [VERIFIED: local build probe]

## Validation Architecture

### Test Framework

| Property | Value |
|---|---|
| Framework | GoogleTest through CTest. [VERIFIED: codebase grep] |
| Config file | CMake test hierarchy; focused blockchain target list in `test/src/blockchain/CMakeLists.txt`. [VERIFIED: codebase grep] |
| Quick run command | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` [VERIFIED: local build probe] |
| Full suite command | `ctest --test-dir build/OSX/Release --output-on-failure` [VERIFIED: local build probe] |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|---|---|---|---|---|
| VOTE-01 | First valid candidate opens exactly one two-second window; winner is frozen by existing comparison; late/stalled candidates cannot affect it. | deterministic unit/lifecycle | focused consensus CTest command | Extend `consensus_pending_lifecycle_test.cpp` [VERIFIED: codebase grep] |
| VOTE-02 | No publish/usable state after write failure; successful write contains slot, full proposal, exact vote bytes, and absolute deadline before first announce. | unit with injected store failure + RocksDB integration | focused consensus CTest command | Extend lifecycle test; direct RocksDB test is available if needed. [VERIFIED: codebase grep] |
| VOTE-03 | Restart reuses only identical stored bytes; record collision rejects different vote; retries stop at deadline without replacement. | lifecycle restart + forced due-work | focused consensus CTest command | Extend lifecycle test [VERIFIED: codebase grep] |
| VOTE-04 | Expiry retains lock; invalid/other-slot/unstored certificate does not delete; durably accepted same-slot certificate—including a different proposal—does. | lifecycle/integration | focused consensus CTest command | Extend lifecycle test [VERIFIED: codebase grep] |

### Sampling Rate

- **Per task commit:** `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` followed by the focused CTest command. [VERIFIED: .planning/phases/08-canonical-slot-certificate-binding/08-01-SUMMARY.md]
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure`. [VERIFIED: local build probe]
- **Phase gate:** Focused suite passes, `git diff --check` passes, and regression coverage includes all four VOTE requirements. [ASSUMED]

### Wave 0 Gaps

- [ ] Extend `test/src/blockchain/consensus_pending_lifecycle_test.cpp` and its friend accessor with deterministic active-vote clock/store/recovery inspection. [VERIFIED: codebase grep] [ASSUMED]
- [ ] Add an explicit fault path for RocksDB write failure and exact-announcement observation; the current concrete RocksDB/PubSub APIs do not expose either directly. [VERIFIED: codebase grep] [ASSUMED]
- [ ] Preserve/extend `consensus_slot_key_test.cpp` only for comparator/slot invariants that risk regression; do not alter `MintTransactionV2::GetSlotID()`. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md]

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---|---|---|
| V2 Authentication | Yes | Retain validator identity/signature verification already performed by `HandleVote` and registry lookup. [VERIFIED: codebase grep] |
| V3 Session Management | No | No browser/session state is introduced. [VERIFIED: .planning/phases/09-durable-one-vote-finality/09-CONTEXT.md] |
| V4 Access Control | Yes | Treat the active-vote datastore as node-local; only `ConsensusManager` creates/removes records. [ASSUMED] |
| V5 Input Validation | Yes | Parse stored protobuf defensively; recompute slot; require vote/proposal identity coherence and signature validation; reject corrupt, mismatched, late, or nonwinning input. [VERIFIED: codebase grep] [ASSUMED] |
| V6 Cryptography | Yes | Reuse `CreateVote`, `VoteSigningBytes`, and `GeniusAccount::VerifySignature`; never hand-roll signing or hashes. [VERIFIED: codebase grep] |

### Known Threat Patterns for consensus persistence

| Pattern | STRIDE | Standard Mitigation |
|---|---|---|
| Disk/record tampering or corruption | Tampering | Validate protobuf parse, canonical slot, proposal ID, vote fields, and vote signature before re-announcement; fail closed and retain no usable reconstructed vote. [VERIFIED: codebase grep] [ASSUMED] |
| Same-slot replacement attempt | Tampering | Existing key accepts only byte-identical record; reject overwrite and broadcast nothing new. [ASSUMED] |
| Replay after deadline | Replay | Re-announce only while absolute deadline is in the future; retain the expired record as a no-revote lock. [ASSUMED] |
| Lock deletion from unauthenticated/undurable certificate | Elevation of privilege | Require validated durable same-slot acceptance before delete; parse/receipt/stall/rejection do not unlock. [ASSUMED] |
| Candidate flooding near deadline | Denial of service | Freeze eligibility at deadline under the existing mutex; pending lifecycle limits remain in force for validation backlog. [VERIFIED: codebase grep] [ASSUMED] |

## Sources

### Primary (HIGH confidence)

- `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` — current immediate self-vote, `SlotState`, ranking, timer, vote publication, certificate ingress, and volatile cleanup. [VERIFIED: codebase grep]
- `src/storage/rocksdb/rocksdb.hpp`, `src/storage/rocksdb/rocksdb.cpp`, and `src/crdt/globaldb/globaldb.hpp` — local store API, synchronous path write options, and direct datastore exposure. [VERIFIED: codebase grep]
- `src/blockchain/impl/proto/Consensus.proto` — existing proposal/vote/certificate protobuf schema. [VERIFIED: codebase grep]
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp`, `test/src/blockchain/consensus_slot_key_test.cpp`, and `test/src/storage/rocksdb/rocksdb_integration_test.cpp` — focused fixture, test-access pattern, canonical-slot coverage, and RocksDB operations. [VERIFIED: codebase grep]
- `.planning/phases/09-durable-one-vote-finality/09-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/ROADMAP.md`, and `.planning/phases/08-canonical-slot-certificate-binding/08-01-SUMMARY.md` — locked scope, requirements, and Phase 8 guardrails. [VERIFIED: codebase grep]

### Secondary (MEDIUM confidence)

- None.

### Tertiary (LOW confidence)

- None; implementation recommendations requiring uninspected callback ordering or a chosen local record schema are listed in the Assumptions Log. [ASSUMED]

## Metadata

**Project Constraints (from AGENTS.md):** No `AGENTS.md` file exists in the repository root or its first two directory levels; no project-defined `.codex/skills` or `.agents/skills` files were found. [VERIFIED: local filesystem probe]

**Confidence breakdown:**

- Standard stack: HIGH — all required facilities are already in this repository; no dependency recommendation is made. [VERIFIED: codebase grep]
- Architecture: HIGH — the entry points and storage/timer seams are directly implemented in `ConsensusManager`; durable-certificate callback ordering remains explicitly gated as A4. [VERIFIED: codebase grep]
- Pitfalls: HIGH — immediate voting, volatile cleanup, and timer cadence are observable in current code; future-schema/test-hook choices are flagged as assumptions. [VERIFIED: codebase grep]

**Research date:** 2026-08-20  
**Valid until:** 2026-09-19 (stable repository-local architecture; re-check before implementation if consensus code changes).
