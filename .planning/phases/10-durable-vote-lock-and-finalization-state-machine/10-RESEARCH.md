---
phase: 10-durable-vote-lock-and-finalization-state-machine
status: complete
researched: 2026-07-27
---

# Phase 10 Research: Durable Vote Lock and Finalization State Machine

## Executive Summary

Phase 10 must replace two independent pieces of transient behavior with durable slot-scoped state:

1. The current local vote guard is an in-memory set of proposal IDs. It votes immediately, can vote again for a better proposal in the same slot, is erased with proposal cleanup, and is empty after restart.
2. The current certificate paths do not finalize the slot in one operation. Local submission stores the certificate pair and publishes it, pubsub handling only validates and clears memory, and CRDT callback delivery later applies the transaction. That is the exact gap that allowed the observed second certificate.

The safest design is a local, versioned consensus state store backed directly by the node's RocksDB, plus one `FinalizeSlot(certificate, source)` operation. The local store owns exact signed vote bytes, processing markers, and conflict evidence; none of those node-local records should be replicated through CRDT. The existing `/cert/v2/slot/<slot>` certificate remains the only durable winner authority.

Candidate selection, vote creation, finalization, and slot cleanup need one explicit state machine under one synchronization boundary. Timers should be driven by the existing owned round-timer thread rather than per-slot detached callbacks. A certificate that is structurally valid and valid for first acceptance must finalize regardless of the local candidate or vote. Only successful application may mark processing complete and permit cleanup.

One protocol gap must be resolved explicitly during implementation: certificate normalization currently imposes no time validity on proposal or vote timestamps. Therefore no uncertified vote has a deterministic expiry under today's live certificate acceptance rules. Phase 10 should split timeless stored-certificate normalization from first-observation acceptance validity, apply the existing timestamp window to first acceptance, and derive the vote-lock horizon from those exact same checks. Historical stored certificates must remain readable and replayable after the live acceptance horizon.

## Current Implementation Findings

### Startup and lifetime ordering

`ConsensusManager::New()` currently:

1. acquires the CRDT work journal;
2. validates certificate namespace compatibility;
3. subscribes to consensus pubsub;
4. starts the round timer;
5. registers certificate filters/callbacks;
6. recovers certificate work.

This ordering is insufficient for VOTE-03 and context D-01. Vote records must be opened, scanned, parsed, cross-checked, and restored before step 3. An unreadable query, malformed key, malformed payload, voter mismatch, slot/proposal mismatch, invalid signature, contradictory active records, or unknown version must return `nullptr` before subscription, timer start, filter/listener registration, or outbound replay.

There is another recovery-order hazard: transaction certificate handlers are registered by `TransactionManager::New()` after `Blockchain::New()` constructs `ConsensusManager`. Current `CertificateReceived()` marks journal work done when no subject handler exists. Recovery must instead leave processing pending when a handler is not registered, and handler registration should wake processing for that subject. Vote replay can occur after pubsub is ready, but vote locks must already be restored before then.

The round timer is owned and joined by `Close()`, and it uses `weak_ptr` promotion. Reuse it for candidate deadlines and recovery. Do not add detached per-slot threads or callbacks that can outlive `ConsensusManager`. The destructor itself does not join a joinable thread, so plans must preserve the existing owner contract that calls `Close()` and should test closure with open selection windows.

### Proposal and vote state today

`ProposalState` stores the proposal, accepted votes, slot key, tally state, and certificate-round data. `SlotState` stores:

- `best_proposal_id`;
- `best_tx_hash`;
- `voted_proposal_ids`, an in-memory set.

`ContinueProposalAfterSubject()` immediately updates the best candidate and signs it if that exact proposal ID is not in `voted_proposal_ids`. A later better candidate can therefore be signed as well. The set is not durable and is erased by `ClearProposalSlot()`.

`CreateVote()` sets proposal ID, voter ID, approval, current wall-clock timestamp, and the three endpoint slot hashes before signing. `SubmitVote()` immediately serializes a new `ConsensusMessage`, publishes it, and only then locally handles it. There is no persistence boundary between signing and publication.

The exact signing surface is `ConsensusAuth::VoteSigningBytes()`: copy the protobuf, clear only `signature`, and serialize all remaining fields. The slot hash fields and timestamp are signature-critical. Replay must never call `CreateVote()` because it would sample a new timestamp, possibly a new endpoint snapshot, and produce a new signature.

