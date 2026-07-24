---
phase: 09-canonical-slot-and-certificate-storage
plan: 16
subsystem: rpc-configuration
tags: [rpc, immutable-snapshot, concurrency, consensus, chainlist]

requires:
  - phase: 09-13
    provides: Endpoint-local weighted receipt status and quorum semantics
provides:
  - Immutable generation-numbered endpoint and transport-factory configurations
  - One-snapshot vote slot hashing and receipt verification
  - Serialized provider/operator merge preserving endpoints and fetched metadata
affects: [phase-10, phase-12, consensus-votes, bridge-validation]

tech-stack:
  added: []
  patterns:
    - Atomic shared_ptr publication with mutex-serialized copy-on-write writers
    - Operation-scoped immutable snapshots retained through blocking RPC decisions
    - Promise-gated concurrency tests without timing sleeps

key-files:
  created: []
  modified:
    - src/account/PublicChainInputValidator.hpp
    - src/account/PublicChainInputValidator.cpp
    - src/account/GeniusNode.cpp
    - test/src/account/public_chain_input_validator_slot_test.cpp
    - test/src/account/public_chain_mint_validation_test.cpp
    - test/src/startup/startup_wiring_test.cpp
    - test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp

key-decisions:
  - "Endpoint maps and transport factories publish together as immutable generation-numbered configurations."
  - "Votes and receipt decisions each capture exactly one configuration and never reload it mid-operation."
  - "Provider AddRpcEndpoints copies the latest operator-published configuration while holding the writer serialization point."

patterns-established:
  - "Snapshot publication: writers lock, copy the current immutable object, update, increment generation, and atomically publish once."
  - "Decision snapshot: consumers retain one shared configuration for the complete vote or weighted receipt decision."

requirements-completed: [SLOT-03, SLOT-04]

duration: 13 min
completed: 2026-07-24
---

# Phase 09 Plan 16: Immutable RPC Configuration Snapshots Summary

**Consensus votes, weighted receipt verification, catch-up URL reads, and delayed provider publication now observe coherent immutable RPC configuration generations.**

## Performance

- **Duration:** 13 min
- **Started:** 2026-07-24T18:19:51Z
- **Completed:** 2026-07-24T18:32:28Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments

- Replaced unsynchronized mutable endpoint and transport-factory state with mutex-serialized copy-on-write configurations published through atomic shared-pointer operations.
- Added a value-returning vote snapshot so `GeniusNode` obtains the chain and all three slot hashes from one generation.
- Retained one endpoint vector and factory through each complete weighted receipt decision, including blocking transports.
- Proved delayed ChainList discovery merges onto the latest operator configuration, deduplicates URLs, and upgrades bridge/topic metadata without torn reads.

## Task Commits

Each task was committed atomically:

1. **Task 1: Publish immutable RPC snapshots and consume one generation per vote** — `c98e90ac` (fix)
2. **Task 2: Hold one snapshot through weighted verification and blocked provider initialization** — `a0cbb444` (test)

## Files Created/Modified

- `src/account/PublicChainInputValidator.hpp` — defines vote/configuration snapshots and the synchronized publication contract.
- `src/account/PublicChainInputValidator.cpp` — implements copy-on-write writers, snapshot readers, one-generation votes, and stable receipt decisions.
- `src/account/GeniusNode.cpp` — populates all signed vote slot hashes from one snapshot call.
- `test/src/account/public_chain_input_validator_slot_test.cpp` — proves concurrent publication cannot create mixed vote tuples.
- `test/src/account/public_chain_mint_validation_test.cpp` — proves a blocked decision retains its old endpoints and factory while later decisions use the new generation.
- `test/src/startup/startup_wiring_test.cpp` — proves blocked provider discovery preserves operator endpoints and coherently merges fetched metadata.
- `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp` — updates friend-level endpoint inspection to use immutable snapshots.

## Decisions Made

- Used the C++17 atomic shared-pointer free functions for wait-free snapshot lifetime ownership while retaining one mutex as the writer serialization point.
- Kept compatibility accessors value-returning and snapshot-based; no public reader exposes references into endpoint storage.
- Preserved endpoint-local weight/status behavior by copying the transport factory and retaining the endpoint snapshot for the entire loop.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Preserved validator move construction**
- **Found during:** Task 2
- **Issue:** Adding a mutex implicitly deleted the move constructor used by existing test helpers that return validators by value.
- **Fix:** Added an explicit locked move constructor that transfers registrations and the published snapshot while resetting the moved-from configuration.
- **Files modified:** `src/account/PublicChainInputValidator.hpp`, `src/account/PublicChainInputValidator.cpp`
- **Verification:** All requested targets compile and the complete mint validation suite passes.
- **Committed in:** `a0cbb444`

**2. [Rule 3 - Blocking] Migrated ChainList friend access to snapshots**
- **Found during:** Task 2
- **Issue:** The ChainList suite's friend accessor referenced the removed mutable endpoint map directly.
- **Fix:** Captured the immutable configuration before inspecting topic metadata or endpoint counts.
- **Files modified:** `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp`
- **Verification:** `bridge_e2e_chainlist_test` builds and passes 12/12.
- **Committed in:** `a0cbb444`

---

**Total deviations:** 2 auto-fixed (2 blocking issues).
**Impact on plan:** Both fixes preserve existing C++ API/test compatibility and enforce the same immutable snapshot boundary; no scope expansion.

## Issues Encountered

None.

## Verification

- Both exact concurrency tests have nonzero list guards and pass.
- `public_chain_mint_validation_test` passes 13/13.
- `public_chain_input_validator_slot_test` passes 7/7.
- `startup_wiring_test` passes 28/28.
- `bridge_e2e_chainlist_test` passes 12/12.
- Required `genius_node_test` library target builds successfully.
- Static audit finds no direct production reader of mutable endpoint or transport-factory state.
- `git diff --check` passes.

## User Setup Required

None.

## Next Phase Readiness

- Phase 09's final RPC publication gap is closed; Phase 10 can rely on coherent slot commitments while adding durable vote locks.
- No execution blocker remains.

## Self-Check: PASSED

- Both task commits are present.
- All seven modified implementation/test artifacts exist.
- Focused list guards, exact tests, complete requested suites, target builds, static audit, and diff checks pass.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-24*
