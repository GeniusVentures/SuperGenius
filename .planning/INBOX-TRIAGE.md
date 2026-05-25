===================================================================
  GSD INBOX TRIAGE — GeniusVentures/SuperGenius — 2026-05-25
===================================================================

SUMMARY
-------
Open issues: 64    Open PRs: 1
  Features:      21       Feature PRs:      0
  Enhancements:  13       Enhancement PRs:  0
  Bugs:          14       Fix PRs:          0
  Chores:         4       Wrong template:   1
  Unclassified:  12       No linked issue:  1
  Stale (>30d):  48

NOTE: No issue or PR templates exist in this repo (no `.github/ISSUE_TEMPLATE/`,
no `.github/PULL_REQUEST_TEMPLATE/`, no `CONTRIBUTING.md`). All reviews are
based on content quality rather than template compliance.

===================================================================
  GATE VIOLATIONS (action required)
===================================================================

  PR #255: "Develop"
    Author: Super-Genius
    Created: 2026-03-26 (60 days ago)
    Problem: Empty body — no template used, no linked issue, no description
    CI:     ALL checks FAILING (12 platforms, all failed)
    Review: REVIEW_REQUIRED (none given)
    Diff:   +26,139 / -6,881 lines across 100+ files
    Branch: develop → main
    Action: CLOSE. This PR has no body, no linked issue, and all CI failing.
            It appears to be a wholesale merge of develop into main without any
            issue tracking. The project requires issue-first workflow.

===================================================================
  ISSUES NEEDING TRIAGE (unlabeled / no type label)
===================================================================

  #291 [henriqueaklein] Review github actions
    Age: 5d | Labels: none → Suggested: `needs-triage`, `security`
    Missing: No labels, no type classification

  #290 [henriqueaklein] Refactor task queue
    Age: 5d | Labels: none → Suggested: `needs-triage`, `bug`
    Body: "faulty locking mechanism that causes thousands of writes into CRDT"

  #223 [itsafuu] CoinGecko Price retrieval proxy
    Age: 136d | Labels: none → Suggested: `needs-triage`, `enhancement`

  #217 [henriqueaklein] Progress tracking of the SDK
    Age: 139d | Labels: none → Suggested: `needs-triage`, `feature`

  #216 [henriqueaklein] SDK default key and key switching
    Age: 139d | Labels: none → Suggested: `needs-triage`, `feature`

  #168 [Am0rfu5] EIP ERC-20 Proxy Interface for Hierarchical Multi-Token Standard
    Age: 258d | Labels: none → Suggested: `needs-triage`

  #154 [Super-Genius] EVM Bridge RLP Expand Test Coverage
    Age: 294d | Labels: none → Suggested: `needs-triage` (may be outdated)

  #153 [Super-Genius] EVM Bridge RLP Protocol: Build system
    Age: 294d | Labels: none → Suggested: `needs-triage` (may be outdated)

  #152 [Super-Genius] EVM Bridge RLP library complete Missing Methods
    Age: 294d | Labels: none → Suggested: `needs-triage` (may be outdated)

  #151 [Super-Genius] EVM Bridge - Template Compilation
    Age: 294d | Labels: none → Suggested: `needs-triage`, `bug`

  #148 [henriqueaklein] Fix dev cut on payouts
    Age: 297d | Labels: none → Suggested: `needs-triage`, `bug`

  #146 [henriqueaklein] Full Node Sharding
    Age: 304d | Labels: none → Suggested: `needs-triage`

  #145 [henriqueaklein] Nonce consensus
    Age: 295d | Labels: none → Suggested: `needs-triage`

  #144 [itsafuu] Pubsub Channels versioning
    Age: 304d | Labels: none → Suggested: `needs-triage`

  #130 [henriqueaklein] Code Scanning on github
    Age: 357d | Labels: none → Suggested: `needs-triage`, `chore`

  #127 [henriqueaklein] Fix unit tests
    Age: 370d | Labels: none → Suggested: `needs-triage`, `bug`

  #118 [luizgrz] Multitopics pending
    Age: 402d | Labels: none → Suggested: `needs-triage`

  #112 [henriqueaklein] Bind SDK instances (wallet + app)
    Age: 419d | Labels: none → Suggested: `needs-triage`

  #103 [itsafuu] Processing Node TTL
    Age: 442d | Labels: none → Suggested: `needs-triage`

  #95 [itsafuu] Nonce Locking
    Age: 451d | Labels: none → Suggested: `needs-triage`

  #79 [henriqueaklein] Encrypt amount values
    Age: 482d | Labels: none → Suggested: `needs-triage`

  #62 [Super-Genius] Decentralized RAG (duplicate of #63)
    Age: 588d | Labels: none → Suggested: DUPLICATE of #63, close

  #60 [Super-Genius] CLI for AI/ML jobs
    Age: 602d | Labels: none → Suggested: `needs-triage`

  #57 [henriqueaklein] Multi party processing
    Age: 615d | Labels: none → Suggested: `needs-triage`

  #53 [henriqueaklein] Update build system
    Age: 677d | Labels: none → Suggested: `needs-triage`

  #50 [henriqueaklein] Bridging mechanism
    Age: 721d | Labels: none → Suggested: `needs-triage`

  #37 [itsafuu] Modify broadcasts multiaddress
    Age: 764d | Labels: none → Suggested: `needs-triage`

  #33 [henriqueaklein] Add cmaketemplate
    Age: 776d | Labels: none → Suggested: `needs-triage`

  #9 [SuperGNUS] Halo2 plonk lookups
    Age: 1141d | Labels: none → Suggested: `needs-triage` (very stale)

  #8 [SuperGNUS] Implement Accumulation
    Age: 1141d | Labels: none → Suggested: `needs-triage` (very stale)

  #4 [SuperGNUS] Zk-Rollup
    Age: 1147d | Labels: none → Suggested: `needs-triage` (very stale)

  #2 [SuperGNUS] Modify finality with HashGraph
    Age: 1148d | Labels: none → Suggested: `needs-triage` (very stale)

