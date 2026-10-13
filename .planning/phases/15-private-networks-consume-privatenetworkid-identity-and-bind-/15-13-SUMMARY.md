---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "13"
subsystem: auth
tags: [networkregistry, membershipfilter, processing, gossip, gridchannel, resultschannel, queuechannel, ingestgate, pnet, failclosed, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "11"
    provides: sgns::networkregistry::MembershipFilter + MakeNetworkMembershipFilter + AuthorizeGossipSender (from-field helper, dependency-light)
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "12"
    provides: per-node NetworkRegistry wiring in GeniusNode (network_registry_ live at StartProcessing) + the setBitswap-style propagation pattern precedent
provides:
  - VERIFICATION gap 5 closed with runnable proof: the processing path binds the same membership enforcement as the gossip host at its three real SGNUS-side message-handling points (the 15-08 descoped membership clause, in the owner-directed application-layer form — no gater, no vendored-tree changes)
  - ProcessingServiceImpl::SetMembershipFilter (store + propagate to existing nodes + apply at BOTH node-creation sites — no enrollment window) + OnMessage grid-channel gate
  - SubTaskQueueAccessorImpl::SetMembershipFilter + OnResultChannelMessage gate (before any result/mirror handling)
  - ProcessingSubTaskQueueChannelPubSub::SetMembershipFilter + OnProcessingChannelMessage gate (before any queue ownership/sync handling)
  - ProcessingNode::SetMembershipFilter forwarding to both its channels
  - Both processing test targets registered standalone in the build (previously commented out — registration gate proves they run)
affects: [15-verification, processing-service, geniusnode-startup, processing-tests]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "One gate shape stamped at three handlers: filter member + dedicated mutex + public SetMembershipFilter + entry check calling sgns::networkregistry::AuthorizeGossipSender — snapshot the filter under the mutex, evaluate unlocked (setters on wiring/teardown threads, evaluation on pubsub callback threads)"
    - "Propagation covers both time axes: set-time (scoped_lock m_mutexNodes, mirror of setBitswap) AND creation-time (both m_processingNodes insertion sites apply the stored filter) — T-15-13-06 no-enrollment-window; the filter mutex is never held across m_mutexNodes acquisition, so the reverse nesting at creation sites cannot deadlock"
    - "Non-vacuous registration gate: ctest -N must list >=2 tests BEFORE any test run — a test target that does not build or register cannot prove a gap closed"
    - "Mutation-verified negative windows: temporarily prefixing each gate's condition with `false &&` makes BOTH new scenes fail at their deny-window assertions (messages genuinely arrive; only the gates stop them), then restored byte-exact from the Task 1 commit"

key-files:
  created: []
  modified:
    - src/processing/processing_service.hpp
    - src/processing/processing_service.cpp
    - src/processing/processing_node.hpp
    - src/processing/processing_node.cpp
    - src/processing/processing_subtask_queue_accessor_impl.hpp
    - src/processing/processing_subtask_queue_accessor_impl.cpp
    - src/processing/processing_subtask_queue_channel_pubsub.hpp
    - src/processing/processing_subtask_queue_channel_pubsub.cpp
    - src/processing/CMakeLists.txt
    - src/account/GeniusNode.cpp
    - test/src/processing/CMakeLists.txt
    - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp
    - test/src/processing/processing_service_test.cpp

key-decisions:
  - "Gate placement: immediately after the null-message check in OnMessage / at the very top of OnResultChannelMessage and OnProcessingChannelMessage — the deny path costs one PeerId parse + set lookup before any protobuf parse or state change (T-15-13-05)"
  - "The two new tests use maximalNodesCount=2 / a distinct denied channel id so a BROKEN gate would observably grow the count — the DISABLED_ProcessingSlotsAreAvailable recipe used max=1, under which the negative window would pass vacuously on room capacity"
  - "Grid test topics append sgns::version::GetNetAndVersionAppendix() explicitly: ProcessingServiceImpl::Listen appends it internally, so a raw GossipPubSubTopic publishing to the plain id would never reach the service (the DISABLED test's plain-topic shape predates the appendix)"
  - "The allow-set is snapshotted from each host's topic peer view (getAllPeers) BEFORE the third node joins — the filter provably excludes the non-member, and self-echo of the service's own publishes is allowed by construction"
  - "Registration source lists per target are load-bearing and asymmetric: processing_service_test compiles ONLY processing_service_test.cpp (it self-contains the ProcessingServiceTest fixture methods; adding base.cpp duplicates SetUp/SetUp/TearDown/Initialize symbols), while the channel-pubsub target needs base.cpp (its fixture inherits ProcessingServiceTest and calls the base methods)"
  - "requirements.mark-complete skipped for this plan's frontmatter IDs (D-08/D-11/PNET-PROC): Phase-15 decision/gap IDs live in 15-VERIFICATION.md, not in REQUIREMENTS.md checkboxes — same precedent as the 15-10/15-11/15-12 docs commits"

patterns-established:
  - "Every inbound gossip surface that carries private coordination state into node state is membership-gated at handler entry: grid channel (channel requests/responses/node-creation intents), results channel (subtask results -> job state), queue channel (queue ownership/sync) — the 15-11 chokepoint lesson applied to the processing layer"

requirements-completed: [D-08, D-11, PNET-PROC]

# Metrics
duration: 20min
completed: 2026-09-03
---

# Phase 15 Plan 13: Processing-Path Membership Gates Summary

**The three real SGNUS-side processing message handlers (grid, results, queue channels) enforce the registry-backed membership filter at entry with identical fail-closed semantics, wired per-node in GeniusNode StartProcessing, proven by mutation-verified deny/allow/widen scenes with both processing test targets registered standalone in the build**

## Performance

- **Duration:** ~20 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T13:45:20Z
- **Completed:** 2026-09-03T14:05:13Z
- **Tasks:** 2/2
- **Files modified:** 13 (0 created)

## Accomplishments

- **Task 1 — gates + propagation chain** (`src/processing/`):
  - `ProcessingServiceImpl`: `SetMembershipFilter` stores under `m_membershipFilterMutex` then — mirroring setBitswap — propagates to every existing `m_processingNodes` entry; BOTH node-creation sites (AcceptProcessingChannel and HandleNodeCreationTimeout insertions) apply the stored filter, so nodes created after the call are covered (T-15-13-06, no enrollment window). `OnMessage` drops a message whose transport `from` fails `AuthorizeGossipSender` immediately after the null-message check, before any protobuf parse or grid handling.
  - `ProcessingNode::SetMembershipFilter` forwards to BOTH its SubTaskQueueAccessorImpl (results channel) and its ProcessingSubTaskQueueChannelPubSub (queue channel), null-checked with the same dynamic_pointer_cast shape as setMirrorResultCallback/setBitswap.
  - `SubTaskQueueAccessorImpl`: gate at the top of `OnResultChannelMessage` (after the weak_this lock, before any result/mirror handling — this also gates the mirror-fetch trigger). `ProcessingSubTaskQueueChannelPubSub`: gate at the top of `OnProcessingChannelMessage` (before any queue ownership/sync handling).
  - All three gates: empty filter -> public pass-through byte-identical; set filter + empty/malformed `from` -> deny (fail-closed, T-15-13-04); debug log only, never payload or key material.
  - `src/processing/CMakeLists.txt`: `networkregistry` added to the PUBLIC link set (NetworkMembershipFilter.hpp appears in processing headers).
- **Task 2 — wiring, registration, tests:**
  - `GeniusNode::StartProcessing`: inside the `processing_service_` guard, before `StartProcessing(ScopedProcessingGridChannel())`, a private node (`!private_network_id_.empty() && network_registry_`) installs `MakeNetworkMembershipFilter(network_registry_)` on the processing service; public nodes take the guard and install nothing (default-arg public path unchanged, D-03-safe log prints only the network id).
  - `test/src/processing/CMakeLists.txt`: the two targets registered standalone with per-target source lists (single-source `processing_service_test`; two-source `processing_subtask_queue_channel_pubsub_test` with `processing_service_test_base.cpp`, mirroring the `processing_validate_result_data_test` precedent). The other FOUR commented sources stay unregistered. REGISTRATION GATE passed before any test was written: both targets linked and `ctest -N -R "processing_service_test|processing_subtask_queue_channel_pubsub"` listed **Total Tests: 2**.
  - Constructor repair: both stale 5-arg `ProcessingServiceImpl` constructions in processing_service_test.cpp upgraded to the current 8-arg signature with recording lambdas (local atomic flags double as spurious-callback detectors) and distinct test node addresses — the file compiles again.
  - NEW `MembershipFilterBlocksNonMemberQueueMessages` (queue channel): reproduces the file's two-host propagation scenario; deny-all filter on the receiver keeps the request sink at 0 across a 3s grace-loop window; replacing the filter with one allowing the sender (learned from the receiver's topic peer view) admits the sender's next message — deny, pass-when-member, and set-time consultation in one scene.
  - NEW `GridMessagesFromNonMemberPeersAreIgnored` (grid channel): allow-set {service host, pubs2} snapshotted from topic peer views before the third node joins; pubs2's `processing_channel_response` ("PROCESSING_QUEUE_ID_ALLOWED") grows `GetProcessingNodesCount()` via the AcceptProcessingChannel path (max=2 so the deny leg is non-vacuous); a mesh-joined THIRD pubsub (bootstrap-dialed to pubs1, subscription-waited before publishing, sign_messages=true so `from` is a real transport sender) publishes the identical shape with "PROCESSING_QUEUE_ID_DENIED" — count unchanged across the 3s window and at the final check.
  - **Mutation-verified non-vacuity:** prefixing each gate's condition with `false &&` (gates disabled) makes GridMessagesFromNonMemberPeersAreIgnored fail at "Non-member peer's grid message was processed although the filter denies it" and MembershipFilterBlocksNonMemberQueueMessages fail at "Non-member queue message reached the receiver's sink although the filter denies it" — the peer messages genuinely arrive and only the gates stop them. Restored byte-exact via `git checkout` from the Task 1 commit; suites green again.

