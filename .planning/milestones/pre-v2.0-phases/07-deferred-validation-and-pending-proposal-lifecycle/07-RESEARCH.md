# Phase 07: Deferred Validation and Pending Proposal Lifecycle - Research

**Researched:** 2026-06-16
**Domain:** C++ consensus runtime lifecycle, local pending proposal state, transaction status semantics
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

### Pending Result Contract

- **D-01:** Subject validation should return a structured result rather than only `ConsensusManager::Check`.
  The result must preserve `Approve`, terminal `Reject`, local `Pending`, and local infrastructure
  `Stalled` semantics.
- **D-02:** `Pending` must carry typed dependency keys plus optional retry metadata. Dependency keys
  are local bookkeeping only; they are not broadcast and do not affect quorum.
- **D-03:** Use typed dependency keys from the beginning to avoid collisions. The first required type
  is `Certificate(tx_hash)`, used when a proposal is waiting for a predecessor transaction
  certificate. Future types may include registry snapshots, CRDT/datastore keys, or RPC receipts.
- **D-04:** For multiple missing dependencies, retry on any dependency arrival. Revalidation is
  incremental: if dependencies remain missing, the handler returns `Pending` again with the remaining
  keys.

### Retry Policy

- **D-05:** Transient failures without explicit dependency events use conservative scheduled backoff:
  retry after roughly 1s, 2s, 5s, then every 10s until the pending TTL expires.
- **D-06:** Dependency-triggered retries wake immediately, but each proposal has a small minimum retry
  interval to prevent retry storms when many dependency events arrive quickly.
- **D-07:** Retrying must be idempotent. A proposal can emit at most one local Approval vote per
  proposal/slot and must not double-count votes, corrupt transaction state, or duplicate cleanup.

### Capacity Policy

- **D-08:** When the pending pool reaches count or byte limits, fail closed for new pending proposals.
  Existing pending entries keep their TTL; do not evict older valid pending work just to admit newer
  work.
- **D-09:** Enforce both global and per-proposer limits. This prevents one proposer from filling the
  node's pending pool while still bounding total memory.
- **D-10:** Start with small conservative defaults: global 1,024 pending proposals, per-proposer 64,
  and 64 MB total retained pending proposal bytes. Values can be adjusted after production data.
- **D-11:** Admission failure due to capacity is not a network-level rejection vote. It is a local
  resource decision and should be logged/observable.

### Expiry Behavior

- **D-12:** Default pending TTL is three minutes. Tests should inject a shorter TTL, normally ten
  seconds.
- **D-13:** When a local outgoing transaction's proposal reaches TTL without a conclusive result, mark
  it `UNCONFIRMED`. `FAILED` is reserved for locally proven invalid transactions.
- **D-14:** Do not automatically resubmit `UNCONFIRMED` outgoing transactions in this phase. Surface
  the state; caller or queue policy decides whether and when to resubmit.
- **D-15:** For remote embedded transactions temporarily tracked as `VERIFYING`, expiry removes the
  temporary transaction record rather than keeping `UNCONFIRMED` state.
- **D-16:** Expiry must remove proposal state, typed dependency indexes, queued votes, retry metadata,
  capacity accounting, and temporary transaction tracking.

### the agent's Discretion

- Exact C++ type names and storage layout for the structured pending result and dependency key.
- Exact minimum retry interval for dependency-triggered throttling.
- Whether small defaults are compile-time constants, constructor config, or both, provided tests can
  inject lower TTL/limits deterministically.
- Exact log message wording and metrics counter names, following existing `ConsensusManagerLogger()`
  and `TransactionManagerLogger()` patterns.

### Deferred Ideas (OUT OF SCOPE)

- Signed Reject votes, rejection certificates, negative quorum, and validator reputation adjudication
  are explicitly out of scope for this phase.
- Automatic resubmission of `UNCONFIRMED` outgoing transactions is out of scope. This phase only
  exposes the state cleanly.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PEND-01 | Structured deferred validation can return `Pending` with dependency keys while preserving `Approve`, terminal `Reject`, and local `Stalled`. | Use a new local result type around the existing four-state `Check` semantics. [VERIFIED: .planning/REQUIREMENTS.md; src/blockchain/Consensus.hpp] |
| PEND-02 | Pending is retained locally and never broadcast or counted toward quorum. | `HandleProposal()` only broadcasts after `ContinueProposalAfterSubject()` creates an approval vote, so pending should stay before that call. [VERIFIED: src/blockchain/Consensus.cpp] |
| PEND-03 | Dependency-triggered retry resumes every proposal waiting on a dependency. | Replace the current `pending_by_subject_hash_` map with a typed dependency index keyed by local dependency keys. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp] |
| PEND-04 | Scheduled transient retry uses bounded backoff. | Extend the existing timer thread to also scan due pending retries, or add a dedicated pending timer using the same `condition_variable` pattern. [VERIFIED: src/blockchain/Consensus.cpp] |
| PEND-05 | Pending proposals expire after default three-minute TTL with test injection. | Add pending lifecycle config to `ConsensusManager`; current code has injectable timing for timestamp, round duration, skew, and certificate delay but not TTL. [VERIFIED: src/blockchain/Consensus.hpp] |
| PEND-06 | Resource bounds and cleanup remove all pending proposal state. | `ClearProposalSlot()` already removes proposals, subject-hash pending entries, and pending votes; it must also remove dependency indexes, retry metadata, TTL accounting, and byte/proposer counters. [VERIFIED: src/blockchain/Consensus.cpp] |
| PEND-07 | Retrying is idempotent. | Existing `SlotState::voted` and `ProposalState::seen_voters` prevent duplicate self-votes and duplicate vote counting; retries must reuse those structures instead of bypassing them. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp] |
| TXSTATE-01 | Inconclusive timeout/TTL uses `UNCONFIRMED`; `FAILED` is reserved for proven invalid transactions. | `TransactionStatus` lacks `UNCONFIRMED`, and `OnProposalTimeoutCleanup()` currently transitions `VERIFYING` to `FAILED`. [VERIFIED: src/account/TransactionManager.hpp; src/account/TransactionManager.cpp] |
</phase_requirements>

