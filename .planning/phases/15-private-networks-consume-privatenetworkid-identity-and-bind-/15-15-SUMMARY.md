---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "15"
subsystem: auth
tags: [privatenetwork, networkregistry, membershipfilter, processingnode, enrollmentwindow, startupwindow, pubsubbroadcaster, gossip, pnet, failclosed, cplusplus]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "14"
    provides: the authenticated gate surfaces (SetGossipSigningKey everywhere, envelope sealing) this plan re-orders around — the filter+key now travel together into node creation
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "12"
    provides: the node-level broadcaster wiring (registry-backed filter install at INITIALIZING_TRANSACTIONS + live refresh) whose interim predecessor this plan adds, and whose set-time live-refresh propagation is preserved
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "13"
    provides: the processing-path gates whose creation-time application this plan moves BEFORE the first subscription
provides:
  - CR-G02a closed: ProcessingNode::New accepts trailing defaulted membershipFilter + gossipSigningKey parameters and installs them on the queue channel BEFORE Listen() and on the results accessor BEFORE CreateResultsChannel/ConnectToSubTaskQueue — no subscription can go live ungated; both creation sites snapshot under m_membershipFilterMutex BEFORE New; the post-hoc blocks are gone while the set-time live-refresh propagation loop is preserved
  - CR-G02b / G-WR-03 closed: sgns::networkregistry::MakeBootstrapMembershipFilter — a config-backed fail-closed predicate over network_bootstrap_peers_ (empty set denies everything) — installed on the tx_globaldb_ broadcaster in INITIALIZING_DATABASE STRICTLY BEFORE the first AddListenTopic; the registry-backed filter replaces it at INITIALIZING_TRANSACTIONS unchanged
  - IN-01 closed: failed dynamic_pointer_casts in ProcessingNode::SetMembershipFilter/SetGossipSigningKey now warn naming component + node id (a skipped security control is never silent)
  - processing_service.hpp contract now states the delivered truth (pre-subscription install; set-time propagation refreshes existing nodes)
  - Behavioral proofs: CreationTimeFilterCoversSubscriptionWindow (mutation-verified at the negative assertion), StartupWindowFilterCoversGlobalDBIngest, BootstrapMembershipFilterSemantics
affects: [15-verification, processing-service, processing-node, geniusnode-startup, networkregistry-tests, processing-tests]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies; ordering + parameter-plumbing changes only
  patterns:
    - "Security-relevant state travels INTO constructors/factories, never post-hoc: a defaulted trailing (filter, key) parameter pair on ProcessingNode::New lets the creation sites hand the snapshotted gate to the node BEFORE any subscription exists — the install point moves from 'after the object is live' to 'inside the object's own initialization', making the ungated window structurally unreachable rather than merely short"
    - "Interim-authority gating for startup windows: when the authoritative filter (registry) is constructed much later than the enforcement point (broadcaster's first subscription), a config-backed predicate over the SAME strings the authority will cache (bootstrap base58 PeerIds) covers the window; handoff is a plain replace on the same mutex-guarded slot, so the worst case is the stricter interim filter persisting slightly longer — never a gap"
    - "Mutation-test fixtures must keep the observable precondition alive: a one-shot queue exhausts in ~50ms and hides the enrollment window; N subtasks plus a slow processing core (detached engine thread) keep HasAvailableWork() true across the whole window, turning a racy negative window into a deterministic one"

key-files:
  created: []
  modified:
    - src/processing/processing_node.hpp
    - src/processing/processing_node.cpp
    - src/processing/processing_service.hpp
    - src/processing/processing_service.cpp
    - src/networkregistry/NetworkMembershipFilter.hpp
    - src/account/GeniusNode.cpp
    - test/src/networkregistry/network_membership_filter_test.cpp
    - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp

key-decisions:
  - "The signing key rides the SAME pre-subscription path as the filter (both as trailing defaulted New params): a filter installed at creation without its key would fail-close every publish from the first message (15-14 semantics), trading a security window for a correctness window — installing both together keeps the CR-G01/CR-G02 symmetry"
  - "Both creation sites pass explicit values for the defaulted subTasks/msSubscriptionWaitingDuration/ttl parameters to reach the trailing filter params (documented with named comments at the call sites); the values match the header defaults (2000ms, 2min)"
  - "The scoped zero-count gate was enforced on the ACTUAL post-edit creation-site function bodies (AcceptProcessingChannel 403-552, HandleNodeCreationTimeout 905-1040) rather than the plan's literal pre-15-14 line ranges (380-445/770-830), which no longer bracket the creation sites after 15-14 moved them; total node->SetMembershipFilter count in the file is exactly 1 (the preserved live-refresh propagation loop at :141)"
  - "The account-switch path (GeniusNode.cpp:2920 AddListenTopic on a retained tx_globaldb_) needs no interim install: it reuses a GlobalDB whose broadcaster already carries the interim or registry-backed filter installed by the original boot"
  - "requirements.mark-complete skipped for this plan's frontmatter IDs (D-07/D-11/PNET-GATE/PNET-PROC): phase-15 decision IDs live in 15-VERIFICATION.md, not REQUIREMENTS.md checkboxes — same precedent as 15-10..15-14"