## Fourth-inbound-path check (run-instruction verification)

Scanned `src/processing` for any subscription surface beyond the three gated ones: the only
GossipPubSubTopic subscriptions in the processing flow are the grid channel
(`ProcessingServiceImpl::Listen` -> m_gridChannel), the results channel
(`SubTaskQueueAccessorImpl::ConnectToSubTaskQueue` -> m_resultChannel), and the queue
channel (`ProcessingSubTaskQueueChannelPubSub::Listen` -> m_processingQueueChannel) —
all gated. No fourth gossip path exists. Data-plane observations (not deviations):

- The mirror-result fetch (GeniusNode's `setMirrorResultCallback` lambda -> bitswap) is
  triggered only from `OnResultChannelMessage` AFTER the new gate, and fetched blocks are
  CID-integrity-checked content (hash-verified reads, not peer-authored state).
- CRDT-carried task/result entries reach processing via the GlobalDB broadcaster chokepoint
  gated since 15-11/15-12; `GeniusNode::DHTInit`'s ProvideCID/FindProviders is discovery
  metadata, not processing data ingestion.

## Task Commits

Each task was committed atomically:

1. **Task 1: Membership gates at all three processing-path message handlers** - `2757ade3` (feat)
2. **Task 2: Test-target registration + GeniusNode threading + gate tests** - `16e743d5` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/processing/processing_service.hpp/.cpp` - SetMembershipFilter API + members; OnMessage gate; filter application at both node-creation sites
- `src/processing/processing_node.hpp/.cpp` - SetMembershipFilter forwarding to accessor + queue channel
- `src/processing/processing_subtask_queue_accessor_impl.hpp/.cpp` - filter member/setter; OnResultChannelMessage gate before result/mirror handling
- `src/processing/processing_subtask_queue_channel_pubsub.hpp/.cpp` - filter member/setter; OnProcessingChannelMessage gate
- `src/processing/CMakeLists.txt` - networkregistry PUBLIC
- `src/account/GeniusNode.cpp` - guarded filter install in StartProcessing before StartProcessing
- `test/src/processing/CMakeLists.txt` - two standalone registrations with per-target source lists
- `test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp` - MembershipFilterBlocksNonMemberQueueMessages
- `test/src/processing/processing_service_test.cpp` - 8-arg constructor repair (x2); GridMessagesFromNonMemberPeersAreIgnored

## Decisions Made

- See key-decisions. The max-nodes=2 non-vacuity call, the explicit topic appendix, the
  pre-join allow-set snapshot, and the per-target registration source lists were the
  substantive interpretation calls; the first three are pinned by the mutation check, the
  fourth by the registration gate (targets linked and listed before any test ran).
- Verified before relying on them: `addtest` registers a ctest entry named after the target
  (cmake/functions.cmake); `GossipPubSubTopic` does NOT append the net-and-version appendix
  (the service's Listen does — hence the explicit appendix in the test topics);
  `ProcessingSubTaskQueueChannelPubSub` exposes `GetActiveNodes()` (not getAllPeers);
  multi-capture lambdas passed to gtest macros need wrapping parentheses (preprocessor
  treats only `()` as grouping); `networkregistry` PUBLIC on processing_service creates no
  dependency cycle (nothing in the networkregistry -> securecrdt cone links back).

## Deviations from Plan

None - plan executed exactly as written. (Line anchors had drifted as expected from 15-12's
edits: GeniusNode::StartProcessing is at :3557 and the 8-arg construction precedent at
:1298; the registration/CMake recipe from the checker-verified commit 87eccfc3 built on the
first attempt.)

## Verification Evidence

- `ninja -C build/OSX/Release processing_service genius_node processing_service_test processing_subtask_queue_channel_pubsub_test` — clean (1 pre-existing switch warning at GeniusNode.cpp:4787, outside edited regions)
- Registration gate (BEFORE any test run): both targets linked; `ctest -N -R "processing_service_test|processing_subtask_queue_channel_pubsub"` -> Total Tests: 2
- Source assertions: `AuthorizeGossipSender` x1 in each of the three handler .cpp files; `networkregistry` in src/processing/CMakeLists.txt; `processing_service_->SetMembershipFilter(` guarded by private_network_id_/network_registry_ inside StartProcessing
- `ctest -R "processing_subtask_queue_channel_pubsub|processing_service|processing_core_gating|private_network_registry_binding|network_config_private_network"` — **5/5 passed** (~75s), all binaries rebuilt against the new libraries first:
  - processing_service_test incl. GridMessagesFromNonMemberPeersAreIgnored
  - processing_subtask_queue_channel_pubsub_test incl. MembershipFilterBlocksNonMemberQueueMessages
  - processing_core_gating_test 4/4 (incl. DefaultArgumentsKeepPublicConstruction — 15-08 Plaintext/pnet regression intact)
  - private_network_registry_binding_test 4/4 (15-12 node wiring intact on the shared GeniusNode.cpp)
  - network_config_private_network_test (public config path unchanged)
- Mutation check: gates disabled -> both new scenes FAIL at their deny-window assertions; gates restored -> all green

## Notes for Downstream Plans

- **15-VERIFICATION gap 5:** the processing path now binds the same enforcement as the gossip host — component halves (three gates) and the node-level wiring half (GeniusNode StartProcessing install) are all in place with runnable proof.
- The per-target registration split in test/src/processing/CMakeLists.txt unblocks any future re-registration of the remaining four commented test sources — each will need its own source-list analysis (base.cpp dependency question per target).
- Grid-channel test recipe: raw topics MUST append `GetNetAndVersionAppendix()` to reach a `ProcessingServiceImpl` subscription; `GetActiveNodes()`/`getAllPeers()` on a topic is the way to learn peer ids for allow-set construction.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 13 modified source/test files exist on disk
- Task commits verified in git log: 2757ade3 (feat), 16e743d5 (test)
- No file deletions in either task commit; working tree clean of task files (only the pre-existing `docs` gitlink modification and pre-existing untracked set remain, untouched per run instructions)
- ctest at close: the 5-suite battery above all PASS against rebuilt binaries; registration gate 2/2

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
