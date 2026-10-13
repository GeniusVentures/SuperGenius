---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "14"
subsystem: auth
tags: [networkregistry, membershipfilter, gossip, pubsubbroadcaster, ingestgate, processing, cryptography, ed25519, payloadauthentication, pnet, failclosed, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "11"
    provides: MembershipFilter + AuthorizeGossipSender + the broadcaster ingest gate this plan hardens
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "13"
    provides: the three processing-path gates this plan hardens
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "12"
    provides: per-node filter wiring in GeniusNode this plan extends with key wiring
provides:
  - CR-G01 closed at the mechanism level: all four membership gates authorize on an AUTHENTICATED sender identity (application-layer envelope: embedded public key <-> PeerId(from) binding + signature over magic+from+payload), fail-closed on missing/invalid envelopes under a set filter; public nodes byte-identical raw
  - sgns::base::SealGossipPayload / OpenGossipPayload / DeriveGossipFromBytes header-only authenticator (gossip_auth INTERFACE library over vendored libp2p crypto, no vendored modifications)
  - PubSubBroadcasterExt::SetGossipSigningKey/HasGossipSigningKey + OnMessage authenticate-before-authorize + Broadcast seal-or-fail-closed
  - SetGossipSigningKey across ProcessingServiceImpl/ProcessingNode/SubTaskQueueAccessorImpl/ProcessingSubTaskQueueChannelPubSub with propagation at set-time and both node-creation sites
  - sign_messages = true at BOTH production gossip construction sites (GeniusNode::StartPubSub + GlobalDbNetworkComposition)
  - Forged-from regression proofs: unit decision table + three end-to-end impostor/unsigned scenes, mutation-verified non-vacuous
affects: [15-verification, crdt_globaldb, processing-service, geniusnode, networkregistry-tests, processing-tests]

# Tech tracking
tech-stack:
  added: [] # header-only helper over vendored libp2p crypto APIs already in the build
  patterns:
    - "Application-layer payload authentication envelope (owner-sanctioned CR-G01 mapping): magic 'SGNSGOSSIP01' + u32be key len + marshaled protobuf public key + u32be sig len + signature + payload; canonical signable bytes = magic + u32be(from len) + from + payload — the signature covers the TRANSPORT FROM-FIELD, so the envelope is bound to the identity it travels under"
    - "Authenticate before authorize at every gate: OpenGossipPayload runs BEFORE the membership predicate under a set filter; membership is consulted on the verified identity; the inner payload view feeds the downstream protobuf parse; no filter -> raw path byte-identical"
    - "Seal exactly when a filter is installed; filter set + no signing key = publish fails closed with an error log naming the topic/channel (leaking unsigned data into a private network only to be denied at every receiver is worse than not publishing)"
    - "outcome::result<T, enum, policy::terminate> for header-local error enums: boost outcome's default policy only supports std::error_code error types in its exception path; the terminate policy compiles with arbitrary enums and every call site checks has_error() first"
    - "gossip_auth.hpp result construction must be fully qualified (libp2p::outcome::success): a bare `outcome` alias happens to resolve inside sgns TUs that alias it but NOT in test TUs"

key-files:
  created:
    - src/base/gossip_auth.hpp
  modified:
    - src/base/CMakeLists.txt
    - src/crdt/globaldb/pubsub_broadcaster_ext.hpp
    - src/crdt/globaldb/pubsub_broadcaster_ext.cpp
    - src/crdt/globaldb/GlobalDbNetworkComposition.cpp
    - src/crdt/globaldb/CMakeLists.txt
    - src/processing/processing_service.hpp
    - src/processing/processing_service.cpp
    - src/processing/processing_node.hpp
    - src/processing/processing_node.cpp
    - src/processing/processing_subtask_queue_accessor_impl.hpp
    - src/processing/processing_subtask_queue_accessor_impl.cpp
    - src/processing/processing_subtask_queue_channel_pubsub.hpp
    - src/processing/processing_subtask_queue_channel_pubsub.cpp
    - src/processing/CMakeLists.txt
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - test/src/networkregistry/network_membership_filter_test.cpp
    - test/src/processing/processing_service_test.cpp
    - test/src/processing/processing_service_test.hpp
    - test/src/processing/processing_service_test_base.cpp
    - test/src/processing/processing_subtask_queue_channel_pubsub_test.cpp

key-decisions:
  - "Envelope binds key <-> from AND signs over from: a forged envelope is denied twice (KEY_FROM_MISMATCH first; with the binding disabled, the signature rebuilt over the wire from fails verification) — mutation testing proved single-check disable still denies, so the recorded mutation disables the whole authentication outcome to demonstrate scene non-vacuity"
  - "The declared protobuf peer (bmsg.peer().id()) remains a POLICY check (it may legitimately carry a peerInfo override); authentication binds the embedded key to the transport from-field — the rewritten broadcaster comment states this real posture instead of the removed false 'Checking both defends against spoofing either field' claim"
  - "sign_messages = true is flipped at both production sites per owner direction BUT documented in-code as NOT consumed by SGNUS gates (Gossip::Message exposes only {from, topic, data}, gossip.hpp:129-135; vendored receive path verifies nothing) — the enforceable authentication is the application-layer envelope"
  - "SetGossipSigningKey mirrors SetMembershipFilter everywhere (mutex-guarded storage beside the filter, set-time propagation to existing nodes, application at both node-creation sites) with the same lock-ordering discipline: the filter/signing-key mutex is never held across m_mutexNodes"
  - "Fixture repairs keep denials attributable: gated WRITERS (intruder in scene 5, pnetB in scenes 6/7, queue/grid senders) are gated+keyed so their publishes SEAL — the negative windows then deny at the intended layer (membership or binding), not vacuously at missing-envelope; public/no-filter scenes stay raw"
  - "Grid gate test: BOTH legs sealed — the allowed member seals with its own key (positive) and the non-member third node seals with its OWN key (negative), so the deny leg proves the MEMBERSHIP predicate, not envelope absence"
  - "requirements.mark-complete skipped for this plan's frontmatter IDs (D-07/D-11/PNET-GATE/PNET-PROC): phase-15 decision IDs live in 15-VERIFICATION.md, not REQUIREMENTS.md checkboxes — same precedent as 15-10..15-13"

patterns-established:
  - "Authenticated private-network publish shape: same keypair constructs the gossip host, seals the payloads, and derives the PeerId the from-field carries — identity, transport, and authorization basis are one key material"

requirements-completed: [D-07, D-11, PNET-GATE, PNET-PROC]

# Metrics
duration: 41min
completed: 2026-09-03
---

# Phase 15 Plan 14: CR-G01 Authenticated Membership Gates Summary

**All four membership gates (gossip/CRDT ingest, grid, results, queue) now authorize on an AUTHENTICATED sender identity — an application-layer envelope whose embedded public key must derive the from-field PeerId with a valid signature over magic+from+payload — fail-closed on unsigned/unverifiable messages under a set filter, sealed publish paths at all seven processing publish sites plus the broadcaster, sign_messages=true at both production construction sites, proven by a unit decision table and three mutation-verified end-to-end forged-from scenes**

## Performance

- **Duration:** ~41 min (main working tree; dependency cone already built)
- **Started:** 2026-09-03T17:22:55Z
- **Completed:** 2026-09-03T18:04:12Z
- **Tasks:** 3/3
- **Files modified:** 23 (1 created, 22 modified)

## Accomplishments

- **Task 1 — authenticator + broadcaster + composition signing** (`732845e8`):
  - `src/base/gossip_auth.hpp` (header-only, no networkregistry/crdt includes): `kGossipAuthEnvelopeMagic` (12-byte "SGNSGOSSIP01"); `SealGossipPayload(keypair, from_bytes, payload)` — canonical signable bytes = magic + u32be(from len) + from + payload, signed with the gossip host keypair; `OpenGossipPayload(from_bytes, wire_data)` — checks in order: magic (NOT_AN_ENVELOPE) -> lengths/unmarshal key (MALFORMED_ENVELOPE) -> `PeerId::fromPublicKey(embedded) == PeerId::fromBytes(from)` (KEY_FROM_MISMATCH; empty/malformed from fails here) -> signature verify over recomputed signable bytes (SIGNATURE_INVALID); `DeriveGossipFromBytes(keypair)` for publishers; crypto provider/marshaller instantiated via function-local singletons following the keypair_file_storage.cpp:20-52 recipe. Registered as the `gossip_auth` INTERFACE CMake library linked PUBLIC into `crdt_globaldb`.
  - `PubSubBroadcasterExt`: `SetGossipSigningKey`/`HasGossipSigningKey` stored under `membership_filter_mutex_`; `OnMessage` runs OpenGossipPayload on from/data BEFORE the membership checks when a filter is installed, parses `bmsg` from the INNER payload, and keeps the declared-peer + from membership checks on the authenticated identity; `Broadcast` seals the serialized BroadcastMessage when a filter is installed and FAILS CLOSED (error log naming the topic, no publish) when a filter is set but no key is wired; no filter -> byte-identical raw paths. The false "Checking both defends against spoofing either field" comment is replaced with the real posture (membership only after application-layer authentication binds key<->from; declared protobuf peer remains a policy check).
  - `GlobalDbNetworkComposition`: `sign_messages = true`, retained keypair copy wired into `db->GetBroadcaster()->SetGossipSigningKey(...)` right after GlobalDB creation.
- **Task 2 — processing gates + GeniusNode wiring** (`54e39f86`):
  - All three processing handlers (grid `OnMessage`, results `OnResultChannelMessage`, queue `OnProcessingChannelMessage`) authenticate with OpenGossipPayload BEFORE `AuthorizeGossipSender` under a set filter; the inner payload feeds the downstream protobuf parse; no filter -> raw byte-identical.
  - All seven publish sites (grid x3, results x2, queue x2) seal with `SealGossipPayload` under a filter; filter+no-key = skip with error log (fail-closed); no filter -> raw unchanged.
  - `SetGossipSigningKey` on `ProcessingServiceImpl` (store + set-time propagation + BOTH node-creation sites, symmetric with the filter), `ProcessingNode` forwarding to accessor + queue channel, and direct setters on `SubTaskQueueAccessorImpl` / `ProcessingSubTaskQueueChannelPubSub`; `gossip_auth` added PUBLIC to the `processing_service` target.
  - `GeniusNode`: `sign_messages = true` in StartPubSub (CR-G01 rationale replaces the "use no signing" comment); `gossip_signing_keypair_` member copy retained before the GossipPubSub move; wired to the `tx_globaldb_` broadcaster in InitDatabase (unconditional when set — harmless for public nodes) and to the processing service inside the StartProcessing private-node guard beside SetMembershipFilter.
- **Task 3 — forged-from proofs** (`1fe13c6b`):
  - `GossipPayloadAuthDecisionTable` unit suite: honest seal/open round trip (payload intact + authenticated peer == from PeerId), forged-from KEY_FROM_MISMATCH, tampered payload SIGNATURE_INVALID, corrupted signature SIGNATURE_INVALID, raw NOT_AN_ENVELOPE, empty-from denial.
  - `MemberImpostorEnvelopeIsDroppedByGatedIngest` (flow, 3 same-PSK nodes): a MEMBER whose broadcaster seals with ANOTHER member's keypair (envelope claims the other member's identity, transport from is its own) never lands its CRDT write on the gated node across the 3s window although membership includes it — only authentication can deny (non-vacuity by construction); the honestly-sealed member replicates (positive control).
  - `UnsignedPayloadFromMemberIsDroppedUnderFilter` (flow): a member's RAW publish is denied (fail-closed against missing envelope) with the sealed positive control.
  - `QueueChannelImpostorEnvelopeIgnored` (processing): sender is in the allow-set but its envelope claims the other member's key — request sink stays at 0 across the window; re-wiring its own key makes the next request propagate.
  - Fixture repairs: `PnetGdbNode::signing_key` retained copies + `SetGossipSigningKey` wherever a filter is installed; gated writers seal so denials stay attributable to the intended layer; processing fixtures switched to the explicit-keypair `GossipPubSub` ctor with retained copies (`m_pubsub_keypairs`) and `SealPayloadForKey`/`GenerateEd25519KeyPair` shared helpers.
  - **Mutation verification:** disabling OpenGossipPayload's authentication (key/from binding + signature verify) makes scene (1) FAIL at "impostor envelope write replicated although it must be denied" and scene (3) FAIL at "Impostor envelope reached the receiver's sink although the from-field binding denies it"; restored (byte-exact from HEAD plus the outcome-qualification fix) and re-run green. Notably, disabling ONLY the binding still denied the impostors — the signature is rebuilt over the wire from-field, so a foreign-key envelope fails verification too (double protection; documented as a key decision).