Persist the complete serialized signed `ConsensusVote` and the exact serialized outbound `ConsensusMessage` payload generated from it. Publication and restart replay should send the stored envelope bytes through a raw publish helper. Parsing a vote and reserializing it is logically equivalent for known fields but does not satisfy the byte-identical replay decision as strongly as publishing the stored bytes.

### Candidate ordering

`IsBetterProposal()` is the existing ordering rule:

- for nonce subjects, select the lexicographically smaller transaction hash;
- when transaction hashes are equal, select the lexicographically smaller proposal ID;
- for other subject types, select the lexicographically smaller proposal ID.

This already supplies the required deterministic proposal-ID tie break. It should be made the sole comparison used while a slot is `Selecting`. The first admitted valid proposal starts one `steady_clock` deadline. Better candidates update the current best but never change the deadline. At expiry, selection atomically freezes the chosen proposal ID before signing begins. Proposals arriving in `Signing`, `Voted`, `Finalized*`, `Applied`, or `SafetyViolation` may be retained for diagnostics but cannot alter the vote.

Current `HandleVote()` drops a valid peer vote when that proposal is not this node's local `best_proposal_id`. That makes local candidate preference leak into certificate aggregation. Remove that gate: local ranking determines only this validator's signature. Aggregators must be able to collect valid one-per-validator votes for a proposal even if their own local best differs.

### Proposal and vote acceptance horizon

Incoming proposals pass `IsTimestampSane(proposal.timestamp())`, using the default symmetric five-minute `timestamp_window_`. Incoming votes do not check timestamp sanity. `NormalizeCertificate()` verifies proposal/vote signatures, registry, quorum, ordering, and canonical deterministic bytes, but does not call `IsTimestampSane()` for the proposal or votes. Consequently a newly received certificate containing an arbitrarily old valid signature can currently be accepted forever.

VOTE-07 must not retire a vote merely because a pending-proposal TTL, candidate timer, round timer, or local transaction timeout elapsed. Those timers do not make the signature unusable. There are two safe choices:

- keep uncertified vote locks forever; or
- define a live first-acceptance rule and use its exact upper bound for retirement.

The phase decisions include validation/expiry metadata and durable retirement, so the recommended implementation is the second choice. Split certificate validation into:

- **timeless structural normalization** for authoritative stored certificate reads and already-finalized replay; and
- **live first-observation validation** for an empty slot, applying timestamp sanity to the signed proposal and every signed vote.

For this validator's vote, the deterministic last acceptance instant is the minimum of the signed proposal's and signed vote's accepted upper bounds. Store that absolute horizon in the journal and recompute it from stored signed material during recovery. Retire only after the same live validation function says a new certificate containing that vote can no longer be accepted. An already stored certificate remains authoritative and processable after this horizon.

### Certificate paths and the present race

The three paths are currently separate:

| Path | Current behavior |
|------|------------------|
| Local `SubmitCertificate()` | Normalize, preflight slot/index, atomically put CRDT pair, publish pubsub certificate; does not call `HandleCertificate()` or application handler |
| Pubsub `HandleCertificate()` | Validate, compare against local best, then `ClearProposalSlot()`; does not persist finality or apply the transaction |
| CRDT `CertificateReceived()` | Normalize, call registry finalization and subject handler, then mark work done/stalled |

This creates two unsafe rules that Phase 10 must remove:

- `HandleCertificate()` clears transient exclusivity before transaction application.
- `ValidateCertificateBestProposal()` rejects a valid certificate that differs from local best (except registry-batch subjects), contradicting VOTE-06 and D-13.

All three paths must normalize their source and call one finalization operation. `ValidateCertificateBestProposal()` should be removed from certificate acceptance. Local preference governs signing only.

`ProcessCertificates()` also calls `ClearProposalSlot()` immediately after `SubmitCertificate()` regardless of submission/application outcome. It must delegate all transition and cleanup decisions to finalization. No caller should clear a slot simply because a certificate was created or received.

### Certificate work journal and application idempotency

`CRDTWorkJournal` is a direct-RocksDB local journal with `Seen`, `Processing`, and `Stalled` states, leases, attempt counts, and stale-processing recovery. It is useful as a model, but not sufficient as the Phase 10 state store:

- mutating APIs return `void` or `bool` and often silently swallow read/write/parse failures;
- entries contain only a logical key and work state, not winner digest/proposal identity;
- `MarkSeen` is driven by the CRDT filter, so local/pubsub first observation is not uniformly covered;
- `MarkDone` removes evidence instead of retaining an explicit durable complete marker;
- it cannot represent vote locks or structured conflict evidence.

Create a dedicated versioned local consensus state store, or harden/generalize the journal with typed errors and atomic records. It should use `GlobalDB::GetDataStore()` only as a handle to the underlying local RocksDB; do not use `GlobalDB::Put()` for vote locks, processing markers, or conflicts because that would replicate private validator state and local diagnostics.

The existing UTXO atomic mint application is the best persistence model: `UTXOManager::ApplyMintEffectsAtomically()` holds a persistence gate, validates an existing application record, builds candidate state, and commits the application record with all mint effects in one RocksDB batch. Duplicate exact application returns `AlreadyApplied`; mismatches fail closed. Phase 10's processing marker should similarly bind slot, authoritative certificate digest, and winning proposal/transaction identity.

Within one process, a finalization mutex must serialize duplicate local, pubsub, CRDT, and recovery invocations. Across a crash, the durable marker controls replay:

- `Pending`: certificate finality exists but application is not complete;
- `Processing`: leased attempt in progress; stale on restart becomes pending;
- `Complete`: exact winning handler completed successfully;
- optionally `Blocked/SafetyViolation` as a separate slot flag, without changing the winner.

If a handler errors, rejects, stalls, or its owner is not registered, leave the marker pending/stalled and retain proposal/vote state. Never roll back the certificate. Mark complete before cleanup. The handler must remain idempotent because a crash can occur after effects commit but before the marker commits; Phase 9 already provides this for mint-v2 through the durable bridge application record.

### Atomicity boundaries

The existing certificate plus transaction index is already one CRDT delta via `GlobalDB::Put(vector<DataPair>)`. It is the durable finality point. Node-local processing metadata lives in a different RocksDB namespace and cannot be atomically committed with a CRDT delta through the current API.

Use this recoverable ordering:

1. validate/normalize the certificate and derive slot, winner, and certificate digest;
2. under the finalization gate, read the authoritative slot;
3. if empty and live-valid, persist the certificate/index pair;
4. immediately establish in-memory `FinalizedPendingApplication` and ensure a local pending processing marker exists;
5. apply the exact winning handler once under the processing lease;
6. durably mark the marker complete;
7. only then clean candidates, pending votes, and temporary vote state.

A crash between steps 3 and 4 is recoverable because startup enumerates authoritative slot certificates and synthesizes/validates missing pending markers before normal participation. The certificate, not the marker, is finality. A crash between steps 5 and 6 is recovered by replaying the idempotent exact winner.

The existing CRDT store has no distributed compare-and-swap. A local mutex closes same-process ingress races, and Phase 9's filter rejects conflicting occupied-slot deltas, but global formation safety comes from durable validator non-equivocation. Plans must not describe certificate preflight as a global CAS.

### Conflicting certificates

Local submission already returns `CertificateStoreError::Conflict` for a differing occupied slot. CRDT delta filtering logs and rejects a differing occupied slot before its callback. Neither path creates durable structured evidence, and the CRDT callback will never see a rejected conflict.

Conflict classification must happen only after the incoming certificate is structurally and cryptographically valid. In particular, move/perform normalization before the current replicated occupied-slot conflict branch. Then record evidence at the rejection point for all sources:

- local submission;
- pubsub delivery;
- CRDT pre-merge filtering;
- recovery, if durable records contradict each other.

Compute SHA-256 over each canonical deterministic certificate byte string. Sort the two digests to form the deduplication identity. A local record should contain version, slot ID, original/incoming proposal IDs, original/incoming digests, first source, sources-seen bitmask or equivalent, first/last observation timestamps, and observation count. It must not contain a second full certificate payload. Repeated observation updates count and last-seen in one local RocksDB batch.

Persistently mark the slot `SafetyViolation`, restore that state at startup, reject further proposals, local voting, aggregation, and certificate publication for the slot, and never overwrite the original certificate. Processing of the already-authoritative winner may continue. A conflicting certificate must not go through normal pubsub publication. Increment a dedicated atomic conflict metric and emit a critical log carrying the evidence key and both proposal IDs/digests.

## Recommended State Model

