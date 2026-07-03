# Phase 03: Network Hardening and Operational Readiness - Research

**Researched:** 2026-05-29
**Domain:** C++17 blockchain consensus networking, operational observability
**Confidence:** HIGH

## Summary

Phase 3 hardens the consensus pipeline at the network boundaries — preventing oversized message publication, tolerating distributed clock skew, cleaning up expired tracking state, and exposing operational metrics for monitoring. All four requirements operate within the existing TransactionManager ↔ ConsensusManager architecture and follow established codebase patterns (subject/certificate handler registration, `outcome::result` error propagation, spdlog-based logging with `[address - full] func:` format).

**Primary recommendation:** Implement SIZE-01 as a simple size gate after serialization, TS-01 by exposing the already-existing `SetTimeFrameToleranceMs` through the existing GeniusNode configuration layer, CLEAN-01 by adding a `ProposalCleanupHandler` callback type (mirroring the `SubjectHandler`/`CertificateSubjectHandler` pattern) to ConsensusManager with registration through Blockchain, and METRICS-01 by adding INFO-level lifecycle logs and `std::atomic<uint64_t>` counters logged on shutdown.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Pre-publish size enforcement | API / Backend | — | `SendTransactionItem` is a local client API path; rejection happens before PubSub |
| Timestamp tolerance configuration | API / Backend | — | `CheckTransactionTimestamp` is a backend validation function; config flows through GeniusNode → TransactionManager |
| Tracking entry cleanup on timeout | API / Backend | — | ConsensusManager owns proposal lifecycle; TransactionManager owns `tx_processed_m`; callback bridges the two |
| Operational metrics | API / Backend | — | Metrics are local counters + logs within TransactionManager; no external service dependency |

## User Constraints (from CONTEXT.md)

### Locked Decisions
- **SIZE-01 (D-01):** Pre-publish enforcement at `SendTransactionItem`, immediately after `SerializeByteVector()` at line 1177
- **SIZE-01 (D-02):** Threshold: 64 KB, matching `MAX_EMBEDDED_TX_BYTES` — defense-in-depth with existing handler check
- **SIZE-01 (D-03):** Error behavior: `outcome::failure` with descriptive error message — no new error code
- **TS-01 (D-04):** Make tolerance window configurable, replace hardcoded 5-minute default in `CheckTransactionTimestamp`
- **TS-01 (D-05):** Default value: ±5 minutes (preserve current behavior)
- **TS-01 (D-06):** Config key design follows existing codebase patterns
- **CLEAN-01 (D-07):** Callback registration mechanism on ConsensusManager/Blockchain; TransactionManager registers handler
- **CLEAN-01 (D-08):** Callback fires from timeout callers of `ClearProposalSlot` (lines 1392 and 1476 in `Consensus.cpp`) — NOT from certificate caller (line 1912)
- **CLEAN-01 (D-09):** Handler receives `tx_hash`, calls `ChangeTransactionState(tx, FAILED)` for VERIFYING entries matching expired proposal's subject hash
- **CLEAN-01 (D-10):** Use `GetTransactionByHash(tx_hash)`; if found and VERIFYING → FAILED; if not found, skip silently
- **CLEAN-01 (D-11):** `ClearProposalSlot` function at line 1984 is unchanged; callback is called by its callers
- **METRICS-01 (D-12):** Use existing `TransactionManagerLogger()` (spdlog-based)
- **METRICS-01 (D-13):** Log lifecycle events at `info` level: certificate fallback deserialization, proposal validation result (Approve/Reject with reason), temp VERIFYING entry creation/promotion/FAILED
- **METRICS-01 (D-14):** Counters: simple atomic counters for vote counts and validation breakdown, logged periodically or on shutdown; no external metrics system

### the agent's Discretion
- SPECIFIC config key name and location for timestamp tolerance (follow existing codebase patterns)
- Callback registration API design (follow `RegisterSubjectHandler`/`RegisterCertificateHandler` patterns)
- Metrics counter implementation (atomic integers, log format, flush interval)
- Exact log message format (follow existing `[address - full] func: msg` pattern)

