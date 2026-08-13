---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
reviewed: 2026-08-13T11:22:21Z
depth: standard
files_reviewed: 60
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
  critical: 4
  warning: 5
  info: 0
  total: 9
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-08-13T11:22:21Z
**Depth:** standard
**Files Reviewed:** 60
**Status:** issues_found

## Summary

The phase has four ship-blocking correctness/security defects. In particular, policy advancement can permanently strand a network before initial burn activation, restart readiness is evaluated against the wrong policy, escrow burn arithmetic can overflow, and the generic SecureCrdt signature path admits unbounded unauthorized signer records. Five additional robustness and test-reliability defects should also be corrected.

Focused non-network tests (`genesis_manifest_test`, `quorum_policy_test`, and `trust_state_store_test`) passed. Network-backed tests could not bind listeners in the restricted review environment; `securecrdt_candidate_test` additionally segfaulted during teardown after fixture setup failed, which is captured below.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01 [BLOCKER]: Advancing policy before initial burn permanently deadlocks economic startup

**File:** `src/account/BurnConfig.cpp:262-310` (also `src/account/TrustStartupController.cpp:255-258` and `src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp:35-43`)

**Issue:** While the node is `WaitingForInitialBurn`, `CanApproveSuccessors()` returns true and the local administration path permits a policy-v2 proposal/activation. Once that policy commits, `OnTrustedPeerGenesisConfirmed()` refuses to create the only allowed burn-v1 candidate because it hard-requires `policy.version == 1` (line 272). The normal `ProposeBurnCandidate()` path simultaneously refuses all proposals until `IsEconomicallyReady()` is already true (line 294). There is then no API path capable of confirming burn v1, so the node remains economically unavailable permanently even though all durable state is otherwise valid. This violates the required TPR-then-burn-v1 sequencing.

**Fix:** Enforce sequencing at the durable transition boundary, not only in UI state. Reject `CommitPolicySuccessor`/policy activation while burn v1 still carries only the bootstrap proof, or allow initial burn v1 to be authorized by the current durable policy and remove the `policy.version == 1` restriction. Also make `CanApproveSuccessors()` and `LocalTrustAdmin::ProposePolicy()` reject policy transitions until the initial burn quorum is durable. Add an E2E test that attempts policy v2 before burn confirmation and proves the network can neither enter this state nor become stranded.

### CR-02 [BLOCKER]: Restart compares a historical burn proof against the current policy threshold

**File:** `src/account/BurnConfig.cpp:166-176`

**Issue:** `TrustStateStore::LoadAndVerify()` correctly verifies a burn record against the policy named by `burn.authorizing_policy_hash`, which may be an older policy. `BurnConfig::NewProduction()` then discards that result unless the proof count also meets `snapshot.policy.burn_threshold`, i.e. the current head's threshold. A valid burn state authorized by policy v1 with two signatures will therefore become economically unready after a policy v2 raises the threshold to three, and every restart will remain in `WaitingForInitialBurn`. The inverse policy change also demonstrates that proof cardinality alone is not the correct authorization test.

**Fix:** Treat `LoadAndVerify()` as the authority and publish every cryptographically verified non-bootstrap burn record. If the code must distinguish the special bootstrap-only v1 record, persist/return an explicit `burn_peer_confirmed` or authorization-kind field from `TrustStateStore`; do not re-evaluate a historical proof using the current policy. Add restart coverage where burn v1 is authorized under policy v1, policy v2 changes `burn_threshold`, and the verified burn remains ready.

### CR-03 [BLOCKER]: Escrow burn multiplication overflows and undercharges large escrows

**File:** `src/account/TransactionManager.cpp:833-838`

**Issue:** `escrow_amount` and `burn_basis_points` are `uint64_t`. `(escrow_amount * burn_basis_points)` wraps before division for any amount above `UINT64_MAX / burn_basis_points`. A confirmed 100% burn, for example, produces the wrong burn for sufficiently large valid escrow amounts and sends value to recipients that should have been destroyed. This is incorrect financial behavior and can cause value-accounting loss.

**Fix:** Use overflow-safe percentage arithmetic, for example a checked 128-bit intermediate where supported:

```cpp
const auto scaled = static_cast<unsigned __int128>( escrow_amount ) * burn_basis_points;
const auto burn_amount = static_cast<uint64_t>( scaled / BASIS_POINTS_TOTAL );
```

Alternatively compute quotient and remainder with checked operations. Add boundary tests at `UINT64_MAX` for 1, 100, and 10,000 basis points.

### CR-04 [BLOCKER]: Unauthorized signers can persist unbounded signature children

**File:** `src/securecrdt/SecureCrdt.cpp:186-228` (also `src/securecrdt/SecureCrdt.cpp:644-658`)

