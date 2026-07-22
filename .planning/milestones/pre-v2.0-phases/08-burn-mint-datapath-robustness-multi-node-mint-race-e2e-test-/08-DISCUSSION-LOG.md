# Phase 8: Burn/Mint Datapath Robustness - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-16
**Phase:** 08-burn-mint-datapath-robustness-multi-node-mint-race-e2e-test-
**Areas discussed:** Todo fold, Mint-race semantics, Fault-injection scope, Fuzzing setup, Fixture strategy

---

## Todo Fold

| Option | Description | Selected |
|--------|-------------|----------|
| Fold mock-RPC part | Fold task 3 (Mock RPC Transport) as fault-injection mechanism | ✓ |
| Fold entire todo | Include startup-wiring tasks too | |
| Don't fold | Keep todo pending | |

**User's choice:** Fold mock-RPC part

---

## Mint-Race Semantics

| Option | Description | Selected |
|--------|-------------|----------|
| Watchers on all nodes | Zero manual MintTokens; production-realistic | ✓ |
| Simultaneous manual MintTokens | Deterministic timing, bypasses watcher | |
| Both variants | One test per style | |

**User's choice:** Watchers on all nodes

| Option | Description | Selected |
|--------|-------------|----------|
| Balance delta == amount, all nodes | Exact delta on every node + stability window | ✓ |
| Balance delta only on full node | Simpler, misses divergent views | |
| Delta + ledger inspection | Also assert single mint record for the burn hash | |

**User's choice:** Balance delta == amount on all nodes

| Option | Description | Selected |
|--------|-------------|----------|
| Seed burn before watchers start | All discover on first poll | ✓ |
| Natural timing, repeat N times | Probabilistic race | |
| Injectable poll trigger | Test-only watcher API | |

**User's choice:** Seed burn before watchers start

| Option | Description | Selected |
|--------|-------------|----------|
| Single + multi-burn batch | One clean test + 3–5 burn batch | ✓ |
| Single burn only | Minimal | |
| Batch only | Fewer tests, harder diagnosis | |

**User's choice:** Single + multi-burn batch

| Option | Description | Selected |
|--------|-------------|----------|
| Keep 1 Full + 2 Light | Same as existing fixture | |
| Multiple Full nodes | Stronger race, diverges from fixture | |
| You decide | Researcher determines | |

**User's choice:** Free text — "Just one full node but more than 2 light nodes. I'd want at least 10 initially and then expand that later if possible"

| Option | Description | Selected |
|--------|-------------|----------|
| Compile-time constant, CI-tuned | kNodeCount=11, programmatic keys | ✓ |
| Env-var override | Runtime-tunable node count | |
| You decide | Planner measures first | |

**User's choice:** Compile-time constant, CI-tuned

| Option | Description | Selected |
|--------|-------------|----------|
| Light-node addresses | Forces propagation to non-authoring nodes | ✓ |
| Full-node address | Weakest propagation test | |
| Mix across batch | Spread recipients | |

**User's choice:** Light-node addresses

---

## Fault-Injection Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Node kill mid-mint | Destroy node between observe and finalize | ✓ |
| RPC disagreement | Conflicting receipts; quorum must handle | ✓ |
| RPC latency/timeout | Slow/timing-out endpoints | ✓ |
| Network partition | Split pubsub mesh, heal, converge | ✓ |

**User's choice:** All four scenarios (multi-select)

| Option | Description | Selected |
|--------|-------------|----------|
| Mock transport via DI | SetTransportFactory injection, deterministic | ✓ |
| Real proxy in front of Anvil | More realistic, more moving parts | |
| Both | Mock + one proxy e2e test | |

**User's choice:** Mock transport via DI

| Option | Description | Selected |
|--------|-------------|----------|
| Destroy GeniusNode object | node.reset(), same as TearDown | ✓ |
| Separate process + SIGKILL | True crash semantics, needs runner binary | |
| Both | Object now, process later | |

**User's choice:** Destroy GeniusNode object (SIGKILL variant noted as deferred idea)

| Option | Description | Selected |
|--------|-------------|----------|
| You decide | Researcher finds cleanest seam | |
| Disconnect/reconnect peers | libp2p/pubsub layer split + heal | ✓ |
| Test broadcaster filter | CRDT broadcaster pattern | |

**User's choice:** Disconnect/reconnect peers

---

## Fuzzing Setup

| Option | Description | Selected |
|--------|-------------|----------|
| ParseBurnEventValues | Burn-event ABI parsing | ✓ |
| ABI/log decoding | decode_log layer | ✓ |
| bridge_chains_config.json parsing | Config parsing | |
| Transaction deserialization | MintTransactionV2/GeniusTransaction | ✓ |

**User's choice:** Three targets (config parsing excluded)

| Option | Description | Selected |
|--------|-------------|----------|
| CMake fuzzer targets, opt-in flag | -DSGNS_FUZZING=ON, libFuzzer+ASan | ✓ |
| Standalone fuzz/ directory | Separate build tree | |
| You decide | Planner checks toolchain | |

**User's choice:** CMake fuzzer targets behind opt-in flag

| Option | Description | Selected |
|--------|-------------|----------|
| Short CI smoke + local deep runs | ~60s per fuzzer per PR + corpus in repo | ✓ |
| Nightly job now | Scheduled long runs this phase | |
| Local only for now | Defer CI wiring | |

**User's choice:** Short CI smoke + local deep runs (nightly job noted as deferred idea)

---

## Fixture Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| New suite, shared fixture code | New directory reusing anvil_fixture.hpp | ✓ |
| Extend bridge_e2e | Add to existing binaries | |
| You decide | Planner decides | |

**User's choice:** New suite, shared fixture code

| Option | Description | Selected |
|--------|-------------|----------|
| Keep Sepolia fork | Proven pattern, network-dependent | ✓ |
| Fully local Anvil | Deterministic, needs deploy scripts | |
| Mock-only (no Anvil) | Fastest, no real chain | |

**User's choice:** Keep Sepolia fork

---

## Claude's Discretion

- Directory/binary names for new suite and fuzzers
- Watcher release-together mechanism
- Mock RPC Transport class design and placement
- Programmatic validator key generation
- Fuzzer corpus layout
- Partition-test fixture sizing

## Deferred Ideas

- Process-level SIGKILL node crash testing
- Nightly long-run fuzzing CI job
- Scaling beyond ~11 nodes (Shadow/Docker)
- Config-JSON parsing fuzz target
