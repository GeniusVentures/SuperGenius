===================================================================
  GSD INBOX TRIAGE — GeniusVentures/SuperGenius — 2026-06-09
===================================================================

SUMMARY
-------
Open issues: 21 (no change)     Open PRs: 4 (↑ from 3)
  Bugs:         11      Fix PRs:           0
  Features:      4      Feature PRs:       0
  Enhancements:  3      Enhancement PRs:   0
  Chores:        2      Untyped PRs:       4
  Integration:   1      Drafts:            1 (#299)

CHANGES SINCE LAST TRIAGE (2026-06-08)
----------------------------------------
  NEW PR #311: "Added fixes for GeniusWallet" (EduMenges, fixes_for_wallet → develop)
    Empty PR body, no linked issue. CI: OSX/iOS ✓, Linux x86_64/aarch64 ✗, Windows queued
  PR #308: APPROVED by henriqueaklein (2026-06-09) — previously CHANGES_REQUESTED
    CI: OSX/iOS/Android ✓, Linux x86_64 Release ✓/Debug ✓, Linux aarch64 Release ✓/Debug ✗, Windows queued
  PR #309: Still CHANGES_REQUESTED (henriqueaklein) — Android build error + unit test failures
  Issues #290, #264-#266 bumped (~19:29 UTC) — likely automated label/priority touch

GATE VIOLATIONS — no PR/issue templates exist in this repo
-----------------------------------------------------------
  PR #311: "Added fixes for GeniusWallet" (fixes_for_wallet → develop) ⚠️ NEW
    Empty PR body, no linked issue. CI: OSX/iOS ✓, Linux x86_64/aarch64 ✗, Windows queued
  PR #309: Bridge phase5 (bridge_phase5 → develop)
    No linked issue. henriqueaklein: CHANGES_REQUESTED (Android build error, unit tests failing)
    CI: OSX/iOS ✓, Linux (all) ✗, Android (all) ✗, Windows queued
  PR #308: crdt backup/config from 3.5 (dev_35todevelop → develop)
    Empty PR body. Now APPROVED by henriqueaklein (2026-06-09)
    CI: OSX/iOS/Android ✓, Linux x86_64 ✓, Linux aarch64 Release ✓/Debug ✗, Windows queued
  PR #299: in-memory secure storage (DRAFT, fix/test-memory-secure-storage)
    Draft, no linked issue. CI: OSX/iOS ✓ only (Win/Linux/Android all ✗)

BRIDGE ISSUES (active)
-----------------------
  #272 P0  Finality rules for bridge admission           evmbridge safety finality
  #294 P1  Phase 4 — E2E integration test                evmbridge consensus integration
  #296 P2  Proto cleanup: ConsensusSubject bytes→oneof   consensus proto-cleanup
  PR #309  Bridge phase5 (in-progress, CHANGES_REQUESTED, CI broken on Linux/Android)

CONSENSUS PERF (from sprint planning)
--------------------------------------
  #261 P1  Cache validator ordering, O(1) lookup
  #262 P1  Event-driven certificate processing
  #263 P1  Reduce global lock contention
  #264 P2  Index slot ownership
  #265 P2  Separate recovery polling
  #266 P2  Reduce certificate persistence latency

FAILING TESTS (auto-filed 2026-06-01, unattended)
--------------------------------------------------
  #300 account_management_test (SEGFAULT)  #301 blockchain_genesis_test
  #302 multi_account_test                  #303 processing_nodes_test
  #304 child_tokens_test                   #305 full_node_test
  #306 transaction_sync_test               #307 migration_sync_test

OTHER ACTIVE
-------------
  #290 P1 bug     Refactor task queue (comment 2026-05-27: CRDT scaling done, locks remain)
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
  #123 openzeppelin-contracts (392d)    #127 Fix unit tests (362d)
  #130 Code Scanning (373d)             #144 Pubsub versioning (318d)
  #145 Nonce consensus (317d)           #146 Full Node Sharding (317d)
  #148 Fix dev cut on payouts (310d)

ACTION ITEMS
------------
  [ ] PR #311: Ask EduMenges to fill PR body and fix Linux build failures
  [ ] PR #309: Fix Android build error + unit test failures (henriqueaklein review)
  [ ] PR #308: Resolve Linux aarch64 Debug failure, then merge (now approved)
  [ ] PR #299: Decide — land as-is (draft, CI partial) or fix non-OSX platforms
  [ ] Issues #300-#307: Assign owner to 8 failing tests (all auto-filed, 8 days stale)
  [ ] Issues #123, #127, #130, #144-#148: Close or schedule — all >300 days stale

===================================================================
