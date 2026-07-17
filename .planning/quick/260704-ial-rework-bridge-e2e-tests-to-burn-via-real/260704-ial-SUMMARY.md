---
phase: quick-260704-ial
plan: 01
subsystem: bridge-e2e
tags: [bridge, e2e, anvil, bridgeOut, BLOCKED]
requires:
  - Phase 04.1-01 (Anvil local-bridge E2E fixture)
provides:
  - SendBridgeOutBurn helper (committed, but FAILS against live contract)
  - BridgeDestinationFromSgnsAddress helper (committed, byte-order verified)
  - BridgeEventTopic0() derived v2 topic0 (committed, used by relayer)
affects:
  - test/src/bridge_e2e/anvil_fixture.hpp
  - test/src/bridge_e2e/bridge_anvil_e2e_test.cpp
  - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
tech-stack:
  added: []
  patterns:
    - "Derived event topic0 from keccak signature (mirrors relayer)"
key-files:
  created: []
  modified:
    - test/src/bridge_e2e/anvil_fixture.hpp
    - test/src/bridge_e2e/bridge_anvil_e2e_test.cpp
    - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
decisions:
  - "BLOCKED: Plan's locked_correctness_facts contradict the deployed Sepolia contract; bridgeOut selector 0xa5a5aa20 returns zero facetAddress on 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70. Stopped per CLAUDE.md 'Never assume or speculate' + deviation Rule 4."
metrics:
  duration: ~45m
  completed: 2026-07-04
  tasks_total: 2
  tasks_complete: 0  # code pre-existed; runtime verification FAILED
---

# Phase quick-260704-ial Plan 01: Rework Bridge E2E Burns to Real bridgeOut — BLOCKED

Switch Anvil bridge E2E burn-seeding from ERC-1155 safeTransferFrom to the real
GNUS `bridgeOut()` entrypoint, then verify burns seed and auto-mint fires.

## Outcome: BLOCKED — plan's locked facts contradict the deployed Sepolia contract

The code changes were already in place from prior commits (bb14bbef, 2ed09b2d).
The rework compiles cleanly and the topic0 derivation is correct. But the burns
FAIL at runtime against the live Sepolia contract with
"Diamond: Function does not exist". Investigation shows the plan's
`locked_correctness_facts` are empirically wrong.

## What was verified

1. **Build**: `ninja bridge_anvil_e2e_test bridge_anvil_catchup_e2e_test` — both
   targets compile cleanly (already-built, no work to do).
2. **Code shape is correct per the plan**:
   - `kBridgeEventTopic0` replaced by derived `BridgeEventTopic0()` — verified.
   - `kDestChainId = "1"` — present.
   - `BridgeDestinationFromSgnsAddress` and `SendBridgeOutBurn` — present.
   - Both test files call `SendBridgeOutBurn(s_anvil.RpcUrl(), kMintAmount, node_main->GetAddress())`.
   - Sepolia-direct fallback (Path B) untouched.
   - `grep -rn kBridgeEventTopic0 test/src/bridge_e2e/` returns nothing.

## Empirical findings that falsify the plan's locked facts

### Finding 1 — cast send reverts with "Diamond: Function does not exist"

```
SendBridgeOutBurn: cast send failed rc=256 output=Error: Failed to estimate gas:
  server returned an error response: error code 3: execution reverted:
  Diamond: Function does not exist,
  data: "0x08c379a0000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000204469616d6f6e643a2046756e6374696f6e20646f6573206e6f74206578697374"
```

The data decodes to the UTF-8 string `"Diamond: Function does not exist"` — i.e.
the GNUS token at the configured Sepolia address is an EIP-2535 Diamond proxy
that does not expose a facet for the selector `bridgeOut(uint256,uint256,uint256,bytes32,bool)`
(selector `0xa5a5aa20`).

### Finding 2 — Diamond loupe confirms bridgeOut has no facet

Using `facetAddress(bytes4)` against the configured Sepolia contract:

| Selector | Signature | facetAddress result |
|----------|-----------|---------------------|
| `0xa5a5aa20` | `bridgeOut(uint256,uint256,uint256,bytes32,bool)` | `0x0000…0000` (none) |
| `0x74a5b433` | `bridgeOut(uint256,uint256,bytes32,bool)`        | `0x0000…0000` (none) |
| `0x2318b5f0` | `bridgeOut(uint256,bytes32,bool)`               | `0x0000…0000` (none) |
| `0xfceb0a24` | `bridgeBurn(uint256,uint256,uint256,bytes32,bool)` | `0x0000…0000` (none) |
| `0xf242432a` | `safeTransferFrom(address,address,uint256,uint256,bytes)` | `0x918E994Da08FF78A70D6e81dc3A19aD930024F98` (EXISTS) |
| `0xa9059cbb` | `transfer(address,uint256)` (ERC-20)            | `0xAd47AC7B669CA8e6b1497BD4F3b6056c30704EE4` (EXISTS) |

