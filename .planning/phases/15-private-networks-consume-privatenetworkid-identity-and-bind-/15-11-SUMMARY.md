---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "11"
subsystem: auth
tags: [networkregistry, membershipfilter, gossip, pubsubbroadcaster, ingestgate, pnet, failclosed, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "03"
    provides: sgns::networkregistry::NetworkRegistry (D-06), GetCurrentPeers() cached PeerId membership
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "09"
    provides: hardened NetworkRegistry lifecycle (WR-02/03/04) the filter integrates with
provides:
  - VERIFICATION gaps 1/2/4 (enforcement-at-upgrade cluster) closed at the COMPONENT level: application-layer gossip membership enforcement per deferred-items.md §3 (owner-directed replacement for the skipped 15-04 libp2p gater)
  - sgns::networkregistry::MembershipFilter type + MakeNetworkMembershipFilter (weak_ptr<NetworkRegistry>-backed, fail-closed) + AuthorizeGossipSender (from-field helper for 15-13 processing-path gates)
  - PubSubBroadcasterExt::SetMembershipFilter/HasMembershipFilter/ClearMembershipFilter + OnMessage dual-identity ingest gate (declared protobuf peer AND transport from-field; empty/malformed from DENIED under a set filter)
affects: [15-12-node-wiring, 15-13-processing-path-gates, crdt_globaldb, networkregistry]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Inline-mirror instead of cross-layer call: PubSubBroadcasterExt::OnMessage reproduces AuthorizeGossipSender line-for-line because the layering rule forbids the crdt .cpp from including networkregistry headers (only NetworkMembershipFilter.hpp couples the two layers); semantic parity pinned by unit case 4"
    - "Filter snapshot under mutex per message: OnMessage copies the std::function out of membership_filter_mutex_ then evaluates unlocked — setters run on node init/teardown threads, evaluation on pubsub callback threads"
    - "Test-owned mutable membership set (mutex-guarded set shared with the filter lambda) to prove per-message consultation: runtime widening admits a live peer's subsequent messages with no filter reinstall"

key-files:
  created:
    - src/networkregistry/NetworkMembershipFilter.hpp
    - test/src/networkregistry/network_membership_filter_test.cpp
  modified:
    - src/crdt/globaldb/pubsub_broadcaster_ext.hpp
    - src/crdt/globaldb/pubsub_broadcaster_ext.cpp
    - test/src/networkregistry/CMakeLists.txt

key-decisions:
  - "Gate placement: immediately after the existing peerId derivation and BEFORE DecodeBroadcast/route/queue — the deny path costs one set lookup per message with no CID decode or datastore access (T-15-11-03), and the declared-peer check reuses the already-derived peerId"
  - "The from-field check is UNCONDITIONAL on emptiness (checker round 3): an empty ByteArray fails PeerId::fromBytes and takes the deny branch — no 'if from is non-empty' skip exists; parity with 15-13's processing-path gates"
  - "AuthorizeGossipSender is dependency-light (libp2p peer types only) so 15-13's processing handlers can consume it without dragging NetworkRegistry.hpp into non-registry translation units"
  - "Expiry fail-closure is proven with an UNREGISTERED (public-ctor) registry: New-constructed registries are deliberately pinned by SecureCrdt::RegisterFilters' element-filter lambda capturing the D-04 entry (strong peer_registry) by value, so Unregister()+reset cannot expire their weak_ptr while the datastore lives (documented phase-13 shared-ownership semantics, not a bug)"

patterns-established:
  - "Application-layer membership gate at the single gossip→CRDT ingest chokepoint (PubSubBroadcasterExt::OnMessage is the only GossipPubSub subscriber in the crdt layer): one gate covers chain elements, network-registry updates, task/result CRDT entries, and the SecureCrdt topic"
  - "Dual-identity inbound check: self-declared protobuf peer id AND transport gossip from-field must both pass — spoofing either one is insufficient"

requirements-completed: [D-03, D-07, PNET-GATE]

# Metrics
duration: 28min
completed: 2026-09-03
---

# Phase 15 Plan 11: Gossip Membership Filter Summary

**Fail-closed application-layer membership gate at the single gossip→CRDT ingest chokepoint (PubSubBroadcasterExt::OnMessage): an unauthorized same-PSK peer can mesh at the transport layer but its writes never enter any member's replicated state — proven by unit semantics tests plus three-node same-PSK flow tests with runtime-widening and empty-membership scenes**

## Performance

- **Duration:** ~28 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T11:53:54Z
- **Completed:** 2026-09-03T12:21:26Z
- **Tasks:** 2/2
- **Files modified:** 5 (2 created, 3 modified)

## Accomplishments

- **Filter component** (`src/networkregistry/NetworkMembershipFilter.hpp`): `MembershipFilter` predicate type; `MakeNetworkMembershipFilter(weak_ptr<NetworkRegistry>)` — expired registry denies, empty `GetCurrentPeers()` denies (15-05 fail-closed posture), else per-message base58 membership test over a copy taken under the call (registry stays lock-free on this path); `AuthorizeGossipSender(filter, from_bytes)` — no filter → pass-through, `PeerId::fromBytes` failure (INCLUDING the empty span) → deny, else the filter verdict. Header doc block records the D-07 replacement decision (deferred-items.md §3).
- **Broadcaster enforcement** (`pubsub_broadcaster_ext.hpp/.cpp`): public `SetMembershipFilter`/`HasMembershipFilter`/`ClearMembershipFilter` with mutex-guarded storage (OnMessage runs on pubsub callback threads, setters on node init/teardown). `OnMessage` snapshots the filter under the mutex, then — before any CID decode, route, or queueing — drops (break, mirroring the blacklist drop) when the declared protobuf peer OR the transport `from` peer fails the predicate; an empty/malformed `from` fails `PeerId::fromBytes` and is denied (no emptiness skip branch). The transport check is a line-for-line inline MIRROR of `AuthorizeGossipSender`, not a call — the layering rule keeps this .cpp free of networkregistry includes (verified: zero networkregistry includes; only doc-comment mentions). No filter installed → byte-identical public pass-through.
- **7-case test suite** (`network_membership_filter_test`, registered in test/src/networkregistry/CMakeLists.txt over the NETWORKREGISTRY_TEST_NODE_LIBS link set):
  - 4 unit cases on the network_registry_test fixture shape (real single-node SecureCrdt + 3-peer TPR): member allow, fresh-Ed25519 non-member deny, expired-registry deny-all, and the full `AuthorizeGossipSender` decision table including filter-set + EMPTY from bytes → false.
  - 3 flow cases over real pnet GossipPubSub nodes carrying real GlobalDBs (pubsub_graphsync replication wiring + pubsub_counts pnet ctor/PSK sentinels): (5) member's write replicates to the filtered node while a connected, same-PSK, continuously-rebroadcasting intruder's write NEVER lands, plus a wrong-PSK transport control; (6) one live filter instance denies a peer's messages before a runtime membership widening and admits that same peer's subsequent message after (per-message consultation); (7) an empty membership set denies a fully-connected same-PSK member (never fails open).
- **Mutation-verified non-vacuity:** temporarily admitting the intruder to flow-5's membership makes the negative-window assertion FAIL ("intruder write replicated although it must be denied") — the intruder's gossip genuinely reaches the filtered node and only the membership gate stops it. Restored; suite green again.

## Task Commits

Each task was committed atomically:

1. **Task 1: MembershipFilter component + broadcaster ingest enforcement** - `44142a6b` (feat)
2. **Task 2: Filter-semantics unit tests + three-node same-PSK flow proofs** - `254eabbb` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/networkregistry/NetworkMembershipFilter.hpp` - MembershipFilter, MakeNetworkMembershipFilter (fail-closed registry-backed predicate), AuthorizeGossipSender (from-field helper); no crdt/globaldb include
- `src/crdt/globaldb/pubsub_broadcaster_ext.hpp` - Set/Has/ClearMembershipFilter API, membership_filter_mutex_ + membership_filter_ members
- `src/crdt/globaldb/pubsub_broadcaster_ext.cpp` - OnMessage dual-identity gate before DecodeBroadcast; filter setter implementations; zero networkregistry includes
- `test/src/networkregistry/network_membership_filter_test.cpp` - 4 unit + 3 flow cases; one sleep_for, inside the grace-window negative assertion loop only
- `test/src/networkregistry/CMakeLists.txt` - network_membership_filter_test registration

## Decisions Made

- See key-decisions. Gate placement (before DecodeBroadcast), the unconditional empty-from denial, the inline-mirror layering compromise, and the direct-ctor expiry fixture were the four substantive interpretation calls; all four are pinned by tests or acceptance-grepped source facts.
- Verified before relying on them: `PeerId::fromBytes` fails on an empty span (it routes through `Multihash::createFromBytes`, which needs a valid header — the fail-closed assumption the whole empty-from denial rests on); gossip `TopicMessage.from` is set to the publisher's host id at publish time (`GossipCore::publish` constructs it from `local_peer_id_`), so production-configured members always present a parseable transport sender; the 100ms CRDT rebroadcast loop means a denied writer keeps retrying throughout any negative window (the window observes repeated denials, not a one-shot miss).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] kInitialPeers string literals are not parseable PeerIds**
- **Found during:** Task 2 (first run: "Unknown C++ exception thrown in the test fixture's constructor")
- **Issue:** The plan says to construct the registry membership from "a syntactically valid PeerId string such as the kInitialPeers entries at network_registry_test.cpp:55-58" — but `PeerId::fromBase58` on those literals THROWS (they are opaque string fixtures in network_registry_test, only ever consumed as strings, never parsed). The unit fixture could not construct the member PeerId object the plan's cases require.
- **Fix:** The member identity is derived from a freshly generated Ed25519 key (`GenerateFreshPeerId` — sha256 multihash of the public key, the libp2p derivation), and the registry is constructed with that peer's own base58 string; the literals remain in use only as opaque non-member set entries (flow case 6's "unrelated id").
- **Files modified:** test/src/networkregistry/network_membership_filter_test.cpp
- **Verification:** All 4 unit cases pass; member allow / non-member deny both proven against a fully valid identity
- **Committed in:** 254eabbb

**2. [Rule 3 - Blocking] The plan's expiry sequence (Unregister + reset) cannot expire the weak_ptr of a New-constructed registry**
- **Found during:** Task 2 (ExpiredRegistryDeniesAll failed: filter still returned true after Unregister + reset)
- **Issue:** `SecureCrdt::RegisterFilters`' element-filter lambda captures the D-04 registry entry BY VALUE, and that entry's `peer_registry` is a STRONG shared_ptr (documented phase-13 shared-ownership precedent). The lambda lives in the datastore's CRDTDataFilter, so a New-constructed NetworkRegistry stays pinned — and functional — until the datastore dies. The plan's assumption that resetting the local shared_ptr destroys the registry is false for the registered shape.
- **Fix:** The expiry semantics are proven with an UNREGISTERED registry built through the public constructor (no SecureCrdtRegistry entry → no element-filter pinning): authorized while alive in a nested scope, denied after scope exit, with secure_crdt_/node_ still alive exactly as the plan requires. The New-constructed live path is separately covered by cases 1/2/4. The pinning itself is deliberate phase-13 semantics, not a bug to fix here.
- **Files modified:** test/src/networkregistry/network_membership_filter_test.cpp
- **Verification:** ExpiredRegistryDeniesAll passes deterministically
- **Committed in:** 254eabbb

---

**Total deviations:** 2 (both blocking test-path realities; both keep the plan's required observables intact)
**Impact on plan:** All must_have truths and artifacts hold. No scope creep; no new dependencies; nothing under 3rdparty/ or thirdparty/ touched; no SetMembershipAllowList surface anywhere.

## Verification Evidence

- `ninja -C build/OSX/Release crdt_globaldb networkregistry` — clean
- Task 1 source assertions: `SetMembershipFilter` x2 in the .hpp, `membership_filter_` x8 in the .cpp; ZERO networkregistry includes in pubsub_broadcaster_ext.cpp/.hpp (only doc-comment mentions of the layering rule); NetworkMembershipFilter.hpp has zero crdt/globaldb includes (doc comment only)
- `ctest -R network_membership_filter` — Passed (binary: 7/7 cases, ~24s)
- Regression: `ctest -R "network_registry_test|pubsub_counts|pubsub_graphsync"` — 3/3 passed (broadcaster change is pass-through without a filter; GlobalDB replication unchanged)
- Mutation check (non-vacuity of the flow-5 negative): with the intruder added to the membership, the test FAILS at "intruder write replicated although it must be denied" — the intruder's gossip reaches the filtered node and only the filter denies it. Restored byte-exact from the Task 2 commit; re-verified green.
- `grep -c sleep_for` in the test file = 1, inside `AssertKeyNeverPresentWithin`'s bounded negative-window loop (the sanctioned grace-window pattern)

## Notes for Downstream Plans

- **15-12 (node wiring):** install the filter with `broadcaster->SetMembershipFilter(sgns::networkregistry::MakeNetworkMembershipFilter(std::weak_ptr(registry)))` on private nodes only; public nodes must keep NO filter (byte-identical behavior is asserted by pubsub_counts/pubsub_graphsync staying green). The fail-closed default (no filter on a private node) is a 15-12 wiring guarantee per T-15-11-04.
- **15-13 (processing-path gates):** consume `sgns::networkregistry::AuthorizeGossipSender(filter, message->from)` directly — it is dependency-light and pins the same decision table (empty/malformed from → deny) the broadcaster's inline mirror implements.
- **Pinning caveat:** a NetworkRegistry stays alive while its GlobalDB lives even after Unregister() (RegisterFilters' D-04 entry capture). Teardown ordering that assumes registry destruction on reset will not observe destruction; use the datastore's lifetime as the boundary.
- Component-level halves of VERIFICATION gaps 1/2/4 are closed; the node-wiring halves (filter actually installed in the production private-node path) belong to 15-12, and the processing-path sender checks to 15-13.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 5 created/modified source + test files exist on disk
- Commits verified in git log: 44142a6b (feat), 254eabbb (test)
- No file deletions in either task commit; working tree clean of task files (only the pre-existing `docs` gitlink modification and pre-existing untracked set remain, untouched per run instructions)
- `ctest -R network_membership_filter`: 7/7; regression guard suites: 3/3

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
