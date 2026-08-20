# Phase 9: Durable One-Vote Finality - Context

**Gathered:** 2026-08-20
**Status:** Ready for planning

<domain>
## Phase Boundary

Make a validator select one eligible winner for a canonical slot within a bounded contention window, durably preserve the exact vote before it is published, and recover that exact vote across restart without ever creating a replacement vote for the same slot.

This phase adds generic consensus-slot contention and local vote durability only. It does not change `MintTransactionV2::GetSlotID()`, define slot-keyed certificate publication/failover (Phase 10), or implement convergent certificate consumption and mint recovery (Phase 11).

</domain>

<decisions>
## Implementation Decisions

### Bounded contention and winner selection
- **D-01:** The first locally validated proposal for a canonical slot opens a fixed two-second contention window.
- **D-02:** Only proposals fully validated by the deadline are eligible. A late, invalid, or still-stalled proposal cannot delay the window, change its winner, or trigger a vote in that attempt. If the window has no eligible candidate, it creates no vote lock; a proposal may begin a fresh window only after it becomes valid.
- **D-03:** Freeze the eligible set at the deadline. Use the existing deterministic order—lexicographically lowest transaction hash, then lowest proposal ID—to select the winner. The transaction hash is only a tie-break and never changes the canonical slot or certificate-to-proposal binding.
- **D-04:** Apply this arbitration generically to every consensus subject with a canonical slot key, not just bridge Mints. Ignore votes for non-winning and late proposals for finality.

### Durable vote record and publication
- **D-05:** Store one generic RocksDB-backed consensus-vote record per canonical slot. It is local persistence, not CRDT state, and must not use a bridge-specific namespace. The exact storage prefix follows established storage conventions.
- **D-06:** Before any broadcast, the record must contain the canonical slot, selected full proposal, exact serialized and signed vote, and an absolute acceptance deadline. If persistence cannot complete, emit no vote and leave no usable in-memory vote state.
- **D-07:** An existing same-slot record is idempotent only for the exact stored vote. A different proposed vote is rejected without overwriting the record or broadcasting a replacement.

### Recovery and lock release
- **D-08:** On restart, while before the recorded deadline, automatically re-announce only the exact stored signed vote. A failed initial send likewise retries that exact vote on bounded backoff.
- **D-09:** After its deadline, stop re-announcing the vote but retain the lock. Expiry never authorizes a second vote for the slot.
- **D-10:** Delete the local vote record only after durable acceptance of a certificate for the same canonical slot. The accepted certificate may name a different winning proposal from this validator's vote; same-slot finality prevents future voting. Never delete on receipt or parse alone.

### the agent's Discretion
- Choose the smallest C++17 data encoding, RocksDB prefix, timer/test seams, and bounded retry parameters consistent with the fixed two-second window and the persisted absolute deadline.
- Preserve existing generic consensus behavior outside the new slot-arbitration and vote-lock contract. Do not introduce new dependencies or a bridge-special case.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone contract
- `.planning/PROJECT.md` — v3.0 safety boundary: one burn, one finality result; direct local vote lock; no bridge-only record or local delivery-source authority.
- `.planning/REQUIREMENTS.md` — authoritative Phase 9 requirements `VOTE-01` through `VOTE-04` and scope exclusions.
- `.planning/ROADMAP.md` § Phase 9 — fixed goal, dependency on Phase 8, and success criteria.
- `.planning/research/SUMMARY.md` — no-new-dependency finality architecture and protocol guardrails.
- `.planning/phases/08-canonical-slot-certificate-binding/08-CONTEXT.md` — locked canonical Mint slot and certificate-binding decisions which Phase 9 must preserve.
- `.planning/phases/08-canonical-slot-certificate-binding/08-VERIFICATION.md` — evidence that Phase 8 binding gates are complete.

### Existing consensus and persistence
- `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` — `SlotState`, `ContinueProposalAfterSubject`, `IsBetterProposal`, certificate acceptance, round timer, and current volatile slot cleanup behavior to replace/constrain.
- `src/storage/rocksdb/rocksdb.hpp` and `src/storage/rocksdb/rocksdb.cpp` — available local RocksDB persistence interface and error semantics.
- `src/account/MintTransactionV2.hpp` and `src/account/MintTransactionV2.cpp` — existing canonical Mint slot calculation; do not alter it.

### Tests and fixtures
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — `ConsensusManager` test-access seam, focused lifecycle coverage, and the in-memory secure-storage fixture.
- `test/src/blockchain/consensus_slot_key_test.cpp` — canonical slot and certificate-binding regression coverage to preserve.
- `src/local_secure_storage/impl/MemorySecureStorage.hpp` — in-memory secure storage required for isolated consensus test fixtures.
- `test/src/storage/rocksdb/rocksdb_integration_test.cpp` — local RocksDB test patterns if persistence needs direct coverage.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ConsensusManager::GetSlotKey`, `SlotState`, and `IsBetterProposal`: existing generic slot grouping and deterministic transaction-hash/proposal-ID ordering.
- `ConsensusManager`'s round timer and condition variable: existing timing/retry mechanism that can host the bounded contention and exact-vote retry work.
- RocksDB storage interfaces: existing local durable storage, avoiding a new dependency or CRDT path.
- `MemorySecureStorage` plus the consensus lifecycle fixture: stable isolated test setup; Phase 8 proved it avoids persistent-store/timer teardown failures.

### Established Patterns
- C++17, `outcome::result<T>`, and fail-closed consensus checks.
- Tests use friend/test-access seams for focused internal-state assertions and CTest targets under `test/src/blockchain/`.

### Integration Points
- `ContinueProposalAfterSubject` currently updates volatile best-proposal state and self-votes immediately; Phase 9 must turn it into window-close selection plus durable vote creation.
- `ClearProposalSlot` and certificate ingress currently clear volatile slot bookkeeping; Phase 9 must gate removal of the persistent vote record on durable accepted same-slot certificate finality.
- `SubmitVote` is the outbound boundary: durable record success must precede every first publish and every recovery retry must use the stored bytes.

</code_context>

<specifics>
## Specific Ideas

The user’s core rule is: a validator cannot wait forever for possible competitors, but once it has voted for a canonical slot it cannot change that vote. The local RocksDB record bridges the interval before a certificate arrives; after durable same-slot certificate acceptance, certificate finality itself prevents another vote.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within Phase 9. Slot-keyed certificate publication/failover remains Phase 10, and unified certificate consumption plus exactly-once mint recovery remain Phase 11.

</deferred>

---

*Phase: 9-Durable One-Vote Finality*
*Context gathered: 2026-08-20*
