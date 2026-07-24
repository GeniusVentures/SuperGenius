---
phase: 09-canonical-slot-and-certificate-storage
verified: "2026-07-24T18:46:25Z"
status: passed
score: "75/75 must-have truths verified"
requirements: "10/10 phase requirements satisfied"
artifacts: "64/64 declared artifact entries present and substantive"
key_links: "53/53 declared key links wired"
roadmap_success_criteria: "4/4 verified"
verified_head: c7ac0a51a05a6091c4cd401510dae4c6ad5d404b
re_verification:
  previous_status: gaps_found
  previous_score: "57/60 must-have truths verified"
  gaps_closed:
    - "Atomic mint effects, burn application record, ordinary-store serialization, duplicate delivery, and restart replay."
    - "Scoped CRDT weak ownership, truthful close completion, and final deletion on a non-worker reaper."
    - "Immutable RPC configurations retained as one snapshot for each vote and receipt decision."
  regressions: []
gaps: []
human_verification: []
deferred:
  - truth: "Concurrent distributed certificate formation from empty state cannot produce competing certificates."
    addressed_in: "Phase 10"
    evidence: "Phase 9 research documents that the CRDT has no distributed compare-and-swap; Phase 10 owns durable one-signature-per-slot vote locking and conflicting-finalization handling."
---

# Phase 09 Verification: Canonical Slot and Certificate Storage

## Verdict

Phase 09 passes at committed `HEAD` `c7ac0a51a05a6091c4cd401510dae4c6ad5d404b`.
The implementation establishes one fail-closed canonical slot identity, stores
the complete authoritative certificate by slot with a verified winning-hash
index, preserves the existing hash-based consumers, and rejects incompatible
legacy certificate state before consensus side effects.

All three Wave 8 closures are present in actual source and exercised by focused
tests. The three warnings in `09-REVIEW.md` are valid follow-up defects, but none
falsifies a Phase 09 requirement or declared must-have. The distributed
competing-certificate race remains explicitly assigned to Phase 10.

## Roadmap Success Criteria

| # | Criterion | Status | Direct evidence |
|---|---|---|---|
| 1 | Normal source/nonce contenders share one slot; candidates for one external burn share one bridge slot | VERIFIED | `GeniusTransaction::MakeNonceSlotPreimage`, `MintTransactionV2::GetSlotPreimage`, and `ConsensusManager::GetSlotKey`; `consensus_slot_key_test` passes. |
| 2 | Complete proposal certificate is authoritative by slot and has a verified transaction-hash index | VERIFIED | Strict certificate normalization, `/cert/v2/slot/<slot>`, `/cert/v2/tx/<winner>`, atomic batch publication, pair-aware remote filtering, and verified lookup; certificate store suite passes. |
| 3 | Previous-nonce and producer-UTXO consumers retrieve through `GetCertificateBySubjectHash()` | VERIFIED | Production Blockchain wrappers remain the consumer seam; winning, absent, corrupt, and operational read cases pass in compatibility and pending-lifecycle suites. |
| 4 | Legacy transaction-keyed certificate state fails startup before side effects | VERIFIED | `HasCompatibleCertificateState()` runs before consensus subscriptions/timers/filter recovery; startup incompatibility cases pass. |

## Plan Must-Haves

Every plan truth, declared artifact entry, and declared key link was checked
against committed source rather than summary claims.

