---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
reviewed: 2026-09-03T14:30:14Z
depth: standard
files_reviewed: 21
files_reviewed_list:
  - src/account/GeniusNode.cpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.cpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.hpp
  - src/networkregistry/NetworkMembershipFilter.hpp
  - src/networkregistry/NetworkRegistry.cpp
  - src/networkregistry/NetworkRegistry.hpp
  - src/processing/processing_node.cpp
  - src/processing/processing_node.hpp
  - src/processing/processing_service.cpp
  - src/processing/processing_service.hpp
  - src/processing/processing_subtask_queue_accessor_impl.cpp
  - src/processing/processing_subtask_queue_accessor_impl.hpp
  - src/processing/processing_subtask_queue_channel_pubsub.cpp
  - src/processing/processing_subtask_queue_channel_pubsub.hpp
  - test/src/account/network_config_private_network_test.cpp
  - test/src/account/private_network_registry_binding_test.cpp
  - test/src/networkregistry/network_membership_filter_test.cpp
  - test/src/networkregistry/network_registry_test.cpp
  - test/src/processing/processing_service_test.cpp
  - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp
  - test/testutil/genius_node_test_access.hpp
findings:
  critical: 2
  warning: 4
  info: 3
  total: 9
status: issues_found
---

# Phase 15 (Gap-Closure Delta, Plans 15-09..15-13): Code Review Report

**Reviewed:** 2026-09-03T14:30:14Z
**Depth:** standard
**Files Reviewed:** 21
**Status:** issues_found

## Summary

Reviewed the phase-15 gap-closure delta (base `87eccfc3`..`ff983785`, plus the four
build-registration files it touched): NetworkRegistry lifecycle hardening (15-09), the
fail-closed network_config chain (15-10), the NetworkMembershipFilter gossip-ingest gate
(15-11), GeniusNode per-node filter wiring (15-12), and the three processing-path
membership gates (15-13).

**All five prior findings are verified fixed** (CR-01, WR-01, WR-02, WR-03, WR-04 —
details below); WR-05 (the `entry.peer_registry = shared_from_this()` ownership cycle)
still stands, unchanged and not worsened, as expected for a deferred item.

The gap closure itself is well executed: the writer/reader fail-closed chain is correct
and regression-tested, the drain-once refresh loop has no lost-wakeup path (verified by
interleaving analysis against the mutex-guarded notify), the duplicate-New check is
non-destructive, `RegisterFilters()` re-registration is genuinely replace-and-succeed
(`CRDTDataFilter::RegisterElementFilter` swaps same-pattern entries), and the teardown
ordering (clear filter -> Unregister registry -> reset) is sound and tested
non-vacuously.

However, the review found **two new Critical defects in the membership-gate mechanism
itself**. Both exploit the fact that the gates decide on the gossip `from` field / the
protobuf-declared peer — data that is **unauthenticated** under the production gossip
configuration (`sign_messages = false` at `GeniusNode.cpp:1885`; the vendored gossip
never verifies `from` or signatures on the receive/forward path), and that node-level
channels go live ungated inside `ProcessingNode::New` before the service can install
the filter. These are findings about the *replacement control the owner accepted*
(app-layer filtering), not about the descoped libp2p gater allow-list, and they are
flagged because the replacement does not deliver the identity guarantee its own comments
claim ("Checking both defends against spoofing either field"; "no enrollment window").

## Prior-Findings Verification (15-REVIEW.md)