## Summary

Phase 07 should be planned as a local runtime-state refactor inside `ConsensusManager`, plus a targeted transaction lifecycle change in `TransactionManager`. The wire schema should not change because dependency keys and retry metadata are explicitly local bookkeeping, and `Consensus.proto` currently contains only subject/proposal/vote/certificate messages with no pending payload fields. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md; src/blockchain/impl/proto/Consensus.proto]

The current implementation already has useful primitives: `ConsensusManager::Check` has `Approve`, `Reject`, `Pending`, and `Stalled`; `HandleProposal()` queues `Pending`; `ResumeProposalHandling()` retries a pending proposal; `CertificateReceived()` observes certificate arrivals; `ClearProposalSlot()` removes proposal and pending-vote state; and `SlotState::voted` plus `ProposalState::seen_voters` provide idempotency foundations. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp]

The main missing pieces are structured pending metadata, typed dependency indexing, scheduled retry/TTL scanning, capacity accounting, and a local outgoing transaction state that distinguishes inconclusive expiry from invalidity. [VERIFIED: .planning/REQUIREMENTS.md; src/account/TransactionManager.hpp; src/account/TransactionManager.cpp]

**Primary recommendation:** Implement a `ValidationResult` plus `PendingDependencyKey` local API, store pending entries in one canonical `pending_entries_` map keyed by `proposal_id`, index those entries by typed dependency keys, and route every retry back through the same validation-to-`ContinueProposalAfterSubject()` path used by first handling. [VERIFIED: src/blockchain/Consensus.cpp] [ASSUMED]

## Project Constraints (from AGENTS.md)

No repo-root `AGENTS.md` exists, so there are no additional project-specific directives from that file. [VERIFIED: shell `test -f AGENTS.md` returned no output]

Project-local `.codex/skills/` and `.agents/skills/` directories were not found. [VERIFIED: shell `find .codex/skills .agents/skills -maxdepth 2 -name SKILL.md` returned no output]

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| Structured validation outcome | Consensus runtime (`ConsensusManager`) | Subject handlers | `ConsensusManager::SubjectHandler` currently returns `Check`; planner should change that contract and adapt handlers. [VERIFIED: src/blockchain/Consensus.hpp; src/account/TransactionManager.cpp] |
| Certificate dependency key | Consensus runtime | Transaction validation | Missing predecessor certificates are detected inside `CheckTransactionReplayProtection()`, but dependency indexing and wakeups belong in consensus. [VERIFIED: src/account/TransactionManager.cpp; src/blockchain/Consensus.cpp] |
| Dependency-triggered retry | Consensus runtime | Blockchain facade | `CertificateReceived()` is the runtime event source; `Blockchain::TryResumeProposal()` is only a facade today. [VERIFIED: src/blockchain/Consensus.cpp; src/blockchain/impl/Blockchain.cpp] |
| Scheduled transient retry and TTL expiry | Consensus runtime | Transaction manager cleanup callback | The consensus timer already drives certificate work; expiry must also fire cleanup callbacks for transaction tracking. [VERIFIED: src/blockchain/Consensus.cpp; src/account/TransactionManager.cpp] |
| Pending capacity limits | Consensus runtime | Logging/observability | Pending admission is local memory policy and should not emit votes. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Local transaction inconclusive state | Transaction manager | Consensus cleanup callback | `TransactionStatus` and `ChangeTransactionState()` own transaction lifecycle side effects. [VERIFIED: src/account/TransactionManager.hpp; src/account/TransactionManager.cpp] |
| Wire/quorum behavior | Existing consensus protocol | - | Pending and reject remain local; only approval votes count in current tally logic. [VERIFIED: .planning/notes/deferred-consensus-validation.md; src/blockchain/Consensus.cpp] |

## Standard Stack

### Core

