# Phase 15 Deferred Items

Out-of-scope discoveries logged during plan execution. Not fixed per the executor scope
boundary (pre-existing issues unrelated to the current task's changes).

## 1. No-genesis full nodes never reach READY on the phase-15 base (breaks network_config_precedence_test)

- **Found during:** 15-01 Task 3 verification (`ctest -R network_config`).
- **Evidence:** `network_config_precedence_test` fails all six scenes with the
  `WaitForReady` 50s timeout on base dc7b40f1 (phase-13 closeout merge + 15-02), with the
  node log showing:
  `TrustedPeerRegistry construction failed (majority-floor violation): configured quorum_threshold is below the majority-safety floor (ceil(0.51*signer_set_size))`
- **Cause:** Post-closeout fail-closed trust startup (13-07 "enforce restricted trust
  startup states", c7aa11a1 runtime policy gates). `GeniusNode::Initialize()` constructs
  `TrustedPeerRegistry::New(...)` with an EMPTY signer set when `sgns_config.json`
  configures no `trusted_peers`/`bootstrapper_node`; the majority-floor check rejects it
  and `Initialize()` returns WITHOUT any `StateTransition` — the node is stuck in
  `MIGRATING_DATABASE` (state 1) forever. Verified unrelated to 15-01's changes: the
  failing path is untouched by this plan, and the precedence test was last modified long
  before the closeout (cf7e7f8b).
- **Affected pre-existing suites:** `network_config_precedence_test` (all 6 scenes),
  `node_type_derivation_test` (any scene waiting for READY), and any other no-genesis
  `GeniusNode::New` test scene.
- **Options for a later plan/fix:**
  1. Closeout-side: allow the empty signer set for dev nets in `TrustedPeerRegistry::New`,
     or transition to `FATAL_TRUST_MISMATCH` instead of returning silently (the silent
     return with no state transition also hides the failure from `GetState()`).
  2. Test-side: migrate the no-genesis scenes to genesis-configured configs + the trust
     approval ceremony (see `account_management_test.cpp` `ConfirmConfiguredTrust`).
- **15-01 workaround (this plan):** the new `network_config_private_network_test` waits
  only for startup to begin (`WaitForStartupSettled`), since every identity assertion is
  about the synchronous `LoadNetworkConfig`/`InitNetwork` path and does not depend on
  READY. Teardown verified clean (suite passes in ~10s, no crashes/hangs).
