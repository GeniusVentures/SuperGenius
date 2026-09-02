---
status: complete
phase: 12-multi-node-finality-fault-proof
source: 12-REVIEW-FIX.md, 12-01-SUMMARY.md, 12-02-SUMMARY.md, 12-03-SUMMARY.md, 12-04-SUMMARY.md, 12-05-SUMMARY.md, 12-08-SUMMARY.md, 12-11-SUMMARY.md, 12-12-SUMMARY.md
round: 2 (post-review-fix verification: commits 2b1a8e47..caf34458)
started: 2026-09-02T10:49:43Z
updated: 2026-09-02T11:55:00Z
---

## Current Test
<!-- OVERWRITE each test - shows where we are -->

[testing complete]

## Tests

### 1. Post-Fix Full Regression
expected: Both full CTest targets (consensus_pending_lifecycle_test, multi_node_finality_fault_test) pass on the post-review-fix build — the original 5 UAT outcomes hold. Note: intermittent failures under back-to-back load were A/B-verified as pre-existing baseline sensitivity, not regressions.
result: issue
reported: "This happened on the multi node finality. The consensus pending worked. — macOS crash report multi_node_finality_fault_test-2026-09-02-075842.ips: EXC_BAD_ACCESS / SIGSEGV KERN_INVALID_ADDRESS at 0x30 in boost::asio::detail::kqueue_reactor::deregister_descriptor during libp2p::transport::TcpConnection::~TcpConnection -> basic_stream_socket dtor -> MultiselectInstance release (shared_ptr teardown chain). Teardown-time near-null deref while deregistering a socket from the asio kqueue reactor."
severity: blocker

### 2. CR-02 Conflicting-Transaction Lookup Safety
expected: With a conflicting transaction whose tx_processed_m key misses (network-prefixed path variance on DEV_NET), certificate handling takes the hash value-scan fallback instead of dereferencing end() — no crash/wild dereference; when nothing local exists to arbitrate, the site approves. Observable via transaction_manager_certificate_fallback_test and consensus_pending_lifecycle_test staying green.
result: issue
reported: "CertificateFallbackTest.SharedMintSlotConfirmsOnlyTheCertifiedTransaction FAILED at transaction_manager_certificate_fallback_test.cpp:740 — loaded.has_value() Actual: false, Expected: true. Preceding logs: '[warning][TransactionManager] Mint-v2 301eb8... did not consume every burn input (already consumed or missing); burn outpoint metadata may have been rebuilt with zero amount' (the WR-07 fix's new warning), cert callback for slot key destination dfc785af... with 'UpdateCRDTHeads: Error, untracked head QmRPFEs...', and two 'ProcessCommittedCertificate: No subject handler' warnings for a different slot (destination aab2ab...). Metrics at teardown: cert_fallback(success=1 failure=0) tracking(confirm=1)."
severity: blocker

### 3. WR-03 Pre-Commit Batch Validation
expected: An oversized/invalid batch is rejected before any CRDT Put/Commit — a rejected oversized transaction leaves no committed CRDT data behind (size gate + UTXO commitment/witness buildability checked pre-commit; post-commit gate retained as defense-in-depth). Mapped to ConsensusSubjectTest.SizeGate_* (consensus_subject_test.cpp:655-723) via ctest -R '^consensus_subject_test$'.
result: pass

### 4. WR-07 Marker-Write Retry Idempotency
expected: A CONFIRMED-marker write retry after a successful parse skips re-parse via the effects_applied flag — exactly one live Mint effect, burn outpoint not clobbered with an amount-0 entry; transient failures before parse success remain retryable. Observable via RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce staying green.
result: pass

### 5. CR-01/WR-05 Receive-Path Hardening
expected: Concurrent certificate receipt over PubSub no longer races proposals_/slot_states_ (proposals_mutex_ held in CreateProposalState, deadlock-checked single caller), and the announcement capture is bounded (1024, halving) and locked — no unbounded growth or race with the test accessor. Observable via a clean full multi_node_finality_fault_test run under normal load.
result: issue
reported: "The multi node finality crashed before RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce ended, but previous tests succeeded. — i.e. FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (multi_node_finality_fault_test.cpp:2299) crashed mid-run in the full-suite execution. Same case as Test 4's focused run, which passed. Presumed same teardown SIGSEGV signature as Test 1 (kqueue_reactor::deregister_descriptor via TcpConnection::~TcpConnection) — confirm against the new crash report."
severity: blocker

## Summary

total: 5
passed: 2
issues: 3
pending: 0
skipped: 0
blocked: 0

## Gaps

- truth: "Both full CTest targets pass on the post-review-fix build — the original 5 UAT outcomes (same-burn canonical finality, late contender, restart exact-once, publisher loss, real-route ownership) hold."
  status: failed
  reason: "User reported: multi_node_finality_fault_test crashed (macOS .ips 2026-09-02-075842): SIGSEGV KERN_INVALID_ADDRESS at 0x30 in kqueue_reactor::deregister_descriptor via TcpConnection::~TcpConnection shared_ptr teardown. consensus_pending_lifecycle_test passed. Unknown whether fix-induced or the documented pre-existing teardown/load intermittence (fixer A/B found baseline intermittence; crash-vs-fail not distinguished)."
  severity: blocker
  test: 1
  artifacts:
    - path: "/Users/henriqueklein/Library/Logs/DiagnosticReports/multi_node_finality_fault_test-2026-09-02-075842.ips"
      issue: "crash report: asio kqueue reactor deregistration during libp2p TcpConnection destruction"
  missing: []  # Filled by diagnosis

- truth: "With a conflicting transaction whose tx_processed_m key misses, certificate handling takes the hash value-scan fallback with no crash; transaction_manager_certificate_fallback_test stays green."
  status: failed
  reason: "User reported: CertificateFallbackTest.SharedMintSlotConfirmsOnlyTheCertifiedTransaction fails at transaction_manager_certificate_fallback_test.cpp:740 (loaded.has_value() false). Logs show the WR-07 fix's burn-consumption warning firing ('did not consume every burn input... rebuilt with zero amount') and an untracked-head CRDT error during the certified slot's callback. Note: the fixer's verification had this target passing 4/4 twice post-fix, so this may be intermittent or order/state-dependent."
  severity: blocker
  test: 2
  artifacts:
    - path: "test/src/account/transaction_manager_certificate_fallback_test.cpp"
      issue: "assertion at line 740 — expected durable loaded state absent after certified mint confirmation"
  missing: []  # Filled by diagnosis

- truth: "Concurrent certificate receipt over PubSub no longer races proposals_/slot_states_ and announcement capture is bounded and locked — a clean full multi_node_finality_fault_test run under normal load."
  status: failed
  reason: "User reported: the multi-node binary crashed before RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (multi_node_finality_fault_test.cpp:2299) ended; preceding cases succeeded. Same case passed focused in Test 4. Presumed same teardown SIGSEGV as Test 1 (TcpConnection::~TcpConnection -> kqueue_reactor::deregister_descriptor); likely shares Test 1's root cause."
  severity: blocker
  test: 5
  artifacts:
    - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
      issue: "crash mid-run of FinalityFaultNetwork.RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (line 2299)"
  missing: []  # Filled by diagnosis