| ID | Status | Evidence |
|----|--------|----------|
| CR-01 (invalid JSON for swarm-key) | **FIXED** | `GeniusNode.cpp:233-272` now escapes `\n \r \t \b \f` and all `< 0x20` bytes as `\uXXXX`; regression `SwarmKeyTextWithNewlinesRoundTrips` asserts the escaped form, zero raw newlines, and identity retention on reload. |
| WR-01 (parse-error fail-open) | **FIXED** (parse branch) | `GeniusNode.cpp:1651-1659` sets `settings.valid = false` on parse error; `InitNetwork` (`:1976-1979`) fails; `New` returns nullptr. Regression `CorruptConfigFailsNodeStart`. The missing-file branch deliberately remains public-defaults (documented D-01 provisioning state at `:1640-1644`) — accepted design, residual risk noted there. |
| WR-02 (RefreshLoop busy-spin) | **FIXED** | `NetworkRegistry.cpp:506-519` clears `refresh_pending_` under `refresh_mutex_` before unlock; the flag-clear is ordered against the callback's mutex-guarded notify, and any notification whose flag is clobbered landed in the datastore *before* the concurrent `TryConfirm` started, so it is consumed by that very read — no lost refresh, no spin. Regression `RefreshLoopDrainsOnceWithoutSpinning` + `RefreshAttemptsForTesting` seam. |
| WR-03 (duplicate-New clobbers live registry) | **FIXED** | `NetworkRegistry.cpp:375-386` resolves the existing policy entry before `make_shared`/`Register` and fails with `address_in_use`; nothing is registered. Regression `DuplicateNewDoesNotClobberLiveRegistry`. (Residual TOCTOU — see WR-04 below.) |
| WR-04 (no ingest filter for network-registry/\<id\>) | **FIXED** | `NetworkRegistry.cpp:405-426` re-runs `secure_crdt_->RegisterFilters()` inside `New` after `RegisterSignerSetSource`; `CRDTDataFilter::RegisterElementFilter` (`crdt_data_filter.cpp:20-43`) replaces same-pattern entries, so the re-run is safe. Regression `IngestFilterCoversLateRegisteredNetworkRegistryPattern`. (Teardown leaves the filter stale — see WR-01 below.) |
| WR-05 (shared_from_this ownership cycle) | **STANDS, unchanged** | `NetworkRegistry.cpp:447` still does `entry.peer_registry = shared_from_this()`. Deferred per instructions; GeniusNode teardown still explicitly `Unregister()`s (`GeniusNode.cpp:2328-2331`), and the `ExpiredRegistryDeniesAll` test documents the pinning. Not worsened. |

## Critical Issues

### CR-01: All four membership gates authorize on the unauthenticated gossip `from` field — a same-PSK non-member can forge a member identity and pass every gate

**File:** `src/account/GeniusNode.cpp:1885`; `src/crdt/globaldb/pubsub_broadcaster_ext.cpp:159-191`; `src/processing/processing_service.cpp:192-206`; `src/processing/processing_subtask_queue_accessor_impl.cpp:401-416`; `src/processing/processing_subtask_queue_channel_pubsub.cpp:101-117`; `src/networkregistry/NetworkMembershipFilter.hpp:109-123`
**Issue:** Every new gate decides authorization solely from wire-supplied identity data:
the declared protobuf peer (`bmsg.peer().id()`) and the transport sender
(`Gossip::Message::from`, via `AuthorizeGossipSender`). Under the production gossip
configuration this data is unauthenticated:

- `GeniusNode.cpp:1885` (and `GlobalDbNetworkComposition.cpp:187`) construct gossip with
  `config.sign_messages = false`.
- In the vendored gossip, `from` is stamped with the publisher's own id at publish
  (`thirdparty/libp2p/src/protocol/gossip/impl/gossip_core.cpp`, `GossipCore::publish` —
  unconditional `TopicMessage(local_peer_id_, ...)`) and is **never verified on the
  receive or forward path** (no signature verification and no `from`-vs-connection check
  anywhere in the gossip impl). Forwarders relay the original `from` verbatim.

Consequently the exact adversary the app-layer filter was built for — a peer that holds
the pnet PSK but is *not* in the NetworkRegistry membership (see
`NetworkMembershipFilter.hpp` doc: "PSK-holding non-member per registry is dropped") —
only needs to publish its gossip messages with `from` set to any member's PeerId (and
the protobuf `peer().id()` likewise) to pass all four gates: CRDT ingest
(`PubSubBroadcasterExt::OnMessage`), grid channel (`ProcessingServiceImpl::OnMessage`),
results channel (`OnResultChannelMessage`), and queue channel
(`OnProcessingChannelMessage`). The gate is effective only against *honest* non-members.

