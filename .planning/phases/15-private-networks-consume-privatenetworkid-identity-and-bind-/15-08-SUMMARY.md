---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "08"
subsystem: processing
tags: [privatenetwork, pnet, processingcore, libp2p, noise, connectiongater, geniusnode, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "01"
    provides: network_key_ member in GeniusNode (NetworkSettings parsing, pnet-mode StartPubSub constructor)
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "05"
    provides: the gossip host's Noise-only + pnet construction pattern mirrored here; network_registry_ member (its membership predicate consumer is descoped, see below)
  - installed vendored injector API (libp2p b28eed2 at /Users/henriqueklein/gnus/thirdparty):
    useSecurityAdaptors / di::bind<network::ConnectionGater> override / usePrivateNetwork(std::string_view)
    with eager PskValidationError; installed deny-only DenyListConnectionGater (NO SetMembershipAllowList - 15-04 skipped)
provides:
  - ProcessingCoreImpl per-subtask host composition (MakeGatedHostInjector) with the gossip
    host's Noise-only security binding set - the unauthenticated transport adaptor is never
    offered in any mode (D-11 both-paths rule, public path included)
  - pnet binding for the processing host when a network key is configured (same eager PSK
    validation path as GossipPubSub); invalid key material is caught in ProcessSubTask and
    mapped to the new ProcessingCoreImpl::Error::PNET_INITIALIZATION_ERROR (never an uncaught
    exception, never a half-configured host)
  - network_key threaded from GeniusNode::InitProcessingModules via trailing defaulted
    constructor arguments - public nodes pass nothing and keep today's construction semantics
  - processing_core_gating_test - injector gating + eager-fail + default-args regression suite
affects: [geniusnode-startup, processing-core, phase-15-gap-closure]

# Tech tracking
tech-stack:
  added: [] # no new dependencies; vendored trees untouched (owner descope)
  patterns:
    - "Two-branch injector composition (public vs pnet) cannot return one auto type: consume each branch through a shared_ptr-held template sink returning a concrete context struct - the same structure as the vendored GossipPubSub::InitHostFromInjector"
    - "Lazy host factory closure: the move-only Boost.DI injector is captured via shared_ptr inside a copyable std::function, keeping the default processing path free of per-subtask host/keypair construction while tests can still materialize the gated host from the same injector (shared-config singletons shared)"

key-files:
  created:
    - test/src/processing/processing_core_gating_test.cpp
  modified:
    - src/processing/impl/processing_core_impl.hpp
    - src/processing/impl/processing_core_impl.cpp
    - src/account/GeniusNode.cpp
    - src/processing/CMakeLists.txt
    - test/src/processing/CMakeLists.txt

key-decisions:
  - "DESCOPE (owner order, 2026-09-02, deferred-items.md 3): NO membership gater / allow-list predicate on the processing host, NO membership admission/rejection test, NO modification under 3rdparty/ or vendored trees - membership filtering is directed to the SuperGenius application layer. Everything else executed as planned (verbatim record below)."
  - "Helper shape adapted (Rule 3): the plan's `static auto MakeGatedHostInjector` with an if/else appending usePrivateNetwork cannot compile - the two branches produce distinct Boost.DI injector types. Implemented as a public static helper returning a concrete GatedHostContext {io_context, make_host} whose definition lives in the .cpp and consumes each branch through a shared_ptr-held template sink (mirrors vendored InitHostFromInjector). All three binding tokens stay greppable in the .cpp as the plan requires."
  - "The deny-list gater binding is kept (plan's binding set minus the descoped allow-list clause): a fresh DenyListConnectionGater is constructed per core and bound via di::override exactly like the gossip host's. With no deny entries it is behaviorally permissive, so the public path stays regression-free; it is not a membership enforcement point (descoped)."
  - "ProcessSubTask materializes only the io_context from the gated composition (as before) - the lazy make_host closure is destroyed unused on the default path, so no per-subtask host/keypair construction cost was introduced; public-path behavior is unchanged"
  - "Error mapping reuses the shared do/while failure tail: the catch sets Error::PNET_INITIALIZATION_ERROR and breaks, so DecProcessingSubTaskCount() + outcome::failure happen exactly once on every path"

patterns-established:
  - "libp2p injector-touching unit tests must initialize the logging system first (fixture SetUp with soralog::ConfiguratorFromYAML + libp2p::log::setLoggingSystem) - injector-created libp2p components create loggers eagerly and segfault without it"

requirements-completed: [] # D-11 / PNET-PROC stay OPEN for gap closure: Noise-only + pnet + error contract are implemented and tested, but the membership-enforcement half is descoped by owner direction (see Descope Record)

# Metrics
duration: 34min
completed: 2026-09-02
---

# Phase 15 Plan 08: Processing-Host Private-Network Gating (Owner-Descoped) Summary

**ProcessingCoreImpl's per-subtask host composition now carries the gossip host's Noise-only security set, pnet binding for configured keys with an eager caught-error contract, and GeniusNode threading via defaulted arguments — proven by a 4-case gating suite; the membership allow-list predicate is descoped by owner order (SuperGenius-side filtering direction)**

## Descope Record (VERBATIM owner order, 2026-09-02)

The owner skipped plan 15-04 (vendored ipfs-pubsub gater allow-list injection) and ordered
15-08 executed DESCOPED. Furthermore, an owner direction (2026-09-02, deferred-items.md §3)
redefined the future of membership enforcement: filtering will be implemented at the
SuperGenius application layer, NOT via libp2p gater allow-list injection. Concretely for
15-08:

> IN SCOPE: ProcessingCoreImpl's per-ProcessSubTask host getting the identical Noise-only
> security adapter set the gossip host has; pnet binding when a network key is configured
> (same PSK constructor path as GossipPubSub); invalid network key → caught error mapped to
> a ProcessingCoreImpl error (never uncaught/half-configured); public-node processing path
> byte-identical behavior via defaulted arguments; threading of construction parameters
> through from GeniusNode.
>
> OUT OF SCOPE (document as descoped gap, do NOT implement): the membership gater /
> allow-list predicate on the processing host (the "membership gater" clause of the plan's
> binding set); any test asserting membership-based admission/rejection on the processing
> host; any modification under 3rdparty/, thirdparty vendored trees, or any libp2p/ipfs-pubsub
> source.

Executed exactly to that order: no allow-list predicate exists in the deliverable
(`grep -rn SetMembershipAllowList src/ test/` = 0 — the installed vendored tree at
/Users/henriqueklein/gnus/thirdparty does not have the API at all, consistent with 15-04
being skipped), no vendored tree was modified (git status clean of any 3rdparty/thirdparty
path), and the descoped test case is documented below.

### Descoped by owner (for the phase gap-closure cycle)

| # | Descoped plan element | Why | Status |
|---|-----------------------|-----|--------|
| 1 | The "membership gater" clause of the binding set: `membership_allow_list` construction parameter, the `SetMembershipAllowList` application in the constructor, and the GeniusNode registry-backed predicate argument (must-have truth 1, third clause; T-15-26 membership half) | Owner direction 2026-09-02 (deferred-items.md §3): membership filtering moves to the SuperGenius application layer, NOT libp2p gater allow-list injection; the installed vendored gater has no allow-list API (15-04 skipped) | NOT implemented |
| 2 | Task 2 case 4 `GaterMembershipRejects` — allow-list admitting exactly one PeerId, `interceptSecured` admission/rejection assertions (T-15-26 unit-level pin) | Asserts membership-based admission/rejection on the processing host — explicitly out of scope | NOT implemented (replaced by `DefaultArgumentsKeepPublicConstruction`, an in-scope public-path regression pin) |

**Net gap for D-11 / PNET-PROC:** the processing host now never offers the unauthenticated
transport adaptor in any mode and honors the PSK boundary when a key is configured, but —
exactly as recorded for the gossip host in 15-05 — a same-PSK peer that is NOT in the
NetworkRegistry membership can still connect to it. The connection-upgrade membership
decision has no enforcement point anywhere until the SuperGenius-side filter
(deferred-items.md §3 direction) lands; gap closure must design it at
message-handling/peer-evaluation points consulting `NetworkRegistry::GetCurrentPeers()`,
respecting 15-05's fail-closed posture.

## Performance

- **Duration:** ~34 min (started 2026-09-02T20:32:08Z, completed 2026-09-02T21:06:14Z)
- **Tasks:** 2/2
- **Files modified:** 6 (1 created, 5 modified)

## Accomplishments

- **Task 1 — Gated processing-host construction + threading:**
  - `processing_core_impl.hpp`: `PNET_INITIALIZATION_ERROR` enum value (ToString
    "Private-network initialization error"); `ProcessingCoreImpl::New` and the private
    constructor gain a trailing `std::string network_key = ""` (public callers compile
    unchanged); members `network_key_` and
    `std::shared_ptr<sgns::ipfs_pubsub::DenyListConnectionGater> connection_gater_` (fresh
    deny-only gater, constructed in the constructor); public nested `GatedHostContext`
    {eager `io_context`, lazy `make_host` factory} and the public static
    `MakeGatedHostInjector(network_key, gater, kademlia_config)` declaration
  - `processing_core_impl.cpp`: `MakeGatedHostInjector` composes
    `makeHostInjector<di::extension::shared_config>(makeKademliaInjector<shared_config>(useKademliaConfig(...)),
    useSecurityAdaptors<libp2p::security::Noise>(),
    di::bind<network::ConnectionGater>().TEMPLATE_TO(gater)[di::override])` — and appends
    `usePrivateNetwork(network_key)` ONLY for non-empty keys; each branch is consumed by a
    shared_ptr-held template sink (both branch injector types are distinct and move-only).
    In `ProcessSubTask` the composition runs inside a try/catch for `std::exception`
    (`PskValidationError` arrives as one): on catch it logs via a new
    `ProcessingCoreLogger()` (message only — key material is never logged) and maps to
    `Error::PNET_INITIALIZATION_ERROR` through the shared failure tail, which decrements the
    in-flight count exactly once. Kademlia config unchanged
  - `GeniusNode.cpp` `InitProcessingModules` passes `network_key_` as the fourth argument;
    public nodes pass nothing (defaulted) and keep today's construction semantics
  - `src/processing/CMakeLists.txt`: `ipfs-pubsub` moved PRIVATE → PUBLIC on
    `processing_service` (the header now exposes `DenyListConnectionGater`)
- **Task 2 — gating tests** (`processing_core_gating_test`, 4 cases):
  1. `EmptyKeyBuildsPublicHost` — public composition builds a host with the gated binding
     set; `make_host()` materializes one shared host instance per injector (shared-config
     singleton pinned)
  2. `ValidKeyBuildsPnetHost` — the pubsub_counts SWARM_KEY_PNET literal composes the pnet
     binding and builds a host
  3. `InvalidKeyFailsEagerly` — malformed swarm key throws `std::exception` eagerly (the
     contract `ProcessSubTask` catches → `PNET_INITIALIZATION_ERROR`)
  4. `DefaultArgumentsKeepPublicConstruction` — `New(queue, 1, TokenID{})` (defaulted) and
     `New(queue, 1, TokenID{}, "")` (explicit public) both construct usable cores — the
     public-path regression pin replacing the descoped membership case
  - Fixture initializes the libp2p logging system (injector-created components log eagerly;
     without it the injector path segfaults — see Deviation 4)

## Task Commits

Each task was committed atomically:

1. **Task 1: Gated processing-host construction + threading** - `9c59d896` (feat)
2. **Task 2: processing-host gating tests** - `fad0d9ed` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/processing/impl/processing_core_impl.hpp` - PNET_INITIALIZATION_ERROR; defaulted network_key params (New + ctor); GatedHostContext + MakeGatedHostInjector declaration; network_key_/connection_gater_ members
- `src/processing/impl/processing_core_impl.cpp` - gated injector composition (Noise-only + gater + conditional pnet); template sink; try/catch error mapping in ProcessSubTask; logger
- `src/account/GeniusNode.cpp` - InitProcessingModules passes network_key_ to ProcessingCoreImpl::New
- `src/processing/CMakeLists.txt` - ipfs-pubsub PUBLIC on processing_service
- `test/src/processing/processing_core_gating_test.cpp` - 4-case gating suite (210 lines)
- `test/src/processing/CMakeLists.txt` - processing_core_gating_test target

## Verification Results

- `ninja processing_service genius_node` - builds clean (1 pre-existing switch warning in GeniusNode.cpp:4710, unrelated to this plan's edit region)
- `ctest -R processing_core_gating` - **1/1 PASS** (4/4 gtest cases, 0.65s)
- `ctest -R processing` - **5/6 PASS**; the single failure `processing_nodes_test` is the documented pre-existing no-genesis READY mechanism (deferred-items.md §1): its node log shows `TrustedPeerRegistry construction failed (majority-floor violation)` — a path untouched by this plan (this plan's composition runs per-ProcessSubTask, never during node startup). Failure mode identical to the base family
- `ctest -R "private_network_registry_binding|account_management"` - **2/2 PASS** (~93s) — full GeniusNode startup incl. pnet-mode nodes and InitProcessingModules is regression-free
- Grep gates: `grep -c Plaintext src/processing/impl/processing_core_impl.cpp` = **0**; `useSecurityAdaptors` = 2 (both branches), `ConnectionGater` bind = 4, `usePrivateNetwork` = 2 (pnet-only branch + doc) — all three bindings present in the .cpp as required; `PNET_INITIALIZATION_ERROR` present in hpp (1) and cpp (2)
- Descope greps: `grep -rn SetMembershipAllowList src/ test/` = **0**; no file under any 3rdparty/thirdparty path modified (git status clean)

## Deviations from Plan

### Descoped by Owner (see VERBATIM record above)

**1. [Descoped - owner order 2026-09-02, deferred-items.md §3] Membership gater + membership tests omitted**
- The plan's `membership_allow_list` construction parameter, the `SetMembershipAllowList`
  constructor application, the GeniusNode registry-backed predicate argument (Task 1), and
  the `GaterMembershipRejects` test case (Task 2, case 4) are not implemented. The deny-list
  gater binding itself IS implemented (it is not a membership enforcement point). No vendored
  tree was modified.

### Plan-Snippet Adaptations (mandated by language/descope)

**2. [Rule 3 - Blocking] `MakeGatedHostInjector` return shape adapted from `static auto`**
- **Found during:** Task 1 implementation
- **Issue:** The plan's helper shape (`static auto MakeGatedHostInjector(...)` with "only
  when network_key is non-empty, appends usePrivateNetwork") cannot compile — the public and
  pnet compositions produce two distinct, move-only Boost.DI injector types, so a single
  `auto`-returning function cannot return both. The plan also wanted the helper test-callable
  from another TU while keeping the bindings in the .cpp, which an inline header `auto`
  definition would violate
- **Fix:** Public static helper returning a concrete `GatedHostContext` (eager io_context +
  lazy `std::function` host factory), defined in the .cpp; each branch is consumed through a
  shared_ptr-held template sink — the same structure as the vendored
  `GossipPubSub::InitHostFromInjector`. Eager-throw, gating, threading, and all grep-gate
  contracts are unchanged
- **Files modified:** src/processing/impl/processing_core_impl.{hpp,cpp}
- **Committed in:** 9c59d896

**3. [Rule 3 - Blocking] processing_service did not propagate the ipfs-pubsub include path**
- **Found during:** Task 1 build
- **Fix:** Moved `ipfs-pubsub` from PRIVATE to PUBLIC in src/processing/CMakeLists.txt —
  processing_core_impl.hpp now exposes `DenyListConnectionGater` to consumers
- **Committed in:** 9c59d896

**4. [Rule 1 - Bug] Gating test segfaulted without a libp2p logging system**
- **Found during:** Task 2 first run (EXC_BAD_ACCESS in `libp2p::log::createLogger` —
  injector-created libp2p components create loggers eagerly and `getLoggingSystem()` was null)
- **Fix:** Test fixture `SetUp` initializes soralog via `ConfiguratorFromYAML` +
  `libp2p::log::setLoggingSystem` (the pubsub_counts pattern)
- **Files modified:** test/src/processing/processing_core_gating_test.cpp
- **Committed in:** fad0d9ed

---

**Total deviations:** 1 owner descope + 3 adaptations/fixes
**Impact on plan:** All IN-SCOPE truths hold and are tested; the enforcement gap is exactly
the descoped membership decision (documented above and in 15-05 for the gossip host, now
unified under the SuperGenius-side filtering direction for gap closure).

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: elevation-of-privilege-gap | src/processing/impl/processing_core_impl.cpp | T-15-26's membership half is NOT mitigated — descoped with the allow-list clause. A same-PSK peer not in NetworkRegistry membership can still connect to the processing host (and the gossip host, per 15-05). Mitigated: Noise-only security everywhere (T-15-25 — 0 Plaintext occurrences, grep-gated) and the pnet PSK boundary on both dial and accept paths for configured keys; eager-fail error contract (T-15-27) implemented and test-proven |

## Known Stubs

- `connection_gater_` is a fresh deny-only `DenyListConnectionGater` with no entries and no
  membership predicate: it is bound into every per-subtask composition (structurally
  identical to the gossip host's gater) but is behaviorally permissive today. This is the
  owner-descoped design (deferred-items.md §3 — SuperGenius-side filtering), not an
  accidental stub; the gap-closure plan should either remove it or wire the
  application-layer filter's deny decisions into it.
- `GatedHostContext::make_host` is unused by `ProcessSubTask` by design: the default
  processing path materializes only the io_context from the composition (exactly as before
  D-11), so no per-subtask host or keypair construction cost was introduced. The closure
  exists so the gated composition is observable (tests/tools); if SGProcessingManager ever
  needs a real per-subtask host, it materializes from the same gated injector.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The standing membership gap is now uniform across both hosts (gossip 15-05 + processing
  15-08): gap closure must implement SuperGenius-side membership filtering
  (deferred-items.md §3) at message-handling/peer-evaluation points consulting
  `NetworkRegistry::GetCurrentPeers()`, against the two-node fixtures in
  test/src/account/private_network_registry_binding_test.cpp and
  test/src/pubsub_counts/pubsub_counts.cpp, respecting 15-05's fail-closed posture
- `processing_core_gating_test` initializes the libp2p logging system in its fixture — copy
  that pattern for any future test that materializes libp2p components directly
- Pre-existing failures untouched: `processing_nodes_test` (no-genesis READY wait,
  deferred-items.md §1) — log-attributed to the TPR majority-floor mechanism during this
  plan's verification

## Self-Check: PASSED

- All 6 created/modified files exist on disk (verified 2026-09-02T21:06Z)
- Task commits verified in git log: 9c59d896 (feat), fad0d9ed (test)
- Working tree clean (only this SUMMARY remained for the docs commit)
- Grep gates at close: Plaintext=0, useSecurityAdaptors=2, ConnectionGater=4, usePrivateNetwork=2,
  PNET_INITIALIZATION_ERROR hpp=1, SetMembershipAllowList in src/+test/ = 0
- ctest at close: processing_core_gating 1/1, processing 5/6 (1 documented pre-existing),
  private_network_registry_binding + account_management 2/2

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-02*
