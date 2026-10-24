---
phase: 19-pruned-node-survival-recovery-hardening
plan: 06
subsystem: transaction
tags: [surv-01, sc-1, restart-matrix, real-boot, startcore, zero-fetch, nonce-responder, gate-04, pruned-frontier, outgoing-chain, double-run]

# Dependency graph
requires:
  - phase: 19-pruned-node-survival-recovery-hardening (19-03)
    provides: the RetryDueFailedRoots isResolved guard + the fetch-attempt/ladder observables the zero-fetch brackets lean on
  - phase: 19-pruned-node-survival-recovery-hardening (19-04)
    provides: the marker consult (empty missing set for locally-pruned refs), the preserve-clear/own-replay survival of spend evidence across restart replays, and the three harness lessons this matrix encodes (head retirement, registry-before-reopen, cert redelivery)
provides:
  - SURV-01 closed end to end: a pruned node restarted on the same path boots through the REAL TransactionManager::StartCore to READY with ZERO pruned-CID fetch attempts across the entire restart window, syncs a new tip, receives/spends, and chains the next outgoing tx with the durable-chain nonce and the GATE-04 cert-slot previous_hash — twice consecutively on fresh DB paths
  - The RestartMatrix family (real-boot leg + seam companion + the double-run full matrix) in tx_pruner_test.cpp
affects: [SURV-01 (complete), SC-1 (the phase's headline survival claim), GATE-04 (the ladder's pruned-frontier resolution exercised on a restarted pruned node)]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [nonce-responder leg (a second real pubsub host + ephemeral account messenger answers the nonce request with has_nonce=false — the trusting-local CheckNonce path — without joining any CRDT topic, so it can move neither the fetch counter nor CRDT state), test-thread tick driving of the StartCore-armed loop (the fixture io_ is hand-driven by contract; the loop logic is the production one, only its scheduler is manual), /utxo/ key-set comparison across restart (values are not byte-stable — the legitimate cert-redelivery replay rewrites consumed records as tombstones), per-instance ladder count beside the process-global fetch counter (the 19-07 dedup caveat)]
key-files:
  created: []
  modified:
    - test/src/account/tx_pruner_test.cpp

key-decisions:
  - "READY strategy (a) shipped, with the measurement recorded: CheckNonce read in full cannot pass a peerless Light node on ANY local pre-seed — it returns true only when FetchNetworkNonce SUCCEEDS (nullopt 'no nonce info', or a network nonce <= local), and with no answering peer the fetch errors (no messenger -> no_such_device; messenger + 0 mesh peers -> NO_RESPONSE_RECEIVED fail-fast in PerformNonceRequest) and fails closed for Light. Strategy (b)'s premise ('pre-seed the local confirmed nonce so the trusting-local path passes') is therefore structurally impossible on this fixture; the plan's own fallback (a) — a nonce responder — is the shipped shape, probed BEFORE StartCore (the A4 measurement made explicit: FetchNetworkNonce(2s) returns success-with-empty-optional) so the boot's first CheckNonce passes deterministically"
  - "The tick loop is driven by the test thread through the per-binary seam: CRDTFixture's io_ is a hand-driven context by contract ('Tests drive it by hand'), so StartCore's posted first TickOnce never runs unprompted — the bounded READY wait (and every matrix-leg wait) ticks the production loop itself; with a self-running io_ the TickOnce self-repost would also make io_->poll() never return, so a runner thread was the only alternative and the manual drive is the family's established discipline"
  - "The durable nonce chain is wired through ConfigureDatabaseDependencies on BOTH sides of the restart (messenger first, then the nonce store — GeniusNode's order): certified confirmations persist during seeding and the reopened account reloads local_confirmed_nonce=3 from the same path, which is what makes the outgoing nonce-4 assertion a DURABLE-chain expectation rather than a fresh-account zero"
  - "SeedCertificate's nonce subject now takes the transaction's own GetSrcAddress() (behavior-identical for every existing self-sourced caller): foreign-source receive deliveries certify through the same recipe — required because CheckTransactionAuthorization verifies the tx signature against the source address (an unsigned foreign tx is filtered before processing) and because UTXO effects only apply on the CONFIRMED transition (VERIFYING tracks but applies nothing)"
  - "The receive legs pay TWO outputs (one to the foreign source, one to us): the second foreign tx then spends the responder's OWN still-unspent output 0 — a real ConsumeUTXOs branch with a spender-of-record binding, not the tombstone not-found class"

