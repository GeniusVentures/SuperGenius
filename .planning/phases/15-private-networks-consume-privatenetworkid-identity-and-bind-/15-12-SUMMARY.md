---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "12"
subsystem: auth
tags: [privatenetwork, networkregistry, membershipfilter, geniusnode, gossip, pubsubbroadcaster, ingestgate, pnet, livecache, teardown, cplusplus]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "09"
    provides: hardened NetworkRegistry lifecycle — WR-02 drain-once refresh loop (no busy-spin once tx_globaldb_ starts it), WR-04 ingest element filter
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "11"
    provides: sgns::networkregistry::MakeNetworkMembershipFilter (weak_ptr-backed, fail-closed) + PubSubBroadcasterExt::Set/Has/ClearMembershipFilter + the OnMessage dual-identity ingest gate
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "05"
    provides: the per-node NetworkRegistry construction site in INITIALIZING_TRANSACTIONS (guard !private_network_id_.empty() && !network_registry_) and the ShutdownNodePolicyServices teardown ordering
provides:
  - VERIFICATION gap 3 closed with node-level proof (plus the node-wiring halves of gaps 1/4): a private node that reaches READY has the registry-backed membership filter installed on its GlobalDB broadcaster — the 15-05 descoped D-07 consumption half, in the owner-directed app-layer form (deferred-items.md §3)
  - Live membership refresh active in-node: NetworkRegistry::New now receives tx_globaldb_ as the trailing global_db argument, so runtime membership widening reaches the enforcement point without a restart (the deliberately-deferred 15-05 wiring, enabled now that the consumer exists)
  - Clean teardown: ShutdownNodePolicyServices clears the broadcaster filter before the registry unregister/reset
  - GeniusNodeTestAccess::BroadcasterMembershipFilterInstalled / BroadcasterOf / RequestShutdownForDestruction test accessors
  - 4-scene private_network_registry_binding suite (filter-installed / public-none / fail-closed / teardown-clear)
affects: [15-13-processing-path-gates, geniusnode-startup, networkregistry, crdt_globaldb]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Enforcement-wiring colocated with authority construction: the filter is installed in the same guarded block that constructs the NetworkRegistry (after assignment, before any READY transition) — there is no reachable window where a private node runs with a registry but without enforcement"
    - "Test-held broadcaster handle for non-vacuous teardown assertion: BroadcasterOf returns the shared_ptr by value so the broadcaster object outlives GlobalDB::ShutdownNow's move-out; Stop() does not touch the filter, so HasMembershipFilter() true->false on the held handle is attributable solely to the ClearMembershipFilter() call"
    - "Trailing-defaulted-args extension of NetworkRegistry::New (signers/{}/fingerprint/{}/tx_globaldb_) to switch on the live CRDT-change cache refresh without touching the call's established prefix — the BurnConfig pattern"

key-files:
  created: []
  modified:
    - src/account/GeniusNode.cpp
    - test/testutil/genius_node_test_access.hpp
    - test/src/account/private_network_registry_binding_test.cpp

key-decisions:
  - "Filter install is unconditional-on-success inside the !private_network_id_.empty() && !network_registry_ guarded block (null-guards on tx_globaldb_/GetBroadcaster() are defensive only — both are always live on this path), immediately after the registry assignment and before the READY transition: a private node either runs membership-enforced gossip or fails closed at construction (T-15-12-01)"
  - "Teardown clear sits after trust_startup_controller_.reset() and immediately before the network_registry_ unregister/reset block: clear first, then release the registry; even without the clear the weak_ptr predicate denies-all (fail-closed both ways, T-15-12-02), but the explicit clear keeps the post-teardown state clean and testable"
  - "The install log prints ONLY private_network_id_ (D-03, never network_key_), beside the unchanged existing public-id-only membership-size log"
  - "requirements.mark-complete skipped for this plan's frontmatter IDs (D-01/D-03/D-06/D-07/PNET-GATE): Phase-15 decision/gap IDs live in 15-VERIFICATION.md, not in REQUIREMENTS.md checkboxes — same precedent as the 15-10/15-11 docs commits"

patterns-established:
  - "Every private node gates its own gossip ingest (per-node wiring at NetworkRegistry construction): the 15-11 debug-session invariant — an ungated private member can relay non-member data into gated nodes via CrdtDatastore::RebroadcastHeads republishing head CIDs under its own identity — is closed at the node level because no reachable private node exists without the filter"

requirements-completed: [D-01, D-03, D-06, D-07, PNET-GATE]

# Metrics
duration: 11min
completed: 2026-09-03
---

# Phase 15 Plan 12: Node-Level Membership Filter Wiring Summary

**GeniusNode installs the registry-backed membership filter on its single GlobalDB broadcaster at NetworkRegistry construction (live-refreshing via the tx_globaldb_ trailing arg) and clears it before registry teardown — every private node that reaches READY is membership-enforced at gossip ingest, public nodes install nothing, proven by a 4-scene binding suite including a non-vacuous teardown scene on a held broadcaster handle**

## Enforcement Invariant (from the 15-11 debug session — restated per run instructions)

