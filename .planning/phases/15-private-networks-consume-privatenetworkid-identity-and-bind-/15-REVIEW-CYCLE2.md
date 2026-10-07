---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
reviewed: 2026-09-03T19:32:12Z
depth: standard
files_reviewed: 24
files_reviewed_list:
  - src/base/gossip_auth.hpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.cpp
  - src/crdt/globaldb/pubsub_broadcaster_ext.hpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/processing/processing_node.cpp
  - src/processing/processing_node.hpp
  - src/processing/processing_service.cpp
  - src/processing/processing_service.hpp
  - src/processing/processing_subtask_queue_accessor_impl.cpp
  - src/processing/processing_subtask_queue_accessor_impl.hpp
  - src/processing/processing_subtask_queue_channel_pubsub.cpp
  - src/processing/processing_subtask_queue_channel_pubsub.hpp
  - src/networkregistry/NetworkMembershipFilter.hpp
  - src/networkregistry/NetworkRegistry.cpp
  - src/networkregistry/NetworkRegistry.hpp
  - src/securecrdt/SecureCrdt.cpp
  - src/securecrdt/SecureCrdt.hpp
  - src/securecrdt/SecureCrdtRegistry.hpp
  - test/src/networkregistry/network_membership_filter_test.cpp
  - test/src/networkregistry/network_registry_test.cpp
  - test/src/processing/processing_service_test.cpp
  - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp
  - test/src/account/private_network_registry_binding_test.cpp
findings:
  critical: 1
  warning: 4
  info: 3
  total: 8
status: issues_found
---

# Phase 15: Code Review Report — CYCLE 2 (plans 15-14, 15-15, 15-16)

**Reviewed:** 2026-09-03T19:32:12Z
**Depth:** standard (with cross-file tracing into `GlobalDbNetworkComposition.cpp`, the vendored gossip tree (`thirdparty/libp2p/src/protocol/gossip/*`, `3rdparty/ipfs-pubsub`), `crdt_data_filter.cpp`, `crdt_callback_manager.cpp`, `globaldb.cpp`)
**Files Reviewed:** 24
**Status:** issues_found

## Summary

The cycle-2 delta was reviewed against the five requested verification axes.

**(1) Prior-findings closure — CONFIRMED IN CODE.**
- **CR-G01 (unauthenticated authorization basis): CLOSED.** `src/base/gossip_auth.hpp` implements a sealed envelope (magic + embedded marshaled public key + signature over `magic || u32be(len(from)) || from || payload`). All four gates verify BEFORE consulting membership: broadcaster (`pubsub_broadcaster_ext.cpp:184-197`), grid (`processing_service.cpp:280-298`), results (`processing_subtask_queue_accessor_impl.cpp:468-485`), queue channel (`processing_subtask_queue_channel_pubsub.cpp:210-227`). `sign_messages=true` is now set at both production sites (`GeniusNode.cpp:1920`, `GlobalDbNetworkComposition.cpp:200`) and the retained host keypair is propagated (`GeniusNode.cpp:1933`, `:2174-2180`, `:3626`; `GlobalDbNetworkComposition.cpp:191`, `:247-250`). All publish sites fail closed when a filter is set but no key is wired.
- **CR-G02a (creation-window): CLOSED.** `ProcessingNode::New` threads filter+key into `Initialize` (`processing_node.cpp:39-42`); the queue channel is gated before `Listen()` (`:225-232` vs `:291`) and the accessor before `CreateResultsChannel`/`ConnectToSubTaskQueue` (`:249-256` vs `:303-315`). Proven by `CreationTimeFilterCoversSubscriptionWindow`.
- **CR-G02b/G-WR-03 (startup window): CLOSED.** The interim bootstrap filter installs in INITIALIZING_DATABASE strictly before the first `AddListenTopic` (`GeniusNode.cpp:774-785`), and the registry-backed filter replaces it on the same mutex-guarded slot (`:1149-1155`). Verified there is no wedge: `SelectAccount` keeps the same GlobalDB/broadcaster and jumps to INITIALIZING_BLOCKCHAIN (`GeniusNode.cpp:2902-2916`), so the already-installed registry filter persists; the interim filter is deny-all on an empty bootstrap set and is replaced unconditionally on the success path.
- **G-WR-01 (teardown leaves ingest filter): CLOSED.** `SecureCrdt::UnregisterFiltersFor` (`SecureCrdt.cpp:711-721`) removes the byte-identical pattern (single construction site `IngestFilterPatternFor`), ownership-gated by the now-boolean `UnregisterIf` (`NetworkRegistry.cpp:695-736`, `SecureCrdtRegistry.hpp:192-207`). Proven by `TeardownRemovesIngestFilter`.
- **G-WR-02 (silent refresh degradation): CLOSED.** `RegisterCrdtChangeCallback` returning false fails `New` with `address_in_use` after explicit cleanup (`NetworkRegistry.cpp:401-425`); the callback manager's duplicate-pattern semantics (returns false, never replaces — `crdt_callback_manager.cpp:44-59`) make the failure real. Proven by `CallbackRegistrationFailureFailsNew`.
- **G-WR-04 (duplicate-New TOCTOU): CLOSED.** `RegisterIfAbsent` is an atomic-detecting insert (`SecureCrdtRegistry.hpp:158-177`); the loser's teardown removes nothing it does not own. Proven by `RegisterIfAbsentDoesNotReplaceLiveEntry`.

