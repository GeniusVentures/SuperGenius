---
phase: 11-slot-owned-bridge-burn-reservations
status: complete
researched: 2026-07-28
requirements:
  - BURN-01
  - BURN-02
  - BURN-03
  - BURN-04
  - BURN-05
---

# Phase 11 Research: Slot-Owned Bridge Burn Reservations

## Executive Summary

Phase 11 must make one node-local durable record, keyed by the canonical mint slot and bound to the exact canonical burn outpoint, the authority for whether a bridge burn is locally available. The current `UTXOManager::local_reservations_` cannot provide that authority: it is transient, account/transaction shaped, cleared on load, and owned by a transaction-style reservation ID. The persisted bridge UTXO state also cannot be the sole authority because proposal construction currently synthesizes that UTXO before consensus, different contenders may have different proposer identities, and certificate-only validators may not have the same synthetic entry.

The recommended architecture is to extend the Phase 10 direct-RocksDB local consensus store with a strict bridge reservation record and a two-way slot/outpoint index. `ConsensusManager` should own reservation sequencing because it already owns candidate admission, durable vote horizons, authoritative certificate observation, cleanup, recovery, and the per-slot concurrency boundary. `TransactionManager` should provide mint-specific descriptor extraction and exact-winner application, but validation must remain free of reservation mutation.

The critical ordering is:

1. semantically validate the proposal without reserving;
2. derive and cross-check the canonical slot and burn descriptor;
3. durably create or join the slot reservation;
4. only then make the proposal an active candidate or allow voting;
5. when a certificate becomes authoritative, durably move the reservation to `FinalizedPendingApplication` before invoking the transaction handler or cleanup;
6. atomically commit the mint outputs, bridge application record, physical bridge-input consumption, and reservation transition to `Consumed`;
7. on retryable failure keep the exact winner pending; on an irreconcilable contradiction durably enter `SafetyError`;
8. without a certificate, delete the reservation only after a conditional generation check and strictly after every persisted candidate/vote acceptance horizon.

The existing Phase 10 timer, state store, certificate finalizer, processing marker, and exact vote acceptance horizon are reusable foundations. The largest implementation risks are startup ordering, the lack of a post-validation admission hook, cross-component atomicity for mint application, and ABA-safe deletion when released records intentionally leave no history.

## Current Call Paths and Gaps

### Proposal creation reserves too early and by the wrong identity

`TransactionManager::MintFunds()` currently:

1. calls `GetBridgeBurnState()`;
2. inserts a synthetic bridge UTXO through `PutUTXO()`;
3. calls `ReserveUTXOs(..., transaction_hash, UTXO_BRIDGE)`;
4. constructs/signs/enqueues the mint proposal;
5. rolls the bridge UTXO back by burn transaction hash if construction throws.

This is before semantic consensus validation and uses a transaction-style owner rather than the canonical slot. `ReserveUTXOs()` mutates only memory; it does not persist the reservation after changing `UTXO_READY` to `UTXO_RESERVED`. `LoadUTXOs()` explicitly clears `local_reservations_`, so restart loses owner identity even if a persisted UTXO snapshot happens to contain a reserved state.

`ChangeTransactionState(FAILED)` also rolls back local mint-v2 inputs using `tx->dag_st.uncle_hash()`. That proposal-local failure path can currently unlock the shared burn. Phase 11 must remove bridge reservation mutation from `MintFunds`, generic failed-transaction rollback, revert paths, and proposal cleanup. Ordinary transfer/escrow use of `ReserveUTXOs` should remain unchanged.

### Validation is the correct pure decision seam

`HandleNonceConsensusSubject()` deserializes and tracks an embedded transaction, checks bindings, witness, replay protection, transaction rules, and public-chain source evidence. `PublicChainInputValidator::ValidateUTXOParameters()` deliberately does not reserve or require local UTXO ownership, and `ValidateWitness()` verifies the exact external receipt event. The burn reservation must not be added to `ValidateUTXOParametersForConsensus`, `CheckTransactionTypeRules`, or the public-chain validator.

