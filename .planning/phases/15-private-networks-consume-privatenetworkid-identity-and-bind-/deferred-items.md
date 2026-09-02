# Phase 15 Deferred Items

Out-of-scope discoveries logged during plan execution. Not fixed per the executor scope
boundary (pre-existing issues unrelated to the current task's changes).

## 2. BurnConfig's CRDT change callback shares the synchronous re-entrancy hazard fixed in NetworkRegistry (15-03)

- **Found during:** 15-03 Task 2 (first `network_registry_test` run segfaulted inside a
  datastore Put callback).
- **Evidence:** `BurnConfig::RegisterCrdtChangeCallback` (src/account/BurnConfig.cpp:404-416)
  registers a `RegisterNewElementCallback` that calls `OnCrdtElementChanged` ->
  `SecureCrdt::ReadIfQuorum` -> `RetainAuthorizedLegacySignatures` -> `GlobalDB::Remove`
  synchronously from inside the datastore's new-element callback. The identical pattern in
  NetworkRegistry corrupted the datastore and crashed (EXC_BAD_ACCESS) the moment the
  signer-set authority transitioned at bootstrap confirmation and stale signature children
  began being pruned from callback context.
- **Why latent in BurnConfig:** its signer set never transitions (always the TPR peers), so
  no signature child is ever unauthorized and the Remove path is not exercised from the
  callback today.
- **Fix applied in 15-03 only:** the NetworkRegistry callback flags + notifies a dedicated
  refresh thread that runs `TryConfirm` outside any datastore callback (commit 5bda5eed).
- **Suggested later fix:** apply the same off-callback dispatch to BurnConfig (out of scope
  for 15-03; BurnConfig behavior is unchanged and its suites pass).

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
- **15-06 observation (2026-09-02, same base family at 52501d57):** the `ctest -R
  "startup|node"` gate fails on the SAME mechanism with the identical suite set before and
  after this plan's changes: `node_type_derivation_test`, `node_initialization_progress`
  (stuck at 0.6/1.0), `processing_nodes_test`, `full_node_test`,
  `genius_node_bootstrap_reconnect_test` — all no-genesis READY waits. Suites that exercise
  the startup wiring without requiring READY (`startup_wiring_test`,
  `node_shutdown_race_test`) pass with 15-06's scoped-channel changes, as do all
  task-queue/keys/transaction-manager suites. No regression introduced; base repair still
  deferred per the options above.
- **Environment note (user instruction, 2026-09-02):** the vendored 3rdparty install
  moved from `/Users/henriqueklein/gnus/3rdparty` to `/Users/henriqueklein/gnus/thirdparty`
  (libp2p b28eed2 verified at the new path). 15-06's build tree is configured against the
  NEW path; later plans/worktrees must use the new THIRDPARTY_DIR.
