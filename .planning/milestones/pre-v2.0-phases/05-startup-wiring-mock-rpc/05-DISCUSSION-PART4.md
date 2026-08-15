---
phase: 05
part: 4
topic: Startup Catch-Up Scan, Mock Enablement, Failure Handling
date: 2026-06-04
status: finalized
---

# Part 4 Discussion: Remaining Design Decisions

> Continuation of Parts 1-3. Covers areas not resolved in prior sessions.

---

## Area 1: Startup Catch-Up Scan Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-mint | Automatically fire MintFunds for each found unprocessed burn | |
| Queue for manual | Store in queue, expose via API for manual processing | |
| Log only | Report found burns, take no action | |
| UTXO-based state machine | Burn UTXO lifecycle: READY → RESERVED → CONFIRMED, with RPC scan to backfill missing UTXOs | ✓ |

**User's choice:** UTXO-based state machine with RESERVED state.

**Notes:**
- Remove the 8 `!is_full_node_ && address != address_` guards in UTXOManager.cpp — all nodes store all UTXOs, needed for HasConfirmedInputConflict validation.
- Add `UTXO_RESERVED` to `UTXOState` enum. Burn UTXO lifecycle: READY → RESERVED (mint initiated, blocks local reuse but allows consensus voting) → CONSUMED (certificate produced).
- Add UTXO type marker on UTXOEntry to distinguish bridge/burn UTXOs.
- Startup scan: probe RPC for historical burns with max depth → match against UTXO set → insert missing as READY.
- READY burns trigger MintFunds() → RESERVED → CONFIRMED on certificate.
- HasConfirmedInputConflict currently only scans tx_processed_m (confirmed transactions), not UTXO set. Having UTXOs for all peers enables broader validation.

---

## Area 2: Mock Transport Enablement

| Option | Description | Selected |
|--------|-------------|----------|
| Compile flag | `#ifdef USE_MOCK_RPC`, separate build config | |
| Runtime config | Env var or config file existence check | |
| Test executables default mock, runtime real-RPC opt-in | Mock via DI, real RPC per test via flag | ✓ |

**User's choice:** Test executables default to mock, runtime real-RPC opt-in.

**Notes:**
- Test executables default to mock transport — injected via ChainRpcEndpointProvider::SetRpcEndpoints(mock_transport).
- Real RPC opt-in per test via runtime switch (env var or gtest flag, e.g. SGNS_E2E_REAL_RPC=1).
- No compile flag — both transports compile into test binary, selection is runtime via DI.
- Production genius_node binary uses real RPC only.
- User explicitly rejected compile-flag approach to keep Release test executables using mock.

---

## Area 3: BridgeRelayer Failure Handling

| Option | Description | Selected |
|--------|-------------|----------|
| Best-effort | Skip failed chains, continue with others, log warning | ✓ |
| Fail-fast | Abort node startup if any chain fails watch registration | |

**User's choice:** Best-effort.

**Notes:**
- Multi-chain Start() registers BridgeSourceBurned watch per chain.
- If some chains fail (missing endpoints, contract not deployed, network issues), skip and continue.
- Log warning per skipped chain.
- No startup abort — remaining chains continue operating.

---

## Agent's Discretion

- BridgeRelayer internal refactoring from single watch_id_ to per-chain watch ID tracking
- InitializeRpcEndpoints() implementation details (chains_config.json parsing, ChainRpcEndpointProvider population)
- Mock transport class name and file location
- Exact UTXOType enum design and placement
- Startup scan depth and query mechanism
- GTest flag name for real-RPC opt-in
