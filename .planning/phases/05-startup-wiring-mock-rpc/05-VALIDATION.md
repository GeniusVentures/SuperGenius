---
phase: 5
slug: startup-wiring-mock-rpc
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-06-04
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test 1.14+ |
| **Config file** | none — see Wave 0 |
| **Quick run command** | `ctest --test-dir build -R BridgeRelayerTest` |
| **Full suite command** | `ctest --test-dir build -R "BridgeRelayer|MockRpc|StartupWiring"` |
| **Estimated runtime** | ~5 seconds (unit), ~30 seconds (integration) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R "BridgeRelayerTest|MockRpcTest"` (unit, < 5 sec)
- **After every plan wave:** Run `ctest --test-dir build` (full suite)
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 05-01-01 | 01 | 1 | D-01 | T-05-01 | Multi-chain Start() registers watches on 8 chains | unit | `ctest -R BridgeRelayerTest.MultiChainStart` | ❌ W0 | ⬜ pending |
| 05-01-02 | 01 | 1 | D-01 | T-05-02 | Best-effort skips failed chains | unit | `ctest -R BridgeRelayerTest.BestEffortFailure` | ❌ W0 | ⬜ pending |
| 05-02-01 | 02 | 1 | D-07 | T-05-03 | Mock returns valid receipt | unit | `ctest -R MockRpcTest.SuccessReceipt` | ❌ W0 | ⬜ pending |
| 05-02-02 | 02 | 1 | D-09 | T-05-04 | Mock returns timeout (nullopt) | unit | `ctest -R MockRpcTest.Timeout` | ❌ W0 | ⬜ pending |
| 05-02-03 | 02 | 1 | D-09 | T-05-04 | Mock returns connection_refused | unit | `ctest -R MockRpcTest.ConnectionRefused` | ❌ W0 | ⬜ pending |
| 05-02-04 | 02 | 1 | D-09 | T-05-04 | Mock returns bad_json | unit | `ctest -R MockRpcTest.BadJson` | ❌ W0 | ⬜ pending |
| 05-02-05 | 02 | 1 | D-09 | T-05-04 | Mock returns wrong_status | unit | `ctest -R MockRpcTest.WrongStatus` | ❌ W0 | ⬜ pending |
| 05-02-06 | 02 | 1 | D-09 | T-05-04 | Mock returns wrong_logs | unit | `ctest -R MockRpcTest.WrongLogs` | ❌ W0 | ⬜ pending |
| 05-02-07 | 02 | 1 | D-10 | T-05-05 | Stateful sequences per tx_hash | unit | `ctest -R MockRpcTest.StatefulSequence` | ❌ W0 | ⬜ pending |
| 05-03-01 | 03 | 1 | D-07 | T-05-06 | TransportFactory typedef + SetTransportFactory | unit | `ctest -R PublicChainValidator.TransportFactoryInjection` | ❌ W0 | ⬜ pending |
| 05-03-02 | 03 | 1 | D-07 | T-05-07 | Factory replaces hard construction | unit | `ctest -R PublicChainInputValidator` | ✅ | ⬜ pending |
| 05-04-01 | 04 | 1 | D-17 | T-05-08 | Foreign address PutUTXO works | unit | `ctest -R UTXOManager.ForeignUtxo` | ❌ W0 | ⬜ pending |
| 05-04-02 | 04 | 1 | D-18 | T-05-09 | RESERVED blocks SelectUTXOs but allows vote | unit | `ctest -R UTXOManager.ReservedState` | ❌ W0 | ⬜ pending |
| 05-04-03 | 04 | 1 | D-19 | T-05-10 | UTXO_BRIDGE distinguishable from UTXO_NORMAL | unit | `ctest -R UTXOManager.BridgeUtxoType` | ❌ W0 | ⬜ pending |
| 05-05-01 | 05 | 2 | D-02 | T-05-11 | chains_config.json has bridge_contract_address on 8 chains | unit | `grep -c "bridge_contract_address" chains_config.json` | ❌ W0 | ⬜ pending |
| 05-05-02 | 05 | 2 | D-04 | T-05-12 | Async bridge init fires in INITIALIZING_TRANSACTIONS | unit | `ctest -R StartupWiring.BridgeInit` | ❌ W0 | ⬜ pending |
| 05-05-03 | 05 | 2 | D-20 | T-05-13 | Catch-up scan inserts missing burn UTXOs | integration | `ctest -R StartupWiring.CatchUpScan` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `test/src/mock/mock_rpc_transport.hpp` — MockRpcTransport class
- [ ] `test/src/mock/mock_rpc_transport.cpp` — Implementation
- [ ] `test/src/mock/mock_rpc_config.hpp` — Config parser
- [ ] `test/src/mock/mock_rpc_test.cpp` — Behavioral tests (6 failure modes + sequences)
- [ ] `test/src/startup/startup_wiring_test.cpp` — Startup integration tests
- [ ] Existing `test/src/account/bridge_relayer_test.cpp` — Extend for multi-chain Start()
- [ ] Framework config: test CMakeLists.txt updates for new directories

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| E2E bridge burn→mint with live Sepolia | D-15 | Requires live testnet + real RPC + PRIVATE_KEY | Set `SGNS_E2E_REAL_RPC=1`, ensure Foundry `cast` installed, run `ctest -R BridgeE2E` |
| Node startup logs show watching N chains | D-04 | Requires full node build | Start node with `--log-level debug`, check logs for "BridgeRelayer startup: watching" |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