`ConsensusManager::HandleProposal()` calls the subject handler and, on `Approve`, immediately calls `ContinueProposalAfterSubject()`. The latter inserts the candidate into the slot state, starts/updates selection, and eventually enables the timer to sign. There is no post-validation/pre-admission callback today. Add an admission/resource hook at this exact boundary. Persistence failure must return without `ContinueProposalAfterSubject()`, without an active candidate, and therefore without vote creation/publication.

Pending proposals are not admitted candidates. A `Pending` validation result must not reserve the burn until a later retry returns `Approve` and the durable admission transition succeeds.

### Local submission publishes before self handling

`SubmitProposal()` currently inserts minimal proposal state, publishes the proposal, then calls `HandleProposal()` for self handling. The safety requirement is that no active local candidate or vote exists before durable reservation, not that an unvalidated proposal can never be transported. Plans may preserve proposal publication ordering if the return/admission semantics remain explicit, but must ensure the locally inserted pre-validation `ProposalState` is not mistaken for an admitted candidate and cannot start selection. A cleaner future refactor can make self-admission return a typed result, but it is not necessary to expand Phase 11 into a transport redesign.

### Cleanup currently conflates expiry and finality

`ClearProposalSlot()` removes every proposal in a slot, fires per-proposal cleanup callbacks, and sets the slot to `FinalizedPendingApplication`. It is correctly used after completed certified application, but `ExpirePendingProposals()` also calls it for a pending TTL expiry even though no certificate exists. That conflation is unsafe for deterministic abandonment and already makes timeout cleanup look like finality.

Phase 11 needs separate operations:

- proposal-local removal/expiry, which changes only ephemeral candidate/tracking state;
- finalized-slot cleanup, allowed only after exact winner application is durably complete;
- whole-slot abandonment, which conditionally deletes an uncertified reservation after all safety horizons.

`OnProposalTimeoutCleanup()` already changes transaction tracking only; keep it that way. It must never release a bridge reservation.

### Finalization has the right authority point but lacks a resource transition

`FinalizeSlot()` already:

- normalizes and validates the certificate;
- establishes the authoritative slot certificate/index pair;
- serializes with signing/publication/finalization slot state;
- creates a durable `CertificateProcessingRecord::PENDING`;
- invokes one exact winner handler under a lease;
- marks processing complete before `ClearProposalSlot()`.

Insert the durable reservation transition immediately after the authoritative certificate is known and before creating/running application work. If that transition fails, return storage failure and do not call the transaction handler or cleanup. Re-entry with the same authoritative certificate must idempotently finish that transition.

The transition must also work when the node never saw a proposal: a valid authoritative mint certificate can create a `FinalizedPendingApplication` reservation directly from the certified embedded mint descriptor. Certificate authority outranks local candidate/vote history.

### Application already has the physical atomic boundary

`UTXOManager::ApplyMintEffectsAtomically()` holds the persistence gate and state lock, verifies an existing bridge application record, builds candidate UTXO maps, and writes in one RocksDB batch:

- all produced mint outputs;
- the consumed bridge input;
- `/bridge/application/v1/<chain>:<burn>:<receipt-index>`.

It returns `AlreadyApplied` only after exact live-state verification. This is the correct physical application boundary and must be extended, not split. The same batch should verify the expected reservation generation, state, slot, outpoint, certificate/winner identity and transition the reservation to `Consumed`. A separate pre- or post-application write would permit crash-visible disagreement between resource state and mint effects.

Because the reservation store and UTXO manager use the same underlying RocksDB, one batch is possible. The planner must establish one shared serialization/locking contract: application and reservation release/finalize operations cannot use unrelated mutexes while writing the same reservation key. Prefer a single reservation-store gate that can participate in the mint batch, or an API that performs the compare/check and batch mutation while holding the reservation authority lock. Document lock order with the existing UTXO persistence gate to avoid deadlock.

### Application failures need richer classification

