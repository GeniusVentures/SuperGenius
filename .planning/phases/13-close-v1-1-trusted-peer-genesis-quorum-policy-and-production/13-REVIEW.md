---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-09-24T21:34:18Z
depth: standard
files_reviewed: 66
files_reviewed_list:
  - docs/trusted-peer-genesis.md
  - example/crdt_globaldb/CMakeLists.txt
  - example/node_test/sgns_config.json
  - src/account/BurnConfig.cpp
  - src/account/BurnConfig.hpp
  - src/account/CMakeLists.txt
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/account/TrustStartupController.cpp
  - src/account/TrustStartupController.hpp
  - src/crdt/globaldb/CMakeLists.txt
  - src/crdt/globaldb/GlobalDbNetworkComposition.cpp
  - src/crdt/globaldb/GlobalDbNetworkComposition.hpp
  - src/securecrdt/CMakeLists.txt
  - src/securecrdt/QuorumThresholdValidation.hpp
  - src/securecrdt/SecureCrdt.cpp
  - src/securecrdt/SecureCrdt.hpp
  - src/securecrdt/SecureCrdtCandidate.cpp
  - src/securecrdt/SecureCrdtCandidate.hpp
  - src/securecrdt/SecureCrdtRegistry.hpp
  - src/trustedpeer/CMakeLists.txt
  - src/trustedpeer/CanonicalTrustCodec.cpp
  - src/trustedpeer/CanonicalTrustCodec.hpp
  - src/trustedpeer/GenesisManifest.cpp
  - src/trustedpeer/GenesisManifest.hpp
  - src/trustedpeer/QuorumPolicy.cpp
  - src/trustedpeer/QuorumPolicy.hpp
  - src/trustedpeer/TrustStateStore.cpp
  - src/trustedpeer/TrustStateStore.hpp
  - src/trustedpeer/TrustedPeerRegistry.cpp
  - src/trustedpeer/TrustedPeerRegistry.hpp
  - src/trustedpeer/genesis_tool/CMakeLists.txt
  - src/trustedpeer/genesis_tool/GenesisCeremony.cpp
  - src/trustedpeer/genesis_tool/GenesisCeremony.hpp
  - src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp
  - src/trustedpeer/genesis_tool/LocalTrustAdmin.hpp
  - src/trustedpeer/genesis_tool/main.cpp
  - test/src/account/CMakeLists.txt
  - test/src/account/account_management_test.cpp
  - test/src/account/burnconfig_policy_e2e_test.cpp
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/multiaccount/policy_lifetime_multi_account_test.cpp
  - test/src/securecrdt/CMakeLists.txt
  - test/src/securecrdt/securecrdt_candidate_race_test.cpp
  - test/src/securecrdt/securecrdt_candidate_test.cpp
  - test/src/securecrdt/securecrdt_quorum_fixture.hpp
  - test/src/securecrdt/securecrdt_quorum_gate_test.cpp
  - test/src/securecrdt/securecrdt_registry_test.cpp
  - test/src/startup/CMakeLists.txt
  - test/src/startup/trust_first_boot_e2e_test.cpp
  - test/src/startup/trust_restart_test.cpp
  - test/src/startup/trust_tamper_e2e_test.cpp
  - test/src/trustedpeer/CMakeLists.txt
  - test/src/trustedpeer/genesis_manifest_test.cpp
  - test/src/trustedpeer/operator_approval_test.cpp
  - test/src/trustedpeer/quorum_policy_test.cpp
  - test/src/trustedpeer/trust_genesis_tool_test.cpp
  - test/src/trustedpeer/trust_state_store_test.cpp
  - test/testutil/genius_node_test_access.hpp
findings:
  critical: 1
  warning: 8
  info: 7
  total: 16
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-09-24T21:34:18Z
**Depth:** standard
**Files Reviewed:** 66
**Status:** issues_found

## Summary

Reviewed the trusted-peer genesis / quorum-policy / burn-policy stack at `bd10e92c..HEAD`: the canonical codecs (`GenesisManifest`, `QuorumPolicy`, `ConfirmedBurnState`, `CandidateCore`), the durable `TrustStateStore` chain verification, `SecureCrdt` candidate gating, `TrustedPeerRegistry`/`BurnConfig` production wiring, `TrustStartupController` (bootstrap state machine + retry dispatch), the `GeniusNode`/`TransactionManager` integration (trust-gated boot, burn-driven escrow payouts, submission leases), and the new `sgns-trust` CLI (`main.cpp`, `GenesisCeremony`, `LocalTrustAdmin`, `GlobalDbNetworkComposition`).

