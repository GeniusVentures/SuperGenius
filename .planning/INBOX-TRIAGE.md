===================================================================
  GSD INBOX TRIAGE — GeniusVentures/SuperGenius — 2026-05-27
===================================================================

SUMMARY
-------
Open issues: 27    Open PRs: 0

  Bridge (evmbridge):   8    (Phase-mapped: #269-272, #285-286, #293-294)
  Consensus perf:        5    (#261-266)
  Consensus (other):     0
  Bugs:                  4    (#290, #151, #148, #127)
  Enhancements:          3    (#223, #144, #123)
  Features (non-bridge): 5    (#217, #216, #152, #146, #145)
  Chores:                4    (#291, #154, #153, #130)

NOTE: No `.github/ISSUE_TEMPLATE/*.yml` or top-level `CONTRIBUTING.md` found.
      Template-based completeness scoring skipped. Labels used for classification.

GATE VIOLATIONS
---------------
None (no open PRs to check against issues)

BRIDGE ISSUES — Phase-Mapped (active work)
----------------
  #293 [Phase 1] Wire RPC endpoints from evmrelay ChainList into GeniusNode startup
    Labels: evmbridge, startup, P1
    Age: 0d  |  Status: READY

  #285 [Phase 2] Submit observed EVM bridge events into the existing consensus pipeline
    Labels: evmbridge, consensus, integration, P1
    Age: 22d  |  Status: PENDING  |  Owner: Henrique

  #272 [Phase 2+4] Gate bridge consensus admission on explicit source finality rules
    Labels: evmbridge, safety, finality, P0
    Age: 22d  |  Status: PENDING

  #269 [Phase 3] Define canonical message_id for EVM bridge source events
    Labels: evmbridge, consensus, protocol, P1
    Age: 22d  |  Status: PENDING  |  Depends on: --

  #270 [Phase 3] Map bridge messages to deterministic slot keys for duplicate suppression
    Labels: evmbridge, consensus, protocol, P1
    Age: 22d  |  Status: PENDING  |  Depends on: #269

  #271 [Phase 3] Add processing reservation state for bridge messages
    Labels: evmbridge, consensus, safety, P0
    Age: 22d  |  Status: PENDING  |  Depends on: #269

  #286 [Phase 3] Persist executed bridge message state for anti-double-mint protection
    Labels: evmbridge, storage, safety, P0
    Age: 22d  |  Status: PENDING  |  Depends on: #269, #270, #271

  #294 [Phase 4] End-to-End bridge integration test (Sepolia live burn -> cert -> mint)
    Labels: evmbridge, consensus, integration, P1
    Age: 0d  |  Status: READY  |  Depends on: #293, #285, #272, Phase 3

CONSENSUS PERFORMANCE ISSUES (non-bridge, P1-P2)
----------------------------------
  #261 P1 Cache active validator ordering and add O(1) validator lookup  (22d)
  #262 P1 Make certificate processing event-driven instead of full-map scanning  (22d)
  #263 P1 Reduce global lock contention in consensus hot paths  (22d)
  #264 P2 Index slot ownership for O(1)-style duplicate/conflict resolution  (22d)
  #265 P2 Separate recovery polling from live consensus progression  (22d)
  #266 P2 Reduce certificate persistence latency on the consensus hot path  (22d)

BUGS
----
  #290 Refactor task queue — labeled bug, minimal body, needs-triage  (7d)
  #151 EVM Bridge RLP Template Compilation — labeled bug, needs-triage  (~296d)
  #148 Fix dev cut on payouts — labeled bug, needs-triage  (~299d)
  #127 Fix unit tests — labeled bug, needs-triage  (~372d)

ENHANCEMENTS
------------
  #223 CoinGecko Price retrieval proxy — needs-triage  (~138d)
  #144 Pubsub Channels version naming — needs-triage  (~306d)
  #123 Update openzeppelin-contracts-diamond — needs-triage  (~378d)

FEATURES (non-bridge)
--------------------
  #217 SDK Progress tracking — needs-triage  (~141d)
  #216 SDK default key and key switching — needs-triage  (~141d)
  #152 EVM Bridge RLP library complete Missing Methods — needs-triage  (~296d)
  #146 Full Node Sharding — needs-triage  (~306d)
  #145 Nonce consensus — needs-triage  (~306d)

CHORES
------
  #291 Review github actions — needs-triage  (7d)
  #154 EVM Bridge RLP Expand Test Coverage — needs-triage  (~296d)
  #153 EVM Bridge RLP Build system and dependencies — needs-triage  (~296d)
  #130 Code Scanning on github — needs-triage  (~359d)

STALE ITEMS (>30 days, no recent activity)
-------------------------------------------
  16 stale issues, most labeled needs-triage, many 200-370+ days old:
    #127, #123, #130, #144, #145, #146, #148, #151, #152, #153, #154,
    #216, #217, #223, #261, #262, #263, #264, #265, #266

LIKELY DONE / CLOSABLE (verify with team)
-----------------------------------------
  #151 EVM Bridge RLP Template Compilation — RLP lib likely complete in evmrelay
  #152 EVM Bridge RLP missing methods — same
  #153 EVM Bridge RLP Build system — same
  #154 EVM Bridge RLP Expand Test Coverage — same (evmrelay has fuzz tests)
  These 4 RLP issues reference geniusventures/RLP which was integrated into evmrelay.

===================================================================