patterns-established:
  - "Responder-leg READY proof: probe the exact precondition (FetchNetworkNonce succeeds empty) before StartCore so the real boot's first CheckNonce is deterministic; the responder stays meshed across the reopen (the fixture host survives, only the db stack is rebuilt)"
  - "Restart survival comparison discipline: key-set equality for /utxo/ (value bytes legitimately mutate on redelivery replay), byte-identity for seals, map equality for heads, specific state+spender assertions for the arm's outpoints"

requirements-completed: [SURV-01]

# Metrics
duration: 34min
completed: 2026-10-24
---

# Phase 19 Plan 06: Restart Matrix on a Pruned Node (SURV-01) Summary

**A pruned node (frontier nonces 1-2 erased with markers, the certified nonce-3 frontier alive) restarts on the same database path and boots through the REAL TransactionManager::StartCore — never ForceManagerReady — reaching READY in ~2.5s while making ZERO pruned-CID fetch attempts across the entire restart window (counter bracket + per-instance ladder count 0), with the missing set empty for the locally-pruned creating refs, spend evidence/live balance/seals/heads intact, and the full matrix proven twice consecutively on fresh paths: new-tip delivery processed, an incoming spend of a still-unspent output applied, and the next outgoing tx built with nonce 4 (durable chain) and previous_hash equal to the GATE-04 ladder's answer — the never-pruned cert-slot hash at <addr>:3, never a pruned tx's hash.**

## Performance

- **Duration:** 34 min (00:07-00:41 UTC)
- **Tasks:** 2
- **Files modified:** 1 (test/src/account/tx_pruner_test.cpp: 2397 -> 3095 lines)

