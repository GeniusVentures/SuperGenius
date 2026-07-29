---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Slot-Scoped Consensus Finality
current_phase: 11
status: ready
last_updated: "2026-07-29T20:46:00.000Z"
last_activity: 2026-07-29 -- Completed Phase 11 Plan 12 exact terminal identity and composed recovery closure
progress:
  total_phases: 4
  completed_phases: 3
  total_plans: 35
  completed_plans: 35
  percent: 75
---

# State: SuperGenius — Slot-Scoped Consensus Finality

**Last updated:** 2026-07-29
**Milestone:** v2.0 — Slot-Scoped Consensus Finality
**Current Phase:** 11

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-07-27)

**Core value:** At most one valid certificate may finalize a canonical consensus slot.
**Current focus:** Phase 12 — Consensus Race and Compatibility Verification

## Current Position

Phase: 11 (Slot-Owned Bridge Burn Reservations) — COMPLETE
Plan: 12 of 12 complete
Status: Ready to plan Phase 12
Last activity: 2026-07-29 -- Completed 11-12-PLAN.md

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 9 | Canonical Slot and Certificate Storage | ✓ complete | SLOT-01..04, CERT-01..04, COMP-01..02 |
| 10 | Durable Vote Lock and Finalization State Machine | ✓ complete | CERT-05..07, VOTE-01..07 |
| 11 | Slot-Owned Bridge Burn Reservations | ✓ complete (12/12 plans) | BURN-01..05 |
| 12 | Consensus Race and Compatibility Verification | ○ ready for planning | TEST-01..06 |

## Key Decisions

- Certificates continue signing exact proposals but are stored authoritatively by canonical slot.
- Transaction-hash certificate lookup remains through a verified secondary index.
- v2.0 is a clean state break; no legacy certificate migration or dual-read support.
- Validators durably record one signature per slot before publishing it.
- Candidate selection may change only before the validator's irreversible slot vote.
- A valid certificate overrides local vote preference and finalizes the slot before proposal cleanup.
- Bridge reservations are owned by the canonical burn slot, not an individual proposal.
- Canonical readable slot preimages are validated before SHA-256 produces the only operational slot ID.
- Registered slot-handler failures are terminal; only unregistered subject types use hashed canonical subject identity.
- Receipt-local position is the mandatory bridge event index; block-wide `logIndex` remains observation metadata.
- Bridge source validation decodes only the indexed receipt log and compares all event facts before endpoint weight contributes.

## Notes

- The milestone is based on `bridge_race_single_burn_test` evidence in `src/account/log_bridge_race.txt`.
- Existing planning artifacts were archived under `.planning/milestones/pre-v2.0-phases/`.
- External research was skipped; requirements derive from the repository-level investigation.
- Broader Phase 8 fault injection and fuzzing remain deferred future work.

## Operator Next Steps

- Discuss and plan Phase 12.

## Session Continuity

**Last session:** 2026-07-29T20:46:00.000Z
**Stopped at:** Completed 11-12-PLAN.md
**Resume file:** None

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| 09 | 01 | 16 min | 2 tasks, 9 files |
| 09 | 02 | 35 min | 3 tasks, 31 files |
| 09 | 03 | 36 min | 3 tasks, 10 files |
| Phase 09 P04 | 18 min | 2 tasks | 9 files |
| Phase 09 P05 | 14 min | 2 tasks | 6 files |
| Phase 09 P06 | 9 min | 1 tasks | 5 files |
| Phase 09 P07 | 21 min | 2 tasks | 3 files |
| Phase 09 P08 | 24 min | 2 tasks | 8 files |
| Phase 09 P09 | 2h 16m | 2 tasks | 7 files |
| Phase 09 P10 | 16 min | 2 tasks | 10 files |
| Phase 09 P11 | 15 min | 1 tasks | 3 files |
| Phase 09 P12 | 16 min | 2 tasks | 5 files |
| Phase 09 P13 | 6 min | 1 tasks | 3 files |
| Phase 09 P14 | 21 min | 2 tasks | 8 files |
| Phase 09 P15 | 22 min | 2 tasks | 4 files |
| Phase 09 P16 | 13 min | 2 tasks | 7 files |
| Phase 10 P01 | 5 min | 1 tasks | 3 files |
| Phase 10 P02 | 9 min | 1 tasks | 5 files |
| Phase 10 P03 | 24 min | 1 tasks | 10 files |
| Phase 10 P04 | 34 min | 1 tasks | 4 files |
| Phase 10 P05 | 41 min | 1 tasks | 5 files |
| Phase 10 P06 | 44 min | 1 tasks | 8 files |
| Phase 10 P07 | 37 min | 1 tasks | 5 files |
| Phase 11 P01 | 32 min | 1 tasks | 2 files |
| Phase 11 P02 | 18 min | 1 tasks | 4 files |
| Phase 11 P03 | 17 min | 1 tasks | 5 files |
| Phase 11 P04 | 17 min | 1 tasks | 9 files |
| Phase 11 P05 | 27 min | 1 tasks | 8 files |
| Phase 11 P06 | 18 min | 1 tasks | 8 files |
| Phase 11 P07 | 19 min | 1 tasks | 8 files |
| Phase 11 P08 | 51 min | 1 tasks | 5 files |
| Phase 11 P09 | 9 min | 1 tasks | 0 files |
| Phase 11 P10 | 26 min | 2 tasks | 4 files |
| Phase 11 P11 | 30 min | 2 tasks | 6 files |
| Phase 11 P12 | 20 min | 2 tasks | 3 files |