The certificate handler currently returns `outcome::result<Check>`. `ProcessFinalizedCertificate()` treats every handler error or non-approve result as retryable `PendingApplication`. Phase 11 requires a durable distinction:

- transient/storage/unavailable: restore pending and retry the exact certified winner;
- exact already-applied: idempotent success;
- irreconcilable reservation/application/UTXO contradiction: durably write `SafetyError`, stop futile retries, retain certificate finality, and never release or apply an alternate winner.

Use a typed application disposition/error mapping for mint resources rather than guessing from log text. Existing `std::errc::state_not_recoverable` paths are likely safety errors, while RocksDB I/O and missing handler remain retryable. Missing ephemeral candidate state is never a safety error.

## Recommended Durable Model

### Record and index

Add a strict versioned protobuf record to the local consensus schema. Recommended fields:

- schema version;
- lifecycle state: `RESERVED`, `FINALIZED_PENDING_APPLICATION`, `CONSUMED`, `SAFETY_ERROR`;
- canonical 64-character slot ID;
- canonical source chain decimal text;
- 32-byte burn transaction hash (or strict canonical lowercase text);
- receipt-local log index;
- fresh random generation token with enough entropy to prevent reuse after deletion;
- maximum admitted candidate certificate-acceptance horizon;
- created/updated timestamps;
- after finality: authoritative certificate digest, proposal ID, and winning transaction hash;
- safety-error reason/code and diagnostic timestamp, without making candidate identity the owner.

Use two direct-RocksDB keys committed together:

- a slot record, e.g. `/consensus/local/v2/burn/slot/<slot-id>`;
- an outpoint index, e.g. `/consensus/local/v2/burn/outpoint/<hash(canonical-chain,burn,index)>` containing slot and generation.

The two-way mapping makes node-wide uniqueness explicit and catches a corrupt or buggy alternate slot mapping for the same outpoint. Strict scans must verify key/value identity, canonical formatting, recomputed mint slot, reciprocal index, legal state fields, and exact finality relationships. These records must never use `GlobalDB::Put()` or CRDT topics.

### Generation and ABA protection

Safe abandonment deletes both keys, as required by D-19. A later admission therefore cannot derive its generation by incrementing a deleted counter. Use a cryptographically random generation token (128 bits minimum; 256 bits is natural in this codebase). Every asynchronous cleanup/reconciliation attempt captures the expected generation and re-reads it under the store gate before deletion.

An in-process mutex alone is not the whole contract: it must be shared by every reservation writer. RocksDB's current wrapper has batches but no compare-and-swap. The safe local pattern is read/validate/batch-write under one store mutex, plus expected generation. Stale callbacks do not survive process restart; persisted generation prevents stale same-process timer work from deleting a newly recreated record.

### State transitions

| Current state | Event | Next state | Durable rule |
|---|---|---|---|
| absent | first valid admitted contender | `Reserved` | create slot + outpoint index atomically before candidate activation |
| `Reserved` | another valid contender, same slot/outpoint | `Reserved` | idempotently join and extend only the maximum candidate horizon; generation unchanged |
| absent/`Reserved` | authoritative certificate | `FinalizedPendingApplication` | bind exact certificate/winner; persist before handler or cleanup |
| `FinalizedPendingApplication` | transient application failure | same | preserve exact winner and retry metadata across restart |
| `FinalizedPendingApplication` | atomic mint application | `Consumed` | transition in same batch as outputs, bridge consumption, and application record |
| `FinalizedPendingApplication` | irreconcilable contradiction | `SafetyError` | durable fail-closed state; no release or alternate application |
| `Reserved` | safe horizon passed, no certificate | absent | conditional delete slot + outpoint keys for expected generation |
| absent after abandonment | later valid proposal | new `Reserved` | fresh unrelated generation token |

No proposal rejection, failed transaction state, best-candidate replacement, or proposal cleanup directly changes this lifecycle.

## Admission and Horizon Semantics

### Descriptor extraction and cross-checks

