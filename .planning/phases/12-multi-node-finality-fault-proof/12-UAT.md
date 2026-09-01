---
status: diagnosed
phase: 12-multi-node-finality-fault-proof
source: 12-01-SUMMARY.md, 12-02-SUMMARY.md, 12-03-SUMMARY.md, 12-04-SUMMARY.md, 12-05-SUMMARY.md, 12-08-SUMMARY.md, 12-11-SUMMARY.md
started: 2026-09-01T14:56:14Z
updated: 2026-09-01T17:28:00Z
---

## Current Test
<!-- OVERWRITE each test - shows where we are -->

[testing complete]

## Tests

### 1. Same-Burn Canonical Finality
expected: With two competing Mint proposals for the same burn, the real four-peer production path reaches one canonical slot and one authoritative certificate. Exactly the deterministic winner receives one Mint effect, the loser has none, and the outcome remains exact after every peer restarts.
result: issue
reported: "This test failed: ConsensusPendingLifecycleTest.FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults — consensus_pending_lifecycle_test.cpp:1279: Value of: filtered.has_value() Actual: true, Expected: false. Logs show FilterCertificate flagging 'consensus equivocation' for BOTH same-slot alternates (existing_tx_hash=7e061d0b... vs candidate_tx_hash=2356e4ba..., then the reverse pairing), followed by 'higher serialized certificate hash rejected' for the same canonical_slot key. Also repeated 'UpdateCRDTHeads: Error, untracked head' during cert callbacks."
severity: blocker

### 2. Late Contender and Passive Recipient
expected: A proposal admitted after the original slot vote cannot acquire a replacement vote or a second certificate. A passive PubSub recipient remains receive-only, never writes the certificate key, and recovers from the authoritative CRDT certificate without a self-CID stall.
result: pass

### 3. Restart-Boundary Exact-Once Recovery
expected: Restarting at the active-vote, accepted-certificate, and Mint-application boundaries preserves the original vote and certificate and results in exactly one durable Mint effect with no duplicate marker or loser state.
result: pass

### 4. Publisher-Loss Durable Recovery
expected: After certificate persistence and before PubSub notification, losing the selected publisher does not create a conflicting certificate. Peers recover through the authoritative CRDT state and retain the one exact Mint result.
result: pass

### 5. Real-Route and Process Ownership
expected: The fault proof uses real PubSub, CRDT, RocksDB, consensus, and Mint ingress without direct authoring shortcuts. The serial real-socket CTest runner owns and reaps its children and leaves its fixed test ports reusable.
result: pass

## Summary

total: 5
passed: 4
issues: 1
pending: 0
skipped: 0
blocked: 0

## Gaps

- truth: "With two competing Mint proposals for the same burn, the real four-peer production path reaches one canonical slot and one authoritative certificate. Exactly the deterministic winner receives one Mint effect, the loser has none, and the outcome remains exact after every peer restarts."
  status: failed
  reason: "User reported: ConsensusPendingLifecycleTest.FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults fails at consensus_pending_lifecycle_test.cpp:1279 — filtered.has_value() Actual: true, Expected: false. FilterCertificate logs 'consensus equivocation' for both same-slot alternates and then 'higher serialized certificate hash rejected'; same-mint alternates should be treated as normal, only different-mint quorum conflicts are faults."
  severity: blocker
  test: 1
  root_cause: "FilterCertificate behaves per its contract — the failure is a fixture/CRDT interaction. The test writes the convergent-immutable slot key (/cert/mint-v2:...) three times with different values via plain db_->Put (WriteCertificateAtKey, test lines 154-161), bypassing production's PutConvergentImmutable (src/blockchain/Consensus.cpp:2115 — reserved UINT64_MAX priority with ConvergentImmutableValueHash lowest-hash convergence). All three no-topic self-created deltas carry the SAME priority (UpdateCRDTHeads 'untracked head' early-return leaves heads static; call 1 and call 3 DAG nodes share an identical CID in logs), and CrdtSet::SetValue (src/crdt/impl/crdt_set.cpp:623-649) falls through to an unconditional last-merge-wins overwrite at equal priority with a different value. The visible slot value after three conflicting same-priority writes depends on merge/scheduling order, not write order; under the loaded full-ctest UAT run, call 3's read observed call 2's certificate (second_serialized), so FilterCertificate correctly flagged the different-mint existing against the same-mint candidate and rejected by higher hash — failing EXPECT_FALSE(filtered.has_value()) at line 1279. Intermittent: 34 reproduction attempts on the identical binary (isolated, nominal-loop, load, full-binary) all passed; production and test SerializedCertificateHash predicates are byte-identical, eliminating hash divergence."
  artifacts:
    - path: "test/src/blockchain/consensus_pending_lifecycle_test.cpp"
      issue: "verify_order (1265-1281) + WriteCertificateAtKey (154-161): repeated plain Put writes of different values to the convergent-immutable certificate slot key"
    - path: "src/crdt/impl/crdt_set.cpp"
      issue: "SetValue (594-667): equal-priority different-value fall-through overwrite — last-merge-wins with no convergence guard outside the reserved UINT64_MAX priority"
    - path: "src/crdt/impl/crdt_datastore.cpp"
      issue: "CreateDAGNode (1443-1477) assigns priority = heads_max+1; UpdateCRDTHeads (1832+) 'untracked head' early-return leaves heads static for no-topic self-created writes, so conflicting writes tie on priority"
    - path: "src/blockchain/Consensus.cpp"
      issue: "FilterCertificate (2603-2674) behaves per contract; SubmitCertificate (2062-2115) shows the production convergent-immutable write path the fixture bypasses"
  missing:
    - "Fixture: write the slot key the way production does — extend ConsensusPendingLifecycleTestAccess with a PutConvergentImmutable-based write helper (mirroring WriteLiveCertificate) or give each verify_order direction a distinct key, removing dependence on same-priority overwrite ordering"
    - "CRDT hardening: require strictly-greater priority (or apply the convergent-hash tiebreak generally) before an equal-priority overwrite in CrdtSet::SetValue"
    - "CRDT hardening: advance DAG heads for self-created writes so priorities are monotonic and replays are harmless"
  debug_session: ".planning/debug/same-burn-canonical-finality.md"