patterns-established:
  - "Fail-closed-throughout now has two structural anchors: creation-time (gate passed into the factory, installed before any subscription) and boot-time (interim config-backed gate before the first topic subscription, replaced by the authoritative gate later)"

requirements-completed: [D-07, D-11, PNET-GATE, PNET-PROC]

# Metrics
duration: 23min
completed: 2026-09-03
---

# Phase 15 Plan 15: CR-G02 Enrollment-Window + G-WR-03 Startup-Window Closure Summary

**Every ProcessingNode now receives its membership filter (and CR-G01 signing key) as parameters to ProcessingNode::New — installed on the queue channel before Listen() and on the results accessor before CreateResultsChannel/ConnectToSubTaskQueue — and every private node's GlobalDB broadcaster carries a bootstrap-membership-backed interim filter from before its first AddListenTopic until the registry-backed filter replaces it, eliminating both ungated windows, with the header contract rewritten to the delivered truth, IN-01 warns added, and mutation-verified behavioral proofs for both windows**

## Performance

- **Duration:** ~23 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T18:43:16Z
- **Completed:** 2026-09-03T19:06:29Z
- **Tasks:** 3/3
- **Files modified:** 8 (0 created)

## Accomplishments

- **Task 1 — filter-before-subscribe at both creation sites** (`8fb515fa`):
  - `ProcessingNode::New` gains trailing defaulted `sgns::networkregistry::MembershipFilter
    membershipFilter = {}` and `std::shared_ptr<const libp2p::crypto::KeyPair> gossipSigningKey =
    {}`; both are threaded into `Initialize`, which installs them on the queue channel immediately
    after its construction (BEFORE `Listen`, processing_node.cpp:227 < :291) and on the
    `SubTaskQueueAccessorImpl` immediately after ITS construction (:251; accessor assigned :257) —
    strictly before `AttachTo`'s `CreateResultsChannel`/`ConnectToSubTaskQueue` (:303, invoked only
    after Initialize returns from New). Empty filter/key (public node) installs nothing — public
    paths byte-identical.
  - Both creation sites (`AcceptProcessingChannel` :495, `HandleNodeCreationTimeout` :989) snapshot
    the filter+key under `m_membershipFilterMutex` BEFORE calling New and pass them as the new
    parameters; the post-hoc `node->SetMembershipFilter` blocks are removed while the set-time
    live-refresh propagation loop is preserved verbatim at processing_service.cpp:141 (the only
    remaining occurrence in the file).
  - `processing_service.hpp` contract rewritten: the filter is "snapshotted BEFORE node creation ...
    passed INTO ProcessingNode::New, where it is installed BEFORE any subscription goes live --
    before the queue-channel Listen() and before the results-channel
    CreateResultsChannel/ConnectToSubTaskQueue"; set-time propagation refreshes existing nodes.
    The false "Applied to all existing processing nodes AND at node creation" sentence structure is
    gone (BEFORE-qualified text at hpp:77-97).
  - IN-01: the four silent failed-`dynamic_pointer_cast` no-ops in `SetMembershipFilter` /
    `SetGossipSigningKey` now `m_logger->warn` naming the component and node id
    (`grep -c "warn" processing_node.cpp` = 4).
- **Task 2 — bootstrap-membership interim filter** (`43a542d6`):
  - `MakeBootstrapMembershipFilter(const std::vector<std::string> &)` in
    NetworkMembershipFilter.hpp:116 — copies the strings once into a shared_ptr-held
    `unordered_set`; empty set DENIES everything (fail-closed, consistent with the 15-05 posture —
    such a node never reaches READY); else allow iff `peer.toBase58()` is in the set. The strings
    are the same base58 PeerId strings NetworkRegistry caches verbatim, so the interim verdict
    matches the registry verdict for the provisioned set.
  - GeniusNode INITIALIZING_DATABASE: inside the `!private_network_id_.empty()` guard (:774), after
    `ConfigureDatabaseDependencies` and STRICTLY BEFORE `tx_globaldb_->AddListenTopic(
    ScopedProcessingChannel() )` (:785), the interim filter installs on the broadcaster (:778-779)
    with a D-03-safe log (public id only). The registry-backed install at INITIALIZING_TRANSACTIONS
    (:1152) is untouched — `SetMembershipFilter` replace-by-design makes the handoff gap-free.
    Public nodes take the guard and install nothing; quorum/blockchain topics (from
    INITIALIZING_BLOCKCHAIN) fall under the interim filter as intended.