For mint-v2, derive a resource descriptor only after the subject handler approves:

- chain ID from `MintTxV2.chain_id`;
- burn transaction hash from the DAG uncle hash and the sole input transaction hash, requiring equality;
- receipt-local position from the sole input `output_index`;
- slot from `mint-v2:<chain>:<burn>:<index>` through the established hash function.

Cross-check the descriptor's recomputed slot against `GetSlotKey(proposal)`. Reject/fail closed on disagreement. Do not place amount, token, destination, proposer, source account, nonce, candidate hash, or current-best identity in reservation ownership. Those facts remain semantic validation/certificate payload facts.

The subject-specific handler can return `not applicable` for normal transactions, preserving their existing path. A bridge descriptor extractor or admission handler should be registered once with the nonce subject type and exposed through `Blockchain` similarly to existing subject/certificate/cleanup registration.

### Candidate horizon

The candidate selection deadline, pending-proposal TTL, and certificate acceptance horizon are different clocks. Abandonment must use the last instant at which a certificate for any admitted contender can still be first-observation valid.

Phase 10's live certificate validation accepts proposal/vote timestamps within `timestamp_window_`. Persist for each joined contender the proposal upper acceptance bound using the same overflow-safe calculation as certificate validation, and keep the maximum in the slot reservation. Do not retain candidate objects forever merely to compute this value. At exact horizon equality the reservation remains locked; release only when `now > horizon`, matching durable vote retirement.

For a local durable vote, use `DurableVoteRecord.acceptance_horizon_ms`. It already stores the minimum bound for that signed proposal/vote pair and retires only strictly beyond the horizon. A reservation's safe-release time is the maximum of its admitted-candidate horizon and every relevant durable vote horizon for that slot. The local node has one validator vote record per slot, but the algorithm should query the durable record rather than infer from transient `SlotState`.

### Bounded automatic abandonment

The existing round timer should drive a reservation reconciliation pass; do not add detached per-slot timers. For each due `Reserved` generation:

1. acquire the reservation/slot sequencing gate;
2. re-read the exact generation and state;
3. check the authoritative slot certificate with typed read errors;
4. if a certificate exists, transition to finality instead of releasing;
5. if certificate lookup is not exact `NotFound`, fail closed and retry/diagnose;
6. re-read the durable vote record and require its acceptance horizon to be strictly past (retire it durably if still active and eligible);
7. require the persisted maximum candidate horizon to be strictly past;
8. verify no admitted in-memory candidate has a later horizon/new generation;
9. atomically delete both reservation keys only if generation still matches.

Candidate admission and certificate finality must use the same slot/generation synchronization boundary. If either wins the race, stale release observes a different state/generation or a certificate and does nothing. Never release first and "reacquire afterward" for a certificate.

## Startup and Recovery

### Required ordering

`ConsensusManager::RestoreLocalState()` currently scans votes, processing markers, conflicts, safety records, and authoritative certificates before subscriptions. Add reservation slot/index scans to that same fail-closed stage. Reconciliation must finish before `subscribe`, certificate filter registration, timer start, proposal handling, or vote replay.

Recommended recovery order:

1. strict-scan certificates and local vote/process/conflict/safety/reservation records;
2. validate every reservation slot/outpoint pair and recompute its canonical mint slot;
3. cross-check certificate-backed states against the authoritative certificate digest, proposal, winner, and embedded burn descriptor;
4. cross-check vote slot and acceptance horizon without requiring ephemeral candidates;
5. synthesize/finalize protection when an authoritative mint certificate exists but no runtime candidate exists;
6. retire expired votes using Phase 10 rules;
7. conditionally abandon only uncertified due generations;
8. install runtime slot states;
9. only then enable live side effects and exact-winner recovery.

Missing proposal/candidate objects after restart are normal. A `Reserved` record remains protected until its persisted candidate/vote horizons prove abandonment. Malformed protobufs, missing reciprocal indexes, slot/outpoint recomputation mismatch, a finalized reservation bound to a different authoritative certificate, or a vote for a contradictory slot must abort consensus construction before side effects.