The security core is unusually disciplined: canonical re-encode round-trips on every decode, domain separation, exact-quorum floors (`floor(M/2)+1`, `M-floor(M/3)`), predecessor/authorizer binding on every successor, and full chain re-verification from the persisted heads on every load. The LKG-vs-replicated-state boundary and the bootstrap→peer-quorum burn upgrade path are enforced in the store, not in callers.

One release-blocking functional defect was found and mechanically verified: the `sgns-trust` CLI's option validation makes `propose-policy`, `propose-burn`, and `approve` permanently un-runnable (unreachable `allowed.insert` branches), so the documented successor-management workflow in `docs/trusted-peer-genesis.md` §6 cannot be executed with the shipped binary. Remaining findings are robustness/races in the `GeniusNode` integration, idempotency edges in mint re-application, resource-limit TOCTOU on the remote candidate filter, and several maintainability items.

## Critical Issues

### CR-01 [BLOCKER]: `sgns-trust propose-policy`, `propose-burn`, and `approve` can never pass option validation — successor administration is unusable

**File:** `src/trustedpeer/genesis_tool/main.cpp:160-217`
**Issue:** In `ValidateOptions`, the per-operation inserts for `--candidate`, `--basis-points`, and `--candidate-id` sit in `else if` branches that are unreachable, because the preceding branch already matches those operations:

```cpp
else if ( arguments.operation == "approve" ) { /* timeout + serve */ }
else if ( arguments.operation == "propose-policy" || arguments.operation == "propose-burn" )
{
    allowed.insert( "--serve-seconds" );
}
else if ( arguments.operation == "propose-policy" )   // DEAD: never reached
    allowed.insert( "--candidate" );
else if ( arguments.operation == "propose-burn" )     // DEAD: never reached
    allowed.insert( "--basis-points" );
else if ( arguments.operation == "approve" )          // DEAD: never reached
    allowed.insert( "--candidate-id" );
```

For `propose-policy` the `allowed` set therefore never contains `--candidate`. The option-presence loop (line 177) then rejects any invocation that supplies it ("option is not valid for propose-policy: --candidate"), while the required-option check (line 212) rejects any invocation that omits it ("required option missing: --candidate"). Identical contradictions hold for `propose-burn`/`--basis-points` and `approve`/`--candidate-id`. Verified by simulating the exact branch logic: all six combinations of supply/omit fail; only `genesis`, `list`, and `make-manifest` pass.

Impact: three of the six documented local operations cannot run. After the one-shot genesis ceremony, operators cannot propose or approve any policy or burn successor via the shipped binary — the quorum-update path documented in `docs/trusted-peer-genesis.md` §6 ("List, propose, and explicitly approve successors") is dead on arrival. The existing tests (`trust_genesis_tool_test.cpp`, `operator_approval_test.cpp`) exercise `LocalTrustAdmin`/`GenesisCeremony` directly and never run `main()`'s `ParseArguments`/`ValidateOptions`, which is why this escapes the suite.

**Fix:** Move the required-option inserts into the matching live branches (or drive the whole function from a per-operation table):

```cpp
else if ( arguments.operation == "propose-policy" || arguments.operation == "propose-burn" )
{
    allowed.insert( "--serve-seconds" );
    if ( arguments.operation == "propose-policy" ) allowed.insert( "--candidate" );
    if ( arguments.operation == "propose-burn" )    allowed.insert( "--basis-points" );
}
else if ( arguments.operation == "approve" )
{
    allowed.insert( "--timeout-seconds" );
    allowed.insert( "--serve-seconds" );
    allowed.insert( "--candidate-id" );
}
```

and add a test that runs `ValidateOptions` for every documented invocation shape (each operation with its full option set) so the CLI layer is covered.

## Warnings

### WR-01: `GetChildBalance` reads `account_` without the lifecycle lock — data race against `SelectAccount`

