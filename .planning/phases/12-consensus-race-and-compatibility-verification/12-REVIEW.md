---
phase: 12-consensus-race-and-compatibility-verification
reviewed: 2026-08-19T00:00:00Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - evmrelay/src/eth/secp256k1_utility.cpp
  - src/account/TransactionManager.hpp
  - src/account/UTXOManager.cpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - test/src/blockchain/certificate_compatibility_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/consensus_certificate_store_test.cpp
  - test/src/blockchain/consensus_finality_race_test.cpp
  - test/src/blockchain/consensus_finalization_test.cpp
  - test/src/blockchain/consensus_vote_journal_test.cpp
  - test/src/bridge_race/bridge_race_fixture.hpp
  - test/src/bridge_race/bridge_race_single_burn_test.cpp
findings:
  critical: 1
  warning: 5
  info: 9
  total: 15
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-08-19
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

Reviewed the consensus finality/race foundations (ConsensusManager finalization
state machine, certificate store, vote journal restore), the atomic mint/UTXO
application path, the secp256k1 submodule utility (read-only), and the new
deterministic harnesses plus the 11-validator bridge race e2e.

The certificate normalization surface (unknown-field rejection, deterministic
bytes, vote ordering, signature verification, strict restore) is thorough and
well-tested. The durable store's `ValidateVote` correctly binds record identity
to the embedded vote, which closes several tamper paths.

The significant finding is a liveness defect in `FinalizeSlot`: several
post-reservation failure exits return `StorageFailure` without restoring the
in-memory slot lifecycle out of `Finalizing`, permanently wedging the slot and
potentially hanging the round-timer thread. There is also one remotely
triggerable unbounded-memory vector (`pending_votes_`), a UTXO durability gap
for foreign-owned bridge inputs, a missing shutdown escape in the admission
barrier, and two test-reliability issues (unsynchronized test seam, leaked
global slot-key handler).

## Critical Issues

### CR-01: FinalizeSlot post-reservation failure exits leave the slot stuck in `Finalizing` forever

**File:** `src/blockchain/Consensus.cpp:2945-3055`
**Issue:** In the first-observation branch of `FinalizeSlot`, once the slot is
reserved (`rechecked.lifecycle = SlotState::Lifecycle::Finalizing` at line 2954),
three failure exits return `FinalizeResult::StorageFailure` without restoring
the prior lifecycle/generation:

1. Lines 3026-3031 — reread bytes differ and `RecordCertificateConflict` fails
   (store error): returns `StorageFailure` with lifecycle stuck at `Finalizing`.
2. Lines 3019-3055 — when `confirmed_absent` is true but the `Put` failed and
   the reread produces a non-`NotFound` error (transient IO/integrity), or when
   the preexisting pair is a dangling index (slot record absent, index record
   present), `exact_pair` stays false and line 3055 returns `StorageFailure`
   with lifecycle stuck at `Finalizing`.

Only the `NotFound + confirmed_absent + put failed` branch (3038-3052) restores
`prior_lifecycle`/`prior_generation`.

A slot stuck in `Finalizing` makes every subsequent `FinalizeSlot` for that slot
block permanently in the `slot_cv_.wait` loop (lines 2906-2922 / 2965-2975) —
the predicate only releases on lifecycle change or `stop_timer_`. This includes
calls from `RecoverPendingCertificateWork`, which runs on the round-timer thread
(`StartRoundTimer`, line 835): one transient datastore error at the wrong moment
can hang the timer thread and stop all consensus cadence (deadlines, retries,
reconciliation, recovery) until process restart. It also wedges
`AdmitProposalResources`/`ReconcileBurnReservations` for the slot.

**Fix:** Restore the reservation on every failure exit between reservation and
the `restored_final_slots_` insert (line 3062). A scope guard is the robust form:

```cpp
// immediately after setting lifecycle = Finalizing (both reservation sites)
auto release_reservation = gsl::finally( [&]() {
    std::lock_guard lock( proposals_mutex_ );
    auto it = slot_states_.find( slot_id );
    if ( it != slot_states_.end() && it->second.lifecycle == SlotState::Lifecycle::Finalizing &&
         it->second.generation == reservation_generation &&
         it->second.reserved_finalization_digest == digest )
    {
        it->second.lifecycle = prior_lifecycle;
        it->second.generation = prior_generation;
        it->second.reserved_finalization_proposal_id.clear();
        it->second.reserved_finalization_digest.clear();
        it->second.reserved_finalization_winner_id.clear();
        slot_cv_.notify_all();
    }
} );
// ... dismiss (release_reservation.dismiss()) only on the success path that
// transitions lifecycle to FinalizedPendingApplication / SafetyViolation.
```

Then delete the hand-rolled restore at lines 3038-3052, which the guard
subsumes.

## Warnings

### WR-01: `pending_votes_` grows unboundedly from unauthenticated network input

**File:** `src/blockchain/Consensus.cpp:4259-4265` (also 4302)
**Issue:** `HandleVote` queues any validly self-signed vote for an unknown
proposal id into `pending_votes_[proposal_id]`. `GeniusAccount::VerifySignature`
only proves the signature matches the embedded `voter_id` string — arbitrary
peers can mint fresh keys and flood votes for random proposal ids. Unlike
pending proposals (bounded by `PendingLifecycleConfig`), `pending_votes_` has no
count/byte cap, no TTL, and is only drained when the proposal arrives or the
slot is cleaned up. A remote peer can grow node memory without limit.
**Fix:** Bound `pending_votes_` (max entries per proposal id and a global byte
budget, plus expiry), or drop votes for unknown proposals from non-registry
voters. At minimum, evict on `ExpirePendingProposals` cadence and cap total
retained vote bytes.

### WR-02: `ConsumeUTXOs` does not persist consumption of foreign-owned bridge inputs

**File:** `src/account/UTXOManager.cpp:264-318` (caller: `src/account/TransactionManager.cpp:2083`)
**Issue:** For `UTXO_BRIDGE` entries, `owner_matches` permits consumption when
`stored_owner != address` (line 281). The entry is consumed in memory and
removed from `address_outpoints_[stored_owner]`, but only `StoreUTXOs( address )`
is called (line 315) — the real owner's durable records are never rewritten, so
the durable store still shows the bridge outpoint as `UTXO_READY`. After a
restart, `LoadUTXOs` resurrects the consumed bridge input as READY: the owner's
computed balance and Merkle root change across restart and in-memory/durable
state diverge. (`ApplyMintEffectsAtomically` handles affected owners correctly;
the legacy `ParseMintTransaction` mint-v2 path at TransactionManager.cpp:2083
does not, and the provisional-owner reassignment branch at UTXOManager.cpp:1171-1190
proves owner != spender occurs.)
**Fix:** In `ConsumeUTXOs`, collect every distinct stored owner whose entry was
mutated and call `StoreUTXOs` for each of them (or document/enforce that bridge
inputs are only consumed through `ApplyMintEffectsAtomically` and remove the
foreign-owner bypass from this path).

### WR-03: `AdmitProposalResources` admission-barrier wait has no shutdown escape and leaks the inflight marker on handler exceptions

**File:** `src/blockchain/Consensus.cpp:1387-1441`
**Issue:** Two compounding problems:
1. Line 1391: `slot_cv_.wait( lock, [&]() { return resource_admissions_inflight_.count( slot_key ) == 0; } )`
   has no `stop_timer_`/closing predicate. If the inflight marker is never
   erased, this thread blocks forever; because callers hold `BeginActivity`,
   `Close()` then hangs waiting for `active == 0` — a shutdown deadlock.
2. If the `ResourceAdmissionHandler` (application code) or
   `state_store_->CreateOrJoinBurnReservation` throws, the inflight marker
   inserted at line 1394 is never erased (no RAII release), producing exactly
   the permanent wedge in (1).
**Fix:** Add a stop predicate to the wait
(`return stop_timer_.load() || resource_admissions_inflight_.count( slot_key ) == 0;`)
and wrap the post-insert section in a scope guard that erases the marker and
notifies `slot_cv_` on any exit that doesn't hand off to
`ContinueProposalAfterSubject`/`ReleaseProposalAdmission`.