### Existing slot-handler startup hazard

The nonce slot-key handler is currently registered by `TransactionManager::New()`, but `ConsensusManager::New()` restores certificates/votes earlier. In a real process restart the static handler map may be empty, causing `GetSlotKey()` to fall back to a generic subject hash rather than the mint slot during restoration. Same-process tests can mask this because static registration survives object reconstruction.

Phase 11 planning must close this prerequisite for reliable burn recovery. Move the built-in nonce/mint slot resolver to code available before `ConsensusManager::RestoreLocalState()`, or inject/register it before constructing consensus. This is production startup ordering required by D-01/D-03, not mock-RPC or broader startup infrastructure.

### Transaction handler registration remains later

`TransactionManager` and UTXO loading still occur after `ConsensusManager` construction. Structural reservation/certificate/vote reconciliation must therefore not depend on an ephemeral transaction object or loaded UTXO maps. Exact mint application stays pending until the transaction handler and UTXO storage are ready, following Phase 10's existing missing-handler recovery behavior.

After handler registration/UTXO load:

- `FinalizedPendingApplication` retries the exact certified mint;
- exact existing application + consumed input completes idempotently;
- storage unavailability remains pending;
- an irreconcilable different application/consumption becomes durable `SafetyError`.

## Synthetic Bridge UTXO Considerations

The synthetic bridge UTXO should be treated as application material, not reservation authority.

- `MintFunds()` currently inserts it before consensus and ignores the boolean result of duplicate `PutUTXO()`.
- public-chain validation does not require local UTXO ownership;
- a validator that receives only a certificate may not have created the synthetic input locally;
- all nodes store UTXOs for all peers, but the synthetic owner may reflect whichever proposal/path inserted it first;
- `ApplyMintEffectsAtomically()` accepts a bridge input in `READY` or `RESERVED` state and already binds the canonical chain/burn/index through the application record.

Plans should either ensure the bridge application path can deterministically materialize the local bridge input from the approved/certified mint facts before the atomic batch, or establish it at post-validation admission. In either case, the durable reservation record—not `UTXO_RESERVED` and not `local_reservations_`—decides availability. Missing local synthetic UTXO is not automatically corruption if it can be reconstructed from the exact certified mint; a different existing application or consumed-by-different-winner state is an irreconcilable contradiction.

`GetBridgeBurnState()` and GeniusNode relayer gates must consult the durable node-wide reservation/application state. A `Reserved` result means the burn is protected, but the consensus admission API must still allow semantically valid contenders for the same slot to join it. Do not make the relayer-facing convenience check the sole consensus safety gate.

## Concurrency and Atomicity Requirements

### Same-slot admission races

Two valid proposals for one burn can validate concurrently. Exactly one creates the record; the other must observe the same slot/outpoint and join the same generation. Different outpoint for the same slot, or same outpoint through a different slot, is an integrity conflict. Neither contender owns the record.

### Admission versus finality

If certificate finality wins, admission sees `FinalizedPendingApplication`/`Consumed` and cannot create a candidate. If admission wins, finality transitions that same generation. The existing `SlotState::Finalizing` reservation should be composed with the durable burn transition; do not create a second unrelated locking system.

### Release versus admission/finality

Due cleanup captures generation G. A concurrent new contender either extends G before the conditional check, or a completed release deletes G and the contender creates fresh generation H. Cleanup for G can never delete H. A certificate always turns the current/absent record into finalized protection before any proposal cleanup.

### Application versus duplicate delivery

Phase 10's `processing_slots_` lease prevents concurrent handler invocation, and the bridge application record makes crash replay idempotent. Extend the atomic application verification to the reservation generation/certificate/winner. Duplicate local/pubsub/CRDT/recovery paths must converge on the same `Consumed` record and exact application bytes.

### Lock ordering