### Deferred Ideas (OUT OF SCOPE)
None.

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SIZE-01 | PubSub message size enforcement — pre-publish check at `SendTransactionItem` line 1177, 64KB threshold | § SIZE-01 Pattern |
| TS-01 | Configurable timestamp tolerance window, default ±5min | § TS-01 Pattern |
| CLEAN-01 | Tracking entry cleanup via callback on proposal timeout → `ChangeTransactionState(FAILED)` | § CLEAN-01 Pattern |
| METRICS-01 | Operational metrics via `TransactionManagerLogger()` + atomic counters | § METRICS-01 Pattern |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C++17 | — | Language standard | Project constraint (per CONVENTIONS.md) |
| Protobuf | (existing) | `NonceSubject`, `ConsensusProposal` message types | Already in use for all consensus serialization |
| spdlog | v1.4.2 | Structured logging via `TransactionManagerLogger()` | Existing project standard (STACK.md) |
| Boost.Asio | 1.85.0 | Timer for periodic metrics flush (if needed) | Already wired in TransactionManager construction |
| `std::atomic<uint64_t>` | — | Thread-safe counters | Zero-dependency, standard library |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `std::function` | — | Callback type erasure for cleanup handler | For `ProposalCleanupHandler` typedef |
| `std::shared_mutex` | — | Thread-safe handler map access | Follows existing pattern in ConsensusManager |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Callback pattern in ConsensusManager | Direct coupling (TransactionManager holds raw ConsensusManager pointer) | Violates established handler registration pattern; ConsensusManager already has the infrastructure |
| External metrics service (Prometheus, etc.) | — | Overkill; D-14 explicitly says no external system |
| Program options for timestamp tolerance | Environment variable | Program options is the existing pattern for node configuration; env vars unused in this subsystem |

**Installation:** No new external packages required. All dependencies are existing project libraries.

## Package Legitimacy Audit

> No external packages are installed in this phase. All changes are source modifications to existing C++ files using existing third-party libraries (Boost, Protobuf, spdlog). The audit requirement is satisfied by confirmation of zero new external dependencies.

| Package | Registry | Age | Downloads | Source Repo | slopcheck | Disposition |
|---------|----------|-----|-----------|-------------|-----------|-------------|
| *(none)* | — | — | — | — | — | No new packages |

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

## Architecture Patterns

