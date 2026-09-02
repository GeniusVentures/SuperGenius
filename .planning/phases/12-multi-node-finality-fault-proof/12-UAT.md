---
status: diagnosed
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
  root_cause: "PRE-EXISTING teardown-order defect (crash reports back to 2026-08-26, six days before the review fixes — NOT fix-induced). FinalityFaultNetwork::Peer::Stop calls pubsub->Stop() (line 389) before db.reset() (line 390). GossipPubSub::StopImpl resets m_host and m_context — destroying the asio io_context/kqueue_reactor — while GlobalDB->CrdtDatastore->GraphsyncDAGSyncer->graphsync Network::host_ still co-owns the libp2p host (wired from peer.pubsub->GetHost() at test line 993; globaldb.cpp:300-307). When db.reset() later releases the host, ~BasicHost's reverse member order destroys transport_manager_ (TcpTransport holds the last shared_ptr<io_context>) BEFORE network_ (ListenerManagerImpl->Multiselect->TcpConnection). Any TcpConnection not cleanly closed during StopImpl (its enumeration skips connections with unresolved remotePeer(); its 1000ms close deadline under load aborts in-flight closes) then deregisters from the freed reactor: kqueue_reactor::deregister_descriptor SIGSEGV at 0x30. Full runs do ~4-5x more Peer::Stop calls under load, so the window opens probabilistically; focused runs usually close everything. Gaps 1 and 5 are ONE crash event (single Sep-2 .ips, owned by RestartAtVote TestBody)."
  artifacts:
    - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
      issue: "Peer::Stop (377-394) destroys io_context owner (pubsub) before host co-owner (db); StartPeer line 993 binds graphsync to the pubsub host"
    - path: "/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp"
      issue: "StopImpl (695-848) resets m_context while m_host has external owners; incomplete connection-close enumeration + 1000ms deadline"
    - path: "src/crdt/globaldb/globaldb.cpp"
      issue: "lines 300-307 give GlobalDB shared ownership of the pubsub host"
  missing:
    - "Peer::Stop: reset db (with transactions/blockchain/consensus already reset) BEFORE pubsub->Stop(), so StopImpl's m_host.reset() is the final host release with clean closes while m_context is alive (asio io_context-outlives-I/O-objects invariant)"
    - "Thirdparty hardening (separate change control): defer m_context destruction when m_host.use_count() > 1; force-close all connections including unresolved-remote ones"
    - "Side finding for separately scoped work: 2026-08-26-173919.ips destroyed-mutex UAF in MintRecoveryDiagnostics dtor -> UTXOManager::GetUTXOs"
  debug_session: ".planning/debug/multi-node-teardown-sigsegv.md"