**File:** `src/account/GeniusNode.cpp:3363-3371`
**Issue:** Both `GetChildBalance` overloads dereference the `account_` member directly. Every sibling (`GetBalance`, `GetAddress`, `GetMnemonicOfActiveAccount`, `ProcessingDone`) was converted in this phase to copy the shared_ptr under `lifecycle_mutex_` via `SnapshotAccountServices()`. `SelectAccount` swaps/resets `account_` while holding that lock; an unsynchronized read is a data race on the `shared_ptr` itself (UB, torn refcount) and can use a freed `GeniusAccount`.
**Fix:**
```cpp
uint64_t GeniusNode::GetChildBalance( const std::string &child_address, const TokenID token_id )
{
    const auto snapshot = SnapshotAccountServices();
    return snapshot.account ? snapshot.account->GetUTXOManager().GetBalance( token_id, child_address ) : 0;
}
```

### WR-02: `--key-stdin` for admin operations skips `TrimLineEnding` — trailing CR rejects a valid key

**File:** `src/trustedpeer/genesis_tool/main.cpp:413-419`
**Issue:** `LoadLocalSigner` (used by `propose-policy`, `propose-burn`, `approve`) feeds the string from `ReadProtectedLine` straight into `create_signer`. The genesis path (`GenesisCeremony.cpp:165`) and the `--key-file` path (`GenesisCeremonyPlatformPosix.cpp:68`) both call `TrimLineEnding` first; `std::getline` strips `\n` but not `\r` (CRLF terminals, pasted input). The result is `INVALID_PRIVATE_KEY` for an otherwise valid key, inconsistent with the genesis behavior documented in the same tool.
**Fix:** After `ReadProtectedLine` in `LoadLocalSigner`, call `sgns::trustedpeer::genesis_ceremony_platform::TrimLineEnding( key );` before `create_signer`.

### WR-03: `ProcessImage` submits escrow without a submission lease — the exact stranding scenario the lease was built to prevent

**File:** `src/account/GeniusNode.cpp:2671-2740` (and `HoldEscrow` call site ~2760)
**Issue:** `TransferFunds`, `MintTokens`, and `RecoverFromChild` acquire a `SubmissionLease` under `lifecycle_mutex_` so a concurrent `SelectAccount` drains them before stopping the manager. `ProcessImage` snapshots account services and checks the balance, then drives `manager->HoldEscrow(...)`, which reserves escrow UTXOs and enqueues the transaction — with no lease held. A `SelectAccount` running between the snapshot and the manager call stops the manager mid-submission: escrow UTXOs stay reserved against an account whose returned job UUID will never be processed. The `SelectAccount` drain comment explicitly enumerates the three leased APIs; the fourth escrow-reserving path was missed.
**Fix:** Acquire the `SubmissionLease` in `ProcessImage` exactly as in `TransferFunds` (snapshot + `lease.Acquire()` in one `lifecycle_mutex_` critical section, release on scope exit).

### WR-04: Corrupted approval records are silently skipped, and one failed genesis activation is permanent for the process lifetime

**File:** `src/securecrdt/SecureCrdt.cpp:100-118` (`QueryCandidateRecords` `continue` on decode failure); `src/account/TrustStartupController.cpp:294-315`
**Issue:** `QueryCandidateRecords` drops any record that fails `CandidateApprovalRecord::DecodeCanonical` without an error or log, so `ReadCandidateApprovals` returns a silently partial set. If the dropped record is the bootstrapper's genesis approval, `TryActivateReviewedGenesisCandidate` fails with `INVALID_CANDIDATE`, and `RefreshClassified` stores the candidate in `failed_genesis_candidate_` — after which the identical genesis candidate is skipped on every later refresh until the process restarts. A transient/partial read at boot thus converts into a permanent WAITING_FOR_TRUST_GENESIS with only one `TRUST_ACTIVATION_FAILED` event, contradicting the retry philosophy of the surrounding dispatch ladder.
**Fix:** Log decode failures at error level in `QueryCandidateRecords` (or propagate a distinguishable error), and make `failed_genesis_candidate_` retryable for non-cryptographic failures (e.g., re-attempt genesis discovery when `ReadCandidateApprovals` returned fewer records than the CRDT key scan found).

### WR-05: Mint-v2 idempotency probe misfires after the minted outputs have been spent