| Plan | Truths | Artifacts | Key links | Evidence and result |
|---|---:|---:|---:|---|
| 09-01 | 4/4 | 4/4 | 3/3 | Canonical address/nonce and burn preimages are validated, SHA-256 hashed, and consumed by one result-returning consensus slot path with no recognized-subject fallback. |
| 09-02 | 4/4 | 6/6 | 3/3 | Receipt-local ordinal flows from live/catch-up observation through relayer/node/mint input; indexed receipt validation binds chain, token, amount, and destination. |
| 09-03 | 4/4 | 3/3 | 3/3 | Slot record and winner index publish in one CRDT delta; replay/conflict and pair integrity are checked; legacy state blocks startup. |
| 09-04 | 4/4 | 4/4 | 3/3 | Slot lookup re-derives the slot; hash lookup verifies index, slot record, and embedded winner; full losing subjects observe the finalized slot. |
| 09-05 | 4/4 | 5/5 | 3/3 | External chain/hash aliases reject before mutation, local bypass is explicit, and endpoint weight requires the exact requested receipt hash. |
| 09-06 | 6/6 | 3/3 | 4/4 | Catch-up stages a complete chunk, resolves exact receipt-local identities, retries with a fresh cache, and advances only after complete publication. The `evmrelay` artifact exists in the exact pinned submodule commit `62a9bbb101732a222466de19b80aca905af37e23`. |
| 09-07 | 5/5 | 2/2 | 3/3 | One canonical certificate byte representation is enforced; invalid votes/unknown fields/redundant fields and `/cert/` tombstones reject before merge. |
| 09-08 | 7/7 | 4/4 | 4/4 | Missing registries park atomic pairs without merge, terminal reject sanitizes only its namespace, and bounded retry scheduling prevents starvation and retained-state leaks. |
| 09-09 | 5/5 | 4/4 | 3/3 | Only datastore `NOT_FOUND` maps to absence; integrity and operational failures remain distinct and fail closed in both real consumers. |
| 09-10 | 5/5 | 7/7 | 5/5 | Catch-up cursor/dedup commit only after `Processed` or verified `AlreadyHandled`; reservation, unavailable dependencies, submission failure, and read failure return Retry. |
| 09-11 | 4/4 | 2/2 | 3/3 | Local and replicated slot/index preflight share typed raw-read classification and preserve visible state on symmetric/asymmetric faults. |
| 09-12 | 5/5 | 3/3 | 3/3 | Mixed Reject/RetryDependency retains the dependency barrier, distinct dependencies fail closed, and shutdown snapshots/barriers drain workers and retained state. |
| 09-13 | 3/3 | 2/2 | 2/2 | Missing/failed receipt status contributes zero endpoint-local weight; later exact endpoints can still reach the 75 threshold. |
| 09-14 | 5/5 | 6/6 | 4/4 | `ApplyMintEffectsAtomically` stages copied UTXO state and commits all owner records plus the canonical burn application record in one RocksDB batch. `StoreUTXOs` holds the same persistence mutex from database query through snapshot serialization and commit, preventing an older ordinary snapshot from committing after the mint. `ChangeTransactionState(CONFIRMED)` publishes confirmation only after atomic apply; exact duplicate and restart replay verify the durable record and all effects. |
| 09-15 | 6/6 | 4/4 | 4/4 | DAG, handle-next, rebroadcast, Put, and Delete callbacks acquire scoped ownership through `weak_ptr::lock()`. Promotions end before waits/next turns. The sole custom deleter enqueues final close/destruction to the process reaper, and the close barrier completes after worker join and retained-state drain. |
| 09-16 | 4/4 | 5/5 | 3/3 | Writers serialize copy-on-write publication of immutable generation-numbered endpoint/factory state. `GetVoteRpcSnapshot()` derives chain and all three hashes from one retained configuration; receipt verification retains one configuration through quorum completion; delayed provider merge copies the latest published generation. |
| **Total** | **75/75** | **64/64** | **53/53** | **VERIFIED** |

## Wave 8 Closure Audit

### Atomic mint effect and application persistence

- `UTXOManager::ApplyMintEffectsAtomically` validates the exact burn/winner,
  stages produced outputs and bridge consumption in copied maps, serializes all
  affected owners, adds the canonical application record, and performs one
  `BufferBatch::commit()` before publishing the in-memory maps.
- All pre-commit fault exits leave both durable and live state unchanged.
- The application record is accepted as completion only when its canonical
  bytes, winner, ordered outputs, live produced UTXOs, and consumed bridge input
  all agree.
- The shared `persistence_mutex_` covers ordinary store query, serialization,
  and commit, as well as atomic mint application and reload/checkpoint seams.
- Duplicate confirmation requires `AlreadyApplied`; restart after a committed
  batch reconstructs UTXO types/effects and recognizes exact replay, while
  restart after pre-commit failure remains retryable.

### CRDT scoped ownership and deletion

- Worker and `CrdtSet` callback lambdas capture weak pointers and perform
  datastore access only through a scoped `shared_ptr` from `lock()`.
- Runtime wait state lives in a separately shared shutdown control and does not
  retain the datastore.
- Final strong-reference release invokes a noexcept custom deleter that only
  enqueues the raw allocation and shutdown control. `CompleteCloseOnReaper`,
  destructor, and `delete` execute on the dedicated reaper thread.
- The isolated subprocess regression proves final external owner release inside
  a real callback, post-callback weak expiration, truthful completion, callback
  suppression, and non-worker destructor/delete execution.

