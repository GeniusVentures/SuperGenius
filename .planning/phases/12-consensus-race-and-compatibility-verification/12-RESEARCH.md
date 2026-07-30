---
phase: 12-consensus-race-and-compatibility-verification
status: complete
researched: 2026-07-30
requirements:
  - TEST-01
  - TEST-02
  - TEST-03
  - TEST-04
  - TEST-05
  - TEST-06
---

# Phase 12 Research: Consensus Race and Compatibility Verification

## Executive Summary

Phase 12 is primarily a proof-completion phase, not a new consensus implementation. Phases 9-11 already provide canonical slot-keyed certificate storage, a durable one-vote-per-slot journal, deterministic candidate-window controls, unified multi-ingress finalization, slot-owned bridge-burn reservations, and atomic mint application. The missing work is to make those guarantees observable at the exact boundaries required by TEST-01..06 and to replace outcome-only or timing-based assertions with direct evidence.

The current 11-node race has the correct high-level setup: it constructs all nodes, waits for all 11 to become `READY`, seeds one burn, checks every `ConfigureRpcEndpoint()` result, and releases all endpoints back-to-back. Its completion test is too weak and partly wrong for v2.0: it waits only for balances, cannot prove that all 11 local watchers submitted proposals, cannot enumerate each validator's distinct published vote target, cannot compare authoritative certificates across nodes, and uses a fixed `sleep_for` as the double-mint guard. The archived log confirms why direct evidence matters: historical code produced two confirmed mint hashes for the same burn and repeated vote/certificate traffic, while the test ultimately timed out because only a subset of nodes converged within the body deadline.

The recommended approach is:

1. add one private, per-manager, read-only consensus trace observer carrying canonical identifiers for locally submitted proposals, published votes, and authoritative certificate observation;
2. expose each race node's real `ConsensusManager` only through a friend-only test accessor, using the existing public `GeniusNode::GetTransactionManager()` as the first hop;
3. rewrite the single-burn race around structured predicate barriers and a bounded per-node evidence snapshot;
4. add one focused `consensus_finality_race_test` target for the exact `HandleCertificate()`-before-CRDT/application interleaving and public-ingress duplicate/conflict matrix;
5. extend the existing vote-journal tests only where the requirement wording is stronger than current coverage, particularly a competing proposal after a real manager reconstruction;
6. retain the existing certificate-store, compatibility, transaction-manager, and UTXO suites as the real TEST-05/06 gates;
7. finish with an enumerated full CTest run and a checked-in report accounting for every configured test and every reviewed prerequisite skip.

No mock RPC transport or bridge startup redesign belongs in this phase. No changes are required in the user's currently modified `src/account/GeniusNode.cpp`; the necessary race access can be added in headers and existing consensus code.

## Current Architecture and Reusable Proof Seams

### Canonical finality and certificate ingress

`ConsensusManager::FinalizeSlot()` is already the shared authority point for:

- local submission through `SubmitCertificate()`;
- PubSub delivery through `OnConsensusMessage()` → `HandleCertificate()`;
- CRDT recovery after `CertificateReceived()` journals the in-flight callback and the owned recovery turn reads the committed pair;
- startup/recovery paths.

The function persists or verifies the `/cert/v2/slot/<slot>` and `/cert/v2/tx/<winner>` pair, inserts the slot into `restored_final_slots_`, changes local lifecycle to `FinalizedPendingApplication`, finalizes a mint reservation, writes the pending process marker, and only then invokes application. This is the correct interleaving boundary for TEST-02.

`finalization_stage_observer_` already supplies friend-only stages such as `reserved` and `burn-finalized`. Add an `authority-established` observation immediately after the authoritative pair is readable and the local slot is marked finalized, before burn/application work. The observer must run without `proposals_mutex_` held. A focused test can block there, attempt a competing proposal/vote/certificate, verify slot lookup already returns the winner, deliver the identical certificate through other ingress paths, and then release application.

### Durable vote and candidate ordering

`consensus_vote_journal_test` already has nearly all TEST-03/04 mechanics:

- `SetClocks()` installs deterministic steady/system clocks;
- `Continue()` admits a proposal through the real post-validation selection path;
- `Deadline()` and `ProcessDeadline()` control the exact selection boundary;
- `ObserveVoteStages()` records sign → durable put → publish ordering;
- `OverrideRawPublish()` captures exact outbound vote bytes;
- `RestartReplaysExactStoredEnvelopeWithoutSigning` closes a manager and reopens the same RocksDB-backed store;
- `FixedDeadlineSelectsComparatorWinnerAndPersistsBeforeExactRawPublish` proves a better pre-deadline candidate can win and later candidates do not re-sign.

The explicit gap is TEST-03's competing proposal after reconstruction. Extend the restart test or add a companion that restores the exact active vote, submits a different proposal for the same slot, drives the deadline, and proves both signer count and distinct published vote bytes remain unchanged except for exact replay.

For TEST-04, preserve the current two-case structure but make the requirement names and assertions explicit: before freeze, the comparator winner is signed; after durable publication, a better proposal is diagnostic/late only and cannot change the durable proposal or create a distinct signature.

### Certificate storage and compatibility

`consensus_certificate_store_test` already covers:

- atomic authoritative slot/index persistence;
- identical replay without duplicate callback;
- conflicting certificate rejection;
- CRDT conflict rejection before merge;
- preflight read-result matrices for not-found, corruption, and I/O;
- malformed/mismatched slot/index deltas, forbidden tombstones, and restart lookup.

`certificate_compatibility_test` already covers typed slot lookup and verified transaction-hash lookup, including malformed indexes, missing slot records, wrong slot relationships, winner mismatch, loser `NotFound`, and full-subject finalized-slot observation.

`transaction_manager_pending_lifecycle_test` contains the real TEST-06 consumer coverage:

- `PreviousNonceCertificateLookupPreservesConsumerSemantics` invokes `TransactionManager::EvaluateTransactionReplayProtection()` and distinguishes winner, absent dependency, corruption, and I/O behavior;
- `ProducerUTXOCertificateLookupPreservesConsumerSemantics` invokes `GeniusInputValidator::ValidateWitness()` against a real producer certificate and exercises not-found, corruption, and I/O failures;
- the same target covers finalized mint handles and retry/restart application.

`utxo_manager_test` already covers exact atomic mint application, bridge input consumption, restart replay, and conflicting-winner artifacts. Phase 12 should run and, only where needed, strengthen these owning tests. The unregistered `transaction_manager_certificate_fallback_test.cpp` is not the TEST-06 authority and should not be introduced merely to increase test count.

## Structured 11-Node Evidence Design

### Why end-state inspection is insufficient

After successful application, Phase 10/11 may clean ephemeral candidates and retire vote state. Querying maps at the end cannot prove what each node published. Log parsing is also unsuitable: repeated delivery/replay is legitimate, human text is unstable, and the test must distinguish repeated bytes from equivocation.

Add a private per-instance observer to `ConsensusManager` with a small structured event value. It should observe only public consensus facts:

- local proposal publication: validator ID, canonical slot ID, proposal ID, subject/winning transaction hash;
- usable vote publication: validator ID, canonical slot ID, proposal ID, deterministic vote/envelope digest;
- authoritative certificate establishment: canonical slot ID, proposal ID, winner hash, deterministic certificate digest, delivery source;
- application/terminal state if needed for bounded diagnostics.

The observer is test-only access: no new public `GeniusNode` or consensus diagnostics API. It must never include private keys or unsigned signing payloads. Invoke callbacks without consensus locks held and treat them as observational only; observer failure must not affect production control flow.

The race fixture can reach the manager through:

`GeniusNode::GetTransactionManager()` → friend-only access to `TransactionManager::blockchain_` → friend-only access to `Blockchain::consensus_manager_`.

Use a thread-safe fixture collector keyed by node/validator. Install it only after all nodes are `READY` and before the one burn is created. This avoids global static observer state and permits all 11 managers to report concurrently.

### Required race barriers and assertions

The revised single-burn test should use the following sequence:

1. assert all 11 nodes and transaction managers are `READY`;
2. install structured observers and record initial destination balance;
3. seed exactly one burn and retain its transaction hash plus receipt log index;
4. configure all 11 nodes back-to-back, asserting every return value;
5. wait until all 11 validators have emitted one local proposal for the same computed mint slot;
6. wait until all 11 nodes expose an authoritative certificate for that slot;
7. group vote-publication events by validator and assert at most one distinct proposal target/digest per validator (exact replay of identical bytes is allowed);
8. assert one distinct authoritative certificate digest, one proposal ID, and one winner transaction hash across all nodes;
9. assert the winning transaction is `CONFIRMED` on every node and every captured losing transaction hash is never `CONFIRMED`;
10. assert every node's destination balance is exactly initial plus one mint amount;
11. use a bounded predicate stability barrier tied to watcher/certificate/transaction quiescence or unchanged structured counters rather than an unconditional sleep;
12. on any timeout/assertion, print one bounded snapshot per node with the fields locked in D-05.

“Exactly one certificate” means one distinct canonical certificate for the slot replicated across all nodes, not one callback event globally. “At most one usable signature” means one distinct signed proposal target per validator; duplicate transport of byte-identical votes is idempotent and must not be counted as equivocation.

### Liveness and teardown diagnosis

Keep separate budgets for startup, participation, convergence, stability, and teardown. The current 90-second body timeout and historical long teardown collapse different failures into one symptom. Record first local proposal, first vote publication, first authoritative certificate, first confirmed winner, and final teardown start/finish timestamps per node/fixture.

Do not increase `kRaceNodeReadyTimeout` as the primary fix. A larger bound is acceptable only after the structured snapshot shows all nodes are making progress and the measured worst case justifies it. If a node never produces a proposal or never converges after a certificate exists, the plan must diagnose the stuck state rather than label it slow.

## Dedicated Deterministic Finality Race Target

Create `test/src/blockchain/consensus_finality_race_test.cpp` and register `consensus_finality_race_test` in `test/src/blockchain/CMakeLists.txt`. This is the dedicated cross-component target selected in Phase 12 context. It should use the real `ConsensusManager`, real RocksDB/CRDT fixture, real canonical mint subject, real burn reservation, real certificate validation, and a deterministic application handler barrier.

Minimum scenarios:

1. **PubSub-before-CRDT/application gap:** invoke the actual PubSub certificate handler path, pause at `authority-established`, verify slot lookup and finalized lifecycle, admit a same-slot competitor, drive voting/certificate creation, and prove the competitor cannot obtain a usable signature or certificate. Then deliver/merge the canonical CRDT pair and release application.
2. **Identical ingress matrix:** deliver the same certificate through local, PubSub, committed-CRDT recovery, and restart recovery routes; application and cleanup occur once and the certificate bytes/digest stay identical.
3. **Conflict ingress matrix:** deliver a valid different certificate for the occupied slot through each externally reachable route; each preserves the original winner, never republishes/applies the conflict, and updates one canonical digest-pair conflict record with deduplicated identity and accumulated source/observation data.

The target should call private handlers only through a single friend access class. It should not call `FinalizeSlot()` as the sole proof because TEST-02/D-19 require externally reachable ingress behavior. Direct `FinalizeSlot()` remains useful for fixture setup and narrow unit assertions.

## Full Repository Suite Gate

The current Release build enumerates 83 CTest targets; Phase 12 will add at least one target, so the gate must discover the count dynamically with `ctest -N` or `ctest --show-only=json-v1` rather than hard-code 83.

Recommended final commands:

- configure/build the intended Release tree;
- run the focused consensus/account targets with `--no-tests=error --output-on-failure`;
- run `bridge_race_single_burn_test` alone with its explicit long timeout;
- enumerate every configured CTest target;
- run the entire repository suite with `ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2` (or the platform-equivalent configured build command);
- compare configured, passed, failed, not-run, and skipped counts and write `12-FULL-SUITE-REPORT.md`.