**(2) Envelope crypto soundness — VERIFIED.** The vendored gossip stamps `from = local_peer_id_` at publish (`gossip_core.cpp:200-201`), preserves the original `from` on relay (`local_subscriptions.cpp:50`), and `PeerId::fromPublicKey`/`fromBytes` use consistent multihash derivation (`peer_id.cpp:31-45`, `:96-99`; identity-hash Ed25519 ids pass `kMaxInlineKeyLength`). A sealed message replayed or relayed under a different `from` fails the `PeerId::fromPublicKey(embedded) == PeerId::fromBytes(from)` binding; forging a member's identity requires a sha256/identity preimage over the embedded marshaled key while ALSO holding the private key those bytes decode to. All `SealGossipPayload` call sites derive `from_bytes` from the same keypair that constructed the gossip host.

**(3) No new fail-open path — ONE EXCEPTION (CR-01 below).** Public nodes install nothing (interim install guarded by `!private_network_id_.empty()`; Publish/OnMessage take the raw branch when no filter) and remain byte-identical at the application layer; filter-set-but-no-key denies all publishes; empty-from fails `OpenGossipPayload` (`KEY_FROM_MISMATCH`), verified by `SealOpenForgeAndTamperCases` case (f). The exception is the failure-path teardown that clears the interim deny-all gate on a still-live GlobalDB.

**(4) Interim bootstrap filter — cannot wedge a node that later builds a registry** (replacement is unconditional on registry-construction success and the membership bases are identical strings); an empty bootstrap set is deny-all and the node stalls by design (pinned by `PrivateNodeWithoutBootstrapMembershipFailsClosed`).

**(5) Prior-cycle fixes — NOT REGRESSED.** JSON escaping in `WriteNetworkConfig` (full control-char table verified at `GeniusNode.cpp:233-271`); drain-once refresh loop preserved (`NetworkRegistry.cpp:534-571`, flag cleared under `refresh_mutex_` before unlock, no retry-spin); `TeardownClearsBroadcasterMembershipFilter` still passes through the real destruction route.

The one Critical finding is a fail-open interaction introduced by this cycle's combination of the 15-15 interim gate and the 15-16 fail-closed `New`: when `NetworkRegistry::New` fails mid-boot, the fail-closed handler CLEARS the just-installed deny-all gate on a GlobalDB that keeps running indefinitely.

## Critical Issues

### CR-01: "Fail-closed" NetworkRegistry construction failure actually opens the gossip ingest gate on a still-live GlobalDB

**File:** `src/account/GeniusNode.cpp:2374-2377` (reached from `:1137`, also `:1084` and `:1094`)
**Issue:** On every policy-stack failure path in INITIALIZING_TRANSACTIONS (`NetworkRegistry::New` failure at `:1127-1138` — now MORE reachable because G-WR-02 makes `New` fail on a duplicate change-callback pattern — plus the BurnConfig/`RegisterFilters` failures at `:1080-1096`), the handler calls `ShutdownNodePolicyServices()`, whose first action is:

```cpp
if ( tx_globaldb_ && tx_globaldb_->GetBroadcaster() )
{
    tx_globaldb_->GetBroadcaster()->ClearMembershipFilter();   // :2376
}
```

That `ClearMembershipFilter()` removes the interim deny-all gate installed at INITIALIZING_DATABASE (`:774-784`) and restores **public pass-through ingest** on a GlobalDB that is never shut down on this path — the node merely parks in INITIALIZING_TRANSACTIONS with PubSub running and topics subscribed. The error log at `:1129-1136` claims "failing closed" while the ingest gate is being opened. Exposure: any same-PSK peer (the exact adversary model this posture targets — no forging needed, RAW unsealed publishes suffice) can push arbitrary CRDT deltas/CID announces into the stalled node's durable datastore for as long as it runs; the pollution persists across restart. `PrivateNodeWithoutBootstrapMembershipFailsClosed` exercises exactly this path, so the exposure is config-reachable (empty `network_bootstrap_peers`, duplicate callback pattern, `RegisterFilters` failure). This is a regression relative to both pre-15-15 behavior (registry degrade kept the registry filter installed) and pre-15-16 behavior (callback-registration failure continued with a working registry + filters). On the full-shutdown path the same clear is followed by `ShutdownNow()` three lines later (`:2461-2464`) — a much smaller window, but the same choice (an expired-registry filter already deny-alls fail-closed; clearing it makes teardown MORE permissive than leaving it).