## Task Commits

Each task was committed atomically:

1. **Task 1: Gossip payload authenticator + broadcaster authentication + production signing config** - `732845e8` (feat)
2. **Task 2: Processing-path gate authentication + GeniusNode signing-key wiring** - `54e39f86` (feat)
3. **Task 3: Forged-from proofs — decision table, impostor flow scenes, fixture key wiring** - `1fe13c6b` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Verification Evidence

- Builds: `ninja -C build/OSX/Release crdt_globaldb`, `processing_service genius_node`, and the five touched/affected test targets — all clean (no new warnings in edited regions).
- Full battery `ctest -R "^(network_membership_filter_test|processing_subtask_queue_channel_pubsub_test|processing_service_test|network_registry_test|private_network_registry_binding_test|network_config_private_network_test|processing_core_gating_test|pubsub_counts_test)$"` — **8/8 passed** (~142s) against binaries rebuilt from the final source state.
- `network_membership_filter_test`: 10 cases (4 unit + decision table + 5 flow) — 40s, passed; `--gtest_list_tests` shows GossipPayloadAuthDecisionTable, MemberImpostorEnvelopeIsDroppedByGatedIngest, UnsignedPayloadFromMemberIsDroppedUnderFilter.
- Mutation evidence (commands + failing assertions above) recorded; restore verified by `grep -c MUTATION == 0` + green re-run.
- Source assertions (Task 1): `SetGossipSigningKey` in pubsub_broadcaster_ext.hpp:161; `sign_messages = true` GlobalDbNetworkComposition.cpp:200; `spoofing either field` 0 matches; `OpenGossipPayload` x5 in pubsub_broadcaster_ext.cpp; "FAILED CLOSED" error logs x3 in Broadcast's seal path. (Task 2): OpenGossipPayload x3 in each handler .cpp; SealGossipPayload total x7 across the three .cpp; setters in all four headers; GeniusNode.cpp sign_messages=true:1892 with no remaining false, SetGossipSigningKey x2, `gossip_signing_keypair_` in GeniusNode.hpp:915.
- Vendored-tree guard: `git -C thirdparty/libp2p status --porcelain | wc -l` == 0 at HEAD **b28eed2** (the pinned dev_pnets commit), ipfs-pubsub clean at bcbc50d — zero modifications under any vendored tree. (The thirdparty superproject shows a pre-existing unrelated `M stb` submodule-pointer state and the 3rdparty sibling checkout has pre-existing mass-deletion entries — both predate this execution and are untouched by it; every write this plan made was inside SGNUS.)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `ninja gossip_auth` cannot succeed — INTERFACE libraries have no ninja build target**
- **Found during:** Task 1 verify
- **Issue:** The acceptance command `ninja -C build/OSX/Release gossip_auth crdt_globaldb` fails with "unknown target 'gossip_auth'": CMake INTERFACE (header-only) libraries produce no build-system target, by design.
- **Fix:** Kept the plan-mandated INTERFACE shape (header-only requirement is the load-bearing constraint); the header's compilation is proven by every consumer target (`crdt_globaldb` first), and the library participates in the CMake export graph via `supergenius_install(gossip_auth)`. Verified with `ninja -C build/OSX/Release crdt_globaldb` + all downstream targets.
- **Files modified:** none beyond the planned files
- **Committed in:** 732845e8