| Library / Facility | Version | Purpose | Why Standard |
|--------------------|---------|---------|--------------|
| C++17 standard library | C++17 project constraint | Local value types, `std::chrono`, maps, sets, mutexes, and condition variables. | The codebase is C++17-only and already uses these primitives in `ConsensusManager`. [VERIFIED: .planning/codebase/ARCHITECTURE.md; src/blockchain/Consensus.hpp] |
| Boost.Outcome wrapper | In-repo `src/outcome` | Fallible result propagation via `outcome::result<T>`. | Consensus and transaction APIs already use outcome results instead of exceptions. [VERIFIED: src/blockchain/Consensus.hpp; src/account/TransactionManager.hpp; .planning/codebase/ARCHITECTURE.md] |
| Protobuf | 7.34.0 generated output | Existing consensus and transaction wire schemas. | Keep pending lifecycle local; only use Protobuf if existing subject/certificate messages are decoded. [VERIFIED: build/OSX/Debug/generated/account/proto/SGTransaction.pb.cc; src/blockchain/impl/proto/Consensus.proto] |
| GTest/GMock | Existing CMake targets | Unit and integration validation. | `addtest()` links `GTest::gtest_main` and `GTest::gmock_main` and emits XML output. [VERIFIED: cmake/functions.cmake] |
| CMake/CTest | CMake 3.31.4, CTest 3.31.4 | Build and test execution. | Configured tests are discoverable in `build/OSX/Debug` with 42 CTest entries. [VERIFIED: shell `cmake --version`; shell `ctest -N` in build/OSX/Debug] |

### Supporting

| Library / Facility | Version | Purpose | When to Use |
|--------------------|---------|---------|-------------|
| `base::Logger` / spdlog backend | Existing project logger | Pending admission failures, retry/expiry events, validation outcomes. | Follow `ConsensusManagerLogger()` and `TransactionManagerLogger()` patterns. [VERIFIED: src/blockchain/Consensus.cpp; src/account/TransactionManager.cpp] |
| `ValidatorRegistry` | Existing project component | Registry loading and vote-weight tally. | Do not change quorum rules; preserve existing tally and approval-only behavior. [VERIFIED: src/blockchain/Consensus.cpp; .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| `crdt::GlobalDB` certificate callbacks | Existing project component | Certificate arrival event source. | Use `CertificateReceived()` to wake `Certificate(tx_hash)` dependencies. [VERIFIED: src/blockchain/Consensus.cpp] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Local `ValidationResult` struct | Extend `Consensus.proto` with pending fields | Rejected because pending dependency keys are local-only and must not be broadcast. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Typed dependency key type | Plain strings like `"cert:" + hash` | Rejected because the phase explicitly requires typed keys to avoid collisions. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Reuse `pending_by_subject_hash_` only | Index pending by proposal subject hash | Rejected because out-of-order predecessor certificate arrival needs `Certificate(previous_hash)` to wake `tx2`, not `tx2`'s own hash. [VERIFIED: .planning/notes/deferred-consensus-validation.md] |
| Real sleeps in tests | Injected TTL/config and direct wake functions | Avoid real wall-clock sleeps where possible; existing tests use deterministic helpers and the phase requires injectable TTL. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md; test/testutil/wait_condition.hpp] |

**Installation:** No external packages should be installed for this phase. [VERIFIED: local codebase inspection]

**Version verification:** No new package registry checks are required because the phase should use existing in-repo and configured dependencies only. [VERIFIED: local codebase inspection]

## Package Legitimacy Audit

No external packages are recommended or installed for this phase, so the package legitimacy gate is not applicable. [VERIFIED: local codebase inspection]

## Architecture Patterns

### System Architecture Diagram

```text
Incoming ConsensusProposal
        |
        v
ConsensusManager::HandleProposal()
        |
        v
Basic proposal/registry/subject checks
        |
        v
SubjectHandler returns ValidationResult
        |
        +--> Approve ------------------------------+
        |                                          |
        |                                          v
        |                         ContinueProposalAfterSubject()
        |                                          |
        |                                          v
        |                         one local approval vote at most
        |
        +--> Reject
        |       |
        |       v
        |   local terminal cleanup, no reject vote
        |
        +--> Stalled
        |       |
        |       v
        |   local infrastructure logging, no vote
        |
        +--> Pending(dependency keys, retry metadata)
                |
                v
        Admit to bounded pending_entries_
                |
                +--> dependency index: PendingDependencyKey -> proposal ids
                +--> retry schedule: next_retry_at / backoff step
                +--> expiry: created_at + ttl
                +--> capacity: global, per-proposer, retained bytes

CertificateReceived(Certificate(tx_hash)) or retry timer
        |
        v
Take due proposal ids and re-run same SubjectHandler path
        |
        +--> Approve -> ContinueProposalAfterSubject()
        +--> Pending -> update dependencies/schedule/accounting
        +--> Reject/expiry/certificate -> cleanup indexes, votes, retry metadata, temp tx state
```

### Recommended Project Structure

```text
src/blockchain/
├── Consensus.hpp                         # Add ValidationResult, PendingDependencyKey, config/accessors
├── Consensus.cpp                         # Implement pending admission, dependency wakeup, retry/TTL scans, cleanup
├── Blockchain.hpp                        # Expose typed dependency resume/config if needed
└── impl/Blockchain.cpp                   # Thin facade only

src/account/
├── TransactionManager.hpp                # Add UNCONFIRMED and cleanup helpers
└── TransactionManager.cpp                # Return Certificate(previous_hash) pending and change timeout semantics

test/src/blockchain/
├── consensus_subject_test.cpp            # Keep subject/wire invariants
├── consensus_slot_key_test.cpp           # Preserve slot-key/idempotency behavior
└── consensus_pending_lifecycle_test.cpp  # New focused tests for dependency, TTL, capacity, retry

test/src/account/
└── transaction_manager_pending_lifecycle_test.cpp # New TXSTATE and predecessor-certificate tests
```