The existing UTXO order is `persistence_mutex_` then `utxos_mutex_`. Reservation operations need a documented order with `proposals_mutex_` and the local state-store mutex. Avoid holding `proposals_mutex_` across arbitrary storage or transaction handler work. Reserve an in-memory slot lifecycle/generation, release the proposal mutex, perform the durable operation, then recheck before activation—mirroring Phase 10 signing/finalization reservations.

## Requirement Coverage

| Requirement | Planning implication |
|---|---|
| BURN-01 | Keep semantic validation pure; add one post-approve/pre-active admission transition; durable write failure means no active candidate or vote |
| BURN-02 | Same canonical slot/outpoint joins one generation; best-candidate changes only ephemeral arbitration and never release/reacquire |
| BURN-03 | Remove bridge rollback from failed/reverted proposal paths; proposal cleanup is transaction-local; only whole-slot reconciliation can delete the reservation |
| BURN-04 | After certificate authority, persist `FinalizedPendingApplication` before handler/cleanup; atomically consume physical input with exact winner effects and reservation `Consumed` state |
| BURN-05 | Persist candidate horizon, consult exact vote horizon and authoritative certificate, release only strictly after all horizons through expected-generation conditional deletion |

## Likely Files and Responsibilities

| File | Expected role |
|---|---|
| `src/blockchain/impl/proto/ConsensusLocalState.proto` | Versioned reservation record/state and strict finality/safety fields |
| `src/blockchain/ConsensusStateStore.hpp/.cpp` | Slot/outpoint keys, strict scans, create/join/finalize/safety/release operations, two-key batches, typed errors |
| `src/blockchain/Consensus.hpp/.cpp` | Post-validation admission hook, slot lifecycle sequencing, startup reconciliation, timer-driven abandonment, pre-application finalization transition, typed application disposition |
| `src/blockchain/Blockchain.hpp`, `src/blockchain/impl/Blockchain.cpp` | Narrow registration/query forwarding for transaction-layer burn descriptor/application integration |
| `src/account/TransactionManager.hpp/.cpp` | Remove proposal-owned bridge reserve/rollback, extract/cross-check burn descriptor, register lifecycle hooks, classify exact-winner application failures, durable state relayer query |
| `src/account/UTXOManager.hpp/.cpp` | Extend atomic mint batch to verify/transition expected reservation and handle deterministic bridge-input materialization without making it the reservation authority |
| `src/blockchain/impl/CMakeLists.txt` | Regenerate/link local-state protobuf if required by build layout |
| `test/src/blockchain/consensus_vote_journal_test.cpp` | Strict store/restart/horizon reconciliation and startup-order assertions |
| `test/src/blockchain/consensus_finalization_test.cpp` | Certificate-before-resource, failure, duplicate ingress, and finalization/admission/release races |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Separate proposal expiry from finality and deterministic candidate cleanup semantics |
| `test/src/account/transaction_manager_pending_lifecycle_test.cpp` | Pure validation, contender sharing, no proposal-local release, exact-winner retry/safety error |
| `test/src/account/utxo_manager_test.cpp` | One-batch outputs/application/input/reservation transition and crash seams |

Prefer a new focused `bridge_burn_reservation_test` target if adding all store/race cases would make Phase 10 suites hard to navigate. Reuse the friend-only production seams and real RocksDB/CRDT fixtures already established in Phase 10.

## Suggested Plan/Wave Decomposition

1. **Test harness and durable reservation store** — protobuf, two-key strict store, generation semantics, typed errors, deterministic clocks/fault seams, scan validation.
2. **Startup reconciliation and built-in mint identity availability** — restore before side effects, cross-check certificate/vote/outpoint state, real-process-like handler absence tests, no ephemeral-candidate requirement.
3. **Post-validation consensus admission** — descriptor hook, persist-before-active ordering, same-slot contender join, storage failure suppression, remove proposal-owned bridge reservation paths.
4. **Finality transition and exact-winner failure classification** — certificate authority to durable final pending before handler, missing-record certificate path, transient retry, durable safety error.
5. **Atomic mint consumption** — extend `ApplyMintEffectsAtomically` so reservation `Consumed`, bridge input, produced outputs, and application record commit together; verify duplicate/restart behavior.
6. **Deterministic abandonment and race closure** — timer/recovery pass, strict horizon boundary, generation-conditional deletion, admission/finality/release interleavings, separate pending expiry from finalized cleanup.
7. **Focused integration and regression** — BURN-01..05 matrix plus Phase 9/10 certificate, vote, compatibility, transaction pending, and UTXO suites.

