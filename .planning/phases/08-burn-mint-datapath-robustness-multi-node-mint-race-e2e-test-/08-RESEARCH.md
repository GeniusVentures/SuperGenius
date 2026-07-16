# Phase 8: Burn/Mint Datapath Robustness - Research

**Researched:** 2026-07-16
**Domain:** C++ multi-node blockchain E2E testing (GTest + Anvil fork), RPC fault injection, libFuzzer harnesses
**Confidence:** HIGH (codebase-verified; no external packages involved)

## Summary

This phase is pure testing-infrastructure work on an existing, well-understood datapath. All
three deliverables extend patterns already present in `test/src/bridge_e2e/` and `test/src/mock/`.
No new third-party dependencies are needed — libFuzzer ships with the Clang toolchain already in
use (Apple Clang 16 confirmed locally; Linux CI presumably also Clang-capable — verify at plan
time whether CI uses GCC-only, which would need a Clang cross-compile leg for fuzzing).

The **Mock RPC Transport already exists** (`test/src/mock/mock_rpc_transport.{hpp,cpp}`,
`mock_rpc_config.hpp`) exactly as STATE.md described: a `JsonRpcTransport`-conforming
`MockRpcTransport` class, a `MockEndpointConfig` struct with 6 `MockBehavior` values, and a
`LoadMockConfig()` JSON loader. It is injected into `PublicChainInputValidator` via
`SetTransportFactory(TransportFactory)`, a `std::function<unique_ptr<JsonRpcTransport>(url,timeout)>`
already wired for exactly this purpose (docstring: "Tests inject a factory that returns
MockRpcTransport instances"). **Extension needed for Phase 8:** the mock currently keys behavior
per-endpoint-config (one `MockEndpointConfig` per URL), which already supports "per-endpoint
disagreement" — the gap is a convenience layer to build 3 divergent `MockEndpointConfig`s (one per
quorum slot: DIRECT + 2×PUBLIC) and wire them through `WeightedRpcEndpoint`/`ConfigureRpcEndpoint`,
mirroring the real-RPC 1-DIRECT+2-PUBLIC pattern already used in
`bridge_anvil_catchup_e2e_test.cpp`. No new mock architecture is required — this is additive
config-building, not a rewrite.

The **exactly-once dedup path** is fully traced: `TransactionManager::MintFunds` checks (1)
`UTXOManager::IsOutPointReserved`/`IsOutPointConsumed` (in-memory, `UTXO_RESERVED` state, Phase 5
D-18) and (2) a CRDT-persisted `kBridgeExecutedPrefix + chainid + tx_hash` key (survives restart),
returning `std::errc::already_connected` on either hit. This is the arbitration the race test
exercises: with 11 nodes' watchers all discovering the same burn near-simultaneously, exactly one
`MintTransactionV2` should be accepted into the UTXO/consensus pipeline; the rest must fail this
check. The race test's job is to prove this holds under true concurrency (not just fast retry) —
which is why D-03 (seed-before-start, release-together) matters: without a genuine race window,
the test would only prove sequential correctness.