**File:** `src/account/TransactionManager.cpp` (`ParseMintTransaction`, "already_applied" / "genuinely consumed" logic)
**Issue:** Redelivery detection is `GetUnconsumedUTXO( hash, 0 ).has_value()`. Once a historical mint's outputs have legitimately been spent, output 0 is consumed, so `already_applied` is false and the code falls into the `IsOutPointGenuinelyConsumed( input )` branch. The burn outpoint was consumed by this mint itself, but the tombstone's metadata cannot distinguish self-consumption from a sibling mint, so a correct historical mint is refused with `already_connected` ("duplicate burn") during rescan/rebuild. The refusal is conservative (no double-application) but produces spurious parse failures for nodes replaying old history.
**Fix:** Before refusing, compare the consuming record against this transaction's own identity (consuming metadata / src address recorded by `ConsumeUTXOs`, or presence of the bridge-executed marker for this `chainId:uncleHash`) and treat a self-match as the idempotent-redelivery path.

### WR-06: Synchronous `Refresh()` failures at boot park the node in `FATAL_TRUST_MISMATCH` for errors the dispatch ladder classifies as transient

**File:** `src/account/TrustStartupController.cpp:242` (`New` → `Refresh`); `src/account/GeniusNode.cpp` INITIALIZING_TRANSACTIONS trust-wiring block
**Issue:** `ClassifyRefreshResult` deliberately treats `PolicyDiscovery`/`BurnDiscovery` errors as `Transient` (retry with backoff) in the async dispatch path. But `TrustStartupController::New` and the SelectAccount re-entry both call `Refresh()` synchronously and treat *any* error as fatal: `New` returns the error, and `GeniusNode` logs "Trust startup failed closed" and transitions to `FATAL_TRUST_MISMATCH` — a state the runbook describes as fatal, not to be overridden. A local CRDT `QueryKeyValues` hiccup during boot or an account switch therefore bricks the node's state machine until manual restart, while the same failure during background dispatch would have been retried six times.
**Fix:** In the synchronous paths, restrict FATAL to corruption/authorization errors (`CORRUPT_*`, `INVALID_*_PROOF`, `NETWORK_MISMATCH`, `WRONG_*`); for discovery-stage I/O errors, start in a WAITING state and arm the retry dispatcher instead.

### WR-07: Trust-administration transport runs gossip with `sign_messages = false`