### WR-04: `certificate_record_reader_` test seam is mutated without synchronization while manager threads read it

**File:** `src/blockchain/Consensus.hpp:1072-1073`; mutated by
`test/src/blockchain/certificate_compatibility_test.cpp:28-40` and
`test/src/blockchain/consensus_certificate_store_test.cpp:50-62`
**Issue:** `certificate_record_reader_` is a plain `std::function` read from
`ReadCertificatePreflightRecord` on the round-timer thread, pubsub callbacks,
and CRDT filter callbacks. The tests' `SetCertificateReader`/`ResetCertificateReader`
write it with no mutex while the manager's timer thread is already running
(`StartRoundTimer` is invoked inside `ConsensusManager::New`). Concurrent
read/write of a `std::function` is UB — this can produce flaky crashes under
TSan or in optimized builds, undermining the otherwise deterministic harnesses.
**Fix:** Guard the reader with a small mutex (set/copy under lock, invoke a
copied `std::function`), or route test injection through an atomic
`std::shared_ptr<const Reader>` swap.

### WR-05: `certificate_compatibility_test` leaks a global slot-key handler, making the binary order-dependent

**File:** `test/src/blockchain/certificate_compatibility_test.cpp:490-499`
**Issue:** `LosingHashIsNotFoundButFullSubjectObservesFinalizedSlot` calls the
static `ConsensusManager::RegisterSlotKeyHandler( NONCE_SUBJECT_TYPE, ...)`,
replacing the built-in nonce resolver in the process-wide
`slot_key_handlers_` map (a `static inline` member) and never unregisters it.
Every later test in the same binary (e.g.
`PreviousNonceAndProducerHashConsumersResolveWinner`) silently runs against the
legacy preimage slot scheme; if gtest shuffles or a new test is added that
assumes the builtin resolver, results change with execution order. The return
value of `RegisterSlotKeyHandler` is also ignored (it fails silently if a
handler is already installed).
**Fix:** Save/restore around the test body:
`ConsensusManager::UnregisterSlotKeyHandler( NONCE_SUBJECT_TYPE )` followed by
`ConsensusManager::EnsureBuiltinSlotKeyHandlers()` in a scope guard, and assert
the registration return value.

## Info

### IN-01: Dead code in ConsensusManager, including a latent thread-safety bug

**File:** `src/blockchain/Consensus.cpp:4392-4432`; declarations at `src/blockchain/Consensus.hpp:920-933, 1018`
**Issue:** `FetchProposalState`, `CreateProposalState`, `CollectCertificateVotes`,
and `ConsensusManager::ValidateCertificate` have no call sites anywhere in the
repo (the `ValidateCertificate` hits in ValidatorRegistry are a different
class). `CreateProposalState` additionally mutates `proposals_` and
`slot_states_` without holding `proposals_mutex_` — a latent data race if it is
ever revived.
**Fix:** Delete all four, or move them under test access if a future plan needs them.

### IN-02: Byte-equality checks rely on non-deterministic `SerializeAsString()`

**File:** `src/blockchain/Consensus.cpp:2930, 3024`
**Issue:** `authority.value().SerializeAsString() != normalized.deterministic_bytes`
compares a non-deterministic serialization against deterministic bytes. It is
currently true that these protos contain no map fields, so the encodings
coincide, but the invariant is implicit; everywhere else the code compares
against the stored raw bytes.
**Fix:** Compare against the stored buffer bytes (as `GetCertificateBySlotId`
does), or add a comment/assert documenting the no-map-field requirement.

### IN-03: Inconsistent clock/negative-window handling

**File:** `src/blockchain/Consensus.cpp:1679` (unguarded `static_cast<uint64_t>( timestamp_window_.count() )`,
cf. guarded versions at 1419-1421 and 1482-1484); direct clock reads bypassing
the override seams at 1130-1132, 3573-3575, 4338-4340, 5028-5030, 1889
**Issue:** Line 1679 wraps a negative `timestamp_window_` to a huge uint64 while
two sibling sites clamp negatives to 0. `timestamp_window_` currently has no
setter so this is latent. Separately, `GetCurrentRound`, `ProcessCertificates`,
`HandleVote`, `RecoverPendingCertificateWork`, and `AddPendingProposal` read
`system_clock`/`steady_clock` directly instead of `CurrentTimeMs()`/
`steady_now_override_`, so the deterministic clock seams don't cover them.
**Fix:** Clamp once in a helper and route all wall/steady reads through the
existing overrides.

