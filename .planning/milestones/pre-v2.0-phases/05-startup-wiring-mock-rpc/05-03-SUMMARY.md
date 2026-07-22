---
phase: 05-startup-wiring-mock-rpc
plan: 03
subsystem: rpc
tags: [mock, rpc, transport, di, dependency-injection]

requires:
  - phase: 05-startup-wiring-mock-rpc
    plan: 02
    provides: MockRpcTransport class, MockBehavior enum, MockEndpointConfig, LoadMockConfig

provides:
  - TransportFactory type alias and SetTransportFactory() DI injection point on PublicChainInputValidator
  - Factory-based transport construction in VerifyPublicChainSmartContract (replaces hard RpcHttpTransport)
  - SGNS_E2E_REAL_RPC=1 env var utility with UseRealRpcTransport() and GetTransportFactory()

affects: [05-startup-wiring-mock-rpc plan 04, GeniusNode InitializeAndStartBridge, test fixtures]

tech-stack:
  added: []
  patterns:
    - "Dependency Injection via std::function factory callable (TransportFactory)"
    - "Fallback pattern: ternary operator providing default RpcHttpTransport factory when transport_factory_ unset"
    - "Mutable member for test-only injection on const methods"

key-files:
  created:
    - test/src/mock/mock_transport_factory.hpp — UseRealRpcTransport(), GetTransportFactory() env var utilities
  modified:
    - src/account/PublicChainInputValidator.hpp — TransportFactory typedef, SetTransportFactory(), transport_factory_ member
    - src/account/PublicChainInputValidator.cpp — Factory-based transport construction in VerifyPublicChainSmartContract

key-decisions:
  - "Forward-declared eth::rpc::JsonRpcTransport in header to avoid transitive include of rpc_receipt_source.hpp"
  - "Default RpcHttpTransport factory defined as inline lambda fallback in .cpp (not header) — avoids exposing implementation details"
  - "Separate mock_transport_factory.hpp header created for env var utilities to prevent circular dependencies with mock_rpc_transport.hpp"
  - "transport_factory_ declared mutable so DI injection works within const VerifyPublicChainSmartContract()"
  - "UseRealRpcTransport() uses static lambda init for thread-safe, one-time env var resolution"

requirements-completed:
  - REQ-MOCK-01
  - REQ-MOCK-03
  - REQ-MOCK-04

duration: 7min
completed: 2026-06-04
---

# Phase 5 Plan 3: TransportFactory DI Wiring Summary

**TransportFactory dependency injection wired into PublicChainInputValidator — production defaults to real RpcHttpTransport, tests inject MockRpcTransport via SetTransportFactory(), runtime switching via SGNS_E2E_REAL_RPC=1 env var. No compile-time flags.**

## Performance

- **Duration:** 7 min
- **Started:** 2026-06-04T17:28:55Z
- **Completed:** 2026-06-04T17:36:21Z
- **Tasks:** 3
- **Files modified:** 4 (2 modified, 1 created, 3 committed)

## Accomplishments

- Added `TransportFactory` typedef (`std::function<std::unique_ptr<JsonRpcTransport>(url, timeout)>`) to PublicChainInputValidator header
- Added `SetTransportFactory()` injection point and `mutable transport_factory_` member for DI-based mock injection
- Replaced hard `RpcHttpTransport transport(ep.url, opts)` construction with `factory(ep.url, kTimeout)` call in VerifyPublicChainSmartContract
- Default factory fallback creates real RpcHttpTransport when `transport_factory_` is unset (production path per D-16)
- Created `mock_transport_factory.hpp` with `UseRealRpcTransport()` and `GetTransportFactory()` for SGNS_E2E_REAL_RPC=1 runtime switching (D-15)
- Zero compile-time `#ifdef` flags for mock switching — fully runtime-configurable

## Task Commits

Each task committed atomically:

1. **Task 1: Add TransportFactory type and injection point** — `a949f66d` (feat)
2. **Task 2: Replace hard RpcHttpTransport construction with factory call** — `8385da99` (feat)
3. **Task 3: Add SGNS_E2E_REAL_RPC=1 env var check** — `b63327d3` (feat)

## Files Created/Modified

- `src/account/PublicChainInputValidator.hpp` — Added `#include <chrono>`, `#include <functional>`, forward declaration of `eth::rpc::JsonRpcTransport`, `TransportFactory` type alias (lines 52-54), `SetTransportFactory()` public method (lines 115-118), `mutable TransportFactory transport_factory_` private member (line 140)
- `src/account/PublicChainInputValidator.cpp` — Added `#include <memory>`, factory resolution with fallback (lines 176-182), replaced `RpcHttpTransport transport(ep.url, opts)` with `auto transport = factory(ep.url, kTimeout)` (line 192), changed to arrow operator `transport->call(request)` (line 204)
- `test/src/mock/mock_transport_factory.hpp` — New header: `UseRealRpcTransport()` with static-cached `std::getenv("SGNS_E2E_REAL_RPC")` check (D-15), `GetTransportFactory()` returning real RpcHttpTransport factory when env=1 or MockRpcTransport factory otherwise (D-14)

## Decisions Made

- Forward-declared `eth::rpc::JsonRpcTransport` in the header instead of including `rpc_receipt_source.hpp` — avoids pulling in heavy receipt source dependencies into every translation unit that includes the validator header
- Default factory defined as inline lambda fallback in `.cpp` — keeps `RpcHttpTransport` construction details out of the public header, header only needs the forward declaration
- Created separate `mock_transport_factory.hpp` rather than adding to `mock_rpc_config.hpp` — avoids circular include risk since `GetTransportFactory()` needs both `PublicChainInputValidator.hpp` (for TransportFactory type) and `mock_rpc_transport.hpp` (for MockRpcTransport), and `mock_rpc_transport.hpp` already includes `mock_rpc_config.hpp`
- `transport_factory_` declared `mutable` so `SetTransportFactory()` (non-const) can modify a member used by `VerifyPublicChainSmartContract()` (const) — standard C++ pattern for caching/instrumentation on const methods

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

TransportFactory DI wiring is complete. Ready for:
- Plan 05-04: Wire `SetTransportFactory()` into test fixtures to inject MockRpcTransport
- GeniusNode `InitializeAndStartBridge()` wiring that sets the default factory on production validators

---

*Phase: 05-startup-wiring-mock-rpc*
*Completed: 2026-06-04*

## Self-Check: PASSED

- ✅ All 4 created/modified files exist on disk
- ✅ All 3 task commits (`a949f66d`, `8385da99`, `b63327d3`) found in git log
- ✅ Build: `sgns_genius_account` target compiles without errors
- ✅ Tests: `mock_rpc_test`, `messaging_watcher_test` all pass
- ✅ Verification: No hard `RpcHttpTransport transport(` construction remains in .cpp
- ✅ Verification: No compile-time `#ifdef` for mock switching in any modified file