===================================================================
  OPEN ISSUES WITH `wontfix` LABEL (should be closed)
===================================================================

  #63 [Super-Genius] Decentralized RAG — Labels: [enhancement, invalid, wontfix]
  #51 [henriqueaklein] Fix blockchain Consensus — Labels: [wontfix]
  #147 [henriqueaklein] Add inter app communication — Labels: [wontfix]

  These 3 issues are marked `wontfix` but remain open. They should be closed.

===================================================================
  PRS NEEDING ATTENTION
===================================================================

  #255 [Unknown] "Develop"
    Score: 0% complete
    Missing: Everything — no body, no template, no linked issue,
             no description, no checklist, no testing notes
    CI:      ALL FAILING (12/12 platforms)
    Review:  REVIEW_REQUIRED (none given)
    Linked:  NO LINKED ISSUE
    Age:     60d
    Action:  CLOSE — violates issue-first policy, all CI failing

===================================================================
  RECENT ACTIVE ISSUES (last 20 days — well-structured)
===================================================================

  #286 [Super-Genius] Persist executed bridge message state (anti-double-mint)
    Labels: storage, evmbridge, safety | Score: 80% — well-structured
    Has: problem statement, implementation notes, checklist, priority

  #285 [Super-Genius] Submit observed EVM bridge events into consensus pipeline
    Labels: consensus, evmbridge, integration | Score: 80%

  #272 [Super-Genius] Gate bridge consensus admission on source finality rules
    Labels: evmbridge, safety, finality | Score: 80%

  #271 [Super-Genius] Add processing reservation state for bridge messages
    Labels: consensus, evmbridge, safety | Score: 80%

  #270 [#271-#261 are all well-structured consensus/performance improvements]
  - #270 through #261: 10 issues by Super-Genius with problem statements,
    appropriate labels, covering consensus performance and bridge safety.
    All ~20d old. These are the most actionable issues in the backlog.

===================================================================
  STALE ITEMS (>30 days, no activity) — 48 of 64
===================================================================

  Most issues have had no updates in 30+ days. The oldest (#2, #4, #8, #9)
  are 1,000+ days old from the SuperGNUS era. Consider bulk-closing
  anything >365 days without activity with a "stale" comment.

  Age breakdown:
    30-90 days:    14 issues
    90-180 days:    8 issues
    180-365 days:  16 issues
    365+ days:     10 issues (SuperGNUS-era, likely obsolete)

===================================================================
  RECOMMENDED ACTIONS
===================================================================

  1. CRITICAL — Close PR #255 (empty body, no linked issue, all CI failing)
  2. Close issues #51, #63, #147 (marked wontfix but still open)
  3. Close duplicate #62 (same as #63)
  4. Bulk-label ~40 unlabeled issues with `needs-triage`
  5. Consider bulk-closing 10 issues older than 365 days (SuperGNUS era)
  6. Create `.github/ISSUE_TEMPLATE/` and `.github/PULL_REQUEST_TEMPLATE/` to
     enforce structured submissions going forward
  7. Add `CONTRIBUTING.md` documenting the issue-first workflow

===================================================================