### System Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                       SIZE-01: Pre-Publish Gate                       │
│                                                                       │
│  SendTransactionItem()                                                │
│    │                                                                  │
│    ├─ SerializeByteVector() ──► serialized_tx (bytes)                │
│    │                                                                  │
│    ├─ [NEW] if serialized_tx.size() > 64KB:                          │
│    │         return outcome::failure("oversized")                     │
│    │                                                                  │
│    └─ CreateConsensusProposal() ──► SubmitProposal() ──► PubSub      │
│                                                                       │
│  (existing defense-in-depth: HandleNonceConsensusSubject checks       │
│   MAX_EMBEDDED_TX_BYTES at line 3734)                                │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│               TS-01: Configurable Timestamp Tolerance                 │
│                                                                       │
│  GeniusNode config layer (program_options)                            │
│    │                                                                  │
│    └─ ConfigureTransactionFilterTimeoutsMs(tolerance_ms, window_ms)  │
│         │                                                             │
│         └─ TransactionManager::SetTimeFrameToleranceMs(ms)           │
│              │                                                        │
│              └─ timestamp_tolerance_m = chrono::milliseconds(ms)     │
│                   │                                                   │
│                   └─ CheckTransactionTimestamp() reads tolerance_ms   │
│                      compares against elapsed time                    │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│            CLEAN-01: Proposal Timeout Cleanup Callback                │
│                                                                       │
│  ConsensusManager::ProcessCertificates()  (periodic timer)           │
│    │                                                                  │
│    ├─ proposal already certified? ──► ClearProposalSlot() (1392)     │
│    │                                    │                             │
│    │                                    └─ [NEW] FireCleanupCallback()│
│    │                                                                  │
│    └─ certificate created ──► ClearProposalSlot() (1476)             │
│                                │                                      │
│                                └─ [NEW] FireCleanupCallback()        │
│                                                                       │
│  FireCleanupCallback(proposal):                                      │
│    1. DecodeNonceSubject(subject) → tx_hash                          │
│    2. For each registered ProposalCleanupHandler:                    │
│         handler(tx_hash)                                             │
│                                                                       │
│  TransactionManager::OnProposalTimeoutCleanup(tx_hash):              │
│    1. tx = GetTransactionByHash(tx_hash)                             │
│    2. if tx && status == VERIFYING:                                  │
│         ChangeTransactionState(tx, FAILED)                           │
│                                                                       │
│  (NOT called from certificate path at line 1912 — confirmed entries  │
│   are already handled by Phase 1/2 certificate logic)                │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│            METRICS-01: Operational Metrics                            │
│                                                                       │
│  TransactionManager atomic counters:                                  │
│    ┌─────────────────────────────────┬──────────────────────────┐    │
│    │ cert_fallback_success_          │ std::atomic<uint64_t>    │    │
│    │ cert_fallback_failure_          │ std::atomic<uint64_t>    │    │
│    │ validation_approve_             │ std::atomic<uint64_t>    │    │
│    │ validation_reject_              │ std::atomic<uint64_t>    │    │
│    │ tracking_verify_insert_         │ std::atomic<uint64_t>    │    │
│    │ tracking_confirm_promote_       │ std::atomic<uint64_t>    │    │
│    │ tracking_fail_transition_       │ std::atomic<uint64_t>    │    │
│    └─────────────────────────────────┴──────────────────────────┘    │
│                                                                       │
│  Lifecycle logs (INFO level):                                         │
│    - OnConsensusCertificate: fallback deser success/failure          │
│    - HandleNonceConsensusSubject: Approve/Reject with reason          │
│    - ChangeTransactionState: temp entry created/promoted/failed      │
│                                                                       │
│  Flush: On ~TransactionManager() destructor (line 267)               │
└──────────────────────────────────────────────────────────────────────┘
```

### Recommended Project Structure
```
src/
├── account/
│   ├── TransactionManager.hpp      # Add: ProposalCleanupHandler method decl, atomic counters
│   ├── TransactionManager.cpp      # Add: SIZE-01 gate, CLEAN-01 handler, METRICS-01 counters+logs
│   └── GeniusNode.cpp              # Add: TS-01 program-option → SetTimeFrameToleranceMs wiring
├── blockchain/
│   ├── Consensus.hpp               # Add: ProposalCleanupHandler typedef, Register/Unregister + private members
│   ├── Consensus.cpp               # Add: FireCleanupCallback calls at 1392/1476, Register/Unregister impl
│   └── Blockchain.hpp              # Add: RegisterProposalCleanupHandler delegation method
├── blockchain/impl/
│   └── Blockchain.cpp              # Add: delegation impl forwarding to consensus_manager_
```

### Pattern 1: Callback Registration (Handler Pattern)
**What:** ConsensusManager exposes typed callback registration for external subscribers. The pattern is: define a `using` handler type → `Register`/`Unregister` methods → `std::unordered_map` keyed by type hash → dispatch in relevant code path.

**When to use:** CLEAN-01 — add `ProposalCleanupHandler` to ConsensusManager following the established `SubjectHandler`/`CertificateSubjectHandler` pattern.

**Example — existing pattern (CertificateSubjectHandler):**
```cpp
// From Consensus.hpp lines 103-105, 135-136
/// Source: src/blockchain/Consensus.hpp
using CertificateSubjectHandler =
    std::function<outcome::result<Check>( const std::string &subject_hash, const Certificate &certificate )>;
bool RegisterCertificateHandler( std::string_view subject_type, CertificateSubjectHandler handler );
```

**Planned extension — ProposalCleanupHandler:**
```cpp
// Conceptual — follows same pattern but simpler (fire-and-forget, no return value)
using ProposalCleanupHandler = std::function<void( const std::string &tx_hash )>;
bool RegisterProposalCleanupHandler( std::string_view subject_type, ProposalCleanupHandler handler );
```

**Key difference from SubjectHandler/CertificateHandler:** Cleanup is fire-and-forget (returns `void`), not a validation. Multiple handlers can be registered for the same subject type (use `std::vector<ProposalCleanupHandler>` or a multicast pattern). The handler map should be keyed by subject type hash to match the same type-based dispatch as existing handlers.

### Pattern 2: Config Flow (SetTimeFrameToleranceMs)
**What:** Configuration flows from GeniusNode's external-facing API → TransactionManager's internal setter → member variable read by validation functions.

**When to use:** TS-01 — the config flow already exists; the task is to connect it to a program-option or environment configuration at the GeniusNode layer.

**Existing pattern:**
```cpp
// Source: src/account/GeniusNode.cpp line 1832-1843
void GeniusNode::ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms )
{
    auto manager_result = GetTransactionManager();
    if ( !manager_result.has_value() )
    {
        node_logger_->error( "{}: Transactions not ready", __func__ );
        return;
    }
    auto manager = manager_result.value();
    manager->SetTimeFrameToleranceMs( timeframe_limit_ms );
    manager->SetMutabilityWindowMs( mutability_window_ms );
}
```

### Pattern 3: Logging with TransactionManagerLogger()
**What:** All logs use the `[address - full] func: msg` format via `TransactionManagerLogger()` (spdlog-based, created via `base::createLogger("TransactionManager")`).

**When to use:** METRICS-01 — all new lifecycle logs follow this pattern.

**Existing pattern:**
```cpp
// Source: src/account/TransactionManager.cpp line 82-87
base::Logger TransactionManagerLogger()
{
    return base::createLogger( "TransactionManager" );
}

