---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "05"
subsystem: auth
tags: [privatenetwork, networkregistry, geniusnode, pnet, quorum, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "01"
    provides: private_network_id / network_bootstrap_peers config identity (NetworkSettings, GeniusNode members, WriteNetworkConfig)
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "03"
    provides: sgns::networkregistry::NetworkRegistry (5-arg New), GetCurrentPeers, Unregister,
      securecrdt::StrictMajorityQuorumFloor / ValidateQuorumThreshold
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "02"
    provides: sgns::peerregistry::PeerRegistry contract + SecureCrdtRegistryEntry::peer_registry association (D-04)
  - pre-existing commit 0515def3: network_key parsing + pnet-mode StartPubSub constructor (unchanged by this plan)
provides:
  - GeniusNode constructs sgns::networkregistry::NetworkRegistry in the INITIALIZING_TRANSACTIONS
    quorum-trio path when private_network_id is provisioned (after TrustedPeerRegistry/BurnConfig),
    wired as the network's PeerRegistry under "network-registry/<id>" with the offline-provisioned
    network_bootstrap_peers as cached membership (D-06 consumption side)
  - Fail-closed private-network startup: a private node whose NetworkRegistry cannot be constructed
    (e.g. empty bootstrap membership below the strict-majority floor) fails the state transition and
    never reaches READY (startup-level enforcement replacing the descoped gater-level enforcement)
  - GeniusNodeTestAccess::NetworkRegistry test accessor
  - private_network_registry_binding_test — 3-scene integration suite (construct + membership,
    public-node-none, fail-closed) over genesis-configured GeniusNodes with LOCAL single-peer
    self-genesis confirmation (works for pnet-mode nodes, where an outside genesis tool cannot
    pass the PSK boundary)
affects: [15-07-account-lifecycle, 15-08-closeout, geniusnode-startup, networkregistry]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies (vendored allow-list gater deliberately NOT taken - descoped)
  patterns:
    - "Local single-peer self-genesis: GeniusNodeTestAccess::ApproveConfiguredTrustGenesis writes the node's own threshold-1 approval into its own SecureCrdt; the controller's trusted-peer-genesis candidate callback triggers the refresh that activates it - no pubsub peer needed, so pnet-mode nodes are testable end-to-end"
    - "Fail-closed registry wiring: the construction guard (private_network_id non-empty) plus NetworkRegistry::New's own floor checks turn an under-provisioned private network (empty network_bootstrap_peers) into a startup failure instead of a silently unenforced network"

key-files:
  created:
    - test/src/account/private_network_registry_binding_test.cpp
  modified:
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - src/account/CMakeLists.txt
    - test/src/account/CMakeLists.txt
    - test/testutil/genius_node_test_access.hpp

key-decisions:
  - "DESCOPE (owner order, 2026-09-02): the GossipPubSub membership allow-list binding is NOT implemented - 15-04 (vendored gater allow-list injection) was skipped, so SetMembershipAllowList does not exist in the tree; everything else (registry construction, PeerRegistry wiring, pnet host binding, fail-closed, public-node unchanged, deny-only-gater-provable isolation) executed as planned. Descoped acceptance criteria listed verbatim below for the gap-closure cycle."
  - "Fail-closed moved from gater-level to startup-level: the descoped plan's 'gater fails closed on empty membership' becomes 'NetworkRegistry::New rejects the empty set below the strict-majority floor and the INITIALIZING_TRANSACTIONS transition fails' - a stronger, still test-proven guarantee (empty-membership node stalls in INITIALIZING_TRANSACTIONS, never READY; error verified in node log)"
  - "Failure path mirrors the adjacent BurnConfig failure path (error log + ShutdownNodePolicyServices + return), not TPR's bare secure_crdt_.reset(): by the construction site TPR/BurnConfig are already live and registered, so they must be torn down; the node stays stalled in INITIALIZING_TRANSACTIONS (fail-closed, same observable as the TPR path)"
  - "Single construction site after the trust if/else covers both the TrustStartupController path (constructed on the post-genesis re-entry of INITIALIZING_TRANSACTIONS) and the direct TPR/BurnConfig branch; guarded by !network_registry_ so state re-entries (account switch) never double-register (Register would fail file_exists)"
  - "Plan-literal 5-arg NetworkRegistry::New call (global_db defaulted to null): the live CRDT-change cache refresh is not wired because its runtime consumer was the descoped allow-list; a later plan can pass tx_globaldb_ when that consumer lands"
  - "Construction threshold = securecrdt::StrictMajorityQuorumFloor(network_bootstrap_peers_.size()) (the plan's (N*51+99)/100) - passes NetworkRegistry::New's own floor validation for any provisioned membership size >= 1"

patterns-established:
  - "pnet-mode nodes are integration-testable without weakening the PSK boundary: confirm the trust genesis locally instead of dialing an outside genesis tool (the tool's GossipPubSub is public-mode and cannot pass the boundary)"

requirements-completed: [D-06, PNET-NETREG] # D-07 / PNET-GATE remain open - allow-list half descoped by owner

# Metrics
duration: 129min
completed: 2026-09-02
---

# Phase 15 Plan 05: Private-Network Membership Binding (Owner-Descoped) Summary

**GeniusNode constructs the per-privateNetworkId NetworkRegistry in the INITIALIZING_TRANSACTIONS quorum-trio path with fail-closed empty-membership semantics and unchanged public-node behavior — proven by a 3-scene genesis-configured integration suite; the GossipPubSub allow-list binding half was descoped by owner order (15-04 skipped)**

## Descope Record (VERBATIM owner order, 2026-09-02)

The owner skipped plan 15-04 (vendored ipfs-pubsub gater allow-list injection: "forget about
3rdparty directory") and ordered 15-05 executed DESCOPED:

> IN SCOPE: GeniusNode constructing the NetworkRegistry in the INITIALIZING_TRANSACTIONS
> quorum-trio path when private_network_id is configured (after TrustedPeerRegistry); wiring
> NetworkRegistry as the network's PeerRegistry; pnet host binding from the configured
> network_key; fail-closed semantics where applicable; public-node behavior unchanged (no
> NetworkRegistry constructed); two-node PSK isolation behavior that IS provable with the
> current deny-only gater.
>
> OUT OF SCOPE (document as descoped gap, do NOT implement): passing NetworkRegistry cached
> membership as the GossipPubSub allow-list; any test asserting allow-list admission/rejection
> at connection upgrade; any modification under 3rdparty/ or thirdparty vendored trees.

Executed exactly to that order: no `SetMembershipAllowList` call exists in the tree (grep-verified),
no vendored tree was touched, and the two-node PSK isolation coverage that survives the deny-only
gater (same-PSK mesh + message exchange, wrong-PSK rejection, gater deny-list) is the unchanged
`PnetIsolationAndGaterBlocking` in pubsub_counts (re-run green).

### Descoped by owner (for the phase gap-closure cycle)

| # | Descoped acceptance criterion (plan text) | Why | Status |
|---|-------------------------------------------|-----|--------|
| 1 | `SetMembershipAllowList` called in StartPubSub private branch with a predicate over `NetworkRegistry::GetCurrentPeers()` (must-have truth 1, second half; key_links gossip binding; T-15-18 grep gate on that symbol) | 15-04 (vendored gater + GossipPubSub surface) skipped by owner - the API does not exist; implementing it requires 3rdparty/ work explicitly excluded | NOT implemented |
| 2 | Task 2 `PrivateNetworkMembershipGating` test: same-PSK-not-in-allow-list peer rejected at connection upgrade (must-have truth 3; T-15-15) | Allow-list admission/rejection at connection upgrade is unprovable with the current deny-only gater | NOT implemented (PnetIsolationAndGaterBlocking still covers wrong-PSK + deny-list rejection) |
| 3 | Task 2 Negative 2: gater fails closed on empty membership at connection time (must-have truth 4 as written; T-15-16 at the gater) | Same gater limitation | REPLACED by startup-level fail-closed (tested: empty-membership private node never reaches READY, stalls in INITIALIZING_TRANSACTIONS) |
| 4 | Task 2 runtime-admission step: peer connects only after the allow-list widens live | Live registry-state consultation happens at connection upgrade - descoped with the binding | NOT implemented |

**Net gap for D-07 / PNET-GATE:** possession of the PSK is enforced (pnet, both dial and accept),
and a private node now refuses to start without a constructible membership authority, but a
same-PSK peer that is NOT in the NetworkRegistry membership can still mesh — the
connection-upgrade allow-list decision has no enforcement point until 15-04 (or equivalent
first-party surface) lands.

## Performance

- **Duration:** ~129 min (started 2026-09-02T14:56:19Z, completed 2026-09-02T17:05:09Z) — includes fresh-worktree submodule init, cmake configure, and the full genius_node dependency-cone build
- **Tasks:** 2/2
- **Files modified:** 6 (1 created, 5 modified)

## Accomplishments

- **Task 1 — GeniusNode NetworkRegistry construction + teardown (descoped to wiring):**
  - `GeniusNode.hpp`: forward-declares `sgns::networkregistry::NetworkRegistry`; member
    `network_registry_` declared immediately after `trusted_peer_registry_` (joins the
    destructor-ordering chain — ~NetworkRegistry calls Unregister() which needs SecureCrdt,
    destroyed last)
  - `GeniusNode.cpp` INITIALIZING_TRANSACTIONS: after the trust if/else (i.e. after the
    TrustedPeerRegistry is live in both the TrustStartupController and direct branches),
    constructs `NetworkRegistry::New(secure_crdt_, trusted_peer_registry_, private_network_id_,
    network_bootstrap_peers_, StrictMajorityQuorumFloor(N))` when `private_network_id_` is
    provisioned; on failure logs (public id + membership size only, never `network_key_`) and
    fails the state transition via `ShutdownNodePolicyServices()` + return
  - `ShutdownNodePolicyServices`: unregisters + resets `network_registry_` before the
    trusted-peer owners (it retains SecureCrdt/TPR references)
  - `src/account/CMakeLists.txt`: `networkregistry` added to the PUBLIC `GENIUS_NODE_LIBS` link set
  - Public nodes construct nothing (guard on `private_network_id_.empty()`); the StartPubSub
    public branch is byte-identical (committed diff touches only includes, the construction
    insert, and the teardown function)
- **Task 2 — integration tests (re-targeted per descope, see Deviations 2):**
  `private_network_registry_binding_test` — 3 scenes over genesis-configured GeniusNodes
  (single-peer self-genesis, thresholds 1/1, confirmed LOCALLY via
  `GeniusNodeTestAccess::ApproveConfiguredTrustGenesis` so pnet-mode nodes need no outside peer):
  1. private node with provisioned membership reaches READY with a NetworkRegistry whose cached
     `GetCurrentPeers()` equals the provisioned `network_bootstrap_peers` (D-06)
  2. public node reaches READY with NO NetworkRegistry (public path unchanged)
  3. fail-closed: private node with EMPTY membership confirms its genesis but stalls in
     INITIALIZING_TRANSACTIONS, never READY, no registry — the exact fail-closed error line
     verified in the node's log file
- Two-node PSK isolation with the deny-only gater: unchanged
  `PubsubCounts.PnetIsolationAndGaterBlocking` re-run green (same-PSK mesh + counted message
  exchange, wrong-PSK never connects, deny-list never connects)

## Task Commits

Each task was committed atomically:

1. **Task 1: GeniusNode NetworkRegistry construction + teardown** - `8064116b` (feat)
2. **Task 2: wiring scenes — construct, public-none, fail-closed** - `45e7ebed` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/account/GeniusNode.hpp` - forward decl + `network_registry_` member after `trusted_peer_registry_`
- `src/account/GeniusNode.cpp` - includes; fail-closed NetworkRegistry construction in INITIALIZING_TRANSACTIONS; ShutdownNodePolicyServices unregister/reset ordering
- `src/account/CMakeLists.txt` - networkregistry linked into genius_node / genius_node_test (PUBLIC)
- `test/src/account/private_network_registry_binding_test.cpp` - 3-scene binding suite with local self-genesis helper
- `test/src/account/CMakeLists.txt` - private_network_registry_binding_test target (genius_node + json_secure_storage + networkregistry, force_load genius_node)
- `test/testutil/genius_node_test_access.hpp` - NetworkRegistry test accessor (forward-declared return type)

## Verification Results

- `ninja -C build/OSX/Release genius_node` - builds clean (1 pre-existing-style switch warning elsewhere in the file)
- `ctest -R "trustedpeerregistry|securecrdt|burnconfig|network_registry"` - **14/14 PASS** (~71s)
- `ctest -R pubsub_counts` - **PASS** incl. PnetIsolationAndGaterBlocking (9.5s)
- `ctest -R network_config_private_network` - **5/5 PASS** (15-01 suite intact)
- `ctest -R private_network_registry_binding` - **3/3 PASS** (~19s)
- `ctest -R account_management` - **PASS** (shared INITIALIZING_TRANSACTIONS/teardown path, reaches READY, ~73s)
- Grep gates: `grep -c "network_registry_" src/account/GeniusNode.cpp` = 9 (>= 4); `NetworkRegistry::New` present; `SetMembershipAllowList` absent (descoped); `sleep_for` count in pubsub_counts.cpp unchanged (file untouched)
- Fail-closed log verified: `[error][SuperGeniusNode] NetworkRegistry construction failed for private network 0x3c... with 0 bootstrap peers (quorum floor 0): ... - private-network membership is not provisioned; failing closed` (public id + size only, no key material — T-15-17)

## Deviations from Plan

### Descoped by Owner (see VERBATIM record above)

**1. [Descoped - owner order 2026-09-02] GossipPubSub allow-list binding + allow-list connection tests omitted**
- The plan's Task 1 `SetMembershipAllowList` call, Task 2 `PrivateNetworkMembershipGating` test, and must-have truths 1 (second half)/3 are not implemented. Verified `SetMembershipAllowList` exists nowhere in the tree (15-04 skipped). No vendored tree modified.

### Plan-Snippet Adaptations (mandated by base drift + descope)

**2. [Rule 3 - Blocking] Task 2 re-targeted from pubsub membership-gating to GeniusNode wiring**
- **Found during:** Task 2 planning (no allow-list API exists; the deny-only gater cannot express admission)
- **Issue:** The plan's Task 2 asserts allow-list admission/rejection at connection upgrade — descoped and unimplementable. The plan's own test file (pubsub_counts.cpp) has nothing left to add: its existing PnetIsolationAndGaterBlocking already proves exactly the in-scope isolation behavior (same-PSK mesh/exchange, wrong-PSK and deny-list rejection)
- **Fix:** Coverage moved to the NEW in-scope behavior: the GeniusNode wiring (construct/public-none/fail-closed), as `private_network_registry_binding_test`. pubsub_counts.cpp is untouched and green
- **Files modified:** test/src/account/private_network_registry_binding_test.cpp (new), test/src/account/CMakeLists.txt, test/testutil/genius_node_test_access.hpp
- **Verification:** 3/3 pass; pubsub_counts green
- **Committed in:** 45e7ebed

**3. Line anchors and failure-path mirror adapted to the post-closeout GeniusNode**
- **Found during:** Task 1 (plan snippets reference pre-closeout shapes)
- **Issue:** INITIALIZING_TRANSACTIONS is at 808-1038 (not 735-767) with the TrustStartupController if/else; the "~GeniusNode teardown region 1780-1800" is now `ShutdownNodePolicyServices()` (2204+); the plan's `network_registry_->Unregister()` teardown mirror belongs there
- **Fix:** Single construction site after the trust if/else; teardown in ShutdownNodePolicyServices; on New failure the BurnConfig-adjacent path (ShutdownNodePolicyServices + return) is used since TPR/BurnConfig are live by then (TPR's own failure path predates their existence)
- **Files modified:** src/account/GeniusNode.cpp

**4. [Rule 3 - Blocking] genius_node did not link networkregistry**
- **Found during:** Task 1 build
- **Fix:** Added `networkregistry` to PUBLIC GENIUS_NODE_LIBS (src/account/CMakeLists.txt) — same pattern as trustedpeer/burnconfig
- **Committed in:** 8064116b

---

**Total deviations:** 1 owner descope + 3 adaptations/blocking fixes
**Impact on plan:** In-scope truths hold and are tested; the enforcement gap is exactly the descoped allow-list (documented above for gap closure).

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: elevation-of-privilege-gap | src/account/GeniusNode.cpp | T-15-15 (same-PSK unauthorized peer rejected at connection upgrade) is NOT mitigated — descoped with the allow-list binding. A correct-PSK peer not in NetworkRegistry membership can still mesh. Startup-level fail-closed (T-15-16 analog) IS implemented and tested; T-15-17 (public-id-only logging) implemented and log-verified; T-15-18's NetworkRegistry::New grep gate passes, its SetMembershipAllowList grep gate is descoped |

## Known Stubs

- `network_registry_` has no runtime consumer inside GeniusNode yet (its consumers were the descoped allow-list and later phase plans): it is constructed, registered as the network's PeerRegistry with SecureCrdtRegistry (D-04 association set inside New), membership-cached, torn down correctly, and observable via the test accessor — this is the descoped plan's design, not an accidental stub. Live CRDT-change cache refresh (`global_db` trailing arg) is intentionally not wired (plan-literal 5-arg call) — enable when the allow-list consumer lands.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 15-07/15-08 edit the same GeniusNode files: `network_registry_` is declared after `trusted_peer_registry_` (before `burn_config_`); ShutdownNodePolicyServices resets it FIRST among the policy owners; construction is guarded by `!private_network_id_.empty() && !network_registry_`
- If the owner later un-descopes the gater binding (15-04 equivalent): call `SetMembershipAllowList` inside StartPubSub's private branch BEFORE `Start` (per the original plan), passing `tx_globaldb_` as NetworkRegistry::New's trailing `global_db` so the cache refreshes live, and port the descoped Task 2 scenes to pubsub_counts.cpp
- Pre-existing no-genesis READY failure (deferred-items #1) is untouched; the new suite sidesteps it by configuring a genesis (account_management_test pattern)

## Self-Check: PASSED

- All 6 created/modified source + test files exist on disk (verified 2026-09-02T17:05Z)
- Task commits verified in git log: 8064116b (feat), 45e7ebed (test)
- Working tree clean of task files (only this SUMMARY remained for the docs commit)
- Descope greps: `SetMembershipAllowList` = 0 occurrences in src/account/GeniusNode.cpp and test/src/pubsub_counts/pubsub_counts.cpp (pubsub_counts.cpp untouched by this plan)
- `grep -c "network_registry_" src/account/GeniusNode.cpp` = 9 (acceptance: >= 4)
- ctest at close: trustedpeerregistry/securecrdt/burnconfig/network_registry 14/14, pubsub_counts 1/1, network_config_private_network 5/5, private_network_registry_binding 3/3, account_management 1/1 — all PASS

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-02*

