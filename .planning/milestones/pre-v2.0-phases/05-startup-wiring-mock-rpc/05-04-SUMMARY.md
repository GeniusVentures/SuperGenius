---
phase: 05-startup-wiring-mock-rpc
plan: 04
subsystem: utxo
tags: [utxo, bridge, enum, lifecycle]

# Dependency graph
requires:
  - phase: 05-startup-wiring-mock-rpc
    provides: CONTEXT decisions D-17, D-18, D-19 for UTXO subsystem changes
provides:
  - UTXO_RESERVED lifecycle state between READY and CONSUMED
  - UTXOType enum distinguishing UTXO_NORMAL from UTXO_BRIDGE
  - IsOutPointReserved() predicate for consensus voting
  - Removed 8 foreign-address guards enabling cross-node UTXO tracking
  - PutUTXO() accepts optional UTXOType parameter for bridge UTXO insertion
affects: [05-startup-wiring-mock-rpc, bridge-relayer, consensus, catch-up-scan]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "UTXO lifecycle: READY → RESERVED → CONSUMED for burn UTXOs (D-18)"
    - "enum class UTXOType : uint8_t with explicit serialization-stable values (D-19)"
    - "Default member initializer for backward-compatible aggregate init extension"

key-files:
  created: []
  modified:
    - src/account/UTXOManager.hpp — UTXOState + UTXO_RESERVED, UTXOType enum, UTXOEntry.type field, IsOutPointReserved declaration, PutUTXO type parameter
    - src/account/UTXOManager.cpp — 8 guard removals, IsOutPointReserved impl, RESERVED proto comment, PutUTXO type storage

key-decisions:
  - "UTXOType field placed last in UTXOEntry for backward-compatible aggregate initialization"
  - "UTXO_RESERVED serialized as UTXO_READY in protobuf — catch-up scan re-detects on deserialization"
  - "is_full_node_ field kept despite -Wunused-private-field warning — other code may reference it"

patterns-established:
  - "UTXOType::UTXO_BRIDGE stored per-entry; defaults UTXO_NORMAL for existing UTXOs"

requirements-completed:
  - REQ-UTXO-01
  - REQ-UTXO-02
  - REQ-UTXO-03

# Metrics
duration: ~25min
completed: 2026-06-04
---

# Phase 5 Plan 4: UTXO Subsystem Changes for Bridge Burn Tracking Summary

**Extended UTXOState with RESERVED lifecycle, added UTXOType enum for bridge UTXO classification, removed 8 foreign-address guards per D-17/D-18/D-19**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-06-04T17:22:00Z
- **Completed:** 2026-06-04T17:47:00Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Added UTXO_RESERVED state between UTXO_READY and UTXO_CONSUMED for burn UTXO consensus lifecycle
- Added UTXOType enum (UTXO_NORMAL=0, UTXO_BRIDGE=1) with UTXOEntry.type field (D-19)
- Removed all 8 foreign-address guards — all nodes now store/manage UTXOs for all peers (D-17)
- Implemented IsOutPointReserved() predicate for burn UTXO state detection
- Updated PutUTXO() to accept optional UTXOType parameter (default UTXO_NORMAL) for bridge callers

## Task Commits

Each task was committed atomically:

1. **Task 1: Add UTXO_RESERVED state and UTXOType enum to header** - `a6d0a11d` (feat)
2. **Task 2: Remove 8 guards and implement RESERVED state handling** - `66104170` (feat)

## Files Created/Modified
- `src/account/UTXOManager.hpp` — UTXOState extended (UTXO_RESERVED between READY/CONSUMED), UTXOType enum class (UTXO_NORMAL=0, UTXO_BRIDGE=1), UTXOEntry.type field as last member, IsOutPointReserved() declaration, PutUTXO() with optional UTXOType parameter
- `src/account/UTXOManager.cpp` — All 8 `!is_full_node_ && address != address_` guards removed from GetBalance (2×), PutUTXO, DeleteUTXO, SetUTXOs, ComputeUTXOMerkleRoot, CreateCheckpoint, LoadLatestCheckpoint; IsOutPointReserved() implementation; RESERVED serialization comment in ToProtoState; PutUTXO stores UTXOType in entry

## Decisions Made
- UTXOType placed as LAST field in UTXOEntry — avoids breaking all existing aggregate initializers (ConsumeUTXOs and SetUTXOs use 5-field aggregate init; 6th field defaults to UTXO_NORMAL)
- PutUTXO switched from aggregate init to named member assignment for type safety when storing the UTXOType parameter
- UTXO_RESERVED has no protobuf equivalent — serialized as UTXO_READY, catch-up scan re-detects on deserialization
- `is_full_node_` member kept despite unused warning — removal is a separate concern from guard removal

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered
- Build environment not configured for `build/` or `build/local` directories — used `build/OSX/Debug` which was pre-configured
- `is_full_node_` now generates `-Wunused-private-field` warning after guard removal (expected; field kept for future use)

## Known Stubs
- UTXOType is not serialized to protobuf (`SGTransaction::UTXOEntryRecord`) — all UTXOs default to UTXO_NORMAL on load. Bridge UTXOs will be re-marked by the catch-up scan (future plan 05-05). This is intentional per D-18/D-19 design.

## Threat Flags

None — all threat surface changes covered by existing plan threat model (T-05-08, T-05-09, T-05-10).

## Next Phase Readiness
- UTXO subsystem ready for bridge UTXO insertion with type tagging
- IsOutPointReserved() predicate available for consensus voting logic
- Guard removal enables cross-node burn UTXO tracking for conflict detection
- Ready for Plan 05-05 (Startup Catch-Up Scan) which uses UTXOType::UTXO_BRIDGE and IsOutPointReserved()

---
*Phase: 05-startup-wiring-mock-rpc*
*Completed: 2026-06-04*
