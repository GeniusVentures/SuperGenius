# Phase 21 Deferred Items

Out-of-scope discoveries logged during plan execution. Not fixed here.

## 2026-10-20 (21-02 execution)

- `bridge_race_single_burn_test` fails in this tree (both solo and concurrent
  runs): 11/11 nodes READY, seeded burn tx confirmed on the Anvil fork, but the
  `BridgeCatchupWatcher` scan reports `0 confirmed, 0 in flight, 0 pending
  retry` and `GatherVerificationEvidence` never runs — the burn is never
  discovered, so no mint ever starts. Pre-existing: this is the open P1 todo
  `.planning/todos/pending/bridge-race-not-all-11-mint-within-window.md`
  (2026-07-21, "still does not pass"); the discovery-level signature here is
  even earlier than the todo's description. Not caused by 21-02: the changed
  code paths (evidence identities, receipt fact binding) executed zero times
  in the failing run (0 `GatherVerificationEvidence` log lines). The
  mock-based sibling `bridge_race_fault_rpc_test` (which drives the full
  mint-v2 witness path deterministically) passes.
