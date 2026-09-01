---
status: complete
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
  artifacts: []  # Filled by diagnosis
  missing: []    # Filled by diagnosis