The debug session (`.planning/debug/xsuite-membership-filter-fail.md`, resolved) established that an
UNGATED private-network member structurally relays non-member data into gated nodes:
`CrdtDatastore::RebroadcastHeads` republishes CID head lists under the rebroadcaster's own identity,
and CIDs carry no origin, so a gated node's filter correctly authorizes the relayed announcement and
graphsync-fetches non-member blocks from its trusted member. The consequence for this plan's wiring —
**the membership filter must be installed on EVERY private-network node's broadcaster** — is
satisfied structurally by this plan: the wiring point (NetworkRegistry construction in GeniusNode) is
per-node, and 15-05 established that every private node constructs a NetworkRegistry or fails closed.
Therefore no reachable state contains a private node without the filter:

- A private node with a constructible membership authority gets the filter at construction, before any
  READY transition — asserted by the filter-installed scene on a READY node.
- A private node whose authority cannot be constructed never reaches READY at all (15-05 fail-closed
  scene re-run green) — it cannot participate as an ungated relay.
- A public node installs nothing (public scene pins it) — public nodes are outside the private
  network's PSK boundary and are specified pass-through.

Every member that could rebroadcast heads into a gated member is itself gated, so the relay vector has
no ungated origin inside a private network.

## Performance

- **Duration:** ~11 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T13:26:50Z
- **Completed:** 2026-09-03T13:38:17Z
- **Tasks:** 2/2
- **Files modified:** 3 (0 created)

## Accomplishments