**11-node topology is feasible with no discovered hard caps.** `Blockchain::SetAdditionalGenesisValidatorAddresses`
takes a `std::vector<std::string>` — no size limit found in the header. `bridge_anvil_e2e_test.cpp`
already generates distinct keys via `FromPrivateKey{hex_key}` from a `static constexpr const char*[]`
array; Phase 8 needs to either extend that array to 11 entries or generate keys programmatically
(e.g. via `EthereumKeyGenerator` or deterministic derivation) — **verify at plan time** whether a
key-generation helper beyond the existing hardcoded arrays already exists, or whether one must be
built (Claude's Discretion per CONTEXT.md).

**Pubsub partition is feasible.** `GossipPubSub` itself exposes only `AddPeers()` (no
`RemovePeer`), but `GossipPubSub::GetHost()` returns the underlying `libp2p::Host`, which exposes
`virtual void disconnect(const peer::PeerId&)`. Partition = call `disconnect()` on cross-partition
peer IDs from both sides; heal = call `AddPeers()` again (already the bootstrap pattern).

**Primary recommendation:** Build the race suite as `test/src/bridge_race/` reusing
`anvil_fixture.hpp`, wire an 11-key array (or generator) into `SetAdditionalGenesisValidatorAddresses`,
seed the burn before any `ConfigureRpcEndpoint` call (gating watcher start, mirroring the catchup
suite's ordering), extend `MockEndpointConfig`-based fixtures with a small helper for divergent
3-slot config, use `Host::disconnect()`/`AddPeers()` for partition, and use `node.reset()` for kill
tests. Fuzzing is a separate, from-scratch `-DSGNS_FUZZING=ON` CMake path with 3 libFuzzer targets
wrapping already-isolated pure functions (`ParseBurnEventValues`, `eth::abi::decode_log`,
`MintTransactionV2::DeSerializeByteVector`).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Multi-node mint-race e2e test | Test/Integration | Watcher + Consensus (under test) | New GTest suite; exercises existing production watcher→mint path unchanged |
| Mock RPC Transport extension | Test infrastructure | Backend (PublicChainInputValidator DI seam) | Existing `SetTransportFactory` DI seam; test-only class in `test/src/mock/` |
| Node-kill fault injection | Test/Integration | — | `node.reset()` — object lifecycle, no new production code |
| RPC disagreement fault injection | Test infrastructure | Backend (consensus quorum) | Exercises existing `WeightedRpcEndpoint` quorum logic via mock transport |
| Pubsub partition fault injection | Test/Integration | P2P (libp2p Host) | Uses existing `Host::disconnect()`/`GossipPubSub::AddPeers()`; no new API |
| libFuzzer harnesses | Build tooling + Test | Backend (parse/deserialize functions under test) | New CMake target type (`addfuzztarget()`); wraps existing pure functions, zero production code changes |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| GoogleTest/GoogleMock | already vendored (project-pinned) | Test runtime for the race suite | Project standard — `addtest()` in `cmake/functions.cmake` |
| Clang libFuzzer + ASan | ships with Clang (Apple Clang 16 confirmed locally) | Coverage-guided fuzzing + memory-safety detection | Industry standard for C++ fuzzing; no external package — part of the compiler toolchain `[VERIFIED: local clang --version]` |
| Foundry (anvil + cast) | already required by existing bridge_e2e suite | Local Sepolia-fork chain for burn seeding | Already the project's proven E2E chain backend (D-16) |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| existing `MockRpcTransport` (`test/src/mock/`) | in-repo, Phase 5 | Fault injection for RPC disagreement/latency/timeout | Extend, do not replace |
| `EthereumKeyGenerator` (ProofSystem) | in-repo | Derive SGNS destination / possibly programmatic validator keys | Already used in catchup suite for destination derivation |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| In-process 11-node cluster | Docker/Shadow network simulation | Deferred explicitly (CONTEXT.md `## Deferred`) — heavier, out of scope for this phase |
| libFuzzer | AFL++ | libFuzzer is already toolchain-native (no extra install) and integrates directly with Clang/ASan; AFL++ needs a separate binary/instrumentation pass — no reason to introduce it here |

**Installation:** No new packages. Confirm CI toolchain has Clang available for the `-DSGNS_FUZZING=ON`
leg (verify at plan time — the normal CI build may be MSVC/GCC-only per D-13's exclusion intent).

**Version verification:** N/A — no versioned package installs in this phase; all tooling is
already present in the repo or the OS toolchain.

## Package Legitimacy Audit

**Not applicable.** This phase introduces zero new external packages (no npm/PyPI/cargo/vcpkg
additions). libFuzzer and ASan are Clang compiler features, not separate packages. Foundry
(anvil/cast) is already a project dependency used by existing bridge_e2e tests. Skip the
Package Legitimacy Gate protocol.

## Architecture Patterns

### System Architecture Diagram

```
[Anvil fork of Sepolia]
   |  (SendBridgeOutBurn — seeded BEFORE any watcher starts, D-03)
   v
[bridgeOut() burn tx on-chain]
   |
   |  polled independently by EACH node's watcher (zero manual MintTokens)
   v
+------------------- 11-node in-process cluster -------------------+
| node_full (1)         node_light_1..10 (10)                       |
|   BridgeCatchupWatcher / BridgeRelayer poll eth_getLogs            |
|   via PublicChainInputValidator -> TransportFactory                |
|      -> [REAL RpcHttpTransport]  OR  [MockRpcTransport] (fault inj)|
|   ParseBurnEventValues (decoded log values)                        |
|   -> TransactionManager::MintFunds                                 |
|        - IsOutPointReserved / IsOutPointConsumed check (dedup #1)  |
|        - kBridgeExecutedPrefix CRDT persistence check (dedup #2)   |
|        - on pass: MintTransactionV2 -> UTXO reservation -> consensus|
|   -> CRDT/consensus propagates mint to ALL 11 nodes                |
+---------------------------------------------------------------------+
   |
   v
[Assert: exactly one mint accepted; recipient balance delta == burn amount
 as observed from EVERY node; stable across an extra poll window]

Fault injection axes (orthogonal to the above):
  - node.reset() mid-mint (kill)             -> lifecycle destruction
  - MockRpcTransport per-slot divergent config -> quorum disagreement
  - GossipPubSub::GetHost()->disconnect(peer) -> mesh partition
  - GossipPubSub::AddPeers() again            -> heal

Fuzzing (separate binary tier, offline, no cluster):
  raw bytes -> BridgeRelayer::ParseBurnEventValues(vector<AbiValue>)
  raw bytes -> eth::abi::decode_log(LogEntry, signature, params)
  raw bytes -> MintTransactionV2::DeSerializeByteVector(vector<uint8_t>)
```

### Recommended Project Structure
```
test/src/bridge_race/
├── CMakeLists.txt                       # own ctest target(s)/timeout, mirrors bridge_e2e pattern
├── bridge_race_fixture.hpp              # 11-node SetUpTestSuite helper, reuses anvil_fixture.hpp
├── bridge_race_single_burn_test.cpp     # D-04 test 1: one contested burn
├── bridge_race_batch_test.cpp           # D-04 test 2: 3-5 burn batch
├── bridge_race_fault_kill_test.cpp      # D-10 node-kill mid-mint
├── bridge_race_fault_rpc_test.cpp       # D-09 RPC disagreement/latency/timeout via mock transport
└── bridge_race_fault_partition_test.cpp # D-11 pubsub partition + heal

test/src/mock/
├── mock_rpc_transport.{hpp,cpp}         # EXTEND (existing) — no rewrite
└── mock_rpc_config.hpp                  # EXTEND: helper to build 3 divergent per-slot configs

fuzz/                                     # new top-level dir, gated by -DSGNS_FUZZING=ON
├── CMakeLists.txt
├── fuzz_parse_burn_event_values.cpp
├── fuzz_decode_log.cpp
├── fuzz_mint_transaction_deserialize.cpp
└── corpus/
    ├── parse_burn_event_values/
    ├── decode_log/
    └── mint_transaction_deserialize/
```

### Pattern 1: Seed-before-start watcher race window (D-03)
**What:** Seed the burn transaction on Anvil BEFORE creating/configuring any node's RPC endpoint
(i.e., before `ConfigureRpcEndpoint`), then call `ConfigureRpcEndpoint` on all 11 nodes back-to-back
in a tight loop so every watcher's first poll sees the burn already present.
**When to use:** This is the *only* way to force genuine concurrent discovery — the catchup suite
already proves the sequential case (burns seeded, THEN nodes start one after another with
`ConfigureRpcEndpoint` called individually per node with waits in between). The race suite must NOT
insert `ASSERT_WAIT_FOR_CONDITION` between each node's `ConfigureRpcEndpoint` call, or it degenerates
into 11 sequential single-node tests.
**Example:**
```cpp
// Source: test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp SetUpTestSuite (existing pattern)
// Seed FIRST, before any ConfigureRpcEndpoint call:
const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
    s_anvil.RpcUrl(), kMintAmount, sgns_dest );

// Then configure ALL nodes' RPC endpoints together, no waits between them:
for ( unsigned int i = 0; i < kNodeCount; ++i )
{
    nodes[i]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps );
}
// Only wait for READY/watcher-poll-completion AFTER all 11 are configured.
```

### Pattern 2: Dedup verification via UTXO reservation + CRDT persistence
**What:** `TransactionManager::MintFunds` (src/account/TransactionManager.cpp:564) is the single
choke point. It returns `outcome::failure(std::errc::already_connected)` if either
`UTXOManager::IsOutPointReserved/IsOutPointConsumed` OR the CRDT key
`kBridgeExecutedPrefix + chainid + kBridgeKeySeparator + tx_hash` already exists.
**When to use:** The race test's exactly-once assertion (D-02) should assert on the *observable
consequence* (balance delta) rather than instrument `MintFunds` directly, per the existing
catchup-suite convention of zero direct manual calls. Optionally add a counting hook (similar to
`MakeCountingBurnProcessor` in the catchup test) at the `BurnProcessor` callback level per node to
additionally assert each node saw the burn exactly once at the watcher layer, independent of mint
outcome.

### Pattern 3: Per-slot divergent Mock RPC config for quorum disagreement (D-09)
**What:** Build 3 `MockEndpointConfig` instances (mirroring the existing 1-DIRECT + 2-PUBLIC
`WeightedRpcEndpoint` pattern already in `bridge_anvil_catchup_e2e_test.cpp`), each with a
DIFFERENT `MockBehavior` or different `responses` map, and register them via
`SetTransportFactory` so the transport factory dispatches by URL to the matching config.
**Example:**
```cpp
// Source: test/src/mock/mock_rpc_config.hpp + PublicChainInputValidator.hpp (existing seams)
sgns::test::MockEndpointConfig direct_cfg{ "mock://direct", sgns::test::MockBehavior::kSuccess, {} };
sgns::test::MockEndpointConfig public1_cfg{ "mock://public1", sgns::test::MockBehavior::kWrongLogs, {} };
sgns::test::MockEndpointConfig public2_cfg{ "mock://public2", sgns::test::MockBehavior::kTimeout, {} };

validator->SetTransportFactory(
    [=]( const std::string &url, std::chrono::seconds timeout ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
    {
        if ( url == direct_cfg.url )  return std::make_unique<sgns::test::MockRpcTransport>( direct_cfg );
        if ( url == public1_cfg.url ) return std::make_unique<sgns::test::MockRpcTransport>( public1_cfg );
        return std::make_unique<sgns::test::MockRpcTransport>( public2_cfg );
    } );
```

### Pattern 4: Pubsub partition + heal via libp2p Host
**What:** `GossipPubSub` has no `RemovePeer`, but `GetHost()` exposes the raw `libp2p::Host`
which has `virtual void disconnect(const peer::PeerId&)`. Partition = disconnect selected peers
mutually; heal = `AddPeers()` again (existing bootstrap call already used everywhere).
**Example:**
```cpp
// Source: 3rdparty/ipfs-pubsub gossip_pubsub.hpp (GetHost), 3rdparty/libp2p host.hpp (disconnect)
node_light_5->GetPubSub()->GetHost()->disconnect( peer_id_of( node_full ) );
// ... run test under partition ...
node_light_5->GetPubSub()->AddPeers( { node_full->GetPubSub()->GetLocalAddress() } ); // heal
```
Obtaining the `PeerId` for a given node requires parsing its `GetLocalAddress()` multiaddress or
using `libp2p::Host`'s local peer info — **verify exact PeerId extraction API at plan time**; not
directly confirmed in this research pass (LOW confidence on the precise call, HIGH confidence that
`disconnect()` exists and is the right seam).

### Anti-Patterns to Avoid
- **Manual `MintTokens()`/`MintFunds()` calls in the race test:** Breaks the project's established
  invariant (explicitly documented in the catchup suite) that the ONLY path to balance increase is
  watcher-driven. Any manual call invalidates the race test's purpose.
- **Sequential `ConfigureRpcEndpoint` + wait-per-node:** Defeats the race window; must configure
  all nodes' endpoints in a tight loop with no intervening waits (see Pattern 1).
- **Rewriting MockRpcTransport from scratch:** It already satisfies all D-09 requirements
  (stateful, per-endpoint, behavioral variance, multi-chain via multiple `MockEndpointConfig`s).
  Only a factory-dispatch helper for 3 divergent per-slot configs is needed.
- **Building fuzz harnesses that touch consensus/CRDT/network:** All 3 selected fuzz targets are
  pure, dependency-free parse/deserialize functions. Keep it that way — no I/O, no globals, no
  static registries in the harness `LLVMFuzzerTestOneInput` bodies (needed for fast, deterministic,
  parallelizable fuzzing).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| RPC fault injection transport | New mock RPC class | Existing `test/src/mock/MockRpcTransport` | Already implements the `JsonRpcTransport` interface with 6 behaviors, stateful per-tx_hash responses, and is already the documented D-09 solution |
| Deterministic validator keys | New crypto key derivation scheme | `FromPrivateKey{hex}` with an extended hardcoded array, or existing `EthereumKeyGenerator` | Established pattern in `bridge_anvil_e2e_test.cpp`/`bridge_anvil_catchup_e2e_test.cpp`; avoids introducing a new key-generation dependency |
| Fuzzing harness build wiring | Ad hoc separate CMakeLists per fuzzer with manual clang invocation | `addfuzztarget()` mirroring `addtest()` in `cmake/functions.cmake` | Consistent with the project's existing build-function convention; single gated switch (`-DSGNS_FUZZING=ON`) keeps MSVC/normal CI unaffected |
| Pubsub mesh partition simulation | Custom network-layer proxy/firewall simulation | `libp2p::Host::disconnect()` + `GossipPubSub::AddPeers()` | Both APIs already exist and are already used for mesh bootstrap; disconnect is the natural inverse |

**Key insight:** Every mechanism this phase needs already exists in the codebase in some form
(mock transport, DI seam, watcher lifecycle, disconnect API, dedup logic). This phase is almost
entirely test-authoring plus two additive extensions (per-slot mock config helper, `addfuzztarget()`
CMake function) — not new production architecture.

## Common Pitfalls

### Pitfall 1: False race test (sequential disguised as concurrent)
**What goes wrong:** Configuring RPC endpoints or starting watchers one node at a time with
`ASSERT_WAIT_FOR_CONDITION` gates between each makes every node "discover" the burn on a different
poll cycle — no actual race occurs, and the exactly-once assertion becomes trivially true even if
dedup is broken.
**Why it happens:** Copy-pasting the existing catchup-suite bootstrap sequence (which explicitly
staggers nodes) without adapting for concurrency.
**How to avoid:** Explicitly batch all `ConfigureRpcEndpoint` calls in one tight loop AFTER the
burn is seeded and BEFORE any wait; only wait once, after configuring all nodes.
**Warning signs:** Test still passes when a deliberately-broken dedup check is injected (should
fail); watcher poll_interval is long enough that the loop naturally serializes anyway.

### Pitfall 2: 11-node startup cost blowing CI timeouts
**What goes wrong:** Each node in the existing 3-node suites already takes tens of seconds to
reach READY (kNodeReadyTimeout = 60000ms). An 11-node suite could plausibly need several minutes
for SetUpTestSuite alone, and CI ctest default timeouts may not accommodate it.
**Why it happens:** Node bootstrap (blockchain genesis, CRDT sync, PubSub mesh) time roughly scales
with validator count; 11 validators register into the same genesis registry as 3, but full mesh
CRDT convergence has more edges.
**How to avoid:** Give the new ctest target its own extended timeout (D-15 already anticipates
this — "its own ctest target/timeout"); consider `kNodeCount` as a compile-time constant tunable
down for CI (D-06 already specifies this). Measure actual startup time from a first working build
before committing to CI inclusion cadence.
**Warning signs:** Local dev build takes >2 min for SetUpTestSuite; CI logs show ctest timeout kill.

### Pitfall 3: Mock RPC Transport per-endpoint dispatch ambiguity
**What goes wrong:** `SetTransportFactory` is a single node-wide/validator-wide factory keyed by
URL string. If two of the three "quorum slots" reuse the same mock URL (e.g., copy-pasted
`ep_public1 = ep_direct` pattern seen in the catchup test, which currently uses the SAME URL for
DIRECT/PUBLIC1/PUBLIC2 with only `consensus_weight` differing), the factory cannot dispatch
different behaviors per slot — all three resolve to the same mock config.
**Why it happens:** The catchup suite intentionally uses one Anvil URL for all 3 slots since it's
testing real-RPC catchup, not disagreement. Phase 8's disagreement fault needs 3 DISTINCT URLs
(even if all backed by mocks, e.g. `mock://direct`, `mock://public1`, `mock://public2`) so the
factory can dispatch per-slot.
**How to avoid:** Assign distinct (even if fake/unused-for-networking) URL strings per
`WeightedRpcEndpoint` when constructing the disagreement fixture, and dispatch on those exact
strings in the `TransportFactory` lambda.
**Warning signs:** All 3 endpoints report identical behavior despite configuring different
`MockBehavior` values.

### Pitfall 4: Fuzz harness crashing on valid production inputs (false positives)
**What goes wrong:** `ParseBurnEventValues` and `MintTransactionV2::DeSerializeByteVector` both use
`std::get<T>`/variant access patterns that can throw `std::bad_variant_access` on malformed input.
A naive fuzz harness that doesn't catch/expect these as "reject with error" rather than "crash" will
flag every malformed-but-intentionally-rejected input as a false-positive fuzzer finding, or worse,
mask a real out-of-bounds/UB bug amongst thousands of expected exceptions.
**Why it happens:** `ParseBurnEventValues` already returns `outcome::result` and uses
`std::holds_alternative` checks BEFORE `std::get` (safe pattern, confirmed in source) — good.
But the ABI decoder and `DeSerializeByteVector` paths need the same audit; unchecked buffer
indexing on attacker-controlled byte streams is the actual vulnerability class libFuzzer should
target.
**How to avoid:** Before writing harnesses, grep the 3 target functions for raw `[]` indexing or
`.at()`-free access on byte buffers derived directly from fuzzer input; add bounds checks in
production code IF a genuine crash is found (per CLAUDE.md rule 0: fix root cause, don't
work around it in the harness).
**Warning signs:** ASan reports heap-buffer-overflow/use-after-free rather than a clean "invalid
argument" outcome::failure.

## Code Examples

### MintFunds dedup check (exactly-once arbitration point)
```cpp
// Source: src/account/TransactionManager.cpp:586-627
// UTXO reservation check — prevent duplicate mint creation for the same burn
if ( utxo_mgr.IsOutPointReserved( burn_tx_hash, 0 ) || utxo_mgr.IsOutPointConsumed( burn_tx_hash, 0 ) )
{
    return outcome::failure( std::errc::already_connected );
}
// Persistence check — reject if this burn was already executed (survives restart)
const std::string persistence_key = chainid + kBridgeKeySeparator + transaction_hash;
auto existing = datastore->get( key_buffer /* kBridgeExecutedPrefix + persistence_key */ );
if ( existing.has_value() )
{
    return outcome::failure( std::errc::already_connected );
}
```

### PublicChainInputValidator TransportFactory DI seam
```cpp
// Source: src/account/PublicChainInputValidator.hpp:143-156
using TransportFactory = std::function<std::unique_ptr<eth::rpc::JsonRpcTransport>(
    const std::string &url, std::chrono::seconds timeout )>;
void SetTransportFactory( TransportFactory factory ) { transport_factory_ = std::move( factory ); }
```

### GossipPubSub / libp2p Host partition seam
```cpp
// Source: 3rdparty/ipfs-pubsub gossip_pubsub.hpp:204, 3rdparty/libp2p host.hpp:171
void AddPeers( const std::vector<std::string> &bootstrapPeers );   // heal / mesh bootstrap
virtual void disconnect( const peer::PeerId &peer_id ) = 0;        // partition (via GetHost())
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| N/A — no prior mint-race test existed | 11-node concurrent-discovery race test | This phase | First test to exercise true cross-node concurrency on the mint dedup path |
| No fault injection on RPC quorum disagreement (Phase 5 built mock, not yet used for 3-slot divergence) | Per-slot divergent MockRpcTransport configs | This phase | Fills the gap Phase 5 left (mock built, disagreement scenario not yet exercised) |
| No fuzzing in the project | libFuzzer targets for 3 parse/deserialize functions, gated build flag | This phase | First fuzz infrastructure in the codebase — establishes the `addfuzztarget()` pattern for future targets |

**Deprecated/outdated:** None — this is additive infrastructure, no existing test/pattern is being
replaced.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | CI toolchain includes Clang (needed for `-DSGNS_FUZZING=ON` libFuzzer/ASan build) — only verified on local macOS/Apple Clang | Standard Stack, Summary | If CI is MSVC/GCC-only, the fuzz CMake target either needs a dedicated Clang CI leg or fuzzing stays local-only (D-14 already anticipates "manual/local deep runs" as a fallback, so risk is low) |
| A2 | No hard cap on validator/node count beyond `std::vector<std::string>` capacity — confirmed no explicit numeric limit in `Blockchain.hpp`, but full runtime behavior (port allocation, PubSub mesh scaling) at n=11 not empirically tested in this research pass | Summary, Pitfall 2 | If an undiscovered practical limit exists (e.g., consensus quorum math tuned for small N), the 11-node topology may need adjustment — first implementation run will surface this quickly |
| A3 | `PeerId` extraction for a given `GeniusNode`'s pubsub identity (needed for `Host::disconnect(peer_id)`) — exact API not confirmed in this pass | Pattern 4 | Partition test implementation may need extra investigation at plan/implementation time to find the correct PeerId accessor; does not block planning the other two deliverables |

**If this table is empty:** N/A — see above.

## Open Questions (RESOLVED)

1. **Programmatic validator key generation approach** (RESOLVED — see 08-01-PLAN.md Task 1)
   - What we know: Existing suites use a hardcoded `static constexpr const char*[]` array of Anvil
     deterministic private keys (currently 3 entries). `EthereumKeyGenerator` exists and is used
     for deriving SGNS destination addresses from a private key, not for generating new keys.
   - What's unclear: Whether "programmatic" (per D-06: "not the hardcoded 3-key array") means (a)
     extend the array to 11 hardcoded Anvil deterministic keys (simplest, still enumerable/testable
     against Anvil's well-known deterministic account set), or (b) truly generate keys at test
     runtime (adds complexity: must also fund each generated account on Anvil).
   - Recommendation: Default to extending the hardcoded array to 11 well-known Anvil deterministic
     keys (Anvil's default mnemonic derives 10 accounts by default; may need `--accounts` flag or
     extra derivation for an 11th) — simplest path satisfying "not manually typed one at a time"
     intent while staying within already-proven Anvil funding patterns. Confirm with user/planner
     if true runtime keygen is required.

2. **PeerId extraction for partition test target selection** (RESOLVED for planning purposes —
   carried forward as a tracked execution-time investigation in 08-04-PLAN.md Task 2, which begins
   with a small investigation spike before writing partition assertions; if no dedicated accessor
   is found, the documented fallback is parsing the PeerId suffix out of
   `GetPubSub()->GetLocalAddress()`'s multiaddress string, e.g. the trailing `/p2p/<peer_id>`
   component)
   - What we know: `GossipPubSub::GetHost()` returns `libp2p::Host`; `disconnect(peer::PeerId)` is
     the right call.
   - What's unclear: The exact accessor to get a *remote* node's `PeerId` from the local node's
     perspective (likely via the multiaddress returned by `GetLocalAddress()`, or via
     `Host::getPeerInfo()`/connection manager introspection) was not confirmed in this pass.
   - Recommendation: First implementation task for the partition test should spend a small
     investigation spike locating this exact API before writing assertions.

3. **CI cadence / build matrix impact of `-DSGNS_FUZZING=ON`** (RESOLVED — see 08-02-PLAN.md
   Task 1, which adds a `NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"` guard emitting a clear
   `message(STATUS ...)` warning when the fuzzing tier is requested on a non-Clang toolchain,
   ensuring MSVC/GCC CI is never silently broken)
   - What we know: D-13/D-14 specify the flag must not affect MSVC/normal CI, and smoke runs are
     ~60s per fuzzer per PR.
   - What's unclear: Whether the project's existing CI (GitHub Actions or similar — not
     inspected in this pass) has any Clang-based leg already, or whether one must be added.
   - Recommendation: Planner should have an early task to inspect `.github/workflows/` (or
     equivalent CI config) to confirm Clang availability before committing to the smoke-run cadence.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Clang (libFuzzer + ASan) | Fuzz harnesses (D-12/D-13) | ✓ (local macOS) | Apple Clang 16.0.0 | Manual/local-only fuzz runs if CI lacks Clang leg (D-14 already allows this) |
| Foundry (anvil + cast) | Race/fault e2e suite (D-16) | Not verified this session — assume same as existing suites (tests `GTEST_SKIP` cleanly if missing) | — | Existing skip-cleanly pattern already in place |
| GoogleTest/GoogleMock | Race suite | ✓ (already project-wide dependency) | project-pinned | — |

**Missing dependencies with no fallback:** None identified — every dependency either exists
locally or has an established graceful-skip/fallback pattern already in the codebase.

**Missing dependencies with fallback:** Clang-based CI leg for fuzzing (falls back to local-only
runs per D-14).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest/GoogleMock (`addtest()` in `cmake/functions.cmake`) |
| Config file | `test/src/bridge_race/CMakeLists.txt` (new, mirrors `test/src/bridge_e2e/CMakeLists.txt`) |
| Quick run command | `./build/<platform>/<config>/test_bin/bridge_race_single_burn_test` |
| Full suite command | `ctest -R bridge_race` (after new target(s) added) |

### Phase Requirements → Test Map
No formal REQ-IDs are mapped for Phase 8 (per orchestrator input: "none mapped (TBD)"). Using the
CONTEXT.md decisions as the effective requirement set:

| Req ID (informal) | Behavior | Test Type | Automated Command | File Exists? |
|--------------------|----------|-----------|--------------------|-------------|
| D-01/D-02/D-03/D-04 | Single contested burn: exactly-once mint via watcher race | e2e | `ctest -R bridge_race_single_burn_test` | ❌ Wave 0 |
| D-04 (batch) | 3-5 concurrent burns: no cross-burn interference | e2e | `ctest -R bridge_race_batch_test` | ❌ Wave 0 |
| D-10 | Node kill mid-mint via `node.reset()` | e2e | `ctest -R bridge_race_fault_kill_test` | ❌ Wave 0 |
| D-08/D-09 | RPC endpoint disagreement (fault injection) | e2e | `ctest -R bridge_race_fault_rpc_test` | ❌ Wave 0 |
| D-08/D-11 | Pubsub partition + heal, CRDT converges to exactly-once | e2e | `ctest -R bridge_race_fault_partition_test` | ❌ Wave 0 |
| D-12 | `ParseBurnEventValues` fuzz harness | fuzz (manual/CI-smoke) | `./fuzz_parse_burn_event_values -runs=1000000 -max_total_time=60 corpus/parse_burn_event_values` | ❌ Wave 0 |
| D-12 | `eth::abi::decode_log` fuzz harness | fuzz | `./fuzz_decode_log -max_total_time=60 corpus/decode_log` | ❌ Wave 0 |
| D-12 | `MintTransactionV2::DeSerializeByteVector` fuzz harness | fuzz | `./fuzz_mint_transaction_deserialize -max_total_time=60 corpus/mint_transaction_deserialize` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** Build and run the single new e2e test binary added in that task (or the
  relevant fuzz target with a short `-max_total_time=10` smoke run).
- **Per wave merge:** `ctest -R bridge_race` (all e2e) plus all 3 fuzzer smoke runs (~60s each).
- **Phase gate:** Full `ctest -R bridge_race` green, all 3 fuzzers pass their smoke run replaying
  the checked-in seed corpus, before `/gsd:verify-work`.

### Wave 0 Gaps
- [ ] `test/src/bridge_race/CMakeLists.txt` — new test target registration
- [ ] `test/src/bridge_race/bridge_race_fixture.hpp` — 11-node SetUpTestSuite helper
- [ ] `cmake/functions.cmake` `addfuzztarget()` function — new build primitive, framework install step
- [ ] `fuzz/CMakeLists.txt` + seed corpus directories — fuzzing framework not yet present at all
- [ ] Extend `test/src/mock/mock_rpc_config.hpp` with a helper to build 3 divergent per-slot
      `MockEndpointConfig`s (small, additive)

## Security Domain

> `security_enforcement` not found in `.planning/config.json` scan for this session; treating as
> enabled per default per protocol.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | no | N/A — test infra, no auth surface changed |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A — no new production access-control logic |
| V5 Input Validation | yes | The 3 fuzz targets ARE the input-validation surface under test: `ParseBurnEventValues`, `decode_log`, `DeSerializeByteVector` must reject malformed untrusted bytes via `outcome::result`/exceptions without UB (Pitfall 4) |
| V6 Cryptography | no | No new crypto introduced; existing key/signature handling untouched |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|----------------------|
| Malformed/malicious burn-event ABI payload causing crash or memory corruption in parser | Tampering / Denial of Service | libFuzzer + ASan harness on `ParseBurnEventValues`/`decode_log` — this phase's explicit goal (D-12) |
| Malformed transaction bytes causing crash in `MintTransactionV2::DeSerializeByteVector` | Tampering / DoS | libFuzzer + ASan harness (D-12) — same mitigation |
| Malicious/compromised RPC endpoint returning divergent data to force incorrect mint | Tampering / Spoofing | Already mitigated in production by `WeightedRpcEndpoint` >75% weighted quorum; this phase's RPC-disagreement fault test (D-09) VERIFIES that mitigation holds, it does not introduce a new one |
| Double-mint via race condition across distributed watchers | Tampering / Repudiation (financial double-spend equivalent) | Already mitigated by `TransactionManager::MintFunds` UTXO-reservation + CRDT-persistence dedup (traced above); this phase's mint-race test VERIFIES that mitigation holds under true concurrency |

## Sources

### Primary (HIGH confidence — direct codebase reads this session)
- `test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp` — full read, node bootstrap ordering,
  dedup-relevant comments, WeightedRpcEndpoint slot pattern
- `test/src/mock/mock_rpc_transport.hpp`, `mock_rpc_config.hpp` — existing Mock RPC Transport API
- `src/account/PublicChainInputValidator.hpp` — `SetTransportFactory`, `WeightedRpcEndpoint`,
  `TransportFactory` type
- `src/account/TransactionManager.cpp:564-695` — `MintFunds` full dedup logic
- `src/account/BridgeRelayer.cpp:210-` — `ParseBurnEventValues` implementation
- `src/account/MintTransactionV2.hpp`, `src/account/GeniusTransaction.hpp` — deserializer entry
  points and registration pattern
- `evmrelay/include/eth/abi_decoder.hpp` — `decode_log`/`decode_log_data`/`AbiValue` signatures
- `src/blockchain/Blockchain.hpp:100-140` — `SetAdditionalGenesisValidatorAddresses` signature
  (no cap found)
- `3rdparty/ipfs-pubsub/.../gossip_pubsub.hpp` — `AddPeers`, `GetHost`, no `RemovePeer`
- `3rdparty/libp2p/include/libp2p/host/host.hpp:171` — `virtual void disconnect(const peer::PeerId&)`
- `cmake/functions.cmake` — `addtest()`/`addtest_part()` pattern to mirror for `addfuzztarget()`
- `test/src/bridge_e2e/CMakeLists.txt` — existing test registration pattern (WHOLEARCHIVE linking
  per-platform)
- `.planning/codebase/TESTING.md` — test layout/naming conventions
- `.planning/STATE.md`, `.planning/phases/08-.../08-CONTEXT.md` — locked decisions and history
- `clang --version` (local shell) — confirmed Apple Clang 16.0.0 available

### Secondary (MEDIUM confidence)
- None used this session — all claims verified directly against source.

### Tertiary (LOW confidence)
- PeerId extraction API for partition target selection (Open Question 2) — not directly confirmed,
  flagged for implementation-time investigation.
- CI toolchain Clang availability for `-DSGNS_FUZZING=ON` (Assumption A1) — only local Apple Clang
  confirmed, CI config not inspected this session.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no external packages; all tooling already present in repo/toolchain
- Architecture: HIGH — every seam (TransportFactory, disconnect(), MintFunds dedup, deserializer
  registration) directly read from source this session
- Pitfalls: HIGH — derived from direct source reading (existing catchup-suite ordering,
  same-URL-for-3-slots pattern, variant-access safety in ParseBurnEventValues)

**Research date:** 2026-07-16
**Valid until:** 30 days (codebase-internal research on a stable, already-merged datapath;
re-verify if Phase 5/05.1/05.2 branches merge additional changes to TransactionManager,
PublicChainInputValidator, or BridgeRelayer before this phase is planned/executed)