The store must precede admission/finality. Startup reconciliation should land before live admission uses the records. Finality state should precede atomic application changes. Abandonment should be last because it depends on admission horizons, vote records, and certificate/resource transitions.

## Implementation Pitfalls

1. Do not reuse the transaction hash, proposer, nonce, account, candidate ID, or current best as reservation owner.
2. Do not use `UTXO_RESERVED` or `local_reservations_` as durable safety authority.
3. Do not mutate reservation state inside semantic validation or public-chain RPC verification.
4. Do not reserve a `Pending` proposal; reserve only after a complete `Approve` result.
5. Do not let `MintFunds`, `ChangeTransactionState(FAILED)`, revert, or proposal cleanup release a bridge burn.
6. Do not reject a same-slot contender merely because the burn is already reserved; idempotently join the slot generation.
7. Do not key only by account-local UTXO owner; enforce reciprocal slot/outpoint identity node-wide.
8. Do not persist reservation records via CRDT or attach them to replicated certificate deltas.
9. Do not use pending TTL, selection deadline, or round number as certificate expiry.
10. Do not release at horizon equality; current certificate/vote validation remains valid through the boundary.
11. Do not require ephemeral candidates to exist after restart.
12. Do not wait indefinitely for absent candidates; persist a deterministic candidate acceptance horizon.
13. Do not delete by slot alone; require expected generation and reciprocal outpoint index.
14. Do not replace random generation with an in-memory counter that resets after deletion/restart.
15. Do not apply the winning mint if durable final reservation persistence failed.
16. Do not split physical input consumption from outputs/application/reservation-consumed transition.
17. Do not classify all application errors as retryable; irreconcilable different-winner state must become durable safety error.
18. Do not classify reconstructible absence of a local synthetic bridge UTXO as corruption without checking the certified facts.
19. Do not make a processing marker or reservation record a second finality authority; the authoritative slot certificate remains the winner.
20. Do not let static slot-handler survival in same-process tests hide real-process startup ordering.
21. Do not add detached timer threads; use the owned consensus timer and shutdown activity leases.
22. Do not expand this phase into mock-RPC/startup simulation or the full 11-node race; Phase 12 owns the end-to-end proof.

## Validation Architecture

### Test infrastructure

| Property | Value |
|---|---|
| Framework | GoogleTest through repository `addtest(...)` |
| Build tree | `build/OSX/Release` |
| Focused build | `cmake --build build/OSX/Release --target <target> -j2` |
| Focused run | `build/OSX/Release/test_bin/<target> --gtest_brief=1` |
| Primary existing targets | `consensus_vote_journal_test`, `consensus_finalization_test`, `consensus_pending_lifecycle_test`, `transaction_manager_pending_lifecycle_test`, `utxo_manager_test` |
| Regression targets | `consensus_certificate_store_test`, `certificate_compatibility_test`, `network_config_precedence_test` plus the Phase 9/10 gate |

Tests should use explicit clocks, fault callbacks, predicate barriers, and friend-only access. Avoid wall-clock sleeps. Restart tests must reconstruct the manager/store over the same RocksDB while clearing static handler state where needed to model a real process.

### Nyquist validation layers

#### Layer 1: strict store unit tests

- first reservation atomically creates reciprocal slot/outpoint records;
- exact same slot/outpoint joins idempotently and preserves generation;
- contender join monotonically extends the maximum candidate horizon;
- same slot/different outpoint and same outpoint/different slot fail conflict/integrity;
- malformed key/value, unknown state/version, missing reciprocal index, noncanonical chain/hash, recomputed-slot mismatch, and illegal finality fields fail scan;
- persistence failure leaves neither half visible;
- safe release removes both halves only for expected generation;
- stale generation cannot remove a recreated reservation;
- finalized, consumed, and safety-error states can never transition back to reserved/absent.