**2. [Rule 3 - Blocking] Installed libp2p header layout differs from the source tree; outcome error-enum policies**
- **Found during:** Task 1/2 builds
- **Issue:** (a) the interface headers live at `libp2p/crypto/crypto_provider.hpp`, `key_marshaller.hpp`, `key_validator.hpp` (not the `crypto_provider/crypto_provider.hpp` shape guessed from the source checkout); (b) boost outcome's default result policy cannot instantiate `.value()` for a local error enum (only std::error_code); (c) a bare `outcome::success` resolves in sgns TUs that alias the namespace but NOT in test TUs.
- **Fix:** corrected include paths; `GossipAuthResult<T> = libp2p::outcome::result<T, GossipPayloadAuthError, policy::terminate>` (every call site checks has_error() before value()); fully-qualified `libp2p::outcome::success`.
- **Files modified:** src/base/gossip_auth.hpp
- **Committed in:** 732845e8 (paths/policy), 1fe13c6b (qualification — post-restore re-fix)

**3. [Rule 3 - Blocking] UNITY_BUILD collides anonymous-namespace helpers across processing .cpp files**
- **Found during:** Task 2 build
- **Issue:** The three processing .cpp files each defined a file-local `ToByteSpan`; UNITY_BUILD merges them into one TU — redefinition error.
- **Fix:** One shared `sgns::base::detail::StringSpan` in gossip_auth.hpp used by all seven publish sites.
- **Files modified:** src/base/gossip_auth.hpp, the three processing .cpp files
- **Committed in:** 54e39f86