// Usage example (line 332):
TransactionManagerLogger()->info( "[{} - full: {}] Adding broadcast to full node on {}",
    account_m->GetAddress().substr( 0, 8 ), full_node_m, ... );
```

### Anti-Patterns to Avoid
- **Direct coupling between TransactionManager and ConsensusManager internals:** Do NOT have TransactionManager directly call `ClearProposalSlot` or access `proposals_`. Always use the callback registration pattern.
- **Calling cleanup from the certificate path (line 1912):** Per D-08, the certificate path already handles CONFIRMED promotion. Cleanup only fires from timeout callers (lines 1392, 1476).
- **New error codes for SIZE-01:** Per D-03, use `outcome::failure` with a descriptive string message — no new enum value needed.
- **External metrics integration:** Per D-14, no Prometheus, StatsD, or other external system. Simple atomic counters logged to spdlog.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Thread-safe counters | Custom mutex-guarded integers | `std::atomic<uint64_t>` | Standard library, lock-free, sufficient for monotonic counters |
| Periodic logging flush | Custom timer thread | `~TransactionManager()` destructor (already at line 267) or `boost::asio::steady_timer` from existing `ctx_` | Already have an io_context; destructor path is simplest |
| Callback type erasure | Custom callback infrastructure | `std::function<void(const std::string&)>` | Follows existing SubjectHandler pattern; no need for custom abstraction |

**Key insight:** ConsensusManager already has the full handler registration infrastructure (type hash computation, mutex-guarded maps, registration/unregistration methods). CLEAN-01 adds a third handler type to this existing framework — it does not require building a new callback mechanism.

## Common Pitfalls

### Pitfall 1: Cleanup callback firing from certificate path (line 1912)
**What goes wrong:** If the cleanup callback fires from the certificate caller at line 1912, it would transition entries to FAILED that were just confirmed. The certificate handler already promotes VERIFYING → CONFIRMED through `OnConsensusCertificate` (Phase 2).
**Why it happens:** `ClearProposalSlot` is called from three places (1392, 1476, 1912). All three call it, but only the timeout callers should trigger cleanup.
**How to avoid:** The callback invocation lives in the caller code (lines 1392, 1476), NOT inside `ClearProposalSlot` itself. Per D-08, the callback is explicitly not called from line 1912.
**Warning signs:** Standalone validators see entries transitioned to FAILED shortly after certificate arrival.

### Pitfall 2: SIZE-01 threshold mismatch with handler check
**What goes wrong:** The pre-publish threshold and the handler's `MAX_EMBEDDED_TX_BYTES` diverge, creating a gap where PubSub silently drops messages but the handler thinks they're valid.
**Why it happens:** Two separate constants maintained independently.
**How to avoid:** Use the SAME constant or reference — either reuse `MAX_EMBEDDED_TX_BYTES` (64 * 1024) directly or define a shared constant. Per D-02, explicitly match `MAX_EMBEDDED_TX_BYTES`.
**Warning signs:** Proposals rejected by a subset of validators after PubSub delivery.

### Pitfall 3: Thread safety of cleanup callback map
**What goes wrong:** The cleanup handler map is accessed from the `ProcessCertificates` timer thread while a handler is being registered/unregistered from the main thread.
**Why it happens:** `ProcessCertificates` runs on a background timer; handler registration happens during startup.
**How to avoid:** Use `std::shared_mutex` (same as `subject_handlers_mutex_` and `certificate_handlers_mutex_`). Take a shared lock during dispatch, unique lock during registration. Copy the handler vector under the lock, then invoke outside the lock.
**Warning signs:** Rare crashes or data races under TSAN.

### Pitfall 4: GetTransactionByHash iterates entire tx_processed_m
**What goes wrong:** `GetTransactionByHash` (line 2654-2668) is O(n) — it loops through all entries comparing hashes. For CLEAN-01 this is acceptable (called infrequently on timeout), but calling it from a hot path would be problematic.
**Why it happens:** The map is keyed by transaction path, not hash.
**How to avoid:** CLEAN-01 calls `GetTransactionByHash` only on proposal timeout (rare event). Acceptable as-is. If metrics were to query it frequently, a separate hash→tracked_tx index would be needed, but that's out of scope.
**Warning signs:** Not applicable — timeout callbacks are infrequent.

## Code Examples

Verified patterns from official sources:

### SIZE-01: Pre-publish size gate
```cpp
// Insertion point: after serialized_tx declaration at line 1177
// Source: src/account/TransactionManager.cpp
auto serialized_tx = transaction->SerializeByteVector();