**Fix:** Do not clear to pass-through on paths where the GlobalDB keeps running. Replace the clear with an explicit deny-all on private nodes (and keep it on the destruction path or gate the clear on an explicit `shutting_down` parameter):

```cpp
// GeniusNode::ShutdownNodePolicyServices
if ( tx_globaldb_ && tx_globaldb_->GetBroadcaster() )
{
    if ( private_network_id_.empty() )
    {
        tx_globaldb_->GetBroadcaster()->ClearMembershipFilter();
    }
    else
    {
        // Fail-closed posture: a private node whose policy stack is going away
        // must never fall back to public pass-through ingest while its GlobalDB
        // is still live (the stalled INITIALIZING_TRANSACTIONS node keeps
        // subscribing topics).
        tx_globaldb_->GetBroadcaster()->SetMembershipFilter(
            []( const libp2p::peer::PeerId & ) { return false; } );
    }
}
```

(The interim `MakeBootstrapMembershipFilter` set could equally be left installed; the essential property is deny-by-default instead of accept-all.)

## Warnings

### WR-01: SecureCrdt ingest element filters fail OPEN when the SecureCrdt weak_ptr expires on a live GlobalDB

**File:** `src/securecrdt/SecureCrdt.cpp:679-687`
**Issue:** The element-filter lambda installed by `RegisterFilters` returns `std::nullopt` (**accept**) when `weak_self.lock()` fails:

```cpp
[weak_self, entry]( const sgns::crdt::pb::Element &element ) ... {
    if ( auto strong = weak_self.lock() )
    {
        return strong->FilterSecureCrdtUpdate( entry, element );
    }
    return std::nullopt;   // <-- expired SecureCrdt = accept everything
} );
```

Note the candidate-domain filter right below (`:695-703`) returns an empty vector (**reject**) on the same expiry — the two expiry policies are contradictory, and the SecureCrdt-update one is the fail-open one. With CR-01's failure path, `secure_crdt_.reset()` (`GeniusNode.cpp:2401`) runs while `tx_globaldb_` lives forever, so every registered branch (`network-registry/<id>`, trust, burn-config) permanently accepts unsigned remote elements on the stalled node. Even on the healthy shutdown path, `ShutdownNodePolicyServices` (`:2461`) resets `secure_crdt_` before `tx_globaldb_->ShutdownNow()` (`:2464`) — a live window in which policy-branch ingest is unfiltered. The `return std::nullopt` itself is pre-existing (not introduced by this cycle's diff, which only refactored the pattern construction), but 15-16's fail-closed `New` makes the unbounded-window case reachable, and G-WR-01's deliberate post-teardown removal has the same accept-after-teardown semantics by design — worth an explicit decision rather than an accident of weak_ptr expiry.
**Fix:** Mirror the candidate filter's policy — return `std::vector<sgns::crdt::pb::Element>{}` (reject) when `weak_self` has expired, so an expired policy owner fail-closes its branches on a GlobalDB that keeps running.

### WR-02: Grid-channel gating relies on an undocumented caller-ordering contract (ungated window if `StartProcessing` precedes `SetMembershipFilter`)

**File:** `src/processing/processing_service.cpp:168-181` (contract claimed at `processing_service.hpp:75-85`)
**Issue:** `ProcessingServiceImpl::Listen()` subscribes the grid channel unconditionally, and `OnMessage` consults `m_membershipFilter` at message time — so a caller that invokes `StartProcessing(...)` before `SetMembershipFilter(...)` runs an ungated grid window, the same class of defect CR-G02 rated a blocker when it existed inside `ProcessingNode::New`. The production site (`GeniusNode.cpp:3622-3630`) happens to order the calls correctly, but nothing in `ProcessingServiceImpl` enforces or documents this for the grid channel specifically (the header only documents the creation-site snapshot for node creation, not the grid subscription ordering), and `GridMessagesFromNonMemberPeersAreIgnored` itself exercises the wrong order (filter set at `processing_service_test.cpp:453` after `StartProcessing` at `:440`), demonstrating how easily the contract is inverted.
**Fix:** Either accept the filter/key as `StartProcessing` parameters (matching the `ProcessingNode::New` shape that closed CR-G02a), or document and defensively enforce the ordering — e.g., in `Listen()`, refuse to subscribe (or subscribe under a deny-all filter) while `private`-style gating inputs are expected; at minimum state the ordering requirement in the `SetMembershipFilter`/`StartProcessing` doc comments.