**4. [Deviation - Test sequencing] Task 1 is marked tdd="true" but the plan places every test file in Task 3**
- **Found during:** Task 1
- **Issue:** The plan's own task structure implements (Tasks 1-2) before testing (Task 3, whose files list contains all four test files); a strict RED-before-GREEN cycle per Task 1 was not structurally possible.
- **Fix:** Followed the plan's task sequencing; the Task 3 decision table + scenes cover the Task 1 <behavior> block case-for-case, and the mutation check supplies the non-vacuity proof. No TDD gate section needed (plan type is `execute`, not `tdd`).
- **Committed in:** 1fe13c6b

**5. [Observation - Stronger than plan] Single-check mutation does not fail the impostor scenes**
- **Found during:** Task 3 mutation verification
- **Issue:** Disabling only the key/from binding check left the impostor scenes GREEN — because the signature is (re)built over the wire from-field, the foreign-key envelope also fails signature verification. The plan's "disabling the binding (or the verify)" assumed one check was the sole denyer.
- **Fix:** The recorded mutation disables the whole authentication outcome (binding + verify), which makes both scenes FAIL at their deny-window assertions — the required non-vacuity proof. The double protection is a property, not a defect (documented in key-decisions).
- **Files modified:** none (transient mutation, restored byte-exact)