- **Task 1 — GeniusNode wiring** (`src/account/GeniusNode.cpp`):
  - `NetworkMembershipFilter.hpp` included beside the existing networkregistry include (no CMake
    change — `networkregistry` is a PUBLIC GENIUS_NODE_LIBS dep since 15-05).
  - The `NetworkRegistry::New` call in INITIALIZING_TRANSACTIONS now passes the trailing defaulted
    args explicitly — `/*initial_network_signers=*/{}, /*pnet_key_fingerprint=*/{}, tx_globaldb_` —
    enabling the live CRDT-change cache refresh (the deliberately-deferred 15-05 wiring; safe now
    because 15-09's WR-02 fix made the refresh loop wake-per-notification, never spin).
  - Immediately after `network_registry_ = network_registry_result.value();`: installs
    `SetMembershipFilter( sgns::networkregistry::MakeNetworkMembershipFilter( network_registry_ ) )`
    on `tx_globaldb_->GetBroadcaster()` (the single GlobalDB broadcaster — `secure_crdt_` wraps
    `tx_globaldb_`, so this one gate covers chain elements, network-registry updates, and all
    job/task/result CRDT ingest), logs `Gossip ingest membership filtering active for private network
    {id}` (public id only, D-03), with the enforcement-posture comment (pnet = credential possession
    at transport; THIS filter = the identity/membership decision, D-07, app-layer per
    deferred-items.md §3; processing-path channels get the same filter in 15-13). The existing
    membership-size log is unchanged.
  - `ShutdownNodePolicyServices`: clears the broadcaster filter immediately before the
    `network_registry_` unregister/reset block (fail-closed either way; no-op for public nodes).
  - Public paths byte-identical: the committed diff touches only the include, the guarded private
    construction block, and the teardown function (diff-verified).
- **Task 2 — test accessors + binding scenes:**
  - `test/testutil/genius_node_test_access.hpp`: `BroadcasterMembershipFilterInstalled` (node &&
    tx_globaldb_ && broadcaster && HasMembershipFilter), `BroadcasterOf` (returns the broadcaster
    shared_ptr BY VALUE — keeps the object alive across GlobalDB::ShutdownNow's move-out; includes
    `crdt/globaldb/globaldb.hpp` because the accessor calls GetBroadcaster/HasMembershipFilter, which
    the forward-declaration trick cannot support), and `RequestShutdownForDestruction` (friend route
    to the PRIVATE `ShutdownForDestruction` at GeniusNode.hpp:1293).
  - `PrivateNodeConstructsNetworkRegistryFromBootstrapMembership` now also asserts the filter is
    installed on the READY private node (both D-06 and D-07 halves in one scene).
  - `PublicNodeConstructsNoNetworkRegistry` also asserts NO filter installed (public-path regression
    pin).
  - NEW `TeardownClearsBroadcasterMembershipFilter`: builds the private node exactly as scene (a),
    confirms READY + filter installed, captures `BroadcasterOf( node )` BEFORE shutdown (ASSERT_NE +
    HasMembershipFilter true), calls `RequestShutdownForDestruction( node )` while the node
    shared_ptr is alive (the real route: ShutdownAccountBoundServices → ShutdownNodePolicyServices —
    where the clear lives — → tx_globaldb_->ShutdownNow), then asserts HasMembershipFilter false on
    the SAME held handle. Non-vacuous by construction: a post-shutdown query through the node would
    see a null broadcaster (ShutdownNow moves m_broadcaster out) and pass vacuously; the held handle
    keeps the object alive and Stop() does not touch the filter, so the false verdict is attributable
    solely to the ClearMembershipFilter call. Double-shutdown safety documented in-scene (~GeniusNode
    re-invokes via the shutdown_started_ CAS no-op).

## Task Commits

Each task was committed atomically:

1. **Task 1: GeniusNode installs the registry-backed gossip membership filter + live refresh** - `b85104c0` (feat)
2. **Task 2: binding scenes — installed/private, absent/public, cleared/teardown** - `7721deff` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/account/GeniusNode.cpp` - include; trailing-args New call (tx_globaldb_ live refresh); SetMembershipFilter install inside the guarded construction block with D-03-safe log + posture comment; ClearMembershipFilter before registry unregister/reset in ShutdownNodePolicyServices
- `test/testutil/genius_node_test_access.hpp` - BroadcasterMembershipFilterInstalled / BroadcasterOf / RequestShutdownForDestruction accessors; crdt/globaldb/globaldb.hpp include
- `test/src/account/private_network_registry_binding_test.cpp` - filter assertions in the private and public scenes; new TeardownClearsBroadcasterMembershipFilter scene (4 scenes total)

## Decisions Made

- See key-decisions. The install-timing call (unconditional-on-success, pre-READY), the teardown-clear
  placement (before registry release, after controller reset), and the by-value BroadcasterOf accessor
  (non-vacuity) were the substantive interpretation calls; all are pinned by the binding scenes or
  acceptance-grepped source facts.
- Verified before relying on them: `globaldb.hpp` includes `pubsub_broadcaster_ext.hpp` directly (the
  accessor header's HasMembershipFilter call compiles); `crdt_globaldb`/`networkregistry` are PUBLIC in
  GENIUS_NODE_LIBS (include dirs propagate to the test target); `MakeNetworkMembershipFilter` accepts
  the shared_ptr `network_registry_` via implicit weak_ptr conversion.

## Deviations from Plan

None - plan executed exactly as written. (Line anchors matched current reality: the construction block
at GeniusNode.cpp:1080-1108, ShutdownNodePolicyServices at :2284, ShutdownForDestruction at :2321 with
the ShutdownNodePolicyServices call at :2372; the ":1079-1082" membership-size log in the plan is the
log at :1104-1107 pre-edit — kept unchanged as ordered.)

## Verification Evidence

- `ninja -C build/OSX/Release genius_node` — clean (1 pre-existing switch warning at :4776, outside
  the edited regions)
- Task 1 source assertions: `MakeNetworkMembershipFilter` x1, `SetMembershipFilter` x1,
  `ClearMembershipFilter` x1 in GeniusNode.cpp; the New call lists `tx_globaldb_` after the quorum
  floor; the install log contains `private_network_id_` and no `network_key_` material; committed
  diff shows no edit outside the guarded block + teardown (+ the include)
- `ctest -R private_network_registry_binding` — Passed (binary: 4/4 scenes —
  PrivateNodeConstructsNetworkRegistryFromBootstrapMembership, PublicNodeConstructsNoNetworkRegistry,
  PrivateNodeWithoutBootstrapMembershipFailsClosed, TeardownClearsBroadcasterMembershipFilter;
  `--gtest_list_tests` confirms all four registered; ~20s)
- `ctest -R network_config_private_network` — Passed (15-10 suites intact on the same GeniusNode.cpp)
- `ctest -R network_registry_test` — Passed (binary carries 12 cases today — 15-09 added beyond the
  plan's 9; all green, live-refresh machinery exercised)
- `ctest -R account_management` — Passed (~74s; shared INITIALIZING_TRANSACTIONS path reaches READY
  unchanged)
- All regression binaries were REBUILT against the new genius_node library before their green runs
  (they statically link it; a stale-binary pass would have proven nothing)

## Notes for Downstream Plans

- **15-13 (processing-path gates):** consume
  `sgns::networkregistry::AuthorizeGossipSender(filter, message->from)` directly (dependency-light;
  same decision table the broadcaster's inline mirror implements). The filter instance for the
  processing paths can be rebuilt from `network_registry_` via `MakeNetworkMembershipFilter` or
  read from the broadcaster — the wiring comment at the install site records the split.
- **Teardown observation:** the broadcaster handle pattern (BroadcasterOf) is the only non-vacuous way
  to assert filter state after `tx_globaldb_->ShutdownNow()` — GetBroadcaster() through the node
  returns null post-shutdown.
- **Live refresh:** the registry cache now refreshes in-node on network-registry CRDT changes;
  membership widening reaches the enforcement point with no restart (component-level proof:
  15-11's MembershipWideningAdmitsNewPeerAtRuntime).

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 3 modified source/test files plus this SUMMARY exist on disk
- Task commits verified in git log: b85104c0 (feat), 7721deff (test)
- No file deletions in either task commit; working tree clean of task files (only the pre-existing
  `docs` gitlink modification and pre-existing untracked set remain, untouched per run instructions)
- ctest at close: private_network_registry_binding (4/4 scenes), network_config_private_network,
  network_registry_test, account_management — all PASS against rebuilt binaries

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