### Pattern 1: Local Structured Validation Result

**What:** Replace or wrap `SubjectHandler` return type with a local result object that carries `Check`, dependency keys, and retry metadata. [VERIFIED: src/blockchain/Consensus.hpp] [ASSUMED]

**When to use:** Use for every subject handler result so `Approve`, `Reject`, `Pending`, and `Stalled` remain explicit. [VERIFIED: .planning/REQUIREMENTS.md]

**Example:**

```cpp
// Source: derived from src/blockchain/Consensus.hpp existing Check and SubjectHandler patterns.
struct PendingDependencyKey
{
    enum class Type
    {
        Certificate,
        RegistrySnapshot,
        DatastoreKey,
        RpcReceipt
    };

    Type        type;
    std::string value;

    bool operator==( const PendingDependencyKey &other ) const = default;
};

struct ValidationResult
{
    ConsensusManager::Check                   check{ ConsensusManager::Check::Reject };
    std::vector<PendingDependencyKey>         dependencies;
    std::optional<std::chrono::milliseconds> retry_after;

    static ValidationResult Approve();
    static ValidationResult Reject();
    static ValidationResult Stalled();
    static ValidationResult Pending( std::vector<PendingDependencyKey> deps,
                                     std::optional<std::chrono::milliseconds> retry = std::nullopt );
};
```

### Pattern 2: Canonical Pending Entry plus Secondary Indexes

**What:** Store one canonical pending entry per `proposal_id`; store dependency indexes as secondary maps from dependency key to proposal ids. [VERIFIED: src/blockchain/Consensus.hpp] [ASSUMED]

**When to use:** Use for dependency-triggered retry, scheduled retry, TTL cleanup, and capacity accounting. [VERIFIED: .planning/REQUIREMENTS.md]

**Example:**

```cpp
// Source: derived from src/blockchain/Consensus.hpp current pending_proposals_ and pending_by_subject_hash_ maps.
struct PendingProposalEntry
{
    Proposal proposal;
    std::vector<PendingDependencyKey> dependencies;
    std::chrono::steady_clock::time_point admitted_at;
    std::chrono::steady_clock::time_point expires_at;
    std::chrono::steady_clock::time_point next_retry_at;
    std::chrono::steady_clock::time_point last_retry_at;
    std::size_t retained_bytes = 0;
    std::size_t backoff_step = 0;
};

std::unordered_map<std::string, PendingProposalEntry> pending_entries_;
std::unordered_map<PendingDependencyKey, std::unordered_set<std::string>, PendingDependencyKeyHash>
    pending_by_dependency_;
std::unordered_map<std::string, std::size_t> pending_count_by_proposer_;
```

### Pattern 3: Retry through the Same First-Validation Path

**What:** Retry should re-run the subject handler and then branch through the same helper used by `HandleProposal()`. [VERIFIED: src/blockchain/Consensus.cpp]

**When to use:** Use for dependency wakeups and scheduled retry due entries. [VERIFIED: .planning/REQUIREMENTS.md]

**Example:**

```cpp
// Source: based on src/blockchain/Consensus.cpp HandleProposal(),
// ResumeProposalHandling(), and ContinueProposalAfterSubject().
void ConsensusManager::RetryPendingProposal( const std::string &proposal_id )
{
    auto proposal = TakeOrCopyPendingProposal( proposal_id );
    if ( !proposal )
    {
        return;
    }

    auto result = ValidateProposalSubject( *proposal );
    if ( result.check == Check::Approve )
    {
        RemovePendingProposal( proposal_id );
        ContinueProposalAfterSubject( *proposal );
        return;
    }
    if ( result.check == Check::Pending )
    {
        UpdatePendingProposal( *proposal, result );
        return;
    }
    RemovePendingProposal( proposal_id );
}
```

### Pattern 4: Transaction Expiry State Is Not Failure

**What:** Add `UNCONFIRMED` to `TransactionStatus`, route local outgoing TTL expiry there, and erase remote temporary `VERIFYING` entries instead of marking them failed. [VERIFIED: src/account/TransactionManager.hpp; src/account/TransactionManager.cpp]

**When to use:** Use only for pending TTL expiry without a conclusive consensus result; keep proven-invalid validation rejects as `FAILED`. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]

**Example:**

```cpp
// Source: derived from src/account/TransactionManager.hpp TransactionStatus
// and src/account/TransactionManager.cpp OnProposalTimeoutCleanup().
enum class TransactionStatus : uint8_t
{
    CREATED,
    SENDING,
    CONFIRMED,
    VERIFYING,
    UNCONFIRMED,
    FAILED,
    INVALID
};
```

### Anti-Patterns to Avoid