Use an explicit slot lifecycle instead of loosely related booleans and sets:

| State | Meaning | Allowed next states |
|-------|---------|---------------------|
| `Empty` | No admitted valid candidate, vote, or finality | `Selecting`, `FinalizedPendingApplication`, `SafetyViolation` |
| `Selecting` | Candidate set open until one immutable deadline | `Signing`, `FinalizedPendingApplication`, `SafetyViolation` |
| `Signing` | Candidate frozen; one signature attempt reserved | `Voted`, `FinalizedPendingApplication`, `SafetyViolation`, same-candidate retry on persistence failure |
| `Voted` | Exact vote durably stored and published/replayable | `FinalizedPendingApplication`, `Retired`, `SafetyViolation` |
| `Retired` | Old vote durably proven unusable | `Selecting` for a later generation, `FinalizedPendingApplication`, `SafetyViolation` |
| `FinalizedPendingApplication` | Authoritative certificate exists; exact winner application pending | `Applied`, `SafetyViolation` |
| `Applied` | Processing marker complete; cleanup is permitted | `SafetyViolation` on later conflict |
| `SafetyViolation` | Conflicting otherwise-valid certificate observed | no new participation; original application retry remains allowed |

The finalization gate and the candidate deadline must synchronize on the same slot state. If finalization wins the race, the signer must not publish. If signing freezes first, it may persist/publish that one vote, then accept a certificate for either proposal. A signature created outside the mutex must be rechecked against the reserved `Signing` generation before durable write/publication.

Suggested local namespaces (exact names are discretionary):

- `/consensus/local/v2/vote/<validator-id-hash>/<slot-id>`
- `/consensus/local/v2/process/<slot-id>`
- `/consensus/local/v2/conflict/<slot-id>/<digest-pair>`

Use a versioned protobuf or equally strict length-delimited encoding, reject unknown versions/fields, and verify that every value agrees with its key. A vote record should include at least canonical slot, proposal ID, validator identity, exact signed vote bytes, exact outbound envelope bytes, signed proposal bytes or sufficient signed validation context, registry CID/epoch, creation timestamp, absolute acceptance horizon, lifecycle state/generation, and last publication metadata.

## Unified Finalization Contract

`FinalizeSlot(certificate, DeliverySource)` should return a typed result such as `Finalized`, `AlreadyFinalized`, `Applied`, `PendingApplication`, `Conflict`, `Invalid`, or `StorageFailure`. Its contract should be:

1. Structural/canonical normalization is identical across all delivery sources.
2. For an empty slot, live first-observation validity is required before persistence.
3. For an occupied slot, byte-identical/digest-identical input is idempotent even after the live horizon.
4. A differing otherwise-valid input records a conflict and never changes the authoritative certificate.
5. Authoritative certificate persistence occurs before any cleanup or application.
6. Local vote preference is never an acceptance condition.
7. The processing marker binds the exact authoritative digest and winner.
8. Only one handler attempt runs at a time; duplicate paths join/observe the same state.
9. Handler failure leaves finality intact and work pending.
10. Completion is durable before candidates/votes are cleaned.

Source adapters should be thin:

- `SubmitCertificate()` calls finalization, publishes only the accepted canonical certificate (never a conflict), and treats exact replay as success.
- `HandleCertificate()` calls finalization and does no direct cleanup.
- `CertificateReceived()` calls finalization, then updates/retires the legacy CRDT work entry only when the new processing marker is complete.
- `RecoverPendingCertificateWork()` enumerates authoritative certificate/processing state and calls finalization recovery, rather than reimplementing application decisions.

## Configuration Recommendation

Use `consensus_vote_selection_window_ms` in `network_config.json`, with a conservative default of `500ms`, matching the existing default consensus round duration. Reject zero, negative, non-integer, and unreasonably large values; fall back to the compiled network default with a warning.

Configuration must reach `ConsensusManager` before its factory starts subscriptions or timers. The clean shape is a `ConsensusConfig` value passed `GeniusNode -> Blockchain::New -> ConsensusManager::New`, with defaults preserving direct test call sites. A setter invoked after `ConsensusManager::New()` is too late for startup ordering. Test the config parser separately and keep a private/test override for deterministic short windows.

