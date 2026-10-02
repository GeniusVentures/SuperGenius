---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
reviewed: 2026-09-04T00:00:00Z
depth: standard
files_reviewed: 71
files_reviewed_list:
  - example/node_test/network_config.json
  - src/CMakeLists.txt
  - src/account/CMakeLists.txt
  - src/account/EscrowTransaction.hpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/base/CMakeLists.txt
  - src/base/gossip_auth.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/ValidatorRegistry.cpp
  - src/blockchain/ValidatorRegistry.hpp
  - src/blockchain/impl/Blockchain.cpp
  - src/crdt/globaldb/CMakeLists.txt
  - src/crdt/globaldb/GlobalDbNetworkComposition.cpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.cpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.hpp
  - src/networkregistry/CMakeLists.txt
  - src/networkregistry/NetworkMembershipFilter.hpp
  - src/networkregistry/NetworkRegistry.cpp
  - src/networkregistry/NetworkRegistry.hpp
  - src/peerregistry/CMakeLists.txt
  - src/peerregistry/PeerRegistry.hpp
  - src/processing/CMakeLists.txt
  - src/processing/impl/TaskKeys.hpp
  - src/processing/impl/TaskQueueImpl.cpp
  - src/processing/impl/TaskQueueImpl.hpp
  - src/processing/impl/processing_core_impl.cpp
  - src/processing/impl/processing_core_impl.hpp
  - src/processing/impl/processing_subtask_result_storage_impl.cpp
  - src/processing/impl/processing_subtask_result_storage_impl.hpp
  - src/processing/processing_node.cpp
  - src/processing/processing_node.hpp
  - src/processing/processing_service.cpp
  - src/processing/processing_service.hpp
  - src/processing/processing_subtask_queue_accessor_impl.cpp
  - src/processing/processing_subtask_queue_accessor_impl.hpp
  - src/processing/processing_subtask_queue_channel_pubsub.cpp
  - src/processing/processing_subtask_queue_channel_pubsub.hpp
  - src/securecrdt/QuorumThresholdValidation.hpp
  - src/securecrdt/SecureCrdt.cpp
  - src/securecrdt/SecureCrdt.hpp
  - src/securecrdt/SecureCrdtRegistry.hpp
  - src/trustedpeer/CMakeLists.txt
  - src/trustedpeer/TrustedPeerRegistry.cpp
  - src/trustedpeer/TrustedPeerRegistry.hpp
  - test/src/CMakeLists.txt
  - test/src/account/CMakeLists.txt
  - test/src/account/network_config_private_network_test.cpp
  - test/src/account/private_network_registry_binding_test.cpp
  - test/src/account/transaction_manager_pending_lifecycle_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/validator_registry_scope_test.cpp
  - test/src/networkregistry/CMakeLists.txt
  - test/src/networkregistry/network_membership_filter_test.cpp
  - test/src/networkregistry/network_registry_test.cpp
  - test/src/peerregistry/CMakeLists.txt
  - test/src/peerregistry/peer_registry_test.cpp
  - test/src/processing/CMakeLists.txt
  - test/src/processing/processing_core_gating_test.cpp
  - test/src/processing/processing_service_test.cpp
  - test/src/processing/processing_service_test.hpp
  - test/src/processing/processing_service_test_base.cpp
  - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp
  - test/src/processing/task_keys_scope_test.cpp
  - test/testutil/genius_node_test_access.hpp
findings:
  critical: 1
  warning: 8
  info: 6
  total: 15
status: issues_found
---

# Phase 15: Code Review Report

**Reviewed:** 2026-09-04
**Depth:** standard
**Files Reviewed:** 71
**Status:** issues_found

## Summary

Cycle-3 review of the current state of the private-network identity work, with focus requested on
the two most recent fail-closed teardown changes (`GeniusNode::ShutdownNodePolicyServices` CR-C2-01
and `SecureCrdt` element-filter reject-on-expiry). Both of those changes verify correct against
their tests and their stated contracts:

- The CR-C2-01 deny-all teardown path installs `MakeBootstrapMembershipFilter({})` (empty set
  provably denies everything) on a still-live GlobalDB, and `private_network_registry_binding_test`
  pins the regression. The destruction route (`global_db_shutdown_follows=true`) clears the filter
  exactly when `ShutdownNow()` follows, asserted on a pre-captured broadcaster handle.
- `SecureCrdt::RegisterFilters` now drops elements when the owner `weak_ptr` expired, and
  `network_registry_test` case (13) proves it end-to-end against a live attacker datastore.
- The CR-G01 envelope (`gossip_auth.hpp`) is bounds-safe on all length arithmetic, binds the
  embedded public key to the transport `from`-field before any membership consultation, and covers
  `from` + payload in the signature. All eight gated publish sites (broadcaster, grid channel,
  queue channel, results channel) fail closed on filter-set-without-key.
- Quorum floor math (`StrictMajorityQuorumFloor` = ceil(0.51·N)) and the empty-set rejection are
  correct; `NetworkRegistry::New` fails closed three ways (bootstrap floor, self-governance floor,
  signer-satisfiability), and the duplicate-construction guard is atomic
  (`RegisterIfAbsent` verified replace-proof).
- The scoping helpers (`TaskKeys`, `ValidatorRegistry::ScopedIdentifier`, `ScopedChainId`) are
  byte-stable on the public scope and verified disjoint across scopes; public-path routing
  (`SelectInputValidator`) correctly treats `supergenius/<id>` as genius-branch.

What remains are one genuine security gap on the validator-registry trust-on-load path (CR-01),
an inconsistent application of this phase's own reject-on-expiry policy to the three non-SecureCrdt
element filters (WR-01), and a set of robustness/quality defects listed below.

## Critical Issues

### CR-01: ValidatorRegistry::InitializeCache trusts persisted registry bytes without signature re-verification (cache poisoning via the unfiltered teardown/re-registration window)

**File:** `src/blockchain/ValidatorRegistry.cpp:1990-2026` (window created by `src/blockchain/impl/Blockchain.cpp:1599-1628`)
**Issue:** Every ingest path verifies a registry update before it is applied
(`FilterRegistryUpdate` → `VerifyUpdate`, ValidatorRegistry.cpp:1221-1242), and the live callback
`RegistryUpdateReceived` relies on that ingest filter having run. But the load path does not:
`InitializeCache` does `db_->Get(registry_key_)` → `DeserializeRegistryUpdate` (parse only) →
writes `cached_update_` / `cached_registry_` and sets `cache_initialized_ = true` without ever
calling `VerifyUpdate`. The signature/quorum check is therefore only as strong as "the element
filter was installed when the bytes landed".

That assumption is breakable: `Blockchain::Stop()` (ValidatorRegistry.cpp:130-161,
Blockchain.cpp:1599-1628) unregisters the `/?<registry-key>` element filter and new-element
callback while the GlobalDB remains alive and subscribed. `GeniusNode::SelectAccount` drives
exactly this sequence — `ShutdownAccountBoundServices(true)` resets `blockchain_` (→ `Stop()` →
filters removed) while `tx_globaldb_` is deliberately kept running, and only later transitions
back through `INITIALIZING_BLOCKCHAIN`, where `ValidatorRegistry::New` → `InitializeCache` reads
whatever landed during the window. On a public network there is no broadcaster membership gate
either, so any peer can push an unsigned `RegistryUpdate` element during the account-switch
window; the unverified bytes then become the active validator registry that feeds
`GetValidatorWeight`, `IsActiveValidator`, quorum thresholds, and certificate validation. A
forged registry (e.g. attacker ids with `GENESIS` weight) poisons consensus weighting for the
lifetime of the manager. On private networks the broadcaster membership filter narrows the
attacker set to same-PSK peers, but the trust-on-load asymmetry remains.
**Fix:** Re-run the same verification the ingest path performs before caching:

```cpp
// ValidatorRegistry.cpp, InitializeCache(), after DeserializeRegistryUpdate succeeds:
if ( !VerifyUpdate( decoded.value(), /*enforce_time_window=*/false ) )
{
    logger_->error( "{}: stored registry update failed signature/quorum verification; "
                    "refusing to activate", __func__ );
    RetryInitializationIfNeeded(); // or fail closed without caching
    return;
}
```

(Mirror the same check in `RegistryUpdateReceived` for defense-in-depth — it currently trusts the
ingest filter exclusively.)

## Warnings

### WR-01: Reject-on-expiry policy applied to SecureCrdt filters only — Blockchain, ValidatorRegistry, and TransactionManager element filters PASS THROUGH when their owner expires

**File:** `src/blockchain/impl/Blockchain.cpp:98-117`; `src/blockchain/ValidatorRegistry.cpp:1192-1203`; `src/account/TransactionManager.cpp:222-248`
**Issue:** This phase fixed `SecureCrdt::RegisterFilters` so an expired owner `weak_ptr` causes
elements to be DROPPED (`return std::vector<crdt::pb::Element>{};`, SecureCrdt.cpp:682-693, 704-709)
and `network_registry_test` case (13) codifies that as the project policy ("the same
reject-on-expiry policy as the candidate filter"). The three other element-filter families still
`return std::nullopt` (ACCEPT) on expiry — Blockchain's genesis/account-creation filters, the
ValidatorRegistry ingest filter, and TransactionManager's tx/proof filters. Today these branches
appear unreachable because the owners deterministically unregister in `Stop()`/`Close()` before
destruction, but that is exactly the invariant that failed for SecureCrdt (WR-C2-01) and the
lambdas now encode a silently fail-open fallback for the consensus-authority paths. Note the
deregistration in `TransactionManager::Stop()` exists precisely because a manager can be destroyed
late across an account switch — a late-destroyed manager whose filters were re-registered by its
successor leaves the old lambda alive with an expired `weak_ptr` and pass-through semantics.
**Fix:** In each of the four expiry branches, mirror the SecureCrdt policy:

```cpp
if ( auto strong = weak_instance.lock() )
{
    return strong->FilterGenesis( element );
}
return std::vector<crdt::pb::Element>{}; // expired owner: reject (fail-closed), never pass through
```

### WR-02: fmt::formatter<GeniusNode::NodeState> is missing three enum values — trust states log as "UNKNOWN"

**File:** `src/account/GeniusNode.cpp:4858-4891`
**Issue:** The formatter handles only `CREATING`, `MIGRATING_DATABASE`, `INITIALIZING_DATABASE`,
`INITIALIZING_PROCESSING`, `INITIALIZING_BLOCKCHAIN`, `INITIALIZING_TRANSACTIONS`, `READY`. The
three trust-lifecycle states — `WAITING_FOR_TRUST_GENESIS`, `WAITING_FOR_BURN_GENESIS`,
`FATAL_TRUST_MISMATCH` — fall through to `"UNKNOWN"`. These states are logged through fmt on the
hottest diagnostic paths of this phase: `"Transitioning to state {}"` (GeniusNode.cpp:713),
`"Ignoring transition to {}, shutdown in progress"` (line 699), `"Skipping blockchain retry,
unexpected state: {}"` (line 2269), and `"Suppressing stale trust transition from {} to {}"`
(lines 1023-1027). The free function `NodeStateToString` (lines 97-124) handles all ten states —
the two mappings have drifted. For a phase whose fail-closed behavior parks nodes in exactly these
three states, misleading logs directly hamper incident diagnosis.
**Fix:** Add the three missing cases to the formatter (or delegate to `NodeStateToString`).

### WR-03: Shipped example config re-enables blocking UPnP discovery that the WriteNetworkConfig contract exists to prevent

**File:** `example/node_test/network_config.json:1-6`
**Issue:** Commit 51df5fbf moved `example/node_test` from runtime `WriteNetworkConfig` to a shipped
static `network_config.json`, and 33931f48 extended it with the placeholder private-network keys —
but the file omits `"upnp_enabled": false`, which `WriteNetworkConfig` unconditionally writes
precisely so that "tests and examples do not depend on the host LAN" (GeniusNode.cpp:218-225,
including the documented multi-node crash this caused). With the key absent,
`LoadNetworkConfig` defaults `upnp_enabled = true` (GeniusNode.hpp:1132), so every example/node_test
run performs real, blocking SSDP/IGD discovery in `InitUPNP()` during construction and spawns the
persistent UPnP refresh thread (`RefreshUPNP`) — non-deterministic, host-dependent behavior in the
exact scenario the code comment warns about.
**Fix:** Add `"upnp_enabled": false` to the shipped JSON (one line), matching what
`WriteNetworkConfig` emits for every other test/example base path.

### WR-04: TaskQueueImpl::GrabTask leaks the task lock on two error paths, stalling the task for LOCK_TIMEOUT

**File:** `src/processing/impl/TaskQueueImpl.cpp:162-175`
**Issue:** After `LockTask(taskKey)` succeeds, the loop can abandon the task without releasing or
superseding the lock: (a) `BOOST_OUTCOME_TRY( auto task, GetTask( taskId ) )` returns failure on a
DB read error, and (b) the `IsProcessingValid` failure path runs `MarkTaskBad( taskId ); continue;`
— in both cases the just-written `/lock_<taskKey>` element stays. Every subsequent `GrabTask` on
any node treats the task as locked and only the `lockedTasks` recovery pass (after the 10-second
`LOCK_TIMEOUT`) can reclaim it via `MoveExpiredTaskLock`. A transient read failure therefore
removes the task from availability for at least 10 seconds, and the `MarkTaskBad` path only
blacklists the task locally, so peer nodes also stall on the orphaned lock.
**Fix:** Verify before locking (fetch + validate the task first, then `LockTask`), or on either
failure path remove the lock key (`db_->Remove(LockKey(taskKey), {processing_topic_})`) before
continuing.

### WR-05: GeniusNode::ProcessImage strands escrowed funds when task enqueue fails after the escrow hold is committed

**File:** `src/account/GeniusNode.cpp:3111-3136`
**Issue:** `ProcessImage` first executes `HoldEscrow` (which reserves/spends UTXOs into the escrow
lock and enqueues the escrow-hold transaction), then writes the escrow info CRDT entry and calls
`task_queue_->EnqueueTask(...)`. If `EnqueueTask` fails (returned as
`Error::DATABASE_WRITE_ERROR`) — or the transaction it commits fails — the function returns an
error, but the escrow hold is already propagating: funds are locked at the escrow address with no
task queued and therefore no `ProcessingDone`/`PayEscrow` that could ever release them, and no
timeout or compensating rollback exists on this path. Compare `MintFunds`
(TransactionManager.cpp:740-763), which wraps the equivalent sequence in try/catch with an explicit
`RollbackUTXOs`. The scoped-path changes in this phase (scoped escrow path + task-carried
`escrow_path`) touched exactly this sequencing without adding the compensating action.
**Fix:** On `EnqueueTask` failure, roll back the escrow hold (release the reserved UTXOs /
consume the lock the way `MintFunds` does), or defer the escrow hold until after the task (and its
CRDT transaction) is durably enqueued.

### WR-06: Private-network seal/publish block copy-pasted eight times across three files — security-control drift risk

**File:** `src/processing/processing_service.cpp:197-240, 572-616, 769-815`; `src/processing/processing_subtask_queue_channel_pubsub.cpp:76-114, 122-164`; `src/processing/processing_subtask_queue_accessor_impl.cpp:263-305, 583-626`
**Issue:** The identical ~40-line block (snapshot filter+key under mutex → raw publish / fail-closed
error / `DeriveGossipFromBytes` → `SealGossipPayload` → publish-or-log) is duplicated verbatim at
eight publish sites, with only the channel pointer and log prefix varying. These are the CR-G01
security controls; a future fix to any branch (e.g. adding a return-value check, changing the
seal's canonical bytes, handling a new failure mode) must be replicated eight times, and the
accessor's two copies additionally juggle lock ordering (`m_mutexResults` held across
`m_mutexMembershipFilter`) that the other six do not — a proven recipe for the exact
one-site-missed class of bug this phase's CR-G01/CR-G02a cycles were fixing.
**Fix:** Extract one helper, e.g. in `base/gossip_auth.hpp` next to the seal primitives:

```cpp
// Returns true when the payload was published (raw on public path, sealed under a set filter).
bool PublishSealedOrRaw( GossipPubSubTopic &channel, std::string_view payload,
                         const sgns::networkregistry::MembershipFilter &filter,
                         const libp2p::crypto::KeyPair &key, base::Logger logger );
```

and call it from all eight sites.

### WR-07: TrustedPeerRegistry::New dereferences a null secure_crdt — no argument null check (asymmetric with NewProduction)

**File:** `src/trustedpeer/TrustedPeerRegistry.cpp:154-178` (deref at line 603 via `RegisterSignerSetSource`)
**Issue:** `NewProduction` validates `!secure_crdt` and fails with `INVALID_CANDIDATE`
(line 191), but the plain `New` factory performs no null check before
`instance->RegisterSignerSetSource()` runs `secure_crdt_->Registry()...` — a null argument is an
immediate null-pointer dereference/UB in a public factory instead of a returned error. In-tree
callers (GeniusNode, tests) always pass a live instance, so this is a latent API-robustness crash
rather than a reachable production fault.
**Fix:** Add the same guard at the top of `New`:

```cpp
if ( !secure_crdt )
{
    return outcome::failure( Error::INVALID_CANDIDATE );
}
```

### WR-08: LoadNetworkConfig accepts negative reconnect intervals — negative scheduler delays produce immediate-retry storms

**File:** `src/account/GeniusNode.cpp:1714-1719` (read_seconds), consumed at `1843-1848`
**Issue:** `read_seconds` narrows `std::chrono::seconds` to `int` and the generic `read` helper
accepts any `IsInt()` value — including negatives — with no bounds check. A config value such as
`"bootstrap_reconnect_base_delay_sec": -1` yields a negative `base_delay`; `ConnectPeer` and
`ScheduleBootstrapReconnect` then compute negative `delay_sec` values (the
`delay_sec > max_delay` cap never triggers for negatives) and hand `std::chrono::seconds(-1)` to
the scheduler, which fires immediately — turning every disconnect into an unthrottled reconnect
storm. Same for a negative health-check interval. This is config-surface input validation on a
file the node treats as trusted-but-user-editable.
**Fix:** Clamp in `read_seconds` (and/or validate after the reads):

```cpp
if ( seconds < 0 ) { node_logger_->warn( "network_config.json: {} is negative, keeping {}", key, out.count() ); }
else { out = std::chrono::seconds( seconds ); }
```

## Info

### IN-01: Accessor logs "Published SubTask results to Results Channel" even when the publish FAILED CLOSED

**File:** `src/processing/processing_subtask_queue_accessor_impl.cpp:307` (also `:627` in `PublishExistingResults`)
**Issue:** The success-path debug log sits outside the raw/sealed branches, so the
filter-set-but-no-key case (which logs an error and publishes nothing) is immediately followed by
"Published SubTask results to Results Channel" — contradicting the actual outcome and undermining
the fail-closed diagnostics the branch was written to produce.
**Fix:** Move the debug log into the successful publish branches only.

### IN-02: ProcessingServiceTest fixture implementation duplicated across two translation units

**File:** `test/src/processing/processing_service_test.cpp:32-272`; `test/src/processing/processing_service_test_base.cpp:39-252`
**Issue:** `SetUp`, `SetUp(name, config)`, `TearDown`, and `Initialize` are defined in full in both
files. The split is deliberate and load-bearing (test/src/processing/CMakeLists.txt:21-28 documents
that compiling both into one target would be duplicate symbols), but the two ~240-line copies are
already drifting (comment text differs) and any fixture change must be made twice. Move the
definitions into `processing_service_test_base.cpp` only, and have `processing_service_test.cpp`
keep just its test cases.

### IN-03: EscapeRegex implemented three times, one copy load-bearing in tests

**File:** `src/securecrdt/SecureCrdt.cpp:54-68`; `src/networkregistry/NetworkRegistry.cpp:65-79`; `test/src/networkregistry/network_registry_test.cpp:83-97`
**Issue:** Two production copies plus a test mirror whose comment says it "must be kept in sync
with the production metacharacter set" — the test's byte-identical-pattern assertions silently
break (wrong pattern → vacuous pass/fail) if either production set changes. Hoist one
`sgns::base::EscapeRegex` (e.g. into `base/hexutil.hpp` or a small `base/regex_util.hpp`) and use
it everywhere.

### IN-04: InitLoggers duplicates the ~50-logger list between debug and release blocks

**File:** `src/account/GeniusNode.cpp:1514-1624`
**Issue:** The `SGNS_DEBUGLOGS` and release blocks enumerate the same logger set twice, differing
only in the level argument. A logger added to one block is silently unconfigured (or mis-leveled)
in the other. Build the tag list once and loop with the level as the only variable.

### IN-05: Broadcast returns success without publishing when the peer has no addresses

**File:** `src/crdt/globaldb/pubsub_broadcaster_ext.cpp:371-376`
**Issue:** When `bpi->addrs_size() <= 0`, `Broadcast` warns and returns `outcome::success()` — the
CRDT publish caller believes the delta was announced when nothing went on the wire. Pre-existing
behavior, but it converts "no addresses yet" into a silent replication gap; returning a failure (or
retrying once addresses are known) would make the gap observable.

### IN-06: DHTInit uses unchecked .value() on outcome results

**File:** `src/account/GeniusNode.cpp:2810-2813`
**Issue:** `cidtest.value()` and `cidstring.value()` are called without checking the results
(decode of a just-encoded CIDv0 realistically cannot fail, which is why this has never fired), and
`pubsub_->GetDHT()` is dereferenced without a null check. Cheap to harden; pre-existing.

---

### Notes on verified-correct behavior (no action)

- CR-C2-01 deny-all teardown (`GeniusNode.cpp:2363-2423`) and the destruction-route clear are
  correct and regression-pinned (`private_network_registry_binding_test.cpp:197-285`).
- `SecureCrdt` reject-on-expiry (SecureCrdt.cpp:666-715) matches the codified policy and is proven
  by `network_registry_test` case (13), including the pin-release ordering.
- `gossip_auth.hpp` envelope parsing is bounds-safe (`ReadU32Be` underflow guard, all subspan
  offsets derived from validated lengths) and the key↔from binding plus from+payload signature
  coverage close the same-PSK forgery path; documented residuals (no topic binding) are accepted.
- `NetworkRegistry::New`/`Unregister` lifecycle (pin-release re-entrancy, `RegisterIfAbsent`
  atomicity, G-WR-02 fail-closed callback registration, WR-02 drain-once refresh loop) is sound and
  each property has a dedicated test.
- Interim bootstrap filter ordering in `INITIALIZING_DATABASE` (GeniusNode.cpp:757-785) holds:
  `GlobalDB::Start()` subscribes nothing (topic set empty), and
  `GeniusAccount::ConfigureDatabaseDependencies` registers handlers only — no subscription precedes
  the filter install.
- `CRDTDataFilter::RegisterElementFilter` replaces by pattern while
  `CRDTCallbackManager::RegisterNewDataCallback` is first-wins — `NetworkRegistry::New`'s re-run of
  `RegisterFilters` (element filters, replace-ok) and its fail-closed handling of a
  pre-occupied callback pattern are consistent with those semantics.

_Reviewed: 2026-09-04_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