// [NEW] Pre-publish size enforcement
static constexpr size_t MAX_PUBSUB_TX_BYTES = 64 * 1024;  // matches MAX_EMBEDDED_TX_BYTES
if ( serialized_tx.size() > MAX_PUBSUB_TX_BYTES )
{
    TransactionManagerLogger()->error(
        "[{} - full: {}] {}: Transaction exceeds PubSub size limit tx={} size={} max={}",
        account_m->GetAddress().substr( 0, 8 ),
        full_node_m,
        __func__,
        transaction->GetHash(),
        serialized_tx.size(),
        MAX_PUBSUB_TX_BYTES );
    return outcome::failure( std::errc::message_size );
}
```

### CLEAN-01: ProposalCleanupHandler type and registration
```cpp
// Add to Consensus.hpp (after CertificateSubjectHandler, line 105)
// Source: src/blockchain/Consensus.hpp pattern
using ProposalCleanupHandler = std::function<void( const std::string &tx_hash )>;
bool RegisterProposalCleanupHandler( std::string_view subject_type, ProposalCleanupHandler handler );
void UnregisterProposalCleanupHandler( std::string_view subject_type );

// Private member (alongside certificate_subject_handlers_ at line 669):
std::unordered_map<std::string, std::vector<ProposalCleanupHandler>>
    proposal_cleanup_handlers_;          ///< Proposal cleanup handlers by subject type hash.