- **Broadcasting pending or reject as votes:** Current `HandleVote()` ignores non-approval votes, and Phase 07 keeps pending/reject local. [VERIFIED: src/blockchain/Consensus.cpp; .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]
- **Indexing dependencies by the pending proposal's own subject hash:** This cannot wake `tx2` when `tx1`'s certificate arrives. [VERIFIED: .planning/notes/deferred-consensus-validation.md]
- **Calling `ContinueProposalAfterSubject()` before pending resolution:** That helper can self-vote; pending must remain before this point. [VERIFIED: src/blockchain/Consensus.cpp]
- **Creating a separate retry path that writes votes directly:** This bypasses `SlotState::voted` and `seen_voters` safeguards. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp]
- **Marking inconclusive expiry as `FAILED`:** The phase explicitly reserves `FAILED` for proven invalid transactions. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Pending dependency identity | Ad hoc string prefixes | Typed `PendingDependencyKey` value type | Prevents collisions and supports future dependency classes. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Retry scheduling | Detached sleep threads per proposal | One manager-owned scheduler using existing timer/condition variable style | Avoids unbounded threads and centralizes shutdown. [VERIFIED: src/blockchain/Consensus.cpp] |
| Cleanup dispatch | Separate transaction cleanup path | Extend `ClearProposalSlot()` and existing proposal cleanup callback semantics | Current cleanup already knows slot-level proposal ids and pending votes. [VERIFIED: src/blockchain/Consensus.cpp; src/account/TransactionManager.cpp] |
| Vote idempotency | New vote-dedup map | Existing `SlotState::voted` and `ProposalState::seen_voters` | Existing state already prevents duplicate local votes and double-counted validator weight. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp] |
| Test timing | Real multi-second sleeps | Inject TTL/retry config and expose test accessors | Phase context requires deterministic short TTL tests. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Wire-level pending protocol | New Protobuf fields for pending | Local C++ runtime state only | Pending is local and not quorum-visible. [VERIFIED: src/blockchain/impl/proto/Consensus.proto; .planning/notes/deferred-consensus-validation.md] |

**Key insight:** The hard part is not storing a pending proposal; the hard part is preserving current consensus invariants while the same proposal is validated multiple times. [VERIFIED: src/blockchain/Consensus.cpp] [ASSUMED]

## Common Pitfalls

### Pitfall 1: Waking by the Wrong Key

**What goes wrong:** A proposal waiting for predecessor certificate `tx1` is indexed by its own hash `tx2`, so `CertificateReceived(tx1)` finds nothing. [VERIFIED: .planning/notes/deferred-consensus-validation.md]

**Why it happens:** The current pending map is `pending_by_subject_hash_`, which was built for subject readiness rather than dependency readiness. [VERIFIED: src/blockchain/Consensus.hpp]

**How to avoid:** Index pending entries by typed dependency keys and wake all proposal ids under `Certificate(tx_hash)`. [VERIFIED: .planning/REQUIREMENTS.md]

**Warning signs:** Tests pass for same-subject retry but fail for out-of-order predecessor certificate recovery. [ASSUMED]

### Pitfall 2: Duplicate Self-Votes on Retry

**What goes wrong:** Retrying a proposal calls vote creation more than once. [VERIFIED: .planning/REQUIREMENTS.md]

**Why it happens:** Retry code can bypass `slot_state.voted` if it does not route through `ContinueProposalAfterSubject()`. [VERIFIED: src/blockchain/Consensus.cpp]

**How to avoid:** All approval retries must call the same helper and reuse the same `slot_states_`. [VERIFIED: src/blockchain/Consensus.cpp]

**Warning signs:** Local validator appears more than once in pending or final vote collections. [VERIFIED: src/blockchain/Consensus.cpp]

### Pitfall 3: Capacity Cleanup Drift

**What goes wrong:** A proposal is removed from one map but byte counters, per-proposer counts, or dependency indexes are left behind. [ASSUMED]

**Why it happens:** Current cleanup only knows `pending_proposals_`, `pending_by_subject_hash_`, and `pending_votes_`; Phase 07 adds more indexes. [VERIFIED: src/blockchain/Consensus.cpp]

**How to avoid:** Implement one `RemovePendingProposal(proposal_id, reason)` helper and call it from certification, terminal rejection, capacity rejection rollback, and expiry. [ASSUMED]

**Warning signs:** Pending count reaches the limit even after proposals expire or certify. [ASSUMED]

### Pitfall 4: Treating TTL Expiry as Invalidity

**What goes wrong:** Local outgoing transactions are marked `FAILED` even though the node only ran out of time. [VERIFIED: src/account/TransactionManager.cpp]

**Why it happens:** `OnProposalTimeoutCleanup()` currently maps `VERIFYING` directly to `FAILED`. [VERIFIED: src/account/TransactionManager.cpp]

**How to avoid:** Add `UNCONFIRMED` and branch local outgoing expiry separately from terminal validation reject. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]

**Warning signs:** A transaction can no longer be retried because `FAILED` released nonce or rolled back UTXOs after an inconclusive pending TTL. [VERIFIED: src/account/TransactionManager.cpp] [ASSUMED]

### Pitfall 5: Confusing `Stalled` and `Pending`

**What goes wrong:** Local infrastructure failure becomes a dependency-indexed pending entry with no meaningful wake event. [VERIFIED: .planning/notes/deferred-consensus-validation.md] [ASSUMED]

**Why it happens:** Current `Check` has only enum values, so there is no structured place to distinguish explicit dependencies from scheduled retry metadata. [VERIFIED: src/blockchain/Consensus.hpp]

