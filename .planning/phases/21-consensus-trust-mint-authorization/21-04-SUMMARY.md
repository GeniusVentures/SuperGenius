---
phase: 21-consensus-trust-mint-authorization
plan: "04"
subsystem: auth
tags: [consensus-trust, registry-cid, grandfather-probe, defer-deadline, verified-cache, trus-01, certificate-validation]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-01
    provides: LoadVerifiedRegistryByCid / LoadUnverifiedRegistryByCid and the deprecated alias this plan consumed and deleted
  - phase: 21-consensus-trust-mint-authorization/21-03
    provides: base::BoundedDeferTracker + authorization_defer_window_sec config retained on GeniusNode
provides:
  - ValidateCertificate D-01 grandfather probe — durable /cert/<slot> record selects the unverified loader with today's semantics; strict verified loader otherwise
  - Classified verdict mapping at the certificate layer — permission_denied → rule-named Reject; missing dependency → Stalled (never memoized); D-03 defer-deadline → deterministic Reject
  - ConsensusManager::SetAuthorizationDeferWindow + kDefaultAuthorizationDeferWindow (30 min) wired from authorization_defer_window_sec via Blockchain::GetConsensusManager
  - Verified-only registry CID cache (cap 64, oldest-first single-entry FIFO eviction, failures uncached)
  - Deprecated LoadRegistryByCid alias removed; zero bare references in src/ + test/
  - TRUST-01 integration proof: forged-registry reject (no durable record), restart grandfather, catch-up convergence
affects: [21-06-mint-sync-ingress, 21-07-enforcement-tests, TRUST-02-audit, 24-bridge-ingestion-safety]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Grandfather probe before the load: durable-record presence (write-path key derivation, records only written post-Approve) selects legacy semantics; strictness applies only to not-yet-final certificates"
    - "Verdict classification by errc: permanent (permission_denied) is Reject/memoizable; retryable (resource_unavailable_try_again) is Stalled/never memoized; a first-seen defer deadline converts an eternal stall into a deterministic timeout Reject"
    - "Verified-only caches: both failure classes return uncached — no error pinning, no unauthorized snapshot ever served"
    - "Oldest-first single-entry FIFO eviction at cap (deque order beside the map) — never a wholesale clear"

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/ValidatorRegistry.hpp
    - src/blockchain/impl/CMakeLists.txt
    - src/account/GeniusNode.cpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - test/src/multiaccount/multi_account_sync.cpp

key-decisions:
  - "Grandfather probe reads db_->Get on GetExpectedCertificateSlotKey inside ValidateCertificate before the registry load; a non-NOT_FOUND probe error is Stalled (environmental, retryable, never memoized) so a store hiccup cannot flip finality re-validation"
  - "D-03 deadline-reject is memoization-safe by construction: BoundedDeferTracker deadlines are first-seen anchored, so once expired always expired — stated in-code beside the Pitfall-1 note that dependency-missing Stalled is never memoized"
  - "The defer tracker member is mutable on the const ValidateCertificate path; it records first-seen deadlines only, never a verdict (mechanism/policy split from 21-03 preserved)"
  - "Task 1 TDD behavior cases live in consensus_pending_lifecycle_test (fast CRDTFixture seam matched by the plan's own ctest -R consensus gate); Task 3 ships the real-socket multi-node pairs the plan specifies"
  - "TRUST-01 closes here: every Consensus.cpp registry load (ValidateCertificate strict path, LoadRegistryByCidCached, SlotWinnerUnwinnableLocked, TallyVotes, HandleProposal) and both test callers now consume the verified loader; the raw path is reachable only via the explicitly-UNTRUSTED LoadUnverifiedRegistryByCid (grandfather + registry verification internals)"

patterns-established:
  - "Multi-node genesis-authority pin: fixtures that finalize certificates must pin Blockchain::SetAuthorizedFullNodeAddress to their registry signer (per network) — the strict walk roots at the authority, exactly like multi_account_sync / policy_lifetime already do"

requirements-completed: [TRUST-01]