The in-code security claims are incorrect under this configuration:
`pubsub_broadcaster_ext.cpp:163-167` ("Checking both defends against spoofing either
field") — both fields are attacker-supplied and unsigned, so checking both defends
against nothing; the flow tests (`network_membership_filter_test.cpp`) prove denial only
for honest senders (the intruder publishes with its own `from`, and the test gossip
configs even use `sign_messages = true`).

This finding is about the accepted replacement control itself, not the descoped
libp2p gater allow-list (not re-flagged).

**Fix:** Authenticate the sender before consulting membership. Minimal viable path:

1. Enable gossip signing (`config.sign_messages = true` in `StartPubSub` /
   `GlobalDbNetworkComposition`) so each message carries `signature` + `key`.
2. Since the vendored gossip does not verify on receive, verify in the gate: in
   `PubSubBroadcasterExt::OnMessage` (and a shared helper used by the three
   processing-path gates), recover the PeerId from the message's embedded public `key`,
   check it matches `from`, verify the signature over the signable message bytes, and
   only then run the membership predicate. Reject missing signature/key under a set
   filter (fail-closed, mirroring the existing empty-`from` denial).

If message signing cannot be enabled at this layer, the code comments and plan documents
must stop claiming spoof-defense and state plainly that the gate is a policy filter over
declared identity, enforceable only against well-behaved peers.

### CR-02: Enrollment window in `ProcessingNode::New` — queue/results channels subscribe and run ungated seconds before the filter is installed, contradicting the "no enrollment window" claim

**File:** `src/processing/processing_node.cpp:24-51,154-233`; `src/processing/processing_service.cpp:388-439,778-820`; `src/processing/processing_service.hpp:73-77`
**Issue:** `processing_service.hpp:73-77` documents "Applied to all existing processing
nodes AND at node creation, so there is no enrollment window (T-15-13-06)". The code does
not deliver that property:

- `ProcessingNode::New` (`processing_node.cpp:33`) calls `Initialize`, which constructs
  the queue channel and calls `processingQueueChannel->Listen(msSubscriptionWaitingDuration)`
  at `:204` — subscribing `OnProcessingChannelMessage` (whose `m_membershipFilter` is
  still **empty** = public pass-through) and waiting up to the 2000 ms default
  (`processing_node.hpp:59`).
- `AttachTo` (`:35`, body `:211-233`) then subscribes `OnResultChannelMessage` on the
  results channel — still ungated.
- Only after `New` returns do the two node-creation sites install the filter:
  `AcceptProcessingChannel` (`processing_service.cpp:430-438`) and
  `HandleNodeCreationTimeout` (`:811-819`).

So on **every node creation**, the two channel handlers that 15-13 exists to gate accept
messages from any PSK-holding peer for a multi-hundred-millisecond to multi-second window
(the `Listen` wait plus `AttachTo`/queue-creation time). A non-member sending a
`SubTaskQueueRequest`, a full `SubTaskQueue`, or a `SubTaskResult` timed into this window
gets it processed ungated — e.g. grabbing queue ownership or pushing a poisoned queue
snapshot/result. (The grid channel is not affected: its filter is installed before
`StartProcessing` -> `Listen` in `GeniusNode.cpp:3557-3572`.)

**Fix:** Pass the filter into the node before any subscription goes live, e.g. add an
optional `sgns::networkregistry::MembershipFilter` parameter to `ProcessingNode::New`
that `Initialize` forwards to the channel (`SetMembershipFilter`) and accessor before
`Listen()` and before `ConnectToSubTaskQueue()`. `ProcessingServiceImpl` snapshots
`m_membershipFilter` under `m_membershipFilterMutex` *before* calling `ProcessingNode::New`
at both creation sites (the current post-hoc `node->SetMembershipFilter(...)` blocks can
then be dropped). The setter-based propagation remains useful for live refresh.

## Warnings

### WR-01: Teardown unregisters the SecureCrdtRegistry entry but never removes the GlobalDB ingest filter installed by `RegisterFilters()` — stale filter keeps accepting unsigned `network-registry/<id>` base elements after the policy owner is gone

**File:** `src/networkregistry/NetworkRegistry.cpp:405-426,652-674`; `src/account/GeniusNode.cpp:2308-2348`; `src/securecrdt/SecureCrdt.cpp:678-686,712-722`
**Issue:** `NetworkRegistry::New` installs a `FilterSecureCrdtUpdate` element filter on
the GlobalDB (`SecureCrdt::RegisterFilters`, `SecureCrdt.cpp:678-686` — the lambda
captures the registry **entry by value**). `NetworkRegistry::Unregister()` removes only
the `SecureCrdtRegistry` entry (`UnregisterIf`); there is no
`db_->UnregisterElementFilter(...)` counterpart, so the GlobalDB filter outlives the
registry it was created for. Its captured `signer_set_source` weak_ptr then fails to lock
and returns an empty snapshot — fail-closed for `sig/<addr>` children
(`ResolveLegacySignerSnapshot` rejects empty signer sets, `SecureCrdt.cpp:148-157`) — but
the **base-value path** (`SecureCrdt.cpp:712-722`) only performs structural
`DeserializeFromBytes` + `Verify` and *accepts* any well-formed record, deferring trust
to `ReadIfQuorum` at read time.

This matters on the failure/retry paths where the policy stack is torn down while
`tx_globaldb_` stays alive: `ShutdownNodePolicyServices()` is invoked from
`INITIALIZING_TRANSACTIONS` failures (`GeniusNode.cpp:1057,1066,1109`) and the node keeps
its GlobalDB syncing. In that state a remote peer can still push unsigned
membership-record bytes into the `network-registry/<id>` branch of the local datastore —
re-opening, post-teardown, exactly the griefing/pending-confirmation-reset vector the
WR-04 fix closed. Not an authorization bypass (`ReadIfQuorum` still gates application),
but a defense-in-depth hole and a stale-callback leak on a live GlobalDB.

**Fix:** Have `NetworkRegistry::Unregister()` remove its ingest filter when it knows the
db (requires keeping a handle/pattern, e.g. store `global_db_` pattern
`"/?" + EscapeRegex(base_key_.GetKey()) + "(/sig(/.*)?)?"` and call
`global_db_->UnregisterElementFilter(pattern)` — note `RegisterFilters` builds the wide
sig pattern, so the removal string must match it), or give `SecureCrdt` an
`UnregisterFiltersFor(pattern)` that `ShutdownNodePolicyServices` calls alongside
`network_registry_->Unregister()`.

### WR-02: `RegisterCrdtChangeCallback` failure degrades silently — `New()` succeeds with no refresh thread and no live membership refresh

**File:** `src/networkregistry/NetworkRegistry.cpp:476-489`
**Issue:** If `RegisterNewElementCallback` returns false (pattern already registered —
reachable via the WR-04 race below, or an unregistered-then-reconstructed registry on the
same GlobalDB), the method logs `warn`, clears `change_callback_pattern_`, and returns
**without starting the refresh thread** — while `NetworkRegistry::New` continues and
returns a fully "successful" instance. The documented contract of the `global_db`
parameter ("null disables the callback") implies non-null enables live refresh; here the
caller gets a registry whose cached membership silently never updates from replicated
quorum-signed changes, so runtime membership widening never reaches the installed
filters on that node. The 15-12 wiring's "live refresh via tx_globaldb_" is the core
feature of the plan; a warn-only partial failure is the wrong posture for it.

**Fix:** Treat `registered == false` as construction failure
(`return outcome::failure(std::errc::address_in_use)` from `New`, mirroring the
duplicate-registry posture), or at minimum surface it via the return value so
`GeniusNode` can fail closed like the other NetworkRegistry failures.

### WR-03: Private-node startup ingest window — GlobalDB listens and merges gossip from `INITIALIZING_DATABASE` onward, but the broadcaster filter is only installed at NetworkRegistry construction in `INITIALIZING_TRANSACTIONS`

**File:** `src/account/GeniusNode.cpp:756-758,1090-1127`
**Issue:** `tx_globaldb_` is created and started (and subscribes
`ScopedProcessingChannel()`) during `INITIALIZING_DATABASE` (`:756-758`), and the
blockchain/quorum topics are added during `INITIALIZING_BLOCKCHAIN`/`INITIALIZING_TRANSACTIONS`
(`:852`). The gossip-ingest membership filter, however, is installed only after the
NetworkRegistry is constructed at `:1121-1127` — i.e. after blockchain init completes. On
every private-node boot there is therefore a startup window (seconds or more, bounded by
blockchain startup) during which inbound broadcasts are ungated public pass-through, so
a same-PSK non-member's CRDT deltas are merged, and — per the documented gated-member
relay property (15-11 deviation: `CrdtDatastore::RebroadcastHeads` republishes head CIDs
under the member's own identity) — can be re-origined by this node into other members
even after the filter installs. The bootstrap membership (`network_bootstrap_peers_`) is
known from config at `InitNetwork` time, so an earlier install is feasible.

**Fix:** Install a bootstrap-membership-backed filter on the broadcaster immediately
after GlobalDB creation (using `MakeNetworkMembershipFilter` over the configured
`network_bootstrap_peers_` — e.g. by constructing the registry, or a lightweight
config-backed stand-in, before `tx_globaldb_->Start()`), then let the registry-backed
filter replace it at `INITIALIZING_TRANSACTIONS` as today. At minimum, document this
window in the 15-12/15-11 deviation notes alongside the relay property.

### WR-04: Duplicate-New check is check-then-act — concurrent constructions for the same network id resurrect the WR-03 clobber

**File:** `src/networkregistry/NetworkRegistry.cpp:375-400`
**Issue:** The WR-03 fix resolves `Registry().Resolve(base_key)` before `make_shared`,
but `SecureCrdtRegistry::Register` still *replaces* same-pattern entries. Two
concurrent `NetworkRegistry::New` calls for the same id can both pass the `Resolve`
check, and the second `Register` then replaces the first registry's policy entry; when
the second instance is destroyed its `~NetworkRegistry -> Unregister()` removes the
(replacement) entry and the first, still-live registry is left with no policy entry —
the exact bricking WR-03 fixed, now requiring only concurrency rather than a failure
path. Today's production wiring is single-threaded (state-machine transitions), so this
is latent, but `NetworkRegistry` is public, per-network, app-constructible API and
`New`'s own doc sells the check as making duplicates safe.

**Fix:** Make the registration itself atomic-detecting: have `RegisterSignerSetSource`
inspect `Register`'s "replaced" return (it already returns false when an entry was
replaced) and fail `New` *after restoring/forwarding nothing* — better, snapshot the
existing entry inside `SecureCrdtRegistry::Register` (under its mutex) and expose a
`RegisterIfAbsent(key, entry)` that fails cleanly, so no destroy-ordering hazard exists.

## Info

### IN-01: `ProcessingNode::SetMembershipFilter` silently no-ops when the `dynamic_pointer_cast` fails

**File:** `src/processing/processing_node.cpp:132-152`
**Issue:** If `m_subTaskQueueAccessor` or `m_queueChannel` is not the expected concrete
type, the cast yields null and the filter is silently not installed on that component —
the node then runs that channel ungated with no log line. All current wirings use the
expected types, so this is latent, but a silent security-control skip deserves a warn.
**Fix:** `else { m_logger->warn(...) }` on each failed cast.

### IN-02: Public `NetworkRegistry` constructor dereferences its dependencies unchecked

**File:** `src/networkregistry/NetworkRegistry.cpp:286-309`
**Issue:** The public constructor immediately dereferences `global_trusted_peers_`
(`:307`). `New()` validates non-null, but the constructor is public and directly used by
tests (`network_membership_filter_test.cpp:262`); a `nullptr` argument is a segfault
rather than an error. **Fix:** Throw `std::invalid_argument` (or assert) on null
`secure_crdt_`/`global_trusted_peers_` in the constructor body.

### IN-03: `WriteNetworkConfig` escapes only `network_key` — `private_network_id` and `network_bootstrap_peers` are written raw

**File:** `src/account/GeniusNode.cpp:274-293`
**Issue:** The CR-01 escaping loop covers `network_key`, but `private_network_id`
(`:277`) and each bootstrap peer (`:290`) are emitted without escaping, justified by
comments about their expected character sets. The helper is a public API; a caller
passing an id or peer string containing `"` or `\` writes invalid JSON. Thanks to the
WR-01 fix this now fails closed on reload (node refuses to start) instead of silently
booting public — but the outcome is a bricked boot from a writer the project itself
ships. **Fix:** Reuse the same escaping loop for all three string fields.

---

_Reviewed: 2026-09-03T14:30:14Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