**How to avoid:** Require `Pending` to carry either dependencies or retry metadata; keep `Stalled` for infrastructure states that cannot be reliably processed. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]

**Warning signs:** Pending entries with no dependency, no retry schedule, and no expiry path. [ASSUMED]

## Code Examples

### Missing Predecessor Certificate Becomes Pending

```cpp
// Source: src/account/TransactionManager.cpp CheckTransactionReplayProtection()
// Recommendation: split boolean validation into structured validation detail.
if ( tx.GetNonce() > 0 )
{
    const auto previous_hash = tx.GetPreviousHash();
    auto previous_cert_result = blockchain_->GetCertificateBySubjectHash( previous_hash );
    if ( previous_cert_result.has_error() )
    {
        return ValidationResult::Pending(
            { PendingDependencyKey{ PendingDependencyKey::Type::Certificate, previous_hash } } );
    }
}
```

### Certificate Arrival Wakes Dependency

```cpp
// Source: src/blockchain/Consensus.cpp CertificateReceived()
// Recommendation: after validating/storing a certificate, wake local waiters.
auto subject_hash = GetSubjectHash( certificate.proposal().subject() );
if ( subject_hash.has_value() )
{
    WakePendingDependency(
        PendingDependencyKey{ PendingDependencyKey::Type::Certificate, subject_hash.value() } );
}
```

### Cleanup Helper Should Own All Pending Index Removal

```cpp
// Source: src/blockchain/Consensus.cpp ClearProposalSlot() currently removes
// proposals_, pending_proposals_, pending_votes_, and pending_by_subject_hash_.
void ConsensusManager::RemovePendingProposal( const std::string &proposal_id )
{
    auto it = pending_entries_.find( proposal_id );
    if ( it == pending_entries_.end() )
    {
        return;
    }

    for ( const auto &dep : it->second.dependencies )
    {
        auto dep_it = pending_by_dependency_.find( dep );
        if ( dep_it != pending_by_dependency_.end() )
        {
            dep_it->second.erase( proposal_id );
            if ( dep_it->second.empty() )
            {
                pending_by_dependency_.erase( dep_it );
            }
        }
    }

    pending_bytes_ -= it->second.retained_bytes;
    DecrementProposerPendingCount( it->second.proposal.proposer_id() );
    pending_entries_.erase( it );
}
```

## State of the Art

| Old Approach | Current Approach for Phase 07 | When Changed | Impact |
|--------------|-------------------------------|--------------|--------|
| One-shot validation treats missing predecessor certificate as failure. | Missing certificate returns local `Pending(Certificate(previous_hash))`. | Phase 07 planning, based on 2026-06-15 design note. [VERIFIED: .planning/notes/deferred-consensus-validation.md] | Out-of-order proposals can recover without re-proposal. |
| Pending map keyed by pending proposal's subject hash. | Pending map indexed by typed dependencies. | Phase 07 locked decision. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] | Certificate arrivals can resume all dependent proposals. |
| Timeout cleanup maps `VERIFYING` to `FAILED`. | Local outgoing TTL expiry maps to `UNCONFIRMED`; remote temporary entries are removed. | Phase 07 locked decision. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] | Failed means invalid, not merely inconclusive. |
| No resource accounting for pending proposals beyond maps. | Count, per-proposer count, retained bytes, TTL, and backoff metadata are bounded. | Phase 07 locked decision. [VERIFIED: .planning/REQUIREMENTS.md] | Prevents local memory exhaustion. |

**Deprecated/outdated:**