# Metrics
duration: 68min
completed: 2026-10-20
---

# Phase 21 Plan 04: Consensus-Side Verified Registry Consumption Summary

**ValidateCertificate now roots every non-final certificate's registry load in the genesis-authorized chain (forged/self-certified CID → rule-named Reject with no durable record) while durable-final certificates re-validate unchanged through the D-01 grandfather probe, missing dependencies stay retryable under a config-wired D-03 deadline, and the ConsensusManager CID cache plus every remaining raw caller serve verified-only data**

## Performance

- **Duration:** 68 min (2026-10-20T21:53:58Z → 2026-10-20T23:02:12Z)
- **Tasks:** 3 (TDD: RED → GREEN for Task 1; Tasks 2 and 3 shipped with their verify gates; plus three behavior-neutral format-normalization prerequisite commits)
- **Files modified:** 9

## Accomplishments

- **Grandfather probe + classified verdicts + D-03 deadline (Task 1, TDD):** `ValidateCertificate` probes the canonical `/cert/<slot>` record with the write path's own key derivation before loading the registry. Record present → grandfathered: `LoadUnverifiedRegistryByCid` with today's Stalled-on-error semantics (D-01; records only exist post-Approve, so pre-seeding grandfathering is impossible — Pitfall 4). Record absent → strict: `LoadVerifiedRegistryByCid`; `permission_denied` → error log naming "registry authorization failed" → Reject; `resource_unavailable_try_again` → `BoundedDeferTracker` keyed by registry CID; an expired first-seen deadline → deadline-named log → deterministic Reject (memoizable, unlike the dependency-missing Stalled which stays never-memoized — the :1383-1387 discipline is untouched). `SetAuthorizationDeferWindow`/`kDefaultAuthorizationDeferWindow{30}` on ConsensusManager, applied by GeniusNode from `authorization_defer_window_sec` right after `Blockchain::New` through the new public `Blockchain::GetConsensusManager()` accessor.
- **Verified-only cache + migration + alias removal (Task 2):** `LoadRegistryByCidCached` fills exclusively from the verified loader; both failure classes return uncached (no error pinning, no unauthorized data cached). Cap 8 → 64 with a deque-tracked oldest-first single-entry eviction replacing the wholesale `clear()` (RESEARCH Pitfall 3). The three remaining raw Consensus.cpp sites (`SlotWinnerUnwinnableLocked`, `TallyVotes`, `HandleProposal`) migrated with each caller's existing error handling preserved; test callers in `multi_account_sync` and `multi_node_finality_fault` migrated; the deprecated `LoadRegistryByCid` alias deleted — grep shows zero bare references in src/ + test/.
- **Multi-node adversarial + compat pairs (Task 3):** adversarial — a self-certified registry update raw-put into the DAG (element-filter bypass): its certificate validates Reject, submission fails, and the raw store holds NO durable record bytes; an honest certificate finalizes on the same node (negative control). Compat restart — a finalized certificate re-validates Approve on a restarted peer with the grandfather probe counter incrementing (strict rule provably skipped). Compat catch-up — a fresh node without the registry stalls on strict validation with zero verdict-cache entries (no pinned Reject), then converges to the approved durable record after a validator head-broadcast delivers the missed registry/certificate deltas through real CRDT gossip + graphsync.

## Task Commits

1. **Task 1: Grandfather probe + classified verdict mapping + D-03 deadline (tdd)** — `23fb94649` (test: RED — forged-registry must Reject [today Approves], grandfather twin, defer-deadline must Reject [today Stalled]; memo-discipline guard passes as compat) + `16dafca1a` (feat: GREEN — probe, classified verdicts, tracker, accessor, GeniusNode wiring, fixture authority pin)
2. **Task 2: Verified-only CID cache + caller migration + alias removal** — `fc511e120` (feat: cache fill/eviction/cap, three-site migration, test-caller migration, alias deletion, two Rule 3 fixture fixes)
3. **Task 3: Multi-node adversarial + compat integration pairs** — `e7bdfd622` (test: three real-socket TEST_Fs + probe counter + TestAccess helpers)