### WR-03: Service-level `SetMembershipFilter`/`SetGossipSigningKey` cannot propagate a clear to existing nodes

**File:** `src/processing/processing_service.cpp:139` and `:161`
**Issue:** Both propagation loops are guarded by the payload's truthiness — `if ( node && filter )` and `if ( node && key_copy )` — so passing an empty filter (or null key) updates the service-level snapshot but silently skips every existing `ProcessingNode`, leaving the queue/results/grid channels of live nodes gated with the stale predicate/key. Every other gate surface treats an empty filter as "clear" (`PubSubBroadcasterExt::SetMembershipFilter` documents "an empty std::function behaves like ClearMembershipFilter()"), so this surface is asymmetric and a future teardown/rotate call through the service API would appear to succeed while changing nothing.
**Fix:** Drop the truthiness guard on the propagation (still skip null `node`s), or add explicit `ClearMembershipFilter`-style methods; if skipping-empty is intentional (immutable-until-restart posture), log a warning so the skipped control is not silent — the file's own IN-01 convention ("a skipped security control must never be silent").

### WR-04: `SecureCrdtRegistry::Register` can destroy a concurrently-inserted entry while holding the registry mutex (destructor re-entrancy deadlock)

**File:** `src/securecrdt/SecureCrdtRegistry.hpp:121-137`
**Issue:** `Register` extracts any replaced entry and destroys it after `lock.unlock()` (the documented re-entrancy safety pattern), but then re-locks and calls `registry_.insert_or_assign(...)`. If another thread inserts an entry for the same pattern between the unlock and the re-lock, `insert_or_assign` destroys that racing entry **while the mutex is held** — and if that entry's `peer_registry` shared_ptr is the last reference to a `NetworkRegistry`, its destructor re-enters `Unregister()` → `UnregisterIf()` → recursive `std::shared_mutex` acquisition (deadlock/UB). This cycle added `RegisterIfAbsent` precisely to make `NetworkRegistry` registration safe, but `Register` remains the path for the other policy owners (TrustedPeerRegistry/BurnConfig pattern). Latent today (production wiring is single-threaded) and pre-existing (unchanged in this cycle's diff), but it sits directly beside the hazard 15-16 was written to close.
**Fix:** Make `Register` extract under the lock, unlock, destroy, then re-lock and only insert when the slot is still empty (or loop on `RegisterIfAbsent` semantics and log/replace explicitly) so no mapped value is ever destroyed under `registry_mutex_`.

## Info

### IN-01: Eight near-identical seal-or-fail-closed publish blocks duplicated across five files

**File:** `src/processing/processing_service.cpp:194-240, 572-616, 769-815`; `src/processing/processing_subtask_queue_channel_pubsub.cpp:73-114, 122-164`; `src/processing/processing_subtask_queue_accessor_impl.cpp:263-305, 583-626`; `src/crdt/globaldb/pubsub_broadcaster_ext.cpp:391-438`
**Issue:** The snapshot-filter/key → derive-from → seal → publish-or-log-fail-closed sequence is copy-pasted eight times (the two `ProcessingSubTaskQueueChannelPubSub` variants alone are ~40 identical lines each). Divergence risk is real: a future fix to one copy (e.g., the WR-01-style policy, or logging) can miss the others.
**Fix:** Extract one helper, e.g. `sgns::base::SealOrPublishRaw(filter, key, payload, logger, publish_fn)`, next to `gossip_auth.hpp`, and call it from all eight sites.

### IN-02: `OpenGossipPayload` parses the embedded public key twice

**File:** `src/base/gossip_auth.hpp:305-316`
**Issue:** The embedded key bytes are copied and unmarshalled once for signature verification (`unmarshalPublicKey`) and independently re-copied for `PeerId::fromPublicKey`. Functionally correct (both operate on the same raw bytes), but the duplication invites future drift; deriving the PeerId from the already-unmarshalled `public_key.value()` (re-marshalled) would NOT be equivalent — keep the raw-bytes derivation and simply note why, or reuse one copy.
**Fix:** Comment the invariant (PeerId must be derived from the exact wire bytes, not a re-marshalled key) or restructure so a single copy feeds both.

### IN-03: Interim-filter install silently skips when the broadcaster is missing

**File:** `src/account/GeniusNode.cpp:776-783`
**Issue:** If `tx_globaldb_->GetBroadcaster()` returns null on a private node, the interim gate is silently not installed (no log), unlike the success path which logs. A missing broadcaster at this point would itself be anomalous; per the codebase's own "a skipped security control must never be silent" convention, the skip should be logged as an error.
**Fix:** Add an `else { node_logger_->error(...); }` on the broadcaster-null branch.

---

_Reviewed: 2026-09-03T19:32:12Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