mutable std::shared_mutex cleanup_handlers_mutex_;  ///< Guards proposal_cleanup_handlers_.
```

### CLEAN-01: FireCleanupCallback dispatch
```cpp
// Called from lines 1392 and 1476 before ClearProposalSlot
// Source pattern: HandleCertificate dispatch at lines 1599-1611
void ConsensusManager::FireProposalCleanupCallbacks( const Proposal &proposal )
{
    auto subject_hash = GetSubjectHash( proposal.subject() );
    if ( subject_hash.has_error() ) return;

    auto nonce_payload = DecodeNonceSubject( proposal.subject() );
    if ( nonce_payload.has_error() ) return;
    auto tx_hash = nonce_payload.value().tx_hash();
    if ( tx_hash.empty() ) return;

    std::vector<ProposalCleanupHandler> handlers_copy;
    {
        std::shared_lock lock( cleanup_handlers_mutex_ );
        auto it = proposal_cleanup_handlers_.find(
            proposal.subject().subject_type_hash().hash() );
        if ( it != proposal_cleanup_handlers_.end() )
        {
            handlers_copy = it->second;
        }
    }
    for ( auto &handler : handlers_copy )
    {
        handler( tx_hash );
    }
}
```

### CLEAN-01: TransactionManager cleanup handler
```cpp
// Member function registered via blockchain_->RegisterProposalCleanupHandler()
void TransactionManager::OnProposalTimeoutCleanup( const std::string &tx_hash )
{
    auto tx = GetTransactionByHash( tx_hash );
    if ( !tx )
    {
        return; // Already cleaned or CRDT-sourced — skip silently per D-10
    }

    std::shared_lock tx_lock( tx_mutex_m );
    const auto key = GetTransactionPath( *tx );
    auto it = tx_processed_m.find( key );
    if ( it != tx_processed_m.end() && it->second.status == TransactionStatus::VERIFYING )
    {
        tx_lock.unlock(); // ChangeTransactionState acquires its own lock
        TransactionManagerLogger()->info(
            "[{} - full: {}] {}: Proposal timeout — transitioning temp entry to FAILED tx={}",
            account_m->GetAddress().substr( 0, 8 ),
            full_node_m,
            __func__,
            tx_hash );
        (void)ChangeTransactionState( tx, TransactionStatus::FAILED );
    }
}
```

### TS-01: Config key recommendation
```cpp
// The existing flow already supports runtime configuration.
// Recommendation: expose via GeniusNode program options or .env.
// 
// GeniusNode already has ConfigureTransactionFilterTimeoutsMs().
// Add a program option like:
//   --timestamp-tolerance-ms=300000
// 
// Or follow the existing pattern in node/cli.cpp where other
// GeniusNode configuration methods are called.
```

### METRICS-01: Atomic counter definitions
```cpp
// Add to TransactionManager.hpp private section
// Follows existing member naming convention (trailing underscore)
std::atomic<uint64_t> metrics_cert_fallback_success_{ 0 };
std::atomic<uint64_t> metrics_cert_fallback_failure_{ 0 };
std::atomic<uint64_t> metrics_validation_approve_{ 0 };
std::atomic<uint64_t> metrics_validation_reject_{ 0 };
std::atomic<uint64_t> metrics_tracking_insert_{ 0 };
std::atomic<uint64_t> metrics_tracking_confirm_{ 0 };
std::atomic<uint64_t> metrics_tracking_fail_{ 0 };
```

### METRICS-01: Flush on shutdown
```cpp
// Add to ~TransactionManager() destructor (after line 267 area)
// Source pattern: existing destructor logging at line 267
TransactionManagerLogger()->info(
    "[{} - full: {}] ~TransactionManager: Metrics — cert_fallback(success={} failure={}) "
    "validation(approve={} reject={}) tracking(insert={} confirm={} fail={})",
    account_m->GetAddress().substr( 0, 8 ),
    full_node_m,
    metrics_cert_fallback_success_.load(),
    metrics_cert_fallback_failure_.load(),
    metrics_validation_approve_.load(),
    metrics_validation_reject_.load(),
    metrics_tracking_insert_.load(),
    metrics_tracking_confirm_.load(),
    metrics_tracking_fail_.load() );
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hardcoded ±5min timestamp tolerance | Configurable via `SetTimeFrameToleranceMs` → program option | Phase 3 | Validators in different regions can widen tolerance |
| No pre-publish size enforcement | 64KB gate before PubSub publish | Phase 3 | Prevents silent PubSub drops of oversized messages |
| Orphaned VERIFYING entries on timeout | Callback → `ChangeTransactionState(FAILED)` | Phase 3 | `tx_processed_m` doesn't leak entries from expired proposals |
| No operational metrics | Atomic counters + lifecycle logging | Phase 3 | Debuggability of standalone validator adoption rate |

**Deprecated/outdated:**
- None — this phase adds new capabilities without removing existing ones.

## Assumptions Log

> All claims in this research are verified against the actual source code (read during this session) — no [ASSUMED] tags needed.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| *(none)* | — | — | — |

**All claims verified against source code:** Source files read include `TransactionManager.cpp` (lines 75-1191, 2640-2669, 2980-2994, 3416-3439, 3723-3739, 4160-4206, 4820-4884), `TransactionManager.hpp` (lines 1-683), `Consensus.cpp` (lines 260-309, 420-449, 1090-1174, 1300-1358, 1361-1482, 1575-1635, 1864-1914, 1970-2050, 2175-2194), `Consensus.hpp` (lines 1-697), `Blockchain.hpp` (lines 130-189), `Blockchain.cpp` (lines 1665-1688), `GeniusNode.cpp` (lines 1832-1843), `Consensus.proto` (lines 55-64).

## Open Questions

1. **TS-01: Exact program-option mechanism**
   - What we know: GeniusNode has `ConfigureTransactionFilterTimeoutsMs` wired to `SetTimeFrameToleranceMs`. The chain exists but needs the config source (program_options, .env, or config file).
   - What's unclear: Where exactly in the GeniusNode/app startup flow to read the config value. Several options exist — the `node/cli.cpp` program_options parsing, environment variable, or config file.
   - Recommendation: Planner should add a task to investigate the `node/cli.cpp` or `app/` startup flow and wire a `--timestamp-tolerance-ms` program option to call `ConfigureTransactionFilterTimeoutsMs`. The default should be 300000 (5 minutes).