- **Task 3 — proofs** (`d541fa4b`):
  - `BootstrapMembershipFilterSemantics.ConfigBackedFailClosedPredicate` (unit): two-peer bootstrap
    set — both members allowed, fresh non-bootstrap PeerId denied, EMPTY set denies everything.
  - `CreationTimeFilterCoversSubscriptionWindow` (processing): a RAW (ungated, unsealed) attacker
    channel publishes queue requests continuously (distinct node ids, 100ms cadence) from BEFORE
    `ProcessingNode::New` is called, spanning the whole up-to-2000ms Listen window; the receiver is
    created with the deny filter passed INTO New — the attacker's queue-update sink stays 0 across
    the window AND at rest, and the receiver keeps queue ownership. Positive control: a sealed
    member publisher (filter set + own host key) through the SAME creation-time parameter with an
    allow filter gets its request processed and observes the published queue — the mesh and gates
    demonstrably work, so the negative is not vacuous.
  - `StartupWindowFilterCoversGlobalDBIngest` (flow, 3 same-PSK GlobalDB nodes): the gated node's
    broadcaster carries ONLY `MakeBootstrapMembershipFilter({member})` (no registry — exactly the
    boot posture); the bootstrap member's honestly-sealed write replicates, the sealed same-PSK
    non-bootstrap peer's write never lands across the 3s negative window.
  - `private_network_registry_binding` regression pin: `BroadcasterMembershipFilterInstalled(node)`
    true on the READY private node already exists from 15-12 (scene 1) — re-run green under the
    interim install (see Deviations #3).

## Task Commits

Each task was committed atomically:

1. **Task 1: filter into ProcessingNode::New before any subscription + creation-site snapshots** - `8fb515fa` (feat)
2. **Task 2: bootstrap-membership interim filter for the GlobalDB startup window** - `43a542d6` (feat)
3. **Task 3: creation-window and startup-window behavioral proofs** - `d541fa4b` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Verification Evidence

- Builds: `ninja -C build/OSX/Release processing_service`, `genius_node`, `networkregistry`, and
  all seven battery test targets — clean (only the pre-existing switch warning at GeniusNode.cpp
  :4776+, outside edited regions).
- Full battery `ctest -R "^(network_membership_filter_test|processing_subtask_queue_channel_pubsub_test|private_network_registry_binding_test|processing_service_test|processing_core_gating_test|network_config_private_network_test|network_registry_test)$"`
  — **7/7 passed** (~158s) against binaries rebuilt from the final source state.
- New test names visible in `--gtest_list_tests`: `BootstrapMembershipFilterSemantics.`,
  `StartupWindowFilterCoversGlobalDBIngest`, `CreationTimeFilterCoversSubscriptionWindow`;
  `private_network_registry_binding` lists 4/4 scenes.
- Scoped zero-count gate: `sed -n '403,552p;905,1040p' src/processing/processing_service.cpp | grep
  -c "node->SetMembershipFilter"` = 0 (the two creation-site function bodies) AND total in file =
  exactly 1 (the live-refresh propagation loop, :141) AND `grep -n "ProcessingNode::New"` returns
  the 2 call sites (:495, :989), each passing the snapshotted filter+key.
- Source-order pins (Task 1): queue-channel filter install processing_node.cpp:227 BEFORE Listen
  :291; accessor filter install :251 BEFORE AttachTo's CreateResultsChannel :303 (AttachTo runs
  after Initialize returns inside New). (Task 2): interim install GeniusNode.cpp:778-779 BEFORE
  AddListenTopic :785, inside the !private_network_id_.empty() guard :774; registry-backed replace
  unchanged at :1152.
- **Mutation evidence (Task 3, negative-leg):** with the four pre-subscription install calls in
  `ProcessingNode::Initialize` disabled (`if (false && membershipFilter)` / `if (false &&
  gossipSigningKey)` — the pre-15-15 post-hoc shape) and the test rebuilt,
  `CreationTimeFilterCoversSubscriptionWindow` FAILS at
  processing_subtask_queue_channel_pubsub_test.cpp:630: "raw attacker queue request was processed
  during/after the creation window (enrollment window is open)" — a window message is demonstrably
  processed without the install. Restored byte-exact via `git checkout --
  src/processing/processing_node.cpp` (`grep -c MUTATION` = 0), rebuilt, full 6-case suite green.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The plan's scoped sed ranges no longer bracket the creation sites**
