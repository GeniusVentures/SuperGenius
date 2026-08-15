# Phase 3: Burn Deduplication Cache - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning (gap closure)

<domain>
## Phase Boundary

Fix 4 Codex review findings from PR #298 that affect the burn dedup and bridge validation code. All findings are security or correctness issues in already-implemented code — this is a gap-closure phase, not new feature work.

</domain>

<decisions>
## Implementation Decisions

### Slot key collision (P1 #3 — Consensus.cpp:2154)
- **D-01:** Add burn tx hash to the slot key. Current key `mint-v2:{chain}:{token}:{amount}:{dest}` collides when two distinct burns have identical params. New key: `mint-v2:{chain}:{token}:{amount}:{dest}:{burn_tx_hash}`.
- **D-02:** Source the burn tx hash from `consumed_outpoints[0].tx_id_hash` in the MintV2's UTXO params — already available in the embedded transaction, no new data plumbing needed.

### Fail-open on missing endpoints (P2 #4 — InputValidators.cpp:491)
- **D-03:** Return `false` (reject) when `rpc_endpoints_` has no entry for the chain ID. A configured chain with missing endpoints should fail closed, not silently skip verification.

### UTXO witness for bridge mints (P1 #1 — InputValidators.hpp:184)
- **D-04:** `PublicChainInputValidator::RequiresConsensusUTXOData()` should return `false`. Bridge mints use EVM tx hash as input, not a local UTXO. The current `true` causes `BuildUTXOWitness` to fail, rejecting bridge mints before consensus.

### Burn event log verification (P1 #2 — InputValidators.cpp:550)
- **D-05:** Use **both** config and log matching (defense in depth):
  - Add `bridge_contract_address` and `event_topic0` fields to `WeightedRpcEndpoint` config per chain
  - In `VerifyPublicChainSmartContract`, after confirming receipt status, verify that at least one receipt log matches the expected bridge contract address and event topic0
  - Use evmrelay's existing `verify_receipt_log()` function or equivalent log-matching logic
- **D-06:** The expected burn details (amount, token, chain, destination) can be cross-verified against decoded log data when available, but the minimum check is contract address + topic0 match.

### Claude's Discretion
- All 4 fixes are minimal, surgical changes — no architectural refactoring
- Fix ordering: #3 (UTXO witness) and #4 (fail-open) first (one-line each), then #1 (slot key), then #2 (log verification) as it's the most involved

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### PR Review
- PR #298 review comments — 4 Codex findings with file:line references (see memory: pr298-codex-review-findings.md)

### Bridge Event System
- `evmrelay/include/eth/bridge_event.hpp` — `verify_receipt_log()`, `BridgeEventClaim`, `ReceiptLogVerificationResult`
- `evmrelay/include/eth/eth_receipt_source.hpp` — `ReceiptResult` struct with `receipt.logs`

### Input Validation
- `src/account/InputValidators.hpp` — `PublicChainInputValidator`, `WeightedRpcEndpoint`, `RequiresConsensusUTXOData()`
- `src/account/InputValidators.cpp` — `VerifyPublicChainSmartContract()` at line 474

### Consensus Slot Key
- `src/blockchain/Consensus.cpp` — `GetSlotKey()` at line 2130, MintV2 slot key at line 2140

### Transaction Manager
- `src/account/TransactionManager.cpp` — `SendTransactionItem()` at line 1175 (UTXO witness flow)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `eth::verify_receipt_log()` in evmrelay — already validates receipt logs against a BridgeEventClaim (contract, topic0, topics, data)
- `BridgeEventClaim` struct — carries all fields needed for log verification
- `WeightedRpcEndpoint` struct — extensible for bridge_contract_address and event_topic0 fields

### Established Patterns
- Friend accessor pattern for GTest private method access (BridgeRelayerTestAccess, CertificateFallbackTestAccess)
- CRDTFixture-based TransactionManager testing
- Config-driven chain validation via `SetRpcEndpoints(chain_id, endpoints)`

### Integration Points
- `SetRpcEndpoints()` already accepts per-chain config — natural place to add bridge contract fields
- `VerifyPublicChainSmartContract()` already iterates receipt logs — natural place to add log matching
- `GetSlotKey()` already detects MintV2 via oneof — natural place to add burn tx hash

</code_context>

<specifics>
## Specific Ideas

- The Codex review findings are the sole driver for this phase — no new features
- Fixes should be backward-compatible (existing tests must still pass)
- Unit tests for each fix are required

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope (fixing PR #298 review findings).

</deferred>

---

*Phase: 03-Burn Deduplication Cache (gap closure)*
*Context gathered: 2026-05-31*