### Immutable RPC decision snapshots

- `SetRpcEndpoints`, `AddRpcEndpoints`, and `SetTransportFactory` lock one writer
  mutex, copy the current immutable configuration, increment its generation,
  and atomically publish once.
- Vote construction makes one `GetVoteRpcSnapshot()` call; chain and all three
  hashes come from that retained generation.
- Receipt verification captures one configuration before endpoint lookup and
  keeps both its endpoint vector and factory alive through the full weighted
  loop.
- The blocked provider test verifies merge onto the newest operator-published
  configuration without losing endpoints or endpoint-local quorum behavior.

## Requirement Accounting

| Requirement | Status | Evidence |
|---|---|---|
| SLOT-01 | SATISFIED | One deterministic result-returning slot derivation feeds proposal arbitration, subject finality checks, and certificate slot validation. |
| SLOT-02 | SATISFIED | Normal preimage remains canonical source address plus unsigned nonce. |
| SLOT-03 | SATISFIED | Mint slot uses canonical source chain, burn transaction hash, and receipt-local index only. |
| SLOT-04 | SATISFIED | Proposer, nonce, transaction hash, amount, destination, token, and other candidate fields do not alter the burn slot. |
| CERT-01 | SATISFIED | Canonical certificate validation retains and verifies the complete proposal, embedded transaction, registry, and valid ordered votes. |
| CERT-02 | SATISFIED | Authoritative certificate is stored under the canonical slot hash with write-once/idempotent semantics. |
| CERT-03 | SATISFIED | The winning transaction index is emitted atomically with the authoritative slot certificate. |
| CERT-04 | SATISFIED | Hash lookup follows the index, validates the slot certificate, and verifies the requested embedded winner hash. |
| COMP-01 | SATISFIED | Previous-nonce and producer-UTXO paths use the verified Blockchain hash-lookup wrapper. |
| COMP-02 | SATISFIED | Legacy/malformed certificate namespaces fail startup before consensus background activity. |

## Advisory Review Warnings

| Warning | Classification for Phase 09 |
|---|---|
| Receipt-source callback captures raw `this` and lacks teardown | Valid defect, but not a Phase 09 gap. It concerns bridge/source object teardown after watch registration; Phase 09's receipt identity, transactional chunk retry, canonical slot, and certificate storage must-haves do not assert lifetime-safe watcher destruction. |
| Moving a registered validator leaves registry pointers aimed at the source object | Valid defect, but not a Phase 09 gap. It is a move/registry ownership bug outside the immutable snapshot contract. Production Phase 09 configuration uses the resident validator; the declared snapshot writer/reader truths remain true. |
| Multi-chain vote snapshot chooses `unordered_map::begin()` | Valid determinism defect, but not a Phase 09 gap. The selected chain and all three hashes still come from one immutable generation as Plan 09-16 requires. Canonical proposal slot identity is independently derived from the consensus subject. Binding public-slot voting to an explicit source chain should be fixed before relying on multi-chain public-slot quorum, but it does not change or fork the canonical finality slot. |

## Test Evidence

Focused execution against the current build:

- Initial 11-target CTest selection: 10 targets passed; `crdt_test` had one
  transient failure in
  `WorkerInitiatedShutdownCompletesBeforeBarrierAndRunsNoPostCloseWork`.
- The exact failed shutdown test then passed 5/5 consecutive repetitions.
- A subsequent complete `crdt_test` run passed 27/27.
- `crdt_datastore_last_owner_test` passed, including its isolated subprocess.
- The other focused targets passed:
  `utxo_manager_test`, `transaction_manager_pending_lifecycle_test`,
  `bridge_event_identity_test`, `public_chain_input_validator_slot_test`,
  `public_chain_mint_validation_test`, `startup_wiring_test`,
  `consensus_slot_key_test`, `consensus_certificate_store_test`, and
  `certificate_compatibility_test`.

The transient broad-suite failure is recorded rather than hidden; repeated
focused and complete reruns, plus direct lifetime source inspection, do not
support a remaining Phase 09 gap.

## Deferred Boundary

Phase 09 provides atomic slot/index publication, local write-once preflight,
replicated pair validation, and conflict detection. It does not provide a
distributed compare-and-swap over empty CRDT state. Preventing independently
formed competing certificates requires Phase 10's durable one-signature-per-slot
vote lock and finalized-slot state machine and is therefore not charged as a
Phase 09 gap.

## Human Verification

None required.
