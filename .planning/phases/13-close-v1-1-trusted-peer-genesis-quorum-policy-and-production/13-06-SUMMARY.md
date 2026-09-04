---
phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
plan: "06"
subsystem: globaldb-networking
tags: [globaldb, crdt, libp2p, gossipsub, graphsync, cpp]

requires:
  - phase: 13-05
    provides: production trusted-peer and BurnConfig candidate activation services
provides:
  - reusable account-independent production GlobalDB networking composition
  - owned and idempotent GlobalDB, scheduler, graphsync, PubSub, and I/O lifecycle
  - existing network-config parsing and caller-supplied production topic reuse
affects: [13-07, 13-09, sgns-trust, trust-first-boot-e2e]

tech-stack:
  added: []
  patterns:
    - stopped factory plus explicit Start/Stop ownership boundary
    - reverse dependency teardown without holding the public state mutex

key-files:
  created:
    - src/crdt/globaldb/GlobalDbNetworkComposition.hpp
    - src/crdt/globaldb/GlobalDbNetworkComposition.cpp
  modified:
    - src/crdt/globaldb/CMakeLists.txt
    - example/crdt_globaldb/CMakeLists.txt

key-decisions:
  - "GlobalDbNetworkComposition validates configuration in Create but defers all network and datastore side effects to Start."
  - "The transport keypair remains an internal database-path resource; callers provide no account or private key."
  - "Listen and broadcast topics are mandatory caller inputs so the composition reuses the production CRDT channel without defining another topic."

patterns-established:
  - "Composition teardown moves owned resources out under lock, then quiesces GlobalDB and joins I/O without deadlocking db() callers."
  - "Production GossipSub settings and network_config.json fields are reused by narrow local tools without constructing GeniusNode."

requirements-completed: [BOOT-03, SCRDT-04, TPR-01, TEST-01]

duration: 8min
completed: 2026-08-12
---

# Phase 13 Plan 06: Reusable GlobalDB Networking Composition Summary

**Account-independent GlobalDB composition now owns the production GossipSub, graphsync, scheduler, datastore, topic, and shutdown lifecycle needed by one-shot trust tooling and first-boot E2E.**

## Performance

- **Duration:** 8 min
- **Started:** 2026-08-12T15:45:16Z
- **Completed:** 2026-08-12T15:53:00Z
- **Tasks:** 1
- **Files modified:** 4

## Accomplishments

- Added `GlobalDbNetworkComposition::Create`, `Start`, `Stop`, and `db` as a narrow production-compatible GlobalDB lifecycle without GeniusNode or account ownership.
- Reused production GossipSub settings plus `pubsub_port`, bind address, bootstrap peers, and connection watermarks from `network_config.json`.
- Kept transport identity internal beneath the composition database path and required callers to supply the existing listen/broadcast topic instead of adding a new channel.
- Added reverse-order, idempotent teardown that quiesces GlobalDB before releasing I/O, graphsync, scheduler, and PubSub resources.

## Task Commits

Each task was committed atomically:

1. **Task 1: Extract reusable minimal GlobalDB networking composition** - `876f2f50` (feat)

## Files Created/Modified

- `src/crdt/globaldb/GlobalDbNetworkComposition.hpp` - Public account-independent composition contract and owned lifecycle state.
- `src/crdt/globaldb/GlobalDbNetworkComposition.cpp` - Network-config parsing, production transport construction, GlobalDB startup, and dependency-safe teardown.
- `src/crdt/globaldb/CMakeLists.txt` - Builds the composition and links the existing RapidJSON header target.
- `example/crdt_globaldb/CMakeLists.txt` - Preserves `crdt_globaldb_app` and exposes the plan's `globaldb_app` verification alias.

## Decisions Made

- Kept `Create` side-effect free after validation so callers can construct, inspect, and explicitly start the owned network boundary.
- Persisted only the libp2p transport identity under the database path, matching the existing GlobalDB app pattern while keeping account/private-key inputs out of `Config`.
- Used caller-supplied topics as a forcing function against accidental creation of a parallel trust channel.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added the literal verification target alias**
- **Found during:** Task 1 verification
- **Issue:** The plan's required command names `globaldb_app`, while the repository's existing executable target is `crdt_globaldb_app`, causing the otherwise successful build gate to exit nonzero.
- **Fix:** Added a dependency-only `globaldb_app` custom target that builds the unchanged `crdt_globaldb_app` executable.
- **Files modified:** `example/crdt_globaldb/CMakeLists.txt`
- **Verification:** `cmake --build build/OSX/Release --target crdt_globaldb globaldb_app securecrdt -j8` exits 0.
- **Committed in:** `876f2f50`

---

**Total deviations:** 1 auto-fixed (1 blocking issue).
**Impact on plan:** The compatibility target only makes the specified verification name resolve; it does not rename the executable or alter runtime behavior.

## Issues Encountered

- `crdt_globaldb` did not previously consume RapidJSON directly; linking the repository's existing `rapidjson` header target completed the intended CMake integration without adding a package.

## Verification

- `cmake --build build/OSX/Release --target crdt_globaldb globaldb_app securecrdt -j8` - PASS.
- Public API symbol scan confirms `GlobalDbNetworkComposition::Create`, `Start`, `Stop`, and `db` are defined - PASS.
- Dependency/topic scan confirms no GeniusNode/account include, private-key input, or new topic constant - PASS.
- `git diff --check` - PASS.

## Known Stubs

None.

## User Setup Required

None - no external service configuration or package installation required.

## Next Phase Readiness

- Plan 13-09 can construct `sgns-trust` around the composition without passing an account or ephemeral bootstrap key into normal node startup.
- Plan 13-07 can run a second in-process composition against the same configured CRDT topic for the production first-boot E2E.

## Self-Check: PASSED

- Both new composition files exist and the `876f2f50` task commit is present in repository history.
- All four plan requirements are represented in frontmatter and all acceptance/build criteria pass.
- No tracked files were deleted and the two unrelated pre-existing untracked paths remain untouched.
- Stub scan found no placeholder/TODO implementation paths.
- Threat-surface scan found only the planned network-config, datastore, and production CRDT transport boundaries; no unplanned endpoint, authentication path, package, or topic was introduced.

---
*Phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production*
*Completed: 2026-08-12*