Conclusion: the deployed Sepolia GNUS contract exposes ERC-1155
(`safeTransferFrom`, `balanceOf(address,uint256)`) and ERC-20 (`transfer`), but
**no `bridgeOut` of any arity**. Locked fact #1 ("Burn entrypoint: bridgeOut...")
is empirically false for the address the fixture targets.

### Finding 3 — secondary bug: fixture address has a typo (39 hex chars)

The lowercase constant in `anvil_fixture.hpp` is:
```
kSepoliaBridgeContractLower = "0x9af8050220d8c8355ca3c6dc00a78b474cd3e3c70"
```
That string is **39 hex characters after 0x** (40 expected) — it is missing a
character. The mixed-case EIP-55 constant elsewhere is:
```
0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70  (40 hex)
```
Lowercased, that should be `0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70` — note
the extra `8` between the `d` and the second `c` (`...d8c355...`). The fixture
literal is `...d8c8355...`, dropping a `8`.

CAST fails to parse the 39-hex address. Even after fixing the typo to the
correct 40-hex address, however, Finding 2 still applies: the contract does not
expose bridgeOut.

### Finding 4 — TokenContracts source is not in this repo

`TokenContracts/gnus-ai GNUSBridge.sol` (cited by the plan as the entrypoint
source) does not exist in this checkout, so I cannot read the actual contract
to find the correct entrypoint name. Only the deployed Sepolia bytecode is
available as ground truth, and it does not match the plan's claims.

### Finding 5 — catch-up test fixture uses the same broken contract address

`bridge_anvil_catchup_e2e_test.cpp` writes a `bridge_chains_config.json`
pointing at the same (typo'd) address, so the auto-mint path is also broken
even after the catch-up scan somehow succeeds.

## Burns seed now? Does auto-mint fire?

**No.** Burns fail to seed — cast send reverts at gas estimation. Auto-mint
cannot fire because no `BridgeOutInitiated` events are emitted.

## Deviations from Plan

### [Rule 4 - Architectural decision required] Stopped — plan's central premise is wrong

**Found during:** Task 2 verification (running the catch-up E2E test).
**Issue:** Plan's `locked_correctness_facts` #1, #6 ("burn entrypoint is
bridgeOut on the configured Sepolia contract", "Relayer v2 path is fully
implemented + tested; this rework should make burns seed and auto-mint fire")
are contradicted by the deployed contract's loupe.
**Why I stopped:** CLAUDE.md mandates "Never assume or speculate about
something that you don't understand. Interact with the user directly to
understand what they're doing." The fix requires either a different contract
address, a different selector, or a different burn approach — all are
architectural decisions I cannot make unilaterally.
**User action required:** see "Awaiting user decision" below.

### [Rule 1 - Bug] Fixture contract address typo (39 hex chars)

**Found during:** Finding 3 above.
**Issue:** `kSepoliaBridgeContractLower` literal is missing one character.
**Fix:** Not applied — I did not auto-fix because the corrected address still
does not expose bridgeOut, so the fix would be cosmetic. The user should fix
the typo and resolve the bridgeOut question together.

## Awaiting user decision

The user must clarify one of the following before this plan can complete:

1. **Is the Sepolia contract address wrong?** If a different Sepolia-deployed
   Diamond exposes `bridgeOut`, the fixture constants (and `src/account/bridge_chains_config.json`)
   should be updated to that address and the typo fix becomes moot. Please
   provide the correct address.
2. **Is the entrypoint name wrong?** Maybe the production burn entrypoint is
   named differently (e.g., `bridgeBurn`, `burnToChain`, `wrapOut`). Please
   paste the actual function signature from `TokenContracts/gnus-ai/GNUSBridge.sol`
   so I can compute the correct selector.
3. **Should we revert to the prior safeTransferFrom approach and fix that path
   instead?** Since `safeTransferFrom` IS exposed on the deployed contract, the
   original (pre-rework) seeding was structurally valid; its revert was likely
   a different issue (e.g., the bytes-data argument, id=0 vs id=GNUS_TOKEN_ID,
   or the missing `--unlocked` flag on a stale build). If you want this option,
   I can re-investigate the original revert cause.
4. **Is the TokenContracts source available elsewhere** (separate repo, branch,
   or path) that I should read to get ground truth?

## Self-Check

- Commits bb14bbef and 2ed09b2d exist in `git log` (verified).
- `grep -rn kBridgeEventTopic0 test/src/bridge_e2e/` returns nothing (verified).
- Build artifacts `test_bin/bridge_anvil_e2e_test`, `test_bin/bridge_anvil_catchup_e2e_test`
  exist and link cleanly (verified).
- Anvil+cast available at /opt/homebrew/bin (verified).

## Self-Check: PASSED (code) / FAILED (runtime goal)