#### Layer 2: consensus admission/lifecycle tests

- rejected and pending mint proposals create no reservation;
- approved mint persistence occurs before `ProposalState` becomes selectable;
- reservation write failure creates no active candidate and no vote publication;
- two differently identified proposals for one burn share generation while comparator may change best;
- losing proposal timeout/failure/cleanup leaves the reservation unchanged;
- normal transactions bypass the burn hook unchanged;
- proposal expiry no longer marks an uncertified slot finalized;
- admission racing release renews/creates safely; admission racing finality cannot reopen the burn.

#### Layer 3: restart reconciliation tests

- a reserved slot survives restart without any candidate objects and remains unavailable before subscribe/timer/replay;
- active vote keeps it reserved through equality; strictly after vote/candidate horizons it retires/releases automatically when no certificate exists;
- authoritative certificate upgrades or creates `FinalizedPendingApplication` before handler registration;
- missing handler keeps work pending and later registration retries the exact winner;
- malformed/contradictory reservation-certificate-vote records return `nullptr` before observable startup side effects;
- slot derivation during a real-process-like restart does not depend on a stale static handler registration.

#### Layer 4: finality/application atomicity tests

- certificate authority is persisted, then reservation final pending is durable, then handler begins;
- injected final-reservation write failure prevents handler and cleanup;
- handler storage failure retains exact pending winner/reservation across restart;
- exact already-applied winner is idempotent;
- different existing application or consumed-by-other-winner state writes durable safety error and stops retry;
- outputs, bridge application, physical input consumption, and reservation `Consumed` are all absent before injected batch commit and all present after it;
- crash after batch commit/before processing complete replays to `AlreadyApplied` without duplicate outputs;
- duplicate local/pubsub/CRDT/recovery ingress applies once and never releases the burn.

#### Layer 5: deterministic race tests

- release paused after reading generation loses to a new contender extension;
- release paused before delete cannot remove a freshly recreated generation;
- release racing certificate transitions to final pending, never absent;
- proposal cleanup racing certificate cannot run before durable final pending/application rules;
- shutdown drains reservation reconciliation/application callbacks without post-destruction work.

### Requirement-to-test map

| Requirement | Minimum direct evidence | Recommended targets |
|---|---|---|
| BURN-01 | validation creates no record; approved admission writes before active candidate; write failure produces no vote | new reservation test + `consensus_pending_lifecycle_test` + transaction manager test |
| BURN-02 | differently identified contenders share one generation and best may change without store churn | reservation/consensus test |
| BURN-03 | failed/rejected/cleaned loser cannot release; proposal cleanup callback observes reservation still protected | consensus finalization + transaction manager test |
| BURN-04 | authoritative certificate writes final pending before handler; one batch produces consumed reservation/application/input/outputs; duplicates idempotent | `consensus_finalization_test`, `utxo_manager_test`, transaction manager test |
| BURN-05 | no release with certificate/candidate/usable vote; strict horizon release; restart and ABA races | reservation store + vote journal + pending lifecycle tests |

### Phase gate

The phase gate should build and run the five primary targets plus any new reservation target, then rerun the exact Phase 10 eight-target gate. Phase 12—not Phase 11—will run and assert the complete 11-node single-burn race, HandleCertificate/CRDT interleaving proof, and milestone compatibility matrix.

## Scope Boundary

Phase 11 includes only production startup changes necessary to restore and reconcile durable reservation state before consensus activity. It does not add mock RPC endpoints, relayer startup simulation, broad fault-injection infrastructure, or the full 11-node race. It may add focused deterministic single-process concurrency/restart tests required to prove BURN-01 through BURN-05. The multi-node behavioral proof remains Phase 12.