Use `steady_clock` for in-process candidate deadlines and system-clock Unix milliseconds only for durable signed validity horizons and diagnostic timestamps. Later candidates never mutate `selection_deadline`. The timer wait should include the nearest candidate deadline; a fixed 500ms polling floor can otherwise make a 500ms configured window nondeterministically longer.

## Requirement Coverage

| Requirement | Planning implication |
|-------------|----------------------|
| CERT-05 | The authoritative slot certificate is persisted before application or cleanup; startup repairs missing processing markers from certificates |
| CERT-06 | All ingress calls `FinalizeSlot`; one durable marker plus in-process gate prevents concurrent duplicate application; crash replay requires idempotent handlers |
| CERT-07 | Validate conflicts, preserve original winner, persist deduplicated digest evidence, stop slot participation, log critical, increment metric, never rebroadcast |
| VOTE-01 | Serialize signed vote/envelope once, durably write the slot journal, then publish the stored bytes |
| VOTE-02 | One active slot record and state-machine generation allows only exact stored-vote replay; different proposal signing is rejected |
| VOTE-03 | Scan/validate/restore locks before subscription/timer/filter side effects; replay exact bytes only after transport is ready |
| VOTE-04 | First valid candidate starts fixed window; existing comparator selects best; proposal ID breaks exact ties |
| VOTE-05 | `Signing` freezes selection; `Voted` may track later candidates diagnostically but never replaces/retracts the vote |
| VOTE-06 | Remove local-best certificate rejection; any valid certificate finalizes its winner, while processing marker prevents multiple applications |
| VOTE-07 | Use the same first-observation acceptance horizon for certificate validation and durable lock retirement; never use local TTL alone |

## Suggested Plan Decomposition

1. **Local consensus state store and startup validation** — typed direct-RocksDB records, exact vote encoding, processing/conflict records, fail-closed scan before side effects.
2. **Candidate window and durable vote publication** — explicit slot states, comparator/tie behavior, timer integration, raw-byte publish/replay, durable retirement rule.
3. **Unified finalization and processing recovery** — split structural/live validation, `FinalizeSlot`, marker leases, handler registration wake-up, finality-before-cleanup.
4. **Conflict evidence and participation stop** — all ingress sources including pre-merge CRDT rejection, digest-pair dedup, metric/logging, startup restoration.
5. **Focused integration and lifecycle closure** — concurrent ingress, crash seams, shutdown with open windows, configuration propagation, compatibility regression.

The state-store foundation should precede vote and finalization work. Candidate/vote and finalization can then be implemented against the same explicit slot model, followed by conflict handling because the CRDT filter must call the finalized conflict recorder.

## Implementation Pitfalls

1. Do not persist only slot and proposal ID; exact signed vote and exact publication bytes are required for replay.
2. Do not call `CreateVote()` during recovery or publish a newly serialized logical replacement.
3. Do not hold only `voted_proposal_ids`; proposal cleanup and restart erase it.
4. Do not let pending-proposal TTL, candidate deadline, or round advancement retire a usable vote.
5. Do not time-expire authoritative stored certificates. Live acceptance expiry applies only before first finalization.
6. Do not call the signer twice when persistence or publication fails. Retain the same signed bytes and retry that candidate only.
7. Do not hold the broad proposal mutex across an arbitrary signer or application callback without a reserved state/generation and recheck.
8. Do not use per-slot detached timer threads or callbacks capturing strong manager ownership.
9. Do not reject a certificate because it is not the local best or local vote.
10. Do not drop peer votes merely because they target a non-local-best candidate.
11. Do not clean in `ProcessCertificates()` or `HandleCertificate()`; cleanup belongs after durable processing completion.
12. Do not mark missing-handler work complete during startup.
13. Do not store private vote locks or local conflict evidence through replicated `GlobalDB::Put()`.
14. Do not record malformed/invalid certificates as safety conflicts; conflict evidence is for otherwise-valid competing certificates.
15. Do not wait for a CRDT callback to record a CRDT conflict; the pre-merge filter rejects it before callbacks.
16. Do not overwrite the original slot certificate or apply the conflict winner.
17. Do not treat local preflight plus mutex as distributed compare-and-swap.
18. Do not assume a processing marker alone proves finality; it must always be checked against the authoritative slot certificate.

## Validation Architecture

### Test infrastructure