**File:** `src/crdt/globaldb/GlobalDbNetworkComposition.cpp:170-177`; mirrored in `src/account/GeniusNode.cpp` (`StartPubSub`)
**Issue:** The new production composition used by `sgns-trust` disables gossip message signing, copying the pre-existing node setting. CRDT heads are content-addressed and the DAG is verified, so this does not forge trust state, but during the genesis ceremony — the moment the network's trust is being established — any connected peer can inject unsigned gossip traffic on the trust topic without origin authentication (spam/eclipse pressure against head propagation, wasted fetches). For a security-bootstrap tool this is a defense-in-depth gap, not an exploit path.
**Fix:** Enable `sign_messages` (with the composition's own keypair) for the `sgns-trust` composition, or document the accepted rationale next to the setting.

### WR-08: Candidate resource limits are check-then-put without mutual exclusion on the remote filter path

**File:** `src/securecrdt/SecureCrdt.cpp:377-399` vs `621-631`
**Issue:** `SubmitCandidateApproval` enforces `MAX_ACTIVE_CANDIDATES_PER_PREDECESSOR` / approval-count / byte-budget under `candidate_write_mutex_`. `FilterCandidateApproval` (remote deltas) calls the same `ValidateCandidateApproval(..., enforce_resources=true)` with no lock, and per-element filter invocations can run concurrently on the pubsub/io threads. Each checks the pre-write state, so N simultaneous remote approvals for the same predecessor can all pass the cap before any is persisted — the aggregate limits are advisory under concurrent delivery, not enforced.
**Fix:** Take `candidate_write_mutex_` around the resource-checking path in `FilterCandidateApproval` (the validation itself is otherwise read-only), or maintain the accounting atomically (e.g., counters updated under a small mutex keyed by predecessor hash).

## Info

### IN-01: `ValidateOptions` is a 110-line branch ladder with no test coverage at the CLI layer

**File:** `src/trustedpeer/genesis_tool/main.cpp:107-219`
**Issue:** CR-01 is the direct product of this structure: interleaved per-operation `allowed`/`required` logic where adding an option means touching a live branch ordering. Tests construct `LocalTrustAdmin` directly, so nothing executes `main`'s parsing.
**Fix:** Replace with a table: `static const std::map<std::string, OptionsSpec>` (allowed values, allowed flags, required) and one generic checker; add a unit test iterating every documented invocation.

### IN-02: Runbook "Command-surface audit" drifts from the shipped surface

**File:** `docs/trusted-peer-genesis.md:496-513`
**Issue:** The audit states only `genesis` accepts `--timeout-seconds`/`--serve-seconds`; the code also accepts `--timeout-seconds` for `list` and `approve`, and `--serve-seconds` for `approve`/`propose-policy`/`propose-burn` (help text documents these). The doc's "exactly these local operations" claim should match, especially since the doc is the operator's source of truth.
**Fix:** Update the audit paragraph to the actual matrix.

### IN-03: Unguarded `.value()` on `std::optional` in trust-store verification loops

**File:** `src/trustedpeer/TrustStateStore.cpp:528,612,642` (also `src/account/TrustStartupController.cpp` none; `src/account/BurnConfig.cpp:206,299-300,377` follow the same pattern)
**Issue:** `policy.CanonicalBytes().value()` / `burn.CanonicalBytes().value()` are called during chain verification without checking engagement. The preceding hash-equality checks make them safe today, but any future relaxation of `Canonicalized()` turns a verification failure into a crash (UB) instead of a `CORRUPT_*_RECORD` error.
**Fix:** Check the optional and map `nullopt` to the corresponding `CORRUPT_*_RECORD` / `invalid_argument` failure.

### IN-04: Routine retry events logged at `critical` severity

**File:** `src/account/GeniusNode.cpp` trust event callback (`logger->critical( "{} fingerprint={} fields={}", event.code, ... )`)
**Issue:** Every `TrustStartupController::Event` — including up to six `TRUST_REFRESH_RETRY_SCHEDULED` events per dispatch — is emitted at `critical`. That dilutes the genuinely fatal codes (`TRUST_NETWORK_MISMATCH`, `TRUST_LOCAL_STATE_CORRUPT`) operators are told to alert on.
**Fix:** Map event codes to severities (fatal codes → critical; retry/activation-failed → warn/error).

### IN-05: Candidate-domain strings are duplicated across five call sites with no single source of truth

**File:** `src/account/TrustStartupController.cpp:191-220,802-804,815-817`; `src/trustedpeer/genesis_tool/LocalTrustAdmin.hpp:38-39`; `src/trustedpeer/GenesisManifest.hpp:48`
**Issue:** `"trusted-peer"`, `"burn-config"`, and `"trusted-peer-genesis"` are hardcoded in the controller's callback registrations and pending/failed classification, in `LocalTrustAdmin` defaults, and as `GenesisCandidateCore`'s default domain, while `TrustedPeerRegistry::NewProduction`/`BurnConfig::NewProduction` accept a configurable domain. Passing a custom domain silently desynchronizes the controller's routing (`QueuePendingCandidate` classifies everything not `"trusted-peer"` as burn) and the store's genesis-core verification.
**Fix:** Define the domain strings once (e.g., constants on `TrustedPeerRegistry`/`BurnConfig`) and thread them through; either remove the configurable-domain parameters or plumb them consistently.

### IN-06: Example config's `subnet_id: 0` cannot ever activate the documented example manifest (network 144)

**File:** `example/node_test/sgns_config.json:6`; `src/account/GeniusNode.cpp:768` (store opened with `subnet_id_`)
**Issue:** The node derives its genesis manifest `network_id` from `subnet_id` (0 in the example), while the runbook's example manifest uses `--network-id 144`. The fingerprint binds the network id, so a maintainer mixing the example config with the runbook example produces a node whose derived genesis candidate never matches the ceremony's. The notice added in this phase covers the placeholder keys but not this coupling.
**Fix:** Add one sentence to the `_trust_configuration_notice` (and the runbook's maintainer quick start): `subnet_id` must equal the manifest's `--network-id` for activation to be possible.

### IN-07: `TrustStateStore::Open` failure in the node is silently swallowed

**File:** `src/account/GeniusNode.cpp:374-381` (INITIALIZING_TRANSACTIONS trust wiring)
**Issue:** `auto opened = TrustStateStore::Open(...); if ( opened.has_value() ) {...}` has no else-branch. An open failure (permissions, disk, lock) leaves `trust_state_store_` null and later surfaces as a generic "Trust startup failed closed: invalid argument", hiding the root cause from operators of a security-sensitive path.
**Fix:** Log the `opened.error().message()` at error level when open fails.

---

_Reviewed: 2026-09-24T21:34:18Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