2. **METRICS-01: Periodic flush vs. shutdown-only flush**
   - What we know: D-14 says "logged periodically or on shutdown."
   - What's unclear: Whether "periodically" means on an interval (e.g., every 60s) or just on shutdown. The existing TransactionManager has a periodic loop (line 415) that could carry the flush.
   - Recommendation: Implement shutdown-only flush in destructor for simplicity. If periodic logging is needed, it can be added as an enhancement without structural changes.

3. **CLEAN-01: Cleanup handler registration placement**
   - What we know: The handler must be registered during TransactionManager construction (around line 119-151 where SubjectHandler and CertificateHandler are registered).
   - What's unclear: Whether the Blockchain → ConsensusManager delegation chain needs a new method for cleanup handler registration (currently only Subject and Certificate are delegated).
   - Recommendation: Yes — add `RegisterProposalCleanupHandler` to both `Blockchain.hpp` and `Blockchain.cpp` following the same delegation pattern as `RegisterCertificateHandler` (lines 1679-1683).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| C++17 compiler (Clang) | Build all changes | ✓ | (macOS system) | — |
| CMake 3.22+ | Build system | ✓ | (project requirement) | — |
| Protobuf compiler | Consensus.proto regeneration | ✓ | (project thirdparty) | No proto changes needed |
| Boost 1.85.0 | Asio, logging | ✓ | (project thirdparty) | — |
| spdlog | TransactionManagerLogger | ✓ | (project thirdparty) | — |

**Missing dependencies with no fallback:** None — all dependencies are existing project infrastructure.

**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test (GTest) + Boost.Test |
| Config file | `src/*/CMakeLists.txt` test targets |
| Quick run command | `cmake --build build --target <test_target> && ./build/<test_target>` |
| Full suite command | `ctest --test-dir build` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SIZE-01 | Oversized serialized tx (65KB) rejected at SendTransactionItem | unit | `gtest --gtest_filter="TransactionManagerTest.SizeGate*"` | ❌ Wave 0 |
| SIZE-01 | Normal-sized tx (1KB) passes pre-publish gate | unit | `gtest --gtest_filter="TransactionManagerTest.SizeGate*"` | ❌ Wave 0 |
| TS-01 | Timestamp within ±5min passes validation | unit | `gtest --gtest_filter="TransactionManagerTest.TimestampTolerance*"` | ❌ Wave 0 |
| TS-01 | Timestamp outside configured window (e.g., ±1min) fails | unit | `gtest --gtest_filter="TransactionManagerTest.TimestampTolerance*"` | ❌ Wave 0 |
| TS-01 | SetTimeFrameToleranceMs updates the tolerance value | unit | `gtest --gtest_filter="TransactionManagerTest.TimestampTolerance*"` | ❌ Wave 0 |
| CLEAN-01 | VERIFYING entry transitions to FAILED on proposal timeout callback | integration | `gtest --gtest_filter="ConsensusManagerTest.CleanupCallback*"` | ❌ Wave 0 |
| CLEAN-01 | Non-VERIFYING entry (CONFIRMED) not affected by cleanup callback | integration | `gtest --gtest_filter="ConsensusManagerTest.CleanupCallback*"` | ❌ Wave 0 |
| CLEAN-01 | Cleanup callback NOT fired from certificate path (line 1912) | integration | `gtest --gtest_filter="ConsensusManagerTest.CleanupCallback*"` | ❌ Wave 0 |
| METRICS-01 | Certificate fallback success increments counter | unit | `gtest --gtest_filter="TransactionManagerTest.Metrics*"` | ❌ Wave 0 |
| METRICS-01 | Validation reject with reason is logged at INFO level | unit | `gtest --gtest_filter="TransactionManagerTest.Metrics*"` | ❌ Wave 0 |
| METRICS-01 | Counters flushed on TransactionManager destruction | unit | `gtest --gtest_filter="TransactionManagerTest.Metrics*"` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `cmake --build build --target <test_target> && ./build/<test_target>`
- **Per wave merge:** `ctest --test-dir build`
- **Phase gate:** Full test suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `src/account/test/TransactionManagerTest.cpp` — SIZE-01 size gate tests, TS-01 tolerance tests, METRICS-01 counter tests
- [ ] `src/blockchain/test/ConsensusManagerTest.cpp` — CLEAN-01 callback registration, dispatch, and exclusion from certificate path
- [ ] Test fixtures for creating oversized transactions (65KB+ dummy tx)
- [ ] Test fixtures for time-manipulated transaction timestamps

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | yes | SIZE-01: Pre-publish size cap (64KB) prevents oversized message DoS; defense-in-depth with post-receive `MAX_EMBEDDED_TX_BYTES` check |
| V6 Cryptography | no | — |