## Accomplishments
- `ReopenStackOnSamePath(real_boot)`: the real-boot variant wires the reopened account for production boot (InitMessenger + ConfigureDatabaseDependencies — the durable nonce store) and replaces the final ForceManagerReady with `tm_->StartCore()` plus a bounded 30s READY wait; the seam path is byte-identical to before (the crash matrix's 8 parameterized cases unchanged)
- Nonce responder leg: second real pubsub host (KeyPairFileStorage + loopback-advertised Start, the EraseNodeCollection makeNode wiring) + ephemeral GeniusAccount messenger, one-direction dial (the multi_node lesson), bounded connectedness wait; answers the nonce request has_nonce=false; subscribes only messenger topics — never a CRDT topic — so it moves neither the fetch counter nor CRDT state
- RestartMatrix_RealBoot case: fetch-attempt delta 0 bracketed across teardown+reopen+StartCore+READY; ladder count 0 (dedup-safe per-instance observable); HasMissingTxHashes()==false; P/M PRUNED with blocks absent, frontier value+block present; /utxo/ key set identical; seals byte-identical; heads equal; producer/middle outpoints CONSUMED, frontier READY; durable local confirmed nonce == 3 reloaded
- RestartMatrix_SeamBootZeroFetchCompanion: the same zero-fetch invariants across the db-restart window on the ForceManagerReady seam path (documented as breadth, never the boot truth — Pitfall 5)
- FullMatrix (RestartMatrixDoubleRun Round0/Round1, fresh DB path each): TipSync (foreign certified delivery — tracked map + paid output READY + tip height advanced + zero fetches), ReceiveSpend (second foreign tx spends the tip's still-unspent output 0 — CONSUMED with spender-of-record bound, new READY output, zero fetches), OutgoingChain (nonce 4 == durable chain; ladder answer == frontier hash, != pruned hashes; belt: the decoded cert at slot <addr>:3 embeds exactly the ladder answer; the send driven past CREATED through the production enqueue path with db_->BeginTransaction staging, value durably published, manager still READY — the 19-02 behavior)
- Verification: RestartMatrix family 4/4 twice consecutively (plus a third and fourth run during stability checks); full tx_pruner_test binary 31/31; sibling targets green via ctest (tx_pruner, tx_lookup_tri_state, pending_lifecycle, certificate_fallback); clang-format clean on changed lines (git-clang-format); zero production files touched

## Task Commits

Each task was committed atomically (Task 2's block carved out of the working file for the Task 1 commit, then restored — no interactive staging available):

1. **Task 1: real-boot leg — StartCore to READY with zero pruned-CID fetches (+ seam companion)** - `9101e3f6a` (test)
2. **Task 2: full matrix — tip sync, receive/spend, outgoing chain, double-run** - `c533bfadf` (test)

## Files Created/Modified
- `test/src/account/tx_pruner_test.cpp` — +697 net lines: header scope line, gossip_pubsub/keypair_storage/host includes, four access-class forwarders (EnqueueOutgoing / RunFillDAGStruct / OutgoingPreviousHash / RunTickOnce), responder members + destructor teardown, StartNonceResponder / ProbeNonceResponderAnswersWithoutNonce, RestartArm + SeedRestartArm (certified nonces 1..3 with driver-put head retirement), MakeForeignTransfer / MakeSpenderWithDag, RestartMatrixContext + UtxoRecordKeys + SeedPruneAndReopenRealBoot (the shared zero-fetch-bracketed prologue), SeedCertificate subject generalization, ReopenStackOnSamePath real_boot parameter, and the three RestartMatrix tests + the double-run instantiation

## Decisions Made
- See key-decisions: strategy (a) with the impossibility measurement for (b); test-thread tick driving; nonce-store wiring on both sides; foreign certification through the tx's own source address; two-output receive shape
- The plan's "the /utxo/ + seal + head state matches the pre-restart snapshot" is implemented as key-set + specific-state comparisons, not byte equality: the 19-04 key-decision-3 tombstone degradation makes /utxo/ value bytes legitimately unstable across the restart's cert-redelivery replay
- MatrixDoubleRun is the parameterized full matrix instantiated twice (fresh fixture = fresh DB path per round), matching the plan's "each run on fresh DB paths" literally

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The plan's READY strategy (b) is structurally impossible on this fixture**
- **Found during:** Task 1 design (CheckNonce read in full, per the plan's own instruction)
- **Issue:** "pre-seed the account's local confirmed nonce store so CheckNonce's trusting-local path passes" cannot fire: the trusting-local branch requires FetchNetworkNonce to SUCCEED with an empty optional, which requires a messenger AND an answering peer (RESPONSE_WITHOUT_NONCE); with no peer the fetch errors (no messenger -> no_such_device; wired messenger + empty mesh -> NO_RESPONSE_RECEIVED fail-fast at AccountMessenger.cpp:1114) and a Light node fails closed regardless of any local pre-seed
- **Fix:** the plan's sanctioned fallback (a): a nonce responder (second real pubsub host + ephemeral account's AccountMessenger) answering has_nonce=false — exactly the trusting-local path; probed before StartCore (the A4 measurement) so the first CheckNonce is deterministic. The responder is a bare pubsub+messenger node (no GlobalDB) — the minimum the nonce-responder role needs, lighter than the plan's "second GlobalDB node"
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Verification:** real-boot leg green repeatedly; the boot reaches READY in ~2.5s (well inside the 30s bound)
- **Committed in:** `9101e3f6a`

**2. [Rule 3 - Blocking] StartCore's tick loop never runs on the hand-driven fixture io_**
- **Found during:** Task 1 first run (READY wait timed out at 30s; the state was stuck in INITIALIZING)
- **Issue:** CRDTFixture's io_ is a hand-driven context ("Tests drive it by hand (io_->restart()/poll())", base_crdt_test.cpp) — StartCore's posted first TickOnce never executes, so InitTransactions/CheckNonce never run; polling io_ from the wait is not an alternative either (the TickOnce self-repost would make io_->poll() never return)
- **Fix:** the bounded waits drive the armed production loop through the per-binary seam (RunTickOnce in the ASSERT_WAIT_FOR_CONDITION predicates) — the loop logic is the production one, only its scheduler is manual (the family's established discipline, documented in the access-class comment)
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Verification:** real boot READY in ~2.5s; every matrix leg's wait uses the same drive
- **Committed in:** `9101e3f6a`

**3. [Rule 3 - Blocking] An unsigned foreign-source receive is filtered before processing, and unconfirmed deliveries apply no UTXO effects**
- **Found during:** Task 2 design (traced FilterTransaction -> CheckTransactionAuthorization and the ChangeTransactionState switch before writing the legs)
- **Issue:** (a) CheckTransactionAuthorization verifies the tx signature against the source address, so a fabricated-source transfer is tombstoned by the element filter; (b) only the CONFIRMED transition runs ParseTransaction's ConsumeUTXOs/PutProducedUTXOs — a VERIFYING delivery tracks but applies nothing, so "assert UTXO effects" required certification
- **Fix:** MakeForeignTransfer signs with the responder account's real key, and SeedCertificate's nonce subject now takes the tx's own GetSrcAddress() (behavior-identical for every existing self-sourced caller) so foreign deliveries certify through the proven recipe; the two-output shape gives the follow-up spend a genuine still-unspent foreign outpoint to consume
- **Files modified:** test/src/account/tx_pruner_test.cpp
- **Verification:** TipSync/ReceiveSpend legs green with tracked+UTXO assertions on both rounds
- **Committed in:** `9101e3f6a` (subject generalization), `c533bfadf` (the legs)

---

**Total deviations:** 3 auto-fixed (all Rule 3 blocking; each changed HOW the harness wires the scenario, never WHAT the assertions prove)
**Impact on plan:** All must_haves truths proven. The READY strategy deviation is the plan's own sanctioned fallback with the measurement the plan asked for; the strategy and rationale are recorded here per the acceptance criteria.

## Issues Encountered
- clang-tidy on new lines: the file-family's accepted classes only — bugprone-unchecked-optional-access on ASSERT-guarded .value() accesses (the documented analyzer false-positive class), lowercase literal suffixes and underscore test names (the whole file's existing convention), misc-use-internal-linkage on the fixture. addtest targets disable clang-tidy by design; findings recorded for the phase audit
- One compile fix during bring-up: MakeSignature takes GeniusAccount& (non-const) — MakeForeignTransfer's parameter corrected before the first green run
- The process-global fetch counter is exact-0 here (not a bound) because the only other datastore in the process (the responder's) has none — the matrix never needs the 19-07 slack discipline, but the per-instance ladder count is asserted beside it anyway

## Threat Mitigations (per plan threat_model)
- **T-19-0601** (restart fetch storm): counter-bracketed zero-fetch across the ENTIRE restart window (reset before teardown, delta asserted after the READY wait) + per-leg brackets + per-instance ladder count 0 — DONE
- **T-19-0602** (vacuous READY proof): a real tm_->StartCore() call (grep-able, tx_pruner_test.cpp:871) with no ForceManagerReady anywhere on the leg (the seam is the companion, never the only path) — DONE
- **T-19-0603** (flaky wait): bounded ASSERT_WAIT_FOR_CONDITION with failure messages; the responder precondition probed BEFORE StartCore (the A4 measurement); the family double-run green (Round0/Round1 plus four consecutive family runs) — DONE
- **T-19-0604** (nonce chain regression): nonce == 4 asserted against the durable chain (reloaded local confirmed nonce 3), previous_hash asserted by comparison with the ladder output and belt-checked against the decoded cert-slot certificate; NEITHER pruned tx's hash accepted — DONE
- **T-19-06SC** (supply chain): zero registry installs; test-only plan — DONE

## Known Stubs
None — every path is wired to real stores; no placeholder text or unwired data sources.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- SURV-01 closes; with 19-01..19-07 all landed, every phase-19 requirement (GATE-05, SURV-01, SURV-02, SURV-03) is complete — the phase is ready for /gsd:verify-work
- The nonce-responder leg is reusable for any future real-boot test on this fixture (the trusting-local CheckNonce precondition in one call)
- Phase 20 (reclamation proof) can reuse RestartArm for a pruned-frontier fixture with measured bytes

## Self-Check: PASSED

The one modified file exists (test/src/account/tx_pruner_test.cpp, 3095 lines — artifact min_lines 100 satisfied); both task commits (9101e3f6a, c533bfadf) present in git log; a real `tm_->StartCore()` occurrence at :871 with ForceManagerReady only on the seam branch (:884) and the companion (:2920); `*RestartMatrix*` family 4/4 twice consecutively; full binary 31/31; sibling ctest targets 4/4; zero production files in either commit (git show --stat).

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-24*
