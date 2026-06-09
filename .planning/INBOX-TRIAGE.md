===================================================================
  GSD INBOX TRIAGE — GeniusVentures/SuperGenius — 2026-06-08
===================================================================

SUMMARY
-------
Open issues: 21 (↓ from 26)    Open PRs: 3 (↑ from 0)
  Bugs:         11      Fix PRs:           0
  Features:      4      Feature PRs:       0
  Enhancements:  3      Enhancement PRs:   0
  Chores:        2      Untyped PRs:       3
  Integration:   1      Drafts:            1 (#299)

NEW SINCE LAST TRIAGE (2026-05-27)
-----------------------------------
  8 auto-filed test-failure bugs (#300-#307)
  1 proto cleanup issue (#296)
  3 PRs opened (#308, #309; #299 was already open)

GATE VIOLATIONS — no PR/issue templates exist in this repo
-----------------------------------------------------------
  PR #309: Bridge phase5 (bridge_phase5 → develop)
    No linked issue, raw PR body. CI: OSX/iOS ✓, Win/Linux/Android ✗
  PR #308: crdt backup/config from 3.5 (dev_35todevelop → develop)
    Empty PR body, CHANGES_REQUESTED, CI: mixed
  PR #299: in-memory secure storage (DRAFT, fix/test-memory-secure-storage)
    Draft, no linked issue. CI: OSX/iOS ✓ only

BRIDGE ISSUES (active)
-----------------------
  #272 P0  Finality rules for bridge admission           evmbridge safety finality
  #294 P1  Phase 4 — E2E integration test                evmbridge consensus integration
  #296 P2  Proto cleanup: ConsensusSubject bytes→oneof   consensus proto-cleanup
  PR #309  Bridge phase5 (in-progress PR, CI partial)

CONSENSUS PERF (from sprint planning)
--------------------------------------
  #261 P1  Cache validator ordering, O(1) lookup
  #262 P1  Event-driven certificate processing
  #263 P1  Reduce global lock contention
  #264 P2  Index slot ownership
  #265 P2  Separate recovery polling
  #266 P2  Reduce certificate persistence latency

FAILING TESTS (auto-filed ~7 days ago)
--------------------------------------
  #307 migration_sync_test       #306 transaction_sync_test
  #305 full_node_test            #304 child_tokens_test
  #303 processing_nodes_test     #302 multi_account_test
  #301 blockchain_genesis_test   #300 account_management_test (SEGFAULT)

OTHER ACTIVE
-------------
  #290 P1 bug     Refactor task queue
  #291 P2 chore   Review github actions
  #148 P1 bug     Fix dev cut on payouts
  #127 P1 bug     Fix unit tests (old)
  #145 P1 feature Nonce consensus
  #216 P2 feature SDK default key and key switching
  #223 P2 enh     CoinGecko proxy
  #144 P2 enh     Pubsub Channels versioning
  #146 P3 feature Full Node Sharding
  #217 P3 feature Progress tracking of the SDK
  #130 P2 chore   Code Scanning
  #123 P3 enh     Update openzeppelin-contracts-diamond

STALE (>300 days)
-----------------
  #123 openzeppelin-contracts (391d)    #127 Fix unit tests (361d)
  #130 Code Scanning (372d)             #144 Pubsub versioning (317d)
  #145 Nonce consensus (316d)           #146 Full Node Sharding (316d)
  #148 Fix dev cut on payouts (309d)

===================================================================