| Property | Value |
|----------|-------|
| Framework | GoogleTest through repository `addtest(...)` helper |
| Build tree | `build/OSX/Release` |
| Test binaries | `build/OSX/Release/test_bin/<target>` |
| Focused build | `cmake --build build/OSX/Release --target <target> -j2` |
| Focused run | `build/OSX/Release/test_bin/<target> --gtest_brief=1` |
| Relevant suite | `ctest --test-dir build/OSX/Release -R '(consensus_vote_journal|consensus_finalization|consensus_pending_lifecycle|consensus_certificate_store|network_config_precedence|transaction_manager_pending_lifecycle|utxo_manager)' --output-on-failure` |
| Feedback target | Pure journal/state tests under 10s; CRDT-backed finalization tests under 30s |

The old `consensus_certificate_test` target is commented out. Reuse its fixture ideas only; active Phase 10 coverage should live in new focused targets plus the existing active `consensus_certificate_store_test` and `consensus_pending_lifecycle_test` targets.

### Required test seams

Prefer private friend-only seams, following `ConsensusManagerTestAccess`, rather than production failure APIs:

- state-store read/write/query failure injection;
- observer after signature, after durable vote commit, and before raw publication;
- controllable clock or explicit `now` for selection and acceptance horizons;
- direct candidate-deadline processing without wall-clock sleeps;
- observer after certificate pair commit, after pending marker commit, after handler effect, and before complete marker;
- handler invocation counter/blocker for concurrent ingress;
- conflict metric/evidence readers;
- startup side-effect counters for subscribe, listener/filter registration, timer, and publish.

Tests should use barriers/latches or fault observers instead of long sleeps. Restart tests must close the first manager and construct a new manager over the same database path.

### Requirement-to-test map

| Requirement | Focused behavior | Test level | Recommended target | Pre-phase status |
|-------------|------------------|------------|--------------------|------------------|
| VOTE-01, VOTE-02 | Durable commit occurs before publish; concurrent competing candidates produce one exact signature; duplicate request reuses exact bytes | unit/integration | `consensus_vote_journal_test` | Missing Wave 0 target |
| VOTE-03 | Restart restores lock before any side effect and republishes byte-identical stored envelope; corrupt journal refuses startup | RocksDB/CRDT fixture | `consensus_vote_journal_test` | Missing Wave 0 target |
| VOTE-04, VOTE-05 | Better-before-deadline wins, deadline never extends, tie uses proposal ID, late better candidate cannot revote | deterministic state/timer unit | `consensus_pending_lifecycle_test` or new `consensus_vote_selection_test` | Existing target needs cases/seams |
| VOTE-07 | Boundary just before/at/after live certificate horizon; durable retirement precedes a later-generation vote; finalized old certificate remains readable | unit/integration | `consensus_vote_journal_test`, `consensus_certificate_store_test` | Missing |
| CERT-05, VOTE-06 | Certificate for non-local-voted winner persists finality before cleanup/application and suppresses signer racing at deadline | deterministic concurrent integration | `consensus_finalization_test` | Missing Wave 0 target |
| CERT-06 | Local, pubsub, CRDT, and recovery delivery call handler once concurrently; failure leaves pending; restart retries exact winner; complete duplicate is no-op | CRDT/RocksDB integration | `consensus_finalization_test` | Missing Wave 0 target; mint handler already has atomic primitive |
| CERT-07 | Valid conflict from each source preserves original, writes one dedup record, increments count/last-seen and metric, blocks slot, and is not published | CRDT filter + local store integration | `consensus_finalization_test`, `consensus_certificate_store_test` | Existing store only returns/logs conflict |
| D-01 startup | Malformed/unreadable/identity-mismatched state fails before subscribe/timer/listener/publish | factory integration | `consensus_vote_journal_test` | Missing |
| D-05/D-06 window config | Config value reaches manager before timer start; invalid/missing values use default | parser/factory unit | `network_config_precedence_test` | Existing target needs new key cases |
| Lifetime | Close with open windows or blocked recovery joins cleanly and invokes no callback after destruction | concurrency unit | `consensus_finalization_test` | Missing |

### Minimum scenario matrix

#### Vote journal

- first vote record contains canonical slot, proposal, local voter, exact signed vote bytes, exact envelope bytes, registry metadata, and recomputable horizon;
- write failure produces no publication and no second-candidate signature;
- crash after durable write/before publish replays identical bytes;
- crash after publish restores the same vote and never signs competitor;
- duplicate same proposal republishes/stays idempotent without signer invocation;
- corrupt key, truncated record, unknown version, wrong voter, wrong slot, wrong proposal ID, invalid signature, or contradictory state fails startup;
- finalized slot suppresses vote replay and restores finalization processing;
- expired lock is durably retired before a new selection/vote generation.

