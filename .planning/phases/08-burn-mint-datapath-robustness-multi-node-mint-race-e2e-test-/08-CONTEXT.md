# Phase 8: Burn/Mint Datapath Robustness - Context

**Gathered:** 2026-07-16
**Status:** Ready for planning

<domain>
## Phase Boundary

Testing infrastructure for the burn→mint bridge datapath — no production datapath changes except
minimal test seams. Three deliverables:

1. **Multi-node mint-race e2e test** — one burn observed concurrently by all nodes' watchers,
   asserting exactly-once mint across the cluster.
2. **Fault injection** — node kill mid-mint, RPC endpoint disagreement/latency/timeout (via a new
   Mock RPC Transport), and pubsub network partition + heal.
3. **libFuzzer harnesses** — burn-event parsing, ABI/log decoding, and transaction deserialization.

</domain>

<decisions>
## Implementation Decisions

### Mint-Race Test Semantics
- **D-01:** Race trigger is the watcher datapath on ALL nodes — zero manual `MintTokens()` calls.
  Every node independently discovers the same burn (extends the invariant from
  `bridge_anvil_catchup_e2e_test.cpp` to the whole cluster).
- **D-02:** Exactly-once assertion: recipient balance delta == burn amount as observed from EVERY
  node, and the balance stays stable through an additional watcher poll window (catches both
  double-mint and lost-mint).
- **D-03:** Race window: seed the burn BEFORE watchers start polling, then release all nodes'
  watchers together so all discover it on their first poll. No test-only poll-trigger API.
- **D-04:** Two race tests: a single contested burn (clean diagnostic signal) and a small batch of
  3–5 burns (cross-burn interference), mirroring the `kNumCatchupBurns` pattern.
- **D-05:** Topology: 1 Full node + at least 10 Light nodes initially, designed to expand later.
- **D-06:** Node count is a compile-time `kNodeCount` constant (11 initially), tuned down for CI if
  needed. Validator keys generated programmatically (not the hardcoded 3-key array).
- **D-07:** Burns target Light-node addresses — forces mint state to propagate via CRDT/consensus
  to non-authoring nodes (the harder correctness case).

### Fault Injection
- **D-08:** Fault scenarios in scope (all four): node kill mid-mint, RPC endpoint disagreement,
  RPC latency/timeout, and network partition + heal (CRDT must converge to exactly-once).
- **D-09:** RPC faults delivered via a Mock RPC Transport injected through
  `PublicChainInputValidator::SetTransportFactory()`. Requirements (from folded todo): stateful,
  per-endpoint configurable receipts/tx statuses/addresses, behavioral variance
  (success/error/timeout), multi-chain support.
  **NOTE (verify first):** STATE.md records that Phase 5 already implemented a Mock RPC Transport
  (drop-in `RpcHttpTransport` replacement, per-node `mock_rpc_config.json`, stateful ordered
  responses keyed by tx_hash, 6 failure modes: success, timeout, connection_refused, bad_json,
  wrong_status, wrong_logs). Researcher MUST locate it and Phase 8 should EXTEND it (per-endpoint
  disagreement scenarios across the 3 quorum slots) rather than build a new one.
- **D-10:** Node kill = destroy the `GeniusNode` object (`node.reset()`) at a chosen point — same
  lifecycle as fixture TearDown. Process-level SIGKILL crash testing is out of scope.
- **D-11:** Partition induced by disconnecting/reconnecting peers at the pubsub/libp2p layer to
  split and heal the mesh.

### Fuzzing
- **D-12:** Fuzz targets (three): `BridgeRelayer::ParseBurnEventValues` (v1+v2 burn-event ABI
  values), the `eth::abi` log-decoding layer feeding it, and MintTransactionV2/GeniusTransaction
  deserialization from untrusted bytes. Config-JSON fuzzing explicitly NOT selected.
- **D-13:** Build integration: CMake fuzzer targets gated behind `-DSGNS_FUZZING=ON` with
  libFuzzer + ASan (e.g., an `addfuzztarget()` function alongside `addtest()` in
  `cmake/functions.cmake`). Excluded from normal builds so MSVC/normal CI is unaffected.
- **D-14:** CI cadence: short smoke runs (~60s per fuzzer per PR, replaying the regression corpus)
  plus manual/local deep runs. Seed corpus checked into the repo.

### Fixture Strategy
- **D-15:** New test suite directory (e.g., `test/src/bridge_race/`) reusing the shared
  `anvil_fixture.hpp` helpers — keeps the 11-node heavyweight tests isolated from the fast 3-node
  `bridge_e2e` suite, with its own ctest target/timeout.