---

**Total deviations:** 5 (3 blocking build-system realities, 1 plan-structure note, 1 favorable observation; all keep the plan's required observables intact)
**Impact on plan:** All must_have truths and artifacts hold. No scope creep; no new dependencies; zero modifications under thirdparty/ or 3rdparty/.

## Threat Model Disposition (T-15-14 register)

- T-15-14-01 (Spoofing, gates): mitigate — delivered (authenticate before authorize at all four gates)
- T-15-14-02 (Spoofing, publish): mitigate — delivered (host-keypair sealing; filter+no-key fails closed)
- T-15-14-03 (Tampering, envelope): mitigate — delivered (signature covers magic+from+payload; unit cases c/d)
- T-15-14-04 (Replay across topics): accepted — envelope carries no topic/freshness binding (documented in gossip_auth.hpp header; gossip message-id dedup + downstream ids)
- T-15-14-05 (Info disclosure, logs): mitigate — deny logs name failure kind (enum int) and topic only, never key material
- T-15-14-06 (DoS via verify cost): accepted — one Ed25519 verify per inbound message under a filter
- T-15-14-SC (supply chain): mitigate — no new packages; vendored trees verified unmodified

## Notes for Downstream Plans

- **15-VERIFICATION:** CR-G01 is closed at the mechanism level with mutation-verified proof. CR-G02 (ungated enrollment/startup windows: ProcessingNode::New pre-subscription window and the private-node boot GlobalDB window before NetworkRegistry installs the filter) is explicitly 15-15 scope per this plan's read_first note ("ordering is 15-15 scope") — do not score it against 15-14.
- **15-15 (ordering):** `SetGossipSigningKey` now exists on every surface 15-15 will reorder; pass the filter+key INTO ProcessingNode::New before Listen/ConnectToSubTaskQueue to close the enrollment window, and consider a bootstrap-membership filter right after GlobalDB creation for the boot window.
- The gossip-layer wire signatures (sign_messages=true) exist on production gossip now but remain UNVERIFIED by any vendored code and UNCONSUMED by SGNUS gates — if a future vendored libp2p exposes them, the app-layer envelope can be retired in favor of wire verification (the binding semantics are identical).
- Teardown: `ClearMembershipFilter` does NOT clear the signing key (a set key without a filter is inert); clearing both is unnecessary for correctness.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

- All 23 created/modified source + test files exist on disk
- Commits verified in git log: 732845e8 (feat), 54e39f86 (feat), 1fe13c6b (test)
- No file deletions in any task commit; working tree clean of task files (only the pre-existing `docs` gitlink modification and pre-existing untracked set remain, untouched per run instructions)
- Final battery 8/8 green against rebuilt binaries; vendored libp2p b28eed2 / ipfs-pubsub pristine

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
