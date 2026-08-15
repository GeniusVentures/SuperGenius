# Phase 3: Network Hardening and Operational Readiness - Context

**Gathered:** 2026-05-28
**Status:** Ready for planning

## Phase Boundary

Protocol robustness at scale — oversized messages are caught before PubSub publish, timestamp validation tolerates distributed clock skew, temporary tracking data is cleaned up, and operational metrics are available for monitoring and debugging.

## Implementation Decisions

### PubSub Message Size Enforcement (SIZE-01)
- **D-01:** Pre-publish enforcement at `SendTransactionItem` in `TransactionManager.cpp`, immediately after `transaction->SerializeByteVector()` at line 1177. Reject before entering the consensus pipeline — prevents silent PubSub message drops.
- **D-02:** Threshold: 64 KB, matching the existing `MAX_EMBEDDED_TX_BYTES` in the handler (Phase 1). Keep both checks for defense-in-depth (pre-publish + post-receive).
- **D-03:** Error behavior: return an `outcome::failure` with a descriptive error message — no special error code needed.

### Timestamp Clock Skew Tolerance (TS-01)
- **D-04:** Make the timestamp tolerance window configurable. Current hardcoded value is ±5 minutes in `CheckTransactionTimestamp`. Replace with a configurable value from the existing config/env system.
- **D-05:** Default value: ±5 minutes (preserve current behavior for existing deployments). Wider values configurable per-validator for geographically distributed setups.
- **D-06:** Config key design at the agent's discretion — follow existing patterns in the codebase for timestamp/tolerance configuration.

### Tracking Entry Cleanup (CLEAN-01)
- **D-07:** Add a callback registration mechanism to `ConsensusManager`/`Blockchain` for proposal cleanup notifications. TransactionManager registers a handler.
- **D-08:** The callback fires from the timeout callers of `ClearProposalSlot` (lines 1392 and 1476 in `Consensus.cpp`) — NOT from the certificate caller (line 1912, where entries are already CONFIRMED via Phase 2).
- **D-09:** The handler receives `tx_hash` and calls `ChangeTransactionState(tx, FAILED)` for any VERIFYING entries in `tx_processed_m` that match the expired proposal's subject hash.
- **D-10:** Use `GetTransactionByHash(tx_hash)` to find the entry; if found and status is VERIFYING, transition to FAILED. If not found (already cleaned or was CRDT-sourced), skip silently.
- **D-11:** The `ClearProposalSlot` function at `Consensus.cpp:1984` already cleans `proposals_`, `pending_proposals_`, `pending_votes_`, and `pending_by_subject_hash_`. No changes needed to that function — the callback is called by its callers.

### Operational Metrics (METRICS-01)
- **D-12:** Use existing `TransactionManagerLogger()` (spdlog-based) — consistent with all other logging in the module.
- **D-13:** Log the following lifecycle events at `info` level:
  - Certificate fallback deserialization (success/failure) — standalone validator adoption rate
  - Proposal validation result: `Approve` / `Reject` with rejection reason (witness, nonce, conflict, etc.)
  - Temp VERIFYING entry creation (embedded tx) / promotion to CONFIRMED / transition to FAILED
- **D-14:** Counters at the agent's discretion — simple atomic counters for vote counts and validation breakdown, logged periodically or on shutdown. No external metrics system integration.

### the agent's Discretion
- SPECIFIC config key name and location for timestamp tolerance (follow existing codebase patterns)
- Callback registration API design (follow `RegisterSubjectHandler`/`RegisterCertificateHandler` patterns)
- Metrics counter implementation (atomic integers, log format, flush interval)
- Exact log message format (follow existing `[address - full] func: msg` pattern)

## Canonical References

### Core Implementation Files
- `src/account/TransactionManager.cpp` — `SendTransactionItem()` (line 1177 for size check), `HandleNonceConsensusSubject()` (handler entry), `tx_processed_m` map
- `src/blockchain/Consensus.cpp` — `ClearProposalSlot()` (line 1984), timeout callers (lines 1392, 1476), certificate caller (line 1912)
- `src/blockchain/Consensus.hpp` — `RegisterSubjectHandler`, `RegisterCertificateHandler` (patterns for callback registration)
- `src/blockchain/Blockchain.hpp` — Handler registration facade methods

### Planning Context
- `.planning/PROJECT.md` — Project scope, constraints
- `.planning/REQUIREMENTS.md` — Phase 3 requirements (SIZE-01, TS-01, CLEAN-01, METRICS-01)
- `.planning/ROADMAP.md` — Phase 3 success criteria
- `.planning/phases/01-core-embedded-transaction-validation-path/01-CONTEXT.md` — Phase 1 decisions (ChangeTransactionState lifecycle)
- `.planning/phases/02-conflict-and-replay-detection-hardening/02-CONTEXT.md` — Phase 2 decisions (certificate fallback path)

### Protocol Schema
- `src/blockchain/impl/proto/Consensus.proto` — ConsensusCertificate, ConsensusProposal, NonceSubject

## Existing Code Insights

### Reusable Assets
- `RegisterSubjectHandler` / `RegisterCertificateHandler` patterns — follow for cleanup callback registration
- `ChangeTransactionState(tx, FAILED)` — already handles FAILED transition with ReleaseNonce, UTXO rollback
- `TransactionManagerLogger()` — spdlog-based structured logging with `[address - full] func: msg` format
- `MAX_EMBEDDED_TX_BYTES = 64 * 1024` — existing constant for reuse in pre-publish size check

### Established Patterns
- Callback registration: `Blockchain::RegisterCertificateHandler(subject_type, handler)` — mirror for cleanup
- Config pattern: existing config keys in `.env` or program options
- Handler pattern: outcome::result return types with descriptive error messages

### Integration Points
- `SendTransactionItem()` — pre-publish size enforcement insertion point
- `CheckTransactionTimestamp()` — timestamp tolerance window
- `ClearProposalSlot()` callers (lines 1392, 1476) — cleanup callback trigger points  
- `OnConsensusCertificate()` — already handles CONFIRMED promotion for certificate path

## Specific Ideas

No specific user-requested implementation patterns beyond the decisions above. All areas follow existing codebase conventions.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 3-Network Hardening and Operational Readiness*
*Context gathered: 2026-05-28*