- `pending_by_subject_hash_` as the only pending index is outdated for dependency-triggered retries. [VERIFIED: src/blockchain/Consensus.hpp; .planning/notes/deferred-consensus-validation.md]
- `OnProposalTimeoutCleanup()` always transitioning `VERIFYING` to `FAILED` is outdated for inconclusive pending TTL expiry. [VERIFIED: src/account/TransactionManager.cpp; .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A small dependency-trigger throttle should be implemented as a local config value, with a default in the 250-500 ms range. | Architecture Patterns | Retry storms may be over- or under-throttled. |
| A2 | One manager-owned scheduler is preferable to detached per-proposal timers. | Don't Hand-Roll | If existing threading constraints require a separate scheduler, the implementation plan must adjust. |
| A3 | A single cleanup helper can own pending index and accounting removal. | Common Pitfalls | If slot cleanup semantics require multi-proposal grouped removal, helper design needs a group-aware variant. |
| A4 | New focused tests can use friend test accessors rather than public production introspection. | Validation Architecture | If maintainers reject friend accessors, tests need public diagnostic methods or integration-only validation. |

## Open Questions (RESOLVED)

1. **RESOLVED — Should `ValidationResult` replace `SubjectHandler` directly or be introduced as an adapter first?**
   - What we know: `SubjectHandler` currently returns `outcome::result<Check>`. [VERIFIED: src/blockchain/Consensus.hpp]
   - Resolution: Plan 07-02 introduces `ValidationResult` as the handler-facing result while preserving adapter coverage for existing simple handlers in the same wave.
   - Recommendation: Introduce `ValidationResult` as the new handler return type and provide a small adapter for existing simple handlers in the same wave. [ASSUMED]

2. **RESOLVED — Should scheduled retry share `round_timer_` or use a separate pending timer?**
   - What we know: `round_timer_` already waits on `timer_cv_`, processes certificates, and recovers certificate work. [VERIFIED: src/blockchain/Consensus.cpp]
   - Resolution: Plan 07-04 uses the existing timer primitives and splits pending retry/expiry into named helpers.
   - Recommendation: Share the existing timer primitives but split work into named helpers such as `ProcessDuePendingRetries()` and `ExpirePendingProposals()`. [ASSUMED]

3. **RESOLVED — How should remote temporary transaction removal be represented in `TransactionManager`?**
   - What we know: `tx_processed_m` stores `TrackedTx` by transaction path and `ChangeTransactionState(FAILED)` has rollback and nonce side effects. [VERIFIED: src/account/TransactionManager.cpp]
   - Resolution: Plan 07-05 adds explicit remote temporary tracking cleanup so expiry can remove `VERIFYING` remote embedded entries without invoking failure side effects.
   - Recommendation: Add an explicit helper to erase only `VERIFYING` remote embedded entries without invoking failure side effects. [ASSUMED]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| CMake | Configure/build | yes | 3.31.4 | Use existing `build/OSX/Debug` tree. [VERIFIED: shell `cmake --version`] |
| CTest | Test discovery/execution | yes | 3.31.4 | Run test binaries directly from `build/OSX/Debug/test_bin`. [VERIFIED: shell `ctest --version`; shell `ctest -N`] |
| Apple clang++ | C++ build | yes | Apple clang 16.0.0 | Use configured platform build. [VERIFIED: shell `clang++ --version`] |
| Ninja | Optional build generator | yes | 1.13.0 | Existing Debug tree uses Makefiles, so `cmake --build` is sufficient. [VERIFIED: shell `ninja --version`; build/OSX/Debug/Makefile] |
| Protobuf compiler on PATH | Manual proto generation | no | - | CMake has imported protobuf protoc at `/Users/henriqueklein/gnus/thirdparty/build/OSX/Debug/protobuf_host/bin/protoc`. [VERIFIED: shell `protoc --version`; build/OSX/Debug/CMakeCache.txt] |
| Configured Debug test tree | Validation | yes | 42 CTest tests | Use `build/OSX/Debug`; `build/local` reports zero tests. [VERIFIED: shell `ctest -N` in build/OSX/Debug and build/local] |

**Missing dependencies with no fallback:**

- None for the recommended code-only implementation. [VERIFIED: local environment audit]

**Missing dependencies with fallback:**

- Standalone `protoc` is absent on PATH; use CMake's imported `protobuf::protoc` rather than manual `protoc` commands. [VERIFIED: shell `protoc --version`; cmake/functions.cmake]

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | GTest/GMock via CMake `addtest()`. [VERIFIED: cmake/functions.cmake] |
| Config file | `test/src/blockchain/CMakeLists.txt`, `test/src/account/CMakeLists.txt`, and `cmake/functions.cmake`. [VERIFIED: local files] |
| Quick run command | `ctest --test-dir build/OSX/Debug -R 'consensus_subject_test|consensus_slot_key_test' --output-on-failure` [VERIFIED: shell `ctest -N`] |
| Full suite command | `ctest --test-dir build/OSX/Debug --output-on-failure -j4` [VERIFIED: shell `ctest -N`] |

### Phase Requirements -> Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| PEND-01 | Structured result preserves approve/reject/pending/stalled and carries dependencies. | unit | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| PEND-02 | Pending is local and emits no vote. | unit/integration | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| PEND-03 | `Certificate(previous_hash)` arrival resumes all waiting proposals. | integration | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| PEND-04 | Transient pending retries follow bounded backoff. | unit | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| PEND-05 | TTL defaults to three minutes and tests inject short TTL. | unit | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| PEND-06 | Certification/reject/expiry removes proposal, dependencies, votes, retry metadata, capacity, and temp tx tracking. | unit/integration | `ctest --test-dir build/OSX/Debug -R 'consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test' --output-on-failure` | No - Wave 0 |
| PEND-07 | Revalidation emits at most one local approval vote and does not double-count. | unit | `ctest --test-dir build/OSX/Debug -R consensus_pending_lifecycle_test --output-on-failure` | No - Wave 0 |
| TXSTATE-01 | Inconclusive local outgoing expiry becomes `UNCONFIRMED`; proven invalid stays `FAILED`; remote temp entry is removed. | unit/integration | `ctest --test-dir build/OSX/Debug -R transaction_manager_pending_lifecycle_test --output-on-failure` | No - Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build/OSX/Debug -R 'consensus_subject_test|consensus_slot_key_test|consensus_pending_lifecycle_test|transaction_manager_pending_lifecycle_test' --output-on-failure` [VERIFIED: shell `ctest -N`; ASSUMED for new tests]
- **Per wave merge:** `ctest --test-dir build/OSX/Debug --output-on-failure -j4` [VERIFIED: shell `ctest -N`]
- **Phase gate:** Full Debug CTest suite green before `$gsd-verify-work`. [VERIFIED: .planning/config.json]

### Wave 0 Gaps

- [ ] `test/src/blockchain/consensus_pending_lifecycle_test.cpp` - covers PEND-01 through PEND-07. [VERIFIED: file absent via `find`; .planning/REQUIREMENTS.md]
- [ ] `test/src/account/transaction_manager_pending_lifecycle_test.cpp` - covers TXSTATE-01 and remote temp cleanup. [VERIFIED: file absent via `find`; .planning/REQUIREMENTS.md]
- [ ] `test/src/blockchain/CMakeLists.txt` - add new pending lifecycle test target; note `consensus_certificate_test.cpp` is present but commented out today. [VERIFIED: test/src/blockchain/CMakeLists.txt]
- [ ] `test/src/account/CMakeLists.txt` - add new transaction-manager pending lifecycle test target. [VERIFIED: test/src/account/CMakeLists.txt]

## Security Domain

Security enforcement is enabled in `.planning/config.json`, and OWASP ASVS 5.0.0 is the latest stable ASVS release listed by OWASP on the project page. [VERIFIED: .planning/config.json] [CITED: https://owasp.org/www-project-application-security-verification-standard/]

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | No user authentication or session login flow is changed in this phase. [VERIFIED: .planning/REQUIREMENTS.md] |
| V3 Session Management | no | No web session or token lifecycle is changed in this phase. [VERIFIED: .planning/REQUIREMENTS.md] |
| V4 Access Control | yes | Preserve validator registry checks and do not alter quorum or proposer/voter eligibility. [VERIFIED: src/blockchain/Consensus.cpp] |
| V5 Input Validation | yes | Validate dependency key type/value, retained byte sizes, TTL config, retry intervals, proposal timestamps, and handler results. [VERIFIED: src/blockchain/Consensus.cpp; .planning/REQUIREMENTS.md] |
| V6 Cryptography | yes | Do not change proposal/vote/certificate signing bytes or signature verification; keep cryptographic routines in existing `ConsensusAuth` and account signing paths. [VERIFIED: src/blockchain/Consensus.cpp; src/blockchain/ConsensusAuth.hpp] |

### Known Threat Patterns for Consensus Pending Lifecycle

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Pending pool exhaustion by one proposer | Denial of Service | Enforce global and per-proposer limits plus retained-byte accounting. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Retry storm from repeated dependency events | Denial of Service | Enforce per-proposal minimum retry interval and bounded backoff. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Dependency key collision | Tampering | Use typed dependency keys, not raw strings. [VERIFIED: .planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md] |
| Duplicate local approval on retry | Elevation of Privilege / Tampering | Route retries through existing slot/vote idempotency state. [VERIFIED: src/blockchain/Consensus.hpp; src/blockchain/Consensus.cpp] |
| Inconclusive expiry misclassified as failure | Repudiation / Integrity | Add `UNCONFIRMED` and preserve `FAILED` for locally proven invalid transactions only. [VERIFIED: .planning/REQUIREMENTS.md] |

## Sources

### Primary (HIGH confidence)

- `.planning/phases/07-deferred-validation-and-pending-proposal-lifecycle/07-CONTEXT.md` - locked decisions, discretion, deferred scope.
- `.planning/REQUIREMENTS.md` - PEND-01 through PEND-07 and TXSTATE-01.
- `.planning/ROADMAP.md` - Phase 07 success criteria and phase scope.
- `.planning/notes/deferred-consensus-validation.md` - problem statement and retry model.
- `.planning/phases/03-network-hardening-and-operational-readiness/03-CONTEXT.md` - prior cleanup callback and tracking decisions.
- `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` - current consensus state, handlers, pending maps, timers, cleanup, vote/certificate handling.
- `src/blockchain/Blockchain.hpp` and `src/blockchain/impl/Blockchain.cpp` - facade registration and retry methods.
- `src/account/TransactionManager.hpp` and `src/account/TransactionManager.cpp` - transaction status, validation, certificate fallback, cleanup callback, state transitions.
- `src/blockchain/impl/proto/Consensus.proto` - consensus wire schema.
- `test/src/blockchain/CMakeLists.txt`, `test/src/account/CMakeLists.txt`, `cmake/functions.cmake` - test target patterns.
- `build/OSX/Debug/CMakeCache.txt` and `ctest -N` output - environment and configured tests.

### Secondary (MEDIUM confidence)

- `.planning/codebase/ARCHITECTURE.md` - project architecture and C++17/outcome conventions.
- `.planning/codebase/TESTING.md` - testing patterns and commands.
- OWASP ASVS project page - ASVS 5.0.0 status and purpose. https://owasp.org/www-project-application-security-verification-standard/

### Tertiary (LOW confidence)

- Assumptions in `## Assumptions Log` about exact throttle defaults, helper shape, and test accessor style.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH - phase uses existing C++/CMake/GTest/Protobuf stack verified locally.
- Architecture: HIGH - primary seams are visible in current consensus and transaction-manager code.
- Pitfalls: HIGH - most pitfalls map directly to current code gaps and locked phase decisions.
- Security: MEDIUM - ASVS category mapping is based on local phase scope and OWASP ASVS project page, not a full ASVS requirement-by-requirement audit.

**Research date:** 2026-06-16
**Valid until:** 2026-07-16 for local codebase findings, or until `ConsensusManager` / `TransactionManager` are materially changed.