- **Found during:** Task 1 acceptance
- **Issue:** `sed -n '380,445p;770,830p'` was anchored to pre-15-14 line numbers; 15-14's sealing
  blocks moved the creation sites to :403-552 and :905-1040, so the literal ranges would grep
  regions containing no `node->SetMembershipFilter` either way (vacuously true).
- **Fix:** Enforced the checker-corrected semantic requirement on the ACTUAL post-edit creation-site
  function bodies (403-552 Accept, 905-1040 Handle): zero occurrences there AND exactly 1 total in
  the file (the preserved live-refresh loop at :141 — deleting it would regress the 15-12
  live-refresh contract).
- **Files modified:** none beyond the planned files
- **Committed in:** 8fb515fa

**2. [Rule 1 - Test correctness] One-shot queue made the mutation check racy at the intended assertion**
- **Found during:** Task 3 mutation verification
- **Issue:** With a single initial subtask and the 50ms fixture core, the engine exhausts the queue
  within milliseconds of New returning; under the mutation the attacker's requests then hit
  "No available work" and produce no publish — the first mutation run failed only at the POSITIVE
  control (sealed member vs ungated receiver = raw-parse failure), leaving the negative assertion
  vacuously green.
- **Fix:** 8 initial subtasks + a 120s processing core (ProcessingCoreImpl(120000), executed on the
  engine's detached thread — nothing blocks): a grabbed subtask is LOCKED (not available), so the
  remaining items keep HasAvailableWork() true across the whole window; the mutation now fails
  deterministically at the negative enrollment-window assertion.
- **Files modified:** test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp
- **Committed in:** d541fa4b

**3. [No-op per the plan's own parenthetical] The binding-scene "extension" assertion already exists**
- **Found during:** Task 3
- **Issue:** The plan asks to extend PrivateNodeConstructsNetworkRegistryFromBootstrapMembership
  with `BroadcasterMembershipFilterInstalled(node) == true` "(unchanged from 15-12)" — that exact
  assertion has been in the scene since 15-12 (test line ~163).
- **Fix:** Added nothing (a duplicate assertion would be noise); the suite was re-run green against
  the interim install, which is the pin's purpose (the interim install did not break the
  registry-backed end state). The plan's file list named private_network_registry_binding_test.cpp
  as modified; it is NOT modified in this plan.
- **Committed in:** n/a (verified in d541fa4b's battery run)

---

**Total deviations:** 3 (1 checker-scope adaptation, 1 fixture strengthening that made the required
mutation proof deterministic, 1 documented no-op)
**Impact on plan:** All must_have truths and artifacts hold. No scope creep; no new dependencies;
zero modifications under thirdparty/ or 3rdparty/.

## Threat Model Disposition (T-15-15 register)

- T-15-15-01 (Spoofing, creation-window queue/results): mitigate — delivered (filter into New,
  installed before Listen and before the results-channel subscribe; mutation-verified)
- T-15-15-02 (Tampering, boot-window CRDT ingest): mitigate — delivered (interim filter before the
  first AddListenTopic; registry replace; flow-proven)
- T-15-15-03 (Spoofing, empty bootstrap config): mitigate — delivered (deny-all on empty set,
  unit-proven)
- T-15-15-04 (Tampering, interim -> registry handoff): accept — plain replace on the same
  mutex-guarded slot; worst case the stricter interim filter persists slightly longer, never a gap
- T-15-15-05 (Info disclosure, interim log): mitigate — logs private_network_id_ only, never
  network_key_ or peer key material (D-03)
- T-15-15-SC (supply chain): mitigate — no new packages; ordering + parameter plumbing only

## Notes for Downstream Plans

- **15-VERIFICATION:** CR-G02 (a) and (b) are closed with structural (source-order) AND behavioral
  (mutation-verified creation window; flow-proven boot window) evidence; IN-01 is closed. The
  remaining warnings from 15-REVERIFICATION (G-WR-01/02/04) were closed by 15-16. The
  "fail-closed throughout" clause can now be scored against code that subscribes nothing ungated on
  a private node.
- The grid channel was already ordered correctly (GeniusNode StartProcessing installs before
  Listen) and is untouched by this plan.
- `MakeBootstrapMembershipFilter` is deliberately registry-free and DB-free (config strings only) —
  safe to call at any boot state; the interim and registry filters share the base58 set-membership
  semantics, so the handoff cannot change a verdict for provisioned peers.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 8 modified source/test files exist on disk
- Task commits verified in git log: 8fb515fa (feat), 43a542d6 (feat), d541fa4b (test)
- No file deletions in any task commit; working tree clean of task files (only the pre-existing
  `docs` gitlink modification and pre-existing untracked set remain, untouched per run instructions)
- Final battery 7/7 green against rebuilt binaries; mutation restored byte-exact (grep MUTATION = 0)

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