- **D-16:** Chain backing: keep the Anvil fork of Sepolia (proven pattern — impersonated GNUS
  holder funds account #0). Tests skip cleanly when Foundry/network unavailable, as today.

### Claude's Discretion
- Exact directory/binary names for the new suite and fuzzers.
- How watcher release-together (D-03) is achieved (e.g., configure RPC endpoints on all nodes only
  after the burn is seeded, following the existing `ConfigureRpcEndpoint` flow).
- Mock RPC Transport class design, file placement, and how it coexists with real transports in the
  same test binary.
- Programmatic validator key generation approach.
- Fuzzer corpus layout and dictionary contents.
- Whether partition tests need a fixture variant with fewer nodes for runtime reasons.

### Folded Todos
- `bridge-startup-wiring-mock-rpc.md` — **task 3 only (Mock RPC Transport)** folded into this
  phase as the fault-injection mechanism (D-09). Original problem: no in-process mock for Tier 1
  RPC verification with data/behavioral/stateful variance. Tasks 1–2 (BridgeRelayer::Start /
  InitializeRpcEndpoints startup wiring) were addressed in Phase 5 and stay out of this phase.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing bridge e2e infrastructure (patterns to extend)
- `test/src/bridge_e2e/bridge_anvil_e2e_test.cpp` — 3-node Anvil-fork cluster fixture: node
  bootstrap order, genesis validator registration, slot-based RPC endpoint config (1 DIRECT + 2
  PUBLIC), replay-rejection test.
- `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` — watcher-driven auto-mint tests with
  the zero-manual-MintTokens invariant; per-node `bridge_chains_config.json` writing;
  no-double-mint stability assertions.
- `test/src/bridge_e2e/anvil_fixture.hpp` — AnvilProcess lifecycle, `SendBridgeOutBurn`,
  `FundAccount0WithGnus`, readiness polling.

### Datapath under test
- `src/account/BridgeRelayer.hpp` / `.cpp` — burn event watch → `MintFunds`;
  `ParseBurnEventValues` (fuzz target D-12).
- `src/watcher/impl/bridge_catchup_watcher.hpp` / `.cpp` — catch-up scan → `MintTokens`.
- `src/account/PublicChainInputValidator.hpp` — `SetTransportFactory()` DI seam (D-09), weighted
  RPC quorum (≥75%), slot hashes.
- `src/account/MintTransactionV2.hpp` / `src/account/GeniusTransaction.hpp` — deserialization fuzz
  targets (D-12).
- `src/account/TransactionManager.hpp` — `MintFunds` entry point and dedup behavior.

### Folded todo
- `.planning/todos/` `bridge-startup-wiring-mock-rpc.md` — Mock RPC Transport requirements
  (task 3), folded per D-09.

### Build/test conventions
- `cmake/functions.cmake` — `addtest()` pattern that `addfuzztarget()` (D-13) should mirror.
- `.planning/codebase/TESTING.md` — test layout, naming, wait-condition/outcome macros.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `anvil_fixture.hpp` helpers (AnvilProcess, burn seeding, funding) — directly reusable for the
  new suite.
- `testutil/wait_condition.hpp` — `ASSERT_WAIT_FOR_CONDITION` / `waitForCondition` for
  settle/stability assertions.
- `MemorySecureStorage` factory injection and `SetChainlistFetcher` — already used to isolate
  tests from network fetches.
- CRDT test broadcasters (`test/src/crdt/crdt_mirror_broadcaster.*`) — reference if peer
  disconnect proves insufficient for partitions.

### Established Patterns
- Node bootstrap order matters: create all nodes → register genesis validators → wait full-node
  READY → AddPeers mesh → wait processors READY (see `bridge_anvil_e2e_test.cpp` SetUpTestSuite).
- Slot-based consensus quorum requires ≥2 distinct validators per PUBLIC hash group; endpoints
  registered as 1 DIRECT (weight 100) + 2 PUBLIC (weight 0).
- Tests skip cleanly (GTEST_SKIP) when Foundry binaries or funding are unavailable.

### Integration Points
- `GeniusNode::ConfigureRpcEndpoint()` — where per-node endpoint (and thus mock transport
  behavior) is wired.
- `Blockchain::SetAuthorizedFullNodeAddress` / `SetAdditionalGenesisValidatorAddresses` — must
  accept 10+ validator addresses (verify no hardcoded limits).
- `GeniusNode::GetPubSub()->AddPeers(...)` — mesh construction and the seam for partition tests.

</code_context>

<specifics>
## Specific Ideas

- The race test's core invariant is lifted from the catchup suite's comment: "All tests make zero
  manual MintTokens() calls. The only path by which the recipient balance can increase is
  watcher → MintTokens()."
- User explicitly wants the node count to start at ~11 (1 Full + 10 Light) and be expandable
  later — treat scale-up as a first-class design constraint, not an afterthought.

</specifics>

<deferred>
## Deferred Ideas

- Process-level node crash testing (SIGKILL of a child-process node runner) — true crash
  semantics for RocksDB/CRDT recovery; revisit after object-destruction kill tests exist.
- Nightly long-run fuzzing CI job — after the smoke-run infrastructure proves stable.
- Scaling the race cluster beyond ~11 nodes (Shadow/Docker if in-process hits limits).
- `bridge_chains_config.json` config-parsing fuzz target — considered, not selected.

</deferred>

---

*Phase: 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-*
*Context gathered: 2026-07-16*