### IN-04: `ParseFromArray` size narrowing (`size_t` → `int`)

**File:** `src/blockchain/Consensus.cpp:436, 5052, 5184` (same pattern at 4845)
**Issue:** `Buffer::size()` is `size_t`; `ParseFromArray` takes `int`. A buffer
larger than `INT_MAX` would truncate. Values are certificate-sized in practice,
but the conversion is implicit and unchecked.
**Fix:** Cast explicitly after a bounds check, mirroring the guard used in
`DecodeBuiltinSubject` (line 4623).

### IN-05: `proposals_.at()` can throw in `ContinueProposalAfterSubject`

**File:** `src/blockchain/Consensus.cpp:1311`
**Issue:** `proposals_.at( slot_state.best_proposal_id )` throws
`std::out_of_range` if the best proposal was erased while the slot is still
`Selecting`. Current erase sites appear to keep the invariant, but nothing
enforces it locally; an exception here escapes into the pubsub/timer caller.
**Fix:** Use `find` and skip (or reset the slot) when the best proposal is gone.

### IN-06: `HandleVote` performs no timestamp sanity check on votes

**File:** `src/blockchain/Consensus.cpp:4219-4349`
**Issue:** Proposals are bounded by `IsTimestampSane`, but votes are aggregated
with any timestamp. A registered Byzantine validator can cast far-future votes,
forcing `quorum_reached` followed by certificate creation whose
`ValidateCertificateForFirstObservation` rejects — retried every round
indefinitely (log/CPU churn). Limited to registered validators, hence Info.
**Fix:** Reject or stall votes outside `timestamp_window_` in `HandleVote`.

### IN-07: Unchecked `secp256k1_ec_pubkey_serialize` return (submodule — fix upstream)

**File:** `evmrelay/src/eth/secp256k1_utility.cpp:218-224`
**Issue:** In `DecompressXOnlyPubkey` the serialize call's return value and
`uncompressed_len` are not checked (contrast with `address_from_public_key`,
which checks both). Failure would silently hash zero-padding into the derived
destination. Practically unreachable for a successfully parsed key, but the
file's own style checks these returns elsewhere.
**Fix (upstream):** Check the return and `uncompressed_len == kUncompressedPublicKeyBytes`
before computing the destination.

### IN-08: Bridge-race fixture robustness nits

**File:** `test/src/bridge_race/bridge_race_fixture.hpp`
**Issue:** (a) `kNodePortBase = 40041` hardcodes 11 consecutive ports — a stale
local process causes nondeterministic e2e failure (line 525); (b)
`WriteBridgeChainsConfig` doesn't verify the `ofstream` is good, unlike
`WriteRaceNetworkConfig` which asserts (lines 616-618 vs 626); (c)
`DeterministicBarrier`/`ScopedWorker` are duplicated verbatim between
`consensus_finality_race_test.cpp` and `consensus_finalization_test.cpp`.
**Fix:** Probe/allocate free ports, assert the stream, and factor the barrier
helpers into a shared testutil header.

### IN-09: Timer busy-spin if a `Selecting` slot loses its best proposal

**File:** `src/blockchain/Consensus.cpp:799-803` with `ProcessCandidateDeadlines:1617-1618`
**Issue:** The round timer clamps the wait interval to 0 when a `Selecting`
slot's deadline has passed, but `ProcessCandidateDeadlines` silently skips a
slot whose `best_proposal_id` is absent from `proposals_` — the slot would stay
`Selecting` with an expired deadline and the timer loop spins at 100% CPU.
Currently unreachable (erase sites preserve the invariant), but the loop has no
defensive floor for this state.
**Fix:** In `ProcessCandidateDeadlines`, reset a `Selecting` slot whose best
proposal is missing (or enforce a nonzero minimum wait when work was skipped).

---

_Reviewed: 2026-08-19_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