**Issue:** Both the local `AddSignature()` gate and the remote filter verify only that the supplied signature matches the supplied public key. Neither checks that the address belongs to the registry entry's current signer set before persisting `base/sig/<address>`. `ReadIfQuorum()` later ignores unauthorized signers, but every attacker-controlled keypair can still add another valid child indefinitely. Because there is no per-key cap or cleanup, an untrusted network participant can grow replicated storage without contributing to quorum.

**Fix:** Resolve `entry.signer_set_source(base_key)` in both `AddSignature()` and `FilterSecureCrdtUpdate()` and reject addresses absent from the authorized set before `Put`/acceptance. Validate canonical address shape and impose a bounded signature-child count keyed to the authorized set. Add local and remote tests showing valid outsider signatures are never retained.

## Warnings

### WR-01 [WARNING]: Operator and startup callbacks silently discard activation failures

**File:** `src/trustedpeer/genesis_tool/LocalTrustAdmin.cpp:35-70` (also `src/account/TrustStartupController.cpp:115-143`)

**Issue:** Every opportunistic `TryActivate*` result is cast to `void`. A RocksDB commit failure, corrupt durable head, or invalid proof can therefore occur after an approval is accepted while the CLI still exits successfully and startup emits no diagnostic. Operators receive a candidate ID but no indication that the now-quorate candidate failed to become durable.

**Fix:** Propagate activation errors from `LocalTrustAdmin`; distinguish the expected under-quorum outcome from actual store/validation errors. In asynchronous startup callbacks, log/emit structured activation failures before refreshing state.

### WR-02 [WARNING]: Successful ceremony can unlink a replacement while leaving the secret key behind

**File:** `src/trustedpeer/genesis_tool/GenesisCeremony.cpp:100-139` and `src/trustedpeer/genesis_tool/GenesisCeremony.cpp:343-350`

**Issue:** The protected key is opened and closed during reading, then later deleted by pathname after network confirmation. Nothing verifies that the final pathname still identifies the inode that supplied the key. A rename/replacement race can make the command unlink a different file, report successful cleanup, and leave the actual bootstrap key under another name. The ceremony's security claim therefore exceeds what the implementation guarantees.

**Fix:** Retain the opened descriptor and its `(st_dev, st_ino)` through confirmation; immediately before cleanup, use `lstat`/`fstatat` with no-follow semantics to verify the pathname still names the same inode. Fail closed with the retention warning on mismatch, or use an atomic directory-fd-based deletion protocol.

### WR-03 [WARNING]: `Stop()` can join its own I/O thread and terminate the process

**File:** `src/crdt/globaldb/GlobalDbNetworkComposition.cpp:256-306`

**Issue:** `Stop()` unconditionally calls `io_thread.join()` whenever joinable. If the last composition owner is released, or `Stop()` is otherwise called, from a handler running on that owned `io_context`, this attempts to join the current thread. `std::thread::join()` throws `resource_deadlock_would_occur`; from the destructor this results in termination.

**Fix:** Detect `io_thread.get_id() == std::this_thread::get_id()` and move final joining to an external owner/thread. Prefer an explicit shutdown contract that stops on the I/O thread but joins only from a controlling thread, and add a regression that releases the last owner from a posted handler.

### WR-04 [WARNING]: Candidate test fixtures dereference null after setup failure

**File:** `test/src/securecrdt/securecrdt_candidate_test.cpp:87-94` (same pattern in `test/src/securecrdt/securecrdt_candidate_race_test.cpp:86-92` and `test/src/securecrdt/securecrdt_quorum_fixture.hpp:89-95`)

**Issue:** `SetUp()` can fail at `ASSERT_NE(node_, nullptr)`, leaving `secure_crdt_` null, but `TearDown()` unconditionally dereferences it. In the review run, listener setup was denied and `securecrdt_candidate_test` segfaulted in this exact path, obscuring the actual setup failure and aborting the remaining tests in that binary.

**Fix:** Guard teardown dereferences (`if (secure_crdt_) ...`) and make partial fixture initialization safe. Apply the same guard to every copied fixture pattern.

### WR-05 [WARNING]: Partial filter registration is never rolled back

**File:** `src/securecrdt/SecureCrdt.cpp:578-619`

**Issue:** `RegisterFilters()` continues after individual failures and returns false, but filters successfully registered earlier in the loop remain installed. Callers then tear down the policy owners, leaving stale callbacks/patterns in GlobalDB; a retry can fail because those registrations already exist, and a partially initialized node cannot recover cleanly.

**Fix:** Track each successfully registered pattern and unregister all of them on the first failure, including candidate filters, or expose an idempotent replace/registration token API. Add a fault-injection test that fails the second registration and verifies the registry is unchanged and a retry succeeds.

---

_Reviewed: 2026-08-13T11:22:21Z_
_Reviewer: the agent (gsd-code-reviewer)_
_Depth: standard_