The report must list every skip and its declared missing prerequisite. Current known prerequisite-driven cases include live Sepolia tests guarded by `RUN_E2E_BRIDGE` and signing-key variables, and Anvil-backed tests that require `anvil`, `cast`, and a usable Sepolia fork/funding source. A GTest skip is reviewed only when its message identifies the unavailable prerequisite and the report records it. Build omission, CTest regex exclusion, executable absence, timeout, crash, or silent non-run is not a reviewed skip.

The full suite is a completion gate, not an after-each-task feedback loop. Focused targets provide fast iteration; the whole suite runs after all implementation plans converge.

## Validation Architecture

### Fast feedback layers

| Layer | Purpose | Command |
|-------|---------|---------|
| Vote/candidate | TEST-03/04 deterministic vote lock and ordering | `cmake --build build/OSX/Release --target consensus_vote_journal_test -j2 && build/OSX/Release/test_bin/consensus_vote_journal_test --gtest_brief=1` |
| Finality race | TEST-02/05 external-ingress gap, duplicate, conflict | `cmake --build build/OSX/Release --target consensus_finality_race_test consensus_finalization_test consensus_burn_reservation_test -j2 && ctest --test-dir build/OSX/Release -R '^(consensus_finality_race_test|consensus_finalization_test|consensus_burn_reservation_test)$' --output-on-failure --no-tests=error` |
| Storage/compatibility | TEST-05/06 real index consumers and mint application | `cmake --build build/OSX/Release --target consensus_certificate_store_test certificate_compatibility_test transaction_manager_pending_lifecycle_test utxo_manager_test -j2 && ctest --test-dir build/OSX/Release -R '^(consensus_certificate_store_test|certificate_compatibility_test|transaction_manager_pending_lifecycle_test|utxo_manager_test)$' --output-on-failure --no-tests=error` |
| 11-node race | TEST-01 integration proof | `cmake --build build/OSX/Release --target bridge_race_single_burn_test -j2 && ctest --test-dir build/OSX/Release -R '^bridge_race_single_burn_test$' --output-on-failure --no-tests=error` |
| Full closure | D-16/17 entire configured repository | `ctest --test-dir build/OSX/Release --output-on-failure --no-tests=error -j2` |

### Nyquist sampling

- After observer/seam changes, build and run `consensus_vote_journal_test`, `consensus_finalization_test`, and `consensus_finality_race_test`.
- After race fixture/assertion changes, build and run `bridge_race_single_burn_test` alone.
- After compatibility changes, run the exact four-target compatibility regex.
- After every wave, run the union of all Phase 12 focused targets.
- After the final wave, run the isolated race and then the complete dynamically enumerated CTest suite.
- No three consecutive implementation tasks may pass without an automated focused command.

### Required evidence

- All TEST-01..06 tests are automated; no manual correctness assertion is required.
- External prerequisite skips require human review only to classify availability, not to judge consensus behavior.
- The final report must account for every configured target and preserve the exact command/build identity.

## Security and Safety Threat Model

| ID | Threat | Severity | Required mitigation/proof |
|----|--------|----------|---------------------------|
| T-12-01 | Observer changes consensus timing, re-enters locked state, or changes publication outcome | High | Per-manager observer is read-only, invoked without consensus locks, and ignored for control-flow results; focused tests prove behavior with observer absent/present. |
| T-12-02 | Test counts duplicate replay as equivocation or misses two different signatures | High | Group by validator and canonical slot; validate and compare distinct proposal targets/deterministic vote digests; identical bytes are replay, differing targets are failure. |
| T-12-03 | Application-gap test pauses before authority exists and gives a false proof | High | Barrier fires only at `authority-established`; while paused, slot lookup and finalized slot state must already return the exact winner before competitor injection. |
| T-12-04 | Conflict delivery overwrites the winner or applies twice | High | Every external ingress asserts immutable authoritative bytes, one application/cleanup, no conflict rebroadcast, and one canonical deduplicated conflict record. |
| T-12-05 | Full-suite filtering or silent skips hide regressions | High | Dynamically enumerate all CTest targets, run without exclusion regex, fail on not-run/crash/timeout, and record every prerequisite-driven skip explicitly. |
| T-12-06 | Diagnostics expose private signing material | Medium | Emit validator IDs, proposal/slot/winner IDs, public signatures or digests, statuses, and balances only; never emit private keys, signing seeds, or secure-storage contents. |
| T-12-07 | Static/global test hooks leak across tests or nodes | Medium | Prefer per-instance observers; use RAII reset for existing static clocks/publish overrides; tests close managers and clear hooks deterministically. |