#### Candidate selection and races

- first valid candidate fixes the deadline;
- invalid/pending candidates do not incorrectly start voting;
- better candidate before deadline replaces best;
- worse/equal candidate cannot change winner except proposal-ID tie rule;
- later candidates do not extend deadline;
- proposal arriving exactly as deadline freezes has one deterministic lock ordering and at most one vote;
- certificate racing signer results in either one stored vote plus finality or finality with no publication, never a post-finality vote;
- shutdown cancels deadline work and joins the timer.

#### Finalization and recovery

- each source alone finalizes and applies;
- all sources concurrently invoke one handler attempt;
- certificate is queryable by slot before handler observer fires;
- candidate, pending votes, and vote journal remain while handler fails;
- stale `Processing` marker retries on restart;
- crash after application/before marker completion reaches handler's `AlreadyApplied`/idempotent path and then completes;
- complete marker with missing/mismatched authoritative certificate fails closed;
- winner differing from local vote applies only the certificate winner;
- exact duplicate after live time expiry is still an idempotent finalized replay.

#### Conflicts

- local, pubsub, and CRDT pre-merge conflicts all produce the same digest-pair identity;
- invalid competing payload is rejected without safety-conflict evidence;
- repeated same pair updates only count/last seen/source summary;
- reversed arrival/digest ordering deduplicates to the same key;
- evidence omits full certificate bytes;
- original winner lookup remains unchanged and second handler is never called;
- restart restores `SafetyViolation` and prevents proposal, vote, aggregation, and normal certificate publish for the slot;
- original winner pending application can still complete.

### Sampling strategy for Nyquist VALIDATION.md

- **Wave 0:** add `consensus_vote_journal_test.cpp` and `consensus_finalization_test.cpp` targets plus deterministic clock/fault friend seams.
- **After every state-store task:** build and run `consensus_vote_journal_test`.
- **After every selection task:** run `consensus_pending_lifecycle_test` and the vote journal target.
- **After every finalization/conflict task:** run `consensus_finalization_test` and `consensus_certificate_store_test`.
- **After every plan wave:** run the relevant-suite CTest regex above.
- **Before phase verification:** run the relevant suite plus existing `certificate_compatibility_test`, `transaction_manager_pending_lifecycle_test`, and `utxo_manager_test`. The 11-node race remains Phase 12, but Phase 10 must leave deterministic seams for that suite.
- **Maximum sampling gap:** no more than two implementation tasks without an automated focused run; no watch-mode commands.

## Sources

- `.planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-CONTEXT.md`
- `.planning/REQUIREMENTS.md`
- `.planning/STATE.md`
- `.planning/ROADMAP.md`
- `.planning/PROJECT.md`
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-CONTEXT.md`
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-RESEARCH.md`
- `src/blockchain/Consensus.hpp`
- `src/blockchain/Consensus.cpp`
- `src/blockchain/ConsensusAuth.hpp`
- `src/blockchain/impl/proto/Consensus.proto`
- `src/blockchain/Blockchain.hpp`
- `src/blockchain/impl/Blockchain.cpp`
- `src/crdt/globaldb/globaldb.hpp`
- `src/crdt/globaldb/globaldb.cpp`
- `src/crdt/atomic_transaction.hpp`
- `src/crdt/impl/atomic_transaction.cpp`
- `src/crdt/globaldb/crdt_work_journal.hpp`
- `src/crdt/impl/crdt_work_journal.cpp`
- `src/crdt/impl/crdt_data_filter.cpp`
- `src/crdt/impl/crdt_callback_manager.cpp`
- `src/storage/rocksdb/rocksdb.hpp`
- `src/account/TransactionManager.hpp`
- `src/account/TransactionManager.cpp`
- `src/account/UTXOManager.hpp`
- `src/account/UTXOManager.cpp`
- `src/account/GeniusNode.cpp`
- `test/src/blockchain/CMakeLists.txt`
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp`
- `test/src/blockchain/consensus_certificate_store_test.cpp`
- `test/src/blockchain/consensus_certificate_test.cpp`
- `test/src/account/network_config_precedence_test.cpp`