## Decisions

- [Phase 09]: Certificate storage uses one deterministic slot payload and winner-to-slot index in a single CRDT batch. — The slot is authoritative while the index preserves transaction-hash lookup without duplicate handlers.
- [Phase 09]: Certificate replication validates the complete slot/index delta before element filters or merge. — Partial, malformed, mismatched, and conflicting sibling records must fail atomically while unrelated namespaces remain unaffected.
- [Phase 09]: Protocol v2.0 startup rejects every non-exact key under /cert/ before consensus side effects. — Legacy transaction-keyed state cannot be safely mixed, migrated in place, or dual-read by the v2 certificate store.
- [Phase 09]: Authoritative slot lookup validates certificate payload and derived slot before compatibility hash lookup can return it. — The slot record is authoritative; index metadata cannot bypass complete certificate validation.
- [Phase 09]: Full subjects check canonical slot finality while string hashes remain exact winner lookups. — Losing candidates must observe finalized shared resources without receiving the winner certificate under their own hash.
- [Phase 09]: External mint identities must be canonical unsigned-decimal chains plus nonzero 64-character lowercase hexadecimal burn hashes before mutation. — Slot aliases and invalid sources must fail before UTXO or queue state changes.
- [Phase 09]: Only explicit supergenius and supergenius_chain identifiers bypass external receipt verification. — Empty or unknown chain metadata cannot classify a claim as local.
- [Phase 09]: RPC receipts contribute weight only when their transaction hash exactly matches the requested burn hash. — A valid-looking receipt for another transaction cannot prove the mint input.
- [Phase 09]: Catch-up publishes a chunk only after all enabled v1/v2 queries and receipt dependencies validate. — Partial publication would let a later failure expose burns while preserving a retry cursor.
- [Phase 09]: Catch-up receipt caches are scoped to one poll attempt. — A transient null receipt must be fetched again when the failed chunk retries.
- [Phase 09]: Receipt-local ordinals narrow through one uint64_t-to-uint32_t helper at the production boundary. — Direct UINT32_MAX coverage proves overflow handling without impractical receipt fixtures.
- [Phase 09]: Every serialized certificate vote is validity-critical; invalid or duplicate votes reject the certificate. — Silently skipping attacker-controlled votes permits alternate accepted encodings and weakens canonical quorum semantics.
- [Phase 09]: Certificate timestamp and weights are derived during normalization, and canonical votes sort by raw voter-ID bytes. — Derived redundant fields and total ordering produce one deterministic authoritative protobuf representation.
- [Phase 09]: The live delta filter owns the complete /cert/ namespace and rejects certificate tombstones directly. — Runtime legacy or malformed records must not survive to poison restart; shared tombstone matching and removal remains in Plan 09-08.
- [Phase 09]: External delta filters use Approve, Reject, and RetryDependency decisions across both elements and tombstones. — Terminal namespace attacks must be sanitized before merge while a missing dependency preserves the complete original delta for later validation.
- [Phase 09]: Missing canonical registry content parks the exact pending source under bounded deadline retry with due-time FIFO snapshot fairness. — Dependency roots already queued may run first, but later arrivals cannot starve the retry or force TTL cleanup without another evaluation.
- [Phase 09]: Only true database NOT_FOUND is certificate absence. — Corruption maps to IntegrityError and every other or unknown read failure maps conservatively to StorageError.
- [Phase 09]: Certificate index-to-slot relationship validation stays separate from raw read classification. — A dangling index is IntegrityError while operational slot failures propagate unchanged.
- [Phase 09]: Only previous-certificate NotFound creates an admission dependency. — Integrity and operational errors reject, and producer-witness validation rejects every non-success read.
- [Phase 09]: Certificate failure injection remains private and friend-only. — Production APIs remain unchanged while real consumers receive exact datastore outcomes in focused tests.
- [Phase 09]: Only exact datastore NOT_FOUND permits bridge burn evaluation to continue. — All other executed-record read errors fail before any UTXO, reservation, persistence, or queue mutation.
- [Phase 09]: Catch-up commits only Processed or durably AlreadyHandled chunks. — Retry and callback exceptions preserve the failed chunk cursor and uncommitted dedup tuples.
- [Phase 09]: Certificate ingress uses one typed two-key preflight. — Both exact slot and winner-index reads complete before classification; only exact NOT_FOUND is absence, while corruption and operational failures fail closed before mutation.
- [Phase 09]: Replicated preflight failures are terminal rejection. — Unknown durable occupancy cannot be approved or converted into a missing-dependency retry, regardless of whether the read failed from corruption or I/O.
- [Phase 09]: Reject sanitation preserves retry barriers for retained namespaces; distinct retained dependencies fail closed. — Reject authority is namespace-local and must not choose or erase another namespace dependency.
- [Phase 09]: CRDT RequestClose returns one shared completion barrier while external CancelAndCloseNow waits and joins. — Worker-originated close cannot self-wait, but external owners require a truthful completed-and-drained boundary.
- [Phase 09]: Missing or failed receipt status is endpoint-local and contributes zero weight; later exact endpoints may still reach quorum. — One stale or malicious provider cannot veto independent receipts, while unsuccessful endpoints never help satisfy the 75-weight threshold.
- [Phase 09]: A canonical burn-keyed application record written with every mint effect is the sole durable mint-completion authority. — A naked consumed bridge input cannot prove which winner or outputs were applied.
- [Phase 09]: Ordinary UTXO persistence and atomic mint application share one persistence gate from snapshot read through batch commit. — A stale ordinary snapshot cannot overwrite effects certified by a committed application record.
- [Phase 09]: AlreadyApplied requires exact identity, winner, ordered outputs, and consumed bridge-input verification against live durable state. — Duplicate delivery and restart recovery fail closed on incomplete or conflicting records.
- [Phase 09]: CRDT final strong-owner release queues close and deletion to one process reaper. — Worker and callback weak-pointer promotions remain the sole lifetime guards, while destructor and delete must never execute on the final releasing worker.
- [Phase 09]: CRDT worker promotions are bounded to one operation or callback and released before blocking waits. — Idle workers must not circulate ownership indefinitely or prevent deterministic zero-count finalization.
- [Phase 09]: RPC endpoints and transport factories publish together as immutable generation-numbered snapshots. — Concurrent writers serialize copy-on-write publication while readers retain stable lifetime ownership without mutable references.
- [Phase 09]: One vote snapshot supplies the chain and all three endpoint slot hashes. — Signed votes cannot combine endpoint hashes from different operator/provider configuration generations.
- [Phase 09]: Weighted receipt verification retains one configuration from endpoint lookup through quorum completion. — A blocked RPC call cannot observe endpoints or a factory published after its decision began.
- [Phase 10]: Wave 0 uses real CRDT and RocksDB dependencies with friend-only test access surfaces. — Later durable-vote hooks remain private while the harness exercises production ownership and persistence.
- [Phase 10]: Finalization concurrency tests use predicate barriers and RAII release and join ownership. — Deterministic ordering avoids timing sleeps, detached workers, and stranded threads after assertion failures.
- [Phase 10]: Validator-private consensus state uses only /consensus/local/v2 direct RocksDB keys, with the validator hash included in vote keys. — Private votes and diagnostics must never replicate through CRDT, while key/value identity must remain independently verifiable.
- [Phase 10]: Only exact RocksDB NOT_FOUND represents absence; all query, decode, canonicalization, identity, and relationship failures return typed errors. — Unreadable or contradictory local state cannot safely unlock a validator for another signature.
- [Phase 10]: Conflict evidence and SafetyViolation are one batch, and safety authority must identify one member of the canonical digest/proposal pair. — Crash recovery must never expose conflict evidence without the participation stop, or accept mismatched safety authority.
- [Phase 10]: Consensus startup restores and validates durable vote, process, conflict, safety, and certificate authority before live side effects; replay publishes only exact stored envelopes after transport readiness, while stale or handler-blocked work remains Pending. — This preserves crash safety and signature identity while preventing corrupt local state, stale leases, or unavailable handlers from creating observable consensus actions or false completion.
- [Phase 10]: SigningPublishing reserves a slot through durable storage and completion of the first raw publish attempt. — Finalization cannot interleave with a partially published irreversible vote.
- [Phase 10]: PublishingReplay retries exact persisted envelope bytes without re-signing and stops under finality or safety authority. — Durable signature identity remains stable across transport failure.
- [Phase 10]: Later generations require durable retirement strictly after the acceptance horizon. — Boundary equality remains locked and retirement precedes later signing.
- [Phase 10]: All certificate ingress sources delegate to one authoritative FinalizeSlot path; adapters do not apply or clean independently.
- [Phase 10]: Finalization persists the exact Pending marker before one leased handler attempt and durable Complete before cleanup.
- [Phase 10]: Canonical conflict evidence sorts only its digest-pair key while explicit fields preserve authoritative and incoming direction.
- [Phase 10]: Restored safety loads both conflict proposal IDs before replay so slot-local vote publication remains suppressed after restart.
- [Phase 10]: Close rejects new consensus activity and drains leased callbacks, handlers, recovery, and timer work before returning. — The manager lifetime boundary must prevent post-destruction work and strong-owner self-join cycles.
- [Phase 10]: Synchronous CRDT certificate callbacks journal immediate recovery instead of finalizing before the outer merge commits. — Nested GraphSync persistence on the CRDT processing worker deadlocks its own outer WaitForJob.
- [Phase 11]: Wave 0 exercises an existing strict local consensus record so restart durability is real without introducing reservation production behavior early. — Plan 11-01 is test infrastructure only and must preserve the Phase 12 and production-behavior scope boundary.
- [Phase 11]: Future Phase 11 production hooks remain private and friend-only; the harness adds no public test setters. — Plan 11-01 is test infrastructure only and must preserve the Phase 12 and production-behavior scope boundary.
- [Phase 11]: Burn reservations use reciprocal direct-local slot and hashed canonical-outpoint records. — Strict two-way identity prevents half-present or aliased burns from becoming available.
- [Phase 11]: Burn reservation generations are random 256-bit lowercase hexadecimal tokens. — Fresh CSPRNG identity prevents stale cleanup from deleting a recreated lifecycle after abandoned history is removed.
- [Phase 11]: Consumed reservation state is prepared in a caller-owned batch. — The eventual winning mint effects and reservation consumption must share one physical RocksDB commit boundary.
- [Phase 11]: Authoritative mint certificates synthesize missing local FinalizedPendingApplication reservations before handler recovery. — Certificate authority must restore burn protection even when no earlier local reservation survived.
- [Phase 11]: ConsensusManager::New guarantees the built-in nonce/mint resolver before restoration, while Blockchain::New installs it explicitly before manager construction. — Direct consensus-manager callers and full blockchain startup must share the same fresh-process resolver guarantee.
- [Phase 11]: Slot-key handler registration is insert-only; replacement requires explicit unregistration. — A process-global resolver cannot be silently overwritten by a later registrant.
- [Phase 11]: Canonical resource descriptors exclude candidate identity; consensus owns durable reservation persistence. — Same-burn contenders must join one reservation generation regardless of proposal identity.
- [Phase 11]: The per-slot admission barrier spans descriptor extraction and store admission through candidate activation, and finalization waits for it. — This closes the durable-store to active-candidate visibility gap.
- [Phase 11]: Pending TTL cleanup removes proposal-local state without invoking finality or releasing slot-owned reservations. — Ephemeral proposal expiry is not a finality event and cannot own shared resource lifecycle.
- [Phase 11]: Resource-bearing certificate handlers use a typed application disposition while legacy certificate handlers retain Check semantics. — Exact mint application must distinguish safe retry from permanent durable-state contradiction without changing normal subject behavior.
- [Phase 11]: Authoritative mint finalization persists FinalizedPendingApplication before processing markers, handler leases, or cleanup. — Certificate authority must make the shared burn unavailable even on certificate-only nodes and after local candidate state is gone.
- [Phase 11]: Irreconcilable exact-winner application marks the certificate-bound reservation SafetyError and suppresses later retries and cleanup. — Permanent application contradictions must preserve finality and burn protection without futile remint attempts or reopening the resource.
- [Phase 11]: Finalized mint application carries the exact live shared ConsensusStateStore and complete certificate-bound reservation identity by immutable value. — Reconstructing or non-owning the store would bypass the node's serialization authority.
- [Phase 11]: Finalized batch participation requires shared-object datastore identity, not path or underlying database equivalence. — The shared wrapper object is the lock authority and a second wrapper would create split-brain serialization.
- [Phase 11]: Typed resource-application registration is insert-only while ordinary certificates retain the legacy handler signature. — Overwrite requires explicit removal and weak TransactionManager ownership remains lifecycle-safe.
- [Phase 11]: The finalized participant observes the durable pre-stage reservation while Consumed is staged invisibly in the same physical batch. — This distinguishes first application from exact replay without a split commit.
- [Phase 11]: Certificate-only synthetic bridge inputs may be materialized only from certified transaction facts under the exact finalized reservation handle. — Local proposal state cannot invent or mutate authoritative bridge input identity.
- [Phase 11]: AlreadyApplied requires durable Consumed state plus exact application, output, input-consumption, and winner identity agreement. — Partial or contradictory durable state must fail closed as not recoverable.
- [Phase 11]: Reservation reconciliation is one owned consensus-timer activity serialized through an explicit Reconciling slot lifecycle. — Admission, signing, finality, and cleanup must not cross the abandonment decision.
- [Phase 11]: Deletion requires exact certificate NotFound, strict candidate and vote horizon passage, and matching generation plus persisted candidate horizon. — Lookup uncertainty, renewal, finality, and ABA all retain protection.
- [Phase 11]: Shutdown cancellation restores the prior lifecycle and drains reconciliation before durable deletion. — Manager destruction cannot be followed by reservation mutation.
- [Phase 11]: Applied and AlreadyApplied are advisory for resource-bearing certificates until an exact live-store reread proves the captured reservation identity is CONSUMED. — Handler claims cannot complete or clean up a certified mint without its durable postcondition.
- [Phase 11]: Missing, unreadable, or exact FinalizedPendingApplication postconditions restore the exact process to Pending without completion, cleanup, or dependency wake. — Operational uncertainty remains retryable and winner-bound across restart.
- [Phase 11]: Finalized-handle payload contradictions are irreconcilable while the no-handle legacy fallback retains its established Applied compatibility result. — Certified mint data fails closed without changing legacy non-resource behavior.
- [Phase 11]: Post-consumption contradictions use CONSUMED_SAFETY_ERROR instead of overwriting the durable fact of physical consumption with ordinary SafetyError. — Physical consumption, exact finality identity, and non-release protection must remain durable after contradiction.
- [Phase 11]: Startup installs terminal safety lifecycle before certificate recovery selection, while duplicate ingress short-circuits before handler lookup or cleanup. — Terminal recovery must not invoke application, cleanup, wake, release, or remint side effects.
- [Phase 11]: Live terminal idempotence requires exact canonical outpoint, reciprocal generation, certificate digest, proposal ID, and winner ID agreement with the authoritative process and normalized certificate. — A readable identity contradiction must remain visible and cannot retire certificate work.
- [Phase 11]: Composed consumed-artifact recovery is proven with a genuinely certified mint and real TransactionManager registration over the same reopened RocksDB. — Production recovery must transition the contradiction exactly once and permanently suppress retry, release, remint, cleanup, and wake effects.