Security enforcement is enabled at ASVS Level 1 and blocks on High severity. Every PLAN.md should reference the applicable threats in its `<threat_model>` block and include automated proof for High threats.

## Recommended Plan Decomposition

### Plan 12-01 — Structured consensus trace foundation (Wave 1)

Add the per-manager observer/event value, lock-safe emission points, and friend-only race access path. Cover observer absence/presence, identifiers, duplicate replay identity, and no behavioral influence with focused tests. Requirements: TEST-01 foundation; decisions D-04, D-05, D-13, D-15; threats T-12-01, T-12-02, T-12-06, T-12-07.

### Plan 12-02 — Deterministic finality-gap and ingress matrix (Wave 1 or 2)

Add the `authority-established` stage and dedicated `consensus_finality_race_test` target. Prove PubSub-before-CRDT/application blocking, identical ingress idempotency, and conflicting ingress diagnostics through real entry points. Avoid overlapping Plan 12-01 source lines where possible; if both touch `Consensus.hpp/.cpp`, make this depend on 12-01. Requirements: TEST-02, part of TEST-05; decisions D-06..D-08, D-12, D-19; threats T-12-01, T-12-03, T-12-04.

### Plan 12-03 — Durable restart, candidate ordering, and real consumer compatibility (Wave 1)

Extend the vote-journal restart case with an actual competing same-slot proposal after reconstruction, make the before/after deadline cases requirement-explicit, remove sleep-based assertions from affected certificate tests, and retain the actual previous-nonce/producer-UTXO consumer tests as gates. Requirements: TEST-03, TEST-04, TEST-05, TEST-06; decisions D-07, D-09, D-10, D-18, D-20; threats T-12-02, T-12-07.

### Plan 12-04 — Eleven-node single-burn proof (Wave 2)

Use Plan 12-01's structured trace in the existing fixture and rewrite the E2E assertions for 11 proposals, one slot, one distinct vote target per validator, one certificate digest/winner, winner-only confirmation, losing non-confirmation, exact balance, bounded stability, and diagnostic snapshots. Diagnose the folded timeout/teardown todo. Requirements: TEST-01; decisions D-01..D-05, D-11, D-14, D-15; threats T-12-01, T-12-02, T-12-05, T-12-06.

### Plan 12-05 — Full-suite closure and accountability (Wave 3)

Build and run all focused targets, run the isolated race, dynamically enumerate and run the entire repository CTest suite, classify only declared unavailable prerequisites as reviewed skips, and write `12-FULL-SUITE-REPORT.md`. Requirements: TEST-01..06 completion gate; decisions D-14, D-16, D-17; threat T-12-05.

## Planning Pitfalls

- Do not plan another consensus reservation or UTXO design; Phase 11 already owns it.
- Do not use a global static trace observer for the 11-node cluster when per-manager observation is possible.
- Do not equate publication count with distinct signature count; exact replay is legal.
- Do not call only `FinalizeSlot()` and claim external ingress coverage.
- Do not add sleeps to prove absence. Use barriers, controlled clocks, predicate waits, and stable event counters.
- Do not hard-code the current 83-test inventory; the new integration target changes it and configurations vary.
- Do not silently accept GTest skips or regex-filter the final suite.
- Do not touch bridge startup/mock-RPC infrastructure or the user's unrelated logging-level edit in `GeniusNode.cpp`.
- Do not add `transaction_manager_certificate_fallback_test.cpp` to CMake unless an independently demonstrated TEST-06 gap requires it; the authoritative real-consumer tests already live in `transaction_manager_pending_lifecycle_test`.

## Research Conclusion

The phase is feasible with five plans over three waves. Most safety mechanics are already implemented and directly testable. The only production-source additions research recommends are private observational/test stages that make the existing guarantees provable; all other work belongs in tests, the race fixture, CMake registration, and the final full-suite report.