- truth: "With a conflicting transaction whose tx_processed_m key misses, certificate handling takes the hash value-scan fallback with no crash; transaction_manager_certificate_fallback_test stays green."
  status: failed
  reason: "User reported: CertificateFallbackTest.SharedMintSlotConfirmsOnlyTheCertifiedTransaction fails at transaction_manager_certificate_fallback_test.cpp:740 (loaded.has_value() false). Logs show the WR-07 fix's burn-consumption warning firing ('did not consume every burn input... rebuilt with zero amount') and an untracked-head CRDT error during the certified slot's callback. Note: the fixer's verification had this target passing 4/4 twice post-fix, so this may be intermittent or order/state-dependent."
  severity: blocker
  test: 2
  root_cause: "STALE TEST-FIXTURE DB REUSE — not a regression from CR-02 or WR-07. CRDTFixture (test/testutil/storage/base_crdt_test.cpp:54-105) names its GlobalDB dir CRDT.Datastore.TEST.unit_<N> from a per-process counter (SharedMintSlot... is the 12th TEST_F -> unit_12) and removes it only in the destructor. A previously killed/crashed run leaves unit_12 behind; the next run's 12th fixture silently reopens it. The stale db pre-seeds ValidatorRegistry's cache, so StoreGenesisRegistry takes its cache_initialized_ skip branch (ValidatorRegistry.cpp:413-418) and never registers this run's random account; BuildSignedCertificate embeds the stale registry_cid while its sole vote is signed by the new non-member account; GetCertificateBySlot -> ValidateCertificate -> TallyVotes drops the non-member vote (Consensus.cpp:1894-1903) -> approved_weight=0 -> silent no-quorum reject (2792-2795) -> invalid_argument (3843-3846) -> loaded.has_value()==false at :740. Line 740 reads back line 736's Put BEFORE FetchAndProcess/OnConsensusCertificate — both CR-02-changed sites are downstream and untouched by this chain. Decisive experiment: SIGKILL a focused run, rename leftover dir to unit_12, run full binary -> 19 pass / exactly SharedMintSlot FAILED at :740 reproducing every UAT log line in order; 8 consecutive clean runs 20/20 green. The WR-07 burn warning was a stale-state symptom (constant burn outpoint 64xa/idx 0 already consumed by the stale run's mint), and UpdateCRDTHeads untracked-head appears in healthy runs — neither causal."
  artifacts:
    - path: "test/testutil/storage/base_crdt_test.cpp"
      issue: "db path CRDT.Datastore.TEST.unit_<N> from per-process counter; cleaned only in destructor; leftover dirs from killed runs silently reopened"
    - path: "src/blockchain/ValidatorRegistry.cpp"
      issue: "StoreGenesisRegistry skip-when-cache-initialized (413-418) turns stale registry into current validator set without this run's account"
    - path: "src/blockchain/Consensus.cpp"
      issue: "silent no-quorum rejection in GetCertificateBySlot (1894-1903, 2792-2795, 3843-3846) — correct behavior for unregistered signer, but silent"
    - path: "test/src/account/transaction_manager_certificate_fallback_test.cpp"
      issue: "assertion at :740; constant burn outpoint collides across runs (727-753)"
  missing:
    - "Fixture hygiene: clear db_path_/keypair_path_ at CRDTFixture construction (mirroring FSFixture::clear()) OR make the suffix run-unique (pid + counter) so cross-run db-name collisions are impossible"
    - "Optionally: log/fail when GlobalDB::New opens a pre-existing db path in tests"
  debug_session: ".planning/debug/cert-fallback-loaded-missing.md"

- truth: "Concurrent certificate receipt over PubSub no longer races proposals_/slot_states_ and announcement capture is bounded and locked — a clean full multi_node_finality_fault_test run under normal load."
  status: failed
  reason: "User reported: the multi-node binary crashed before RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce (multi_node_finality_fault_test.cpp:2299) ended; preceding cases succeeded. Same case passed focused in Test 4. Presumed same teardown SIGSEGV as Test 1 (TcpConnection::~TcpConnection -> kqueue_reactor::deregister_descriptor); likely shares Test 1's root cause."
  severity: blocker
  test: 5
  root_cause: "PRE-EXISTING teardown-order defect (crash reports back to 2026-08-26, six days before the review fixes — NOT fix-induced). FinalityFaultNetwork::Peer::Stop calls pubsub->Stop() (line 389) before db.reset() (line 390). GossipPubSub::StopImpl resets m_host and m_context — destroying the asio io_context/kqueue_reactor — while GlobalDB->CrdtDatastore->GraphsyncDAGSyncer->graphsync Network::host_ still co-owns the libp2p host (wired from peer.pubsub->GetHost() at test line 993; globaldb.cpp:300-307). When db.reset() later releases the host, ~BasicHost's reverse member order destroys transport_manager_ (TcpTransport holds the last shared_ptr<io_context>) BEFORE network_ (ListenerManagerImpl->Multiselect->TcpConnection). Any TcpConnection not cleanly closed during StopImpl (its enumeration skips connections with unresolved remotePeer(); its 1000ms close deadline under load aborts in-flight closes) then deregisters from the freed reactor: kqueue_reactor::deregister_descriptor SIGSEGV at 0x30. Full runs do ~4-5x more Peer::Stop calls under load, so the window opens probabilistically; focused runs usually close everything. Gaps 1 and 5 are ONE crash event (single Sep-2 .ips, owned by RestartAtVote TestBody)."
  artifacts:
    - path: "test/src/blockchain/multi_node_finality_fault_test.cpp"
      issue: "Peer::Stop teardown order (377-394); the Sep-2 crash was owned by RestartAtVote TestBody at line 2299 — same single event as test 1"
    - path: "/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp"
      issue: "StopImpl destroys m_context while host has external owners"
  missing:
    - "Same fix as test 1: reorder Peer::Stop to reset db before pubsub->Stop()"
    - "Thirdparty hardening under separate change control (see test 1 gap)"
  debug_session: ".planning/debug/multi-node-teardown-sigsegv.md"