### Known Threat Patterns for C++17 Blockchain Consensus

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Oversized PubSub message DoS | Denial of Service | SIZE-01: Reject at `SendTransactionItem` before PubSub publish; 64KB threshold |
| Clock skew bypass for timestamp validation | Tampering | TS-01: Configurable tolerance window with reasonable default (5 min); negative elapsed is also bounded |
| Memory leak from orphaned temporary entries | Denial of Service | CLEAN-01: Callback transitions VERIFYING → FAILED on proposal timeout, triggering ReleaseNonce + UTXO rollback |
| Insufficient observability for attack detection | Information Disclosure | METRICS-01: Logs validation results and vote rates; counters expose rejection reasons and standalone validator adoption |

## Sources

### Primary (HIGH confidence)
- `src/account/TransactionManager.cpp` — Full function bodies for `SendTransactionItem` (line 1177), `CheckTransactionTimestamp` (line 4165), `GetTransactionByHash` (line 2648), `ChangeTransactionState` (line 4824), `SetTimeFrameToleranceMs` (line 2984), `TransactionManagerLogger` (line 82), `MAX_EMBEDDED_TX_BYTES` (line 3723), handler registration (lines 119-151)
- `src/account/TransactionManager.hpp` — Member variables (`timestamp_tolerance_m` line 501, `tx_processed_m` line 494, `TrackedTx` struct line 285), `TransactionStatus` enum (line 71), `New()` factory signature (line 96)
- `src/blockchain/Consensus.cpp` — `ClearProposalSlot` body (line 1984), timeout callers (lines 1392, 1476), certificate caller (line 1912), `DecodeNonceSubject` (line 2181), `GetSubjectHash` (line 428), `ProcessCertificates` (line 1361), handler dispatch patterns (lines 1599-1611, 1300-1316)
- `src/blockchain/Consensus.hpp` — `SubjectHandler`/`CertificateSubjectHandler` typedefs (lines 101-105), `RegisterSubjectHandler`/`RegisterCertificateHandler` declarations (lines 123-135), private handler maps (lines 665-670)
- `src/blockchain/Blockchain.hpp` — Delegation methods for handler registration (lines 152-170)
- `src/blockchain/impl/Blockchain.cpp` — Delegation implementation (lines 1669-1688)
- `src/account/GeniusNode.cpp` — `ConfigureTransactionFilterTimeoutsMs` (line 1832)
- `src/blockchain/impl/proto/Consensus.proto` — `NonceSubject` message (lines 57-63, `tx_hash` field at field 2)

### Secondary (MEDIUM confidence)
- `.planning/phases/01-core-embedded-transaction-validation-path/01-CONTEXT.md` — Phase 1 tracking lifecycle decisions (D-01 through D-04), ChangeTransactionState state machine
- `.planning/phases/02-conflict-and-replay-detection-hardening/02-CONTEXT.md` — Phase 2 certificate fallback deserialization (D-01, D-02), CONFIRMED promotion via `ChangeTransactionState`
- `.planning/REQUIREMENTS.md` — Phase 3 requirements (SIZE-01, TS-01, CLEAN-01, METRICS-01)

### Tertiary (LOW confidence)
- None — all findings verified against source code.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all dependencies are existing project infrastructure; no new external packages
- Architecture: HIGH — all four requirements have verified insertion points, existing patterns, and code-level line references
- Pitfalls: HIGH — pitfalls derived from source code analysis, not speculation
- Cleanup handler pattern: HIGH — follows established SubjectHandler/CertificateHandler patterns with verified code references
- Metrics implementation: HIGH — TransactionManagerLogger() pattern, atomic counter pattern, and destructor logging all verified in source

**Research date:** 2026-05-29
**Valid until:** 2026-06-28 (30 days — stable codebase, no external dependency changes expected)
