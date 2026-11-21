---
status: verifying
trigger: "Plan 21.1-07 repeated restart recovery: canonical payout exists on every peer but one peer lacks complete payout ledger effects"
created: 2026-11-21T20:27:31Z
updated: 2026-11-21T20:36:00Z
---

## Current Focus

hypothesis: A CRDT callback already snapshotted for delivery outlives Consensus, silently skips receipt through its expired weak pointer, and generic callback completion retires unapplied certificate work.
test: Register an overlapping callback barrier before the real Consensus callback, destroy Consensus after snapshot, release delivery and inspect durable work plus replacement-manager handler calls.
expecting: Unfixed code removes journal work and the replacement handler receives zero calls despite a valid durable certificate.
next_action: Run post-fix D/E twice with boundary snapshots, then the five plan-07 cases and full callback lifecycle/fallback plus compatibility/ownership targets through registered CTest.

reasoning_checkpoint:
  hypothesis: The expired Consensus callback returns without changing Processing, so generic completion removes unapplied durable certificate work.
  confirming_evidence:
    - The actual callback snapshot was paused, the Consensus weak pointer expired, and the signed durable certificate write completed successfully.
    - The RED test observed no journal entry and zero replacement-manager handler calls after all snapshot callbacks completed.
  falsification_test: The same scheduling barrier retains retryable work and invokes the replacement handler before any production fix.
  fix_rationale: Preserve receipt in the existing journal even without the obsolete owner, so generic completion cannot acknowledge unapplied work and the replacement performs normal durable validation/application.
  blind_spots: The original E failing run has no boundary journal snapshot, so this proven loss mechanism is not yet proven to be its sole cause; no Processing lease change is justified.

## Symptoms

expected: Every peer applies the same exact canonical payout once following reorder, restart and passive catch-up.
actual: Focused D/E GTEST_REPEAT=2 fails overall; first E repetition has one peer missing complete payment despite a valid canonical certificate on all peers; second repetition passes.
errors: E 25-second catch-up assertion and one-peer complete payment assertion after duplicate certificate delivery; prior partial restart also hit five-second topology barrier with crossed Noise handshakes.
reproduction: Registered multi_node_finality_fault_test via unchanged owned CTest launcher with EscrowReleaseDelayedDeliveryRestartAndCatchupPaysOnce filter and GTEST_REPEAT=2, head 5267fe748.
started: Additional repetition after initial five-case passing plan-07 gate.

## Eliminated

- hypothesis: ValidateCertificateBestProposal rejection alone explains loss of durable application.
  evidence: This rejection governs pubsub arbitration; canonical certificate validation later succeeds and durable recovery independently validates it.
  timestamp: 2026-11-21T16:30:00Z

## Evidence

- timestamp: 2026-11-21T20:36:00Z
  checked: Same deterministic callback regression after the 11-line Consensus callback fix; four affected target build and changed-line clang-tidy.
  found: GREEN in 0.96s CTest / 77ms gtest. After completed callback dispatch, retryable Stalled work exists; replacement handler applies once; a further recovery does not dispatch twice. Build passed and production Required lint is clear.
  implication: The isolated expired-owner loss mechanism is fixed without changing validation, application, registry or lease policy.

- timestamp: 2026-11-21T20:33:35Z
  checked: CertificateCallbackSnapshotSurvivesManagerShutdownAndRecoversOnce, registered CTest, before production edit.
  found: RED in 0.95s CTest / 54ms gtest. The old manager expired; signed bytes were committed; the journal entry was absent; replacement handler calls remained zero after registration and explicit recovery. Log /tmp/phase21-1-07-callback-red.log.
  implication: Expired outer callback loss is confirmed independently of the nondeterministic network case.

- timestamp: 2026-11-21T20:30:00Z
  checked: Instrumented D/E repeated run with exact certificate-work state before restart, after Consensus close, before reopened manager creation, after restart and after catch-up; genuine spender and every recipient output/balance were captured independently.
  found: Both cases passed (43.74s CTest); E work was Stalled or absent at reopen and all peers converged. One D pre-restart snapshot briefly observed Processing, which completed normally. Log /tmp/phase21-1-07-recovery-diagnostic.log.
  implication: These passing runs do not reproduce the original intermittent failure. Do not change leases based solely on a potential failure path.
- timestamp: 2026-11-21T20:30:00Z
  checked: CRDTCallbackManager::PutDataCallback and ConsensusManager::RegisterCertificateFilter.
  found: Generic completion removes a still-Processing key after void callback return. The outer Consensus callback does nothing on expired owner, unlike the TransactionManager subject handler that returns owner_dead.
  implication: A snapshotted callback can acknowledge work without recording receipt or application; deterministic barrier regression can test this without generic CRDT hooks.

- timestamp: 2026-11-21T16:30:00Z
  checked: Existing partial summary and saved repeat logs.
  found: Counterexample is an overall 67.82-second failure with iteration 1 failing and iteration 2 passing; the last xUnit reports only iteration 2.
  implication: Final-iteration XML is insufficient; retain full CTest exit status and logs.
- timestamp: 2026-11-21T16:30:00Z
  checked: TransactionManager::New certificate subject handler.
  found: The weak owner returns owner_dead on expiry, which ProcessCommittedCertificate classifies retryable; expired transaction owner alone does not return success.
  implication: Inspect the outer CRDT callback lifetime and generic completion separately.
- timestamp: 2026-11-21T16:30:00Z
  checked: ConsensusManager::RecoverPendingCertificateWork and DispatchStalledCertificateEntryLocked.
  found: Processing entries are skipped; stale recovery uses a 15-second fallback, while an explicit live lease can prevent stale recovery.
  implication: Exact journal state and lease are necessary to establish the mechanism.

## Resolution

root_cause: A previously snapshotted outer Consensus CRDT callback silently returns when its weak owner expires, allowing generic completion to retire a durable certificate before application. The original intermittent E run lacks sufficient state to establish this as its sole cause.
fix: Capture the existing CRDTWorkJournal in the outer Consensus receipt callback; when its weak owner expired, preserve receipt with MarkSeen then MarkStalled(0) so normal durable recovery remains responsible for validation/application.
verification: Deterministic RED before production edit (1.24s), GREEN after edit (0.96s); final network and regression checks in progress.
files_changed: [src/blockchain/Consensus.cpp, test/src/blockchain/consensus_pending_lifecycle_test.cpp, test/src/blockchain/multi_node_finality_fault_test.cpp]
