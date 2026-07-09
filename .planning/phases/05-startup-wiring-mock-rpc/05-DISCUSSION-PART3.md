---
phase: 05
part: 3
topic: Mock RPC Transport Design
date: 2026-06-03
status: finalized
---

# Part 3 Discussion: Mock RPC Transport

## Design Decisions

### 1. Transport Interface
Mock implements the same interface as `RpcHttpTransport` — drop-in replacement. No separate API. `PublicChainInputValidator` doesn't know it's talking to a mock.

### 2. Config Format
Per-node JSON config file. Each node can watch different chains — `chains_config.json` drives it, same as production. Mock config only overrides transport behavior.

Mock config fields per endpoint:
- `url` — match key for endpoint routing
- `behavior` — `"success"` | `"timeout"` | `"connection_refused"` | `"bad_json"` | `"wrong_status"` | `"wrong_logs"`
- `responses` — ordered list of canned `eth_getTransactionReceipt` JSON responses, keyed by tx_hash for stateful sequences

### 3. Injection Point
Dependency injection. Mock transport is constructed and passed through `ChainRpcEndpointProvider` → `SetRpcEndpoints()`. Production path unchanged. Test/testnet path injects mock.

### 4. Config Location
Relative to binary executable. Config file path: `<binary_dir>/mock_rpc_config.json`. Test fixtures point to their own config.

### 5. Stateful Sequences
Ordered list of responses keyed by tx_hash. First call gets response[0], second gets response[1], etc. Resets per test case:
```json
{
  "0xabc123...": [
    { "status": "0x1", "logs": [...] },   // call 1
    { "status": "0x0", "logs": [] }       // call 2 — fail
  ]
}
```

### 6. Canned Response Format
Raw JSON strings matching live `eth_getTransactionReceipt` response format. `eth::rpc::parse_transaction_receipt_response()` parses identically to live data. No new types needed.

### 7. Multi-Chain Support
Through `chains_config.json` — mock reads the same chain list. Each chain's endpoints in mock config are matched by URL. Skipped chains use real RPC (or fail-closed if no endpoints).

### 8. Error Simulation
All failure modes supported:
| Behavior | Simulates |
|----------|-----------|
| `success` | Valid receipt, matching logs |
| `timeout` | Transport never responds (>10s) |
| `connection_refused` | RPC endpoint down |
| `bad_json` | Response body is not valid JSON |
| `wrong_status` | Receipt exists but `status: 0x0` (failed tx) |
| `wrong_logs` | Receipt valid but logs don't match bridge contract/topic0 |

### 9. Startup Catch-Up Scan
Mock also supports the missed-burns scan. When the node starts and queries for unprocessed burns, the mock returns canned responses for those queries too — same transport, same config, same behavior controls.