**Prerequisite style commits:** `69478e916`, `0ce6d9d05`, `944888aaf` (behavior-neutral clang-format normalization of Consensus.{hpp,cpp}, the two test files, Blockchain.hpp, multi_account_sync.cpp — kept out of the behavioral diffs per the refactor-first discipline).

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/blockchain/Consensus.hpp` — `SetAuthorizationDeferWindow` + `kDefaultAuthorizationDeferWindow`, `mutable base::BoundedDeferTracker authorization_defer_tracker_`, cache contract rewritten to verified-only with `kRegistryCacheMaxEntries{64}` + `registry_cache_order_` deque, `grandfathered_finality_probes` fault-test counter (mutable counters under the existing mutex).
- `src/blockchain/Consensus.cpp` — grandfather probe + classified verdict mapping + defer deadline in ValidateCertificate; setter definition; verified-only cache with oldest-first eviction; three raw-site migrations.
- `src/blockchain/Blockchain.hpp` — public inline `GetConsensusManager()` accessor beside `GetValidatorRegistry`.
- `src/blockchain/ValidatorRegistry.hpp` — deprecated `LoadRegistryByCid` alias deleted; UNTRUSTED loader contract now names the grandfather path as its second consumer.
- `src/blockchain/impl/CMakeLists.txt` — `blockchain_genesis` links `bounded_defer` (Consensus.hpp now includes it).
- `src/account/GeniusNode.cpp` — applies `authorization_defer_window_sec_` to the consensus manager after `Blockchain::New` (beside the 21-03 D-04 cap wiring).
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — four TRUST-01 behavior cases (forged reject + no durable record, missing-ancestor stall with zero memo entries incl. mid-stall durable bytes, grandfather Approve twin, zero-window deadline reject), `VerdictCacheEntryCount` access, forged-CID + attacker-account fixture helpers, signing-registry fix for QuorumCertificateWorkClears.
- `test/src/blockchain/multi_node_finality_fault_test.cpp` — three real-socket TRUST-01 TEST_Fs, `ValidateCertificate`/`VerdictCacheEntryCount`/`GrandfatheredFinalityProbes` accessors, attacker/forged-CID/certificate-builder/record-probe fixture helpers, genesis-authority pin in StartPeer + per-network reset in StartNetwork.
- `test/src/multiaccount/multi_account_sync.cpp` — both `LoadRegistryByCid` calls → `LoadVerifiedRegistryByCid` (pre-sync error / post-sync success semantics preserved — now the hardened contract).

## Decisions Made

- The grandfather probe sits at the very top of the registry-load step (after `ValidateCertificateBinding`, so the slot key is derivable) and treats a non-NOT_FOUND probe error as Stalled — an unreadable store must never flip finality re-validation either way.
- Task 1's TDD seam is `consensus_pending_lifecycle_test` (single-node CRDTFixture, `MakeSigningRegistry`/`MakeCertificateForRegistry` precedent) rather than only the slow real-socket suite; the plan's own Task 1 verify (`ctest -R consensus`) covers it and Task 3 delivers the multi-node proof the plan requires.
- The restart compat case asserts the plan-sanctioned probe counter instead of logs: `grandfathered_finality_probes` increments under `fault_test_mutex_` on every probe hit, giving a deterministic "strict rule never consulted" proof on the fresh post-restart manager.
- The catch-up delivery uses a validator `RequestHeadBroadcast` over its monitored topics after connecting the fresh node: pubsub does not replay old deltas, and head-broadcast + graphsync fetch is the production catch-up path (restarted-publisher recovery relies on the same machinery).
- TRUST-01 marked complete here per 21-01's recorded split (registry-side half then; certificate-path consumption, cache, and multi-node proof now).

## Requirements Status

`requirements: [TRUST-01]` marked COMPLETE: "A registry loaded by CID for certificate vote tallying is authorization-verified — forged-registry self-certification is rejected (#1)". Every certificate-path consumer (ValidateCertificate strict path, the CID cache, the three migrated hot-path sites) plus both test callers now load through the verified loader; the multi-node adversarial pair proves the forged-registry reject with no durable record, and the compat pairs prove finalized history and catch-up survive.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] blockchain_genesis needed the bounded_defer link**
- **Found during:** Task 1 RED build
- **Issue:** Consensus.hpp includes `base/bounded_defer.hpp` (tracker member); `blockchain_genesis` did not link the 21-03 library → unresolved symbols.
- **Fix:** Added `bounded_defer` to `blockchain_genesis`'s PUBLIC link list (src/blockchain/impl/CMakeLists.txt — outside the plan's files_modified; build wiring necessitated by the plan's own member requirement).
- **Committed in:** 23fb94649

**2. [Rule 3 - Blocking] Multi-node fixture registries were not genesis-verifiable**
- **Found during:** Task 1 GREEN suite probe
- **Issue:** The fixture's honest registry update is signed by `network.first`, but every peer's ValidatorRegistry constructed with the shipped `DEFAULT_FULL_NODE_PUB_ADDRESS` as genesis authority → 33 "registry authorization failed" hits and certificate finalization broke suite-wide. The fixture relied on exactly the unverified-load bypass this plan closes.
- **Fix:** StartPeer pins the process-global authorized-full-node address to the first-started peer (the StoreRegistry signer — the same wiring multi_account_sync.cpp:349 and policy_lifetime apply); StartNetwork resets the pin per network so RestartAtVote's later boundary segments re-root at their own signer. Restarted peers keep the original authority through the fixture member.
- **Files modified:** test/src/blockchain/multi_node_finality_fault_test.cpp
- **Verification:** SameBurnContention probe passed with zero authorization failures; full multi-node suite green.
- **Committed in:** 16dafca1a (pin) / fc511e120 (per-network reset after RestartAtVote surfaced the segment case)

**3. [Rule 3 - Blocking] QuorumCertificateWorkClears fixture classified as unauthorized**
- **Found during:** Task 2 verification
- **Issue:** The test's `MakeRegistry` fixture signs its genesis update with `DummySignature` — under the verified-only cache the quorum-reached processing skips (load error) and the NotInRegistry clearing never fires.
- **Fix:** The test now uses `MakeSigningRegistry` (genesis-verifiable); its intent (local node outside the registry → clearing) is unchanged.
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Committed in:** fc511e120

**4. [Plan-sanctioned] Grandfather probe counter added to fault-test counters**
- **Found during:** Task 3 design
- **Issue:** The restart compat case must prove "the strict rule never consulted"; the plan offers "assert via log or probe counter".
- **Fix:** `grandfathered_finality_probes` in FinalityFaultCounters (mutable — ValidateCertificate is const; guarded by the existing fault mutex) + a TestAccess getter.
- **Files modified:** src/blockchain/Consensus.hpp, src/blockchain/Consensus.cpp
- **Committed in:** e7bdfd622

**5. [Rule 3 - Test-shape] RED compile scaffolding on the config surface**
- **Found during:** Task 1 RED authoring
- **Issue:** The deadline test calls `SetAuthorizationDeferWindow`, which did not exist.
- **Fix:** The RED commit carries the setter/tracker/constant plumbing (mechanism configuration on the already-tested 21-03 tracker) with zero ValidateCertificate behavior — 3 tests fail at RED, 1 compat guard passes (21-02's data-surface precedent).
- **Committed in:** 23fb94649

**6. [Change discipline] Three format-normalization prerequisite commits**
- **Found during:** Task 1/Task 2 format passes
- **Issue:** Consensus.{hpp,cpp}, both test files, Blockchain.hpp and multi_account_sync.cpp carried pre-existing clang-format drift; whole-file passes would mix ~2,700 lines of churn into behavioral diffs.
- **Fix:** `69478e916`, `0ce6d9d05`, `944888aaf` carry the behavior-neutral normalizations alone (same discipline as 7a532a05c / 900f4b3cd).
- **Committed in:** see above

---

**Total deviations:** 4 auto-fixed/blocking + 1 plan-sanctioned counter + 1 TDD-shape scaffolding + 1 change-discipline split
**Impact on plan:** All required for compilability, the plan's own suite-green acceptance criteria, or repo change discipline — no scope creep.

## Issues Encountered

- clang-tidy is not on the default PATH in this shell; used `/opt/homebrew/opt/llvm/bin/clang-tidy` with the filtered single-arch DB at /tmp/sgns-tidy-db (21-01..21-05 methodology). Zero findings on all new-code regions (setter, probe/strict block, cache, wiring, test helpers, integration tests); unfiltered runs confirm the tooling works (22k thirdparty-header suppressions).
- The first full verification round surfaced the two fixture issues above (Deviation 2's segment case, Deviation 3) — both were fixture-side consequences of the hardened loads, fixed and re-verified green (10/10 twice).
- `multi_account_test` takes ~313-459 s per run; ran to completion twice (Task 2 tree and final tree) rather than relying on subsets.

## Verification

- `ctest -R "consensus|multi_account|multi_node_finality_fault"` on the final tree — 10/10 passed (5 consensus targets incl. the extended pending-lifecycle 37/37 cases, multi_account_test, policy_lifetime_multi_account_test, multi_node smoke + full fault suite incl. the three new TRUST-01 cases + process-ownership).
- Task 3 gate `ctest -R multi_node_finality_fault` — 3/3 passed; the TRUST-01 trio also verified standalone (ForgedRegistryCid 6.0 s, RestartedFinalCertificate 7.3 s, FreshNodeCatchUp 11.8 s).
- TDD gates: RED commit `23fb94649` precedes GREEN `16dafca1a`; RED failed 3/4 (compat guard pinned existing behavior), GREEN passed 4/4.
- clang-format `--dry-run --Werror` clean on all changed C++ files; clang-tidy zero findings on new-code regions (see Issues Encountered).
- Acceptance greps: `kDefaultAuthorizationDeferWindow` + `SetAuthorizationDeferWindow` in Consensus.hpp with the GeniusNode apply; `GetConsensusManager` in Blockchain.hpp; `LoadVerifiedRegistryByCid` at every former raw site; `grep -rn "LoadRegistryByCid(" src/ test/` returns only `LoadRegistryByCidCached` and the Verified/Unverified uses; memo block at HasAcceptedCertificateForSlot unchanged for Pending/Stalled; `kRegistryCacheMaxEntries == 64` with deque eviction and no `registry_cache_.clear()`.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - the probe, verdicts, deadline, cache, and wiring are all live production paths; no placeholder data.

## Threat Flags

None - all six `mitigate` dispositions implemented and proven (T-21-0401 strict load → Reject with the durable-record absence asserted on the store path; T-21-0402 probe uses write-path key derivation and records only exist post-Approve; T-21-0403 dependency-missing Stalled never memoized with the empty-cache assertion mid-stall; T-21-0404 config-wired deadline → deterministic Reject with the zero-window test; T-21-0405 fresh-node catch-up converges; T-21-0406 logs name the rule and carry no key material). No new security surface beyond the plan's model.

## Next Phase Readiness

- 21-06 consumes `IsValueCarryingLegacyMint` + the 21-03 window for the sync-admission ingress and ships the MINT-03 multi-node twins; the catch-up pattern here (head-broadcast delivery + strict-stall convergence) is the template for its out-of-order sync cases.
- 21-07's enforcement tests can assert against the grandfather counter and the verdict-cache accessor added here.
- TRUST-02's audit should note: the deadline-reject IS memoizable by design (first-seen anchoring), and the catch-up pair demonstrates the Pitfall-1 negative control at the integration level.

## Self-Check: PASSED

All 9 key files exist on disk; all 7 task/style commits (23fb94649, 16dafca1a, fc511e120, e7bdfd622, 69478e916, 0ce6d9d05, 944888aaf) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-20*
