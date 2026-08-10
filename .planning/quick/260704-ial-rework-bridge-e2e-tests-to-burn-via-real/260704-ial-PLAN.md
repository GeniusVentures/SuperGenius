---
phase: quick-260704-ial
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - test/src/bridge_e2e/anvil_fixture.hpp
  - test/src/bridge_e2e/bridge_anvil_e2e_test.cpp
  - test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
autonomous: true
requirements:
  - IAL-01 (rework burn-seeding to real bridgeOut entrypoint)
  - IAL-02 (correct v2 BridgeOutInitiated topic0)
  - IAL-03 (derive sgnsDestination/destinationYOdd from node GetAddress())
must_haves:
  truths:
    - "Burn-seeding cast send calls invoke bridgeOut(uint256,uint256,uint256,bytes32,bool) on the GNUS contract, not ERC-1155 safeTransferFrom"
    - "cast send for bridgeOut succeeds (rc==0, non-empty transactionHash) — no gas-estimate revert"
    - "kBridgeEventTopic0 in anvil_fixture.hpp equals the keccak256 of the v2 BridgeOutInitiated event signature"
    - "accepted_topic0_hashes derived from kBridgeEventTopic0 in both test files match the v2 topic0"
    - "The bytes32 sgnsDestination passed to bridgeOut seeds from node_main->GetAddress() such that the relayer's v2 decompression path reconstructs the same address"
    - "destinationYOdd passed to bridgeOut matches the parity of the Y half of node_main->GetAddress()"
    - "Both bridge_anvil_e2e_test and bridge_anvil_catchup_e2e_test build and run; the catch-up/auto-mint path executes (not skipped at the burn step)"
  artifacts:
    - path: "test/src/bridge_e2e/anvil_fixture.hpp"
      provides: "kDestChainId constant, BridgeDestinationFromSgnsAddress() helper, SendBridgeOutBurn() helper, corrected kBridgeEventTopic0"
      contains: "SendBridgeOutBurn"
    - path: "test/src/bridge_e2e/bridge_anvil_e2e_test.cpp"
      provides: "BurnToMintPipeline and AnvilReplayRejection using SendBridgeOutBurn"
      contains: "SendBridgeOutBurn"
    - path: "test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp"
      provides: "StartupCatchupScanAutoMintsHistoricalBurns using SendBridgeOutBurn"
      contains: "SendBridgeOutBurn"
  key_links:
    - from: "test/src/bridge_e2e/anvil_fixture.hpp::BridgeDestinationFromSgnsAddress"
      to: "evmrelay/src/eth/secp256k1_utility.cpp::DecompressXOnlyPubkey"
      via: "LSB-first contract byte order convention"
      pattern: "contract_x_bytes\\[kXOnlyKeyBytes - 1 - i\\]"
    - from: "test/src/bridge_e2e/anvil_fixture.hpp::SendBridgeOutBurn"
      to: "TokenContracts/gnus-ai GNUSBridge.sol bridgeOut"
      via: "cast send bridgeOut(uint256,uint256,uint256,bytes32,bool)"
      pattern: "bridgeOut"
    - from: "test/src/bridge_e2e/bridge_anvil_e2e_test.cpp::BurnToMintPipeline"
      to: "test/src/bridge_e2e/anvil_fixture.hpp::SendBridgeOutBurn"
      via: "burn-seeding call site"
      pattern: "SendBridgeOutBurn"
---

<objective>
Rework the two bridge E2E test files to seed burns via the real GNUS `bridgeOut()`
entrypoint instead of the incorrect ERC-1155 `safeTransferFrom(self,self,id=0,amount,0x)`
call (which is not a burn and reverts at gas estimate), and correct the bridge event
topic0 to the v2 `BridgeOutInitiated` signature that the relayer v2 path is already
wired to parse.

Purpose: The committed tests currently fail to seed any burn (safeTransferFrom is not a
burn path on the hybrid ERC-20/ERC-1155 GNUS contract), so neither the live Anvil
burn-to-mint pipeline nor the startup catch-up auto-mint path can exercise end-to-end.
Switching to the real entrypoint makes both tests exercise the production code path the
relayer (BridgeRelayer.cpp v2 branch, calling eth::DecompressXOnlyPubkey) actually runs.

Output: Three modified files (no new files). The fixture gains a destination-derivation
helper and a `SendBridgeOutBurn` helper; both test files replace their safeTransferFrom
burn-seeding with the new helper.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@./CLAUDE.md
@.planning/STATE.md
@test/src/bridge_e2e/anvil_fixture.hpp
@test/src/bridge_e2e/bridge_anvil_e2e_test.cpp
@test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp
@src/account/BridgeEventTypes.hpp
@src/account/BridgeRelayer.cpp
@evmrelay/src/eth/secp256k1_utility.cpp
@evmrelay/include/eth/secp256k1_utility.hpp
</context>

<interfaces>
<!-- Contracts and conventions the executor must respect. No codebase exploration needed. -->

From test/src/bridge_e2e/anvil_fixture.hpp (existing helpers the new code reuses):
```cpp
namespace sgns::test::anvil {
    inline constexpr const char *kAnvilAccount0Address     = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266";
    inline constexpr const char *kAnvilAccount0PrivateKey  = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
    inline constexpr const char *kSepoliaChainId           = "11155111";
    inline constexpr const char *kSepoliaBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";
    inline constexpr const char *kBridgeEventTopic0;  // VALUE TO REPLACE

    static inline std::string RunShellCapture( const std::string &command, int &exit_code );
    static inline std::string ParseTxHashFromCastJson( const std::string &cast_output );
}
```

From src/account/BridgeEventTypes.hpp (the v2 signature — single source of truth):
```cpp
inline constexpr std::string_view kBridgeOutInitiatedSig =
    "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)";
// topic0 MUST be keccak256 of this signature =
//   0xafc92ac6b47a7def03c9905a815ef0108134b18254db535467dfbb83792424b5
```

From evmrelay/src/eth/secp256k1_utility.cpp (the byte-order contract that
BridgeDestinationFromSgnsAddress must satisfy):
```cpp
// DecompressXOnlyPubkey receives contract_x_bytes (LSB-first in hex) and:
//   1. Reverses contract_x_bytes[i] -> x_bigendian[31 - i]  (line 194-198)
//   2. Builds compressed [prefix][x_bigendian], prefix from destination_y_odd
//   3. Decompresses, then writes Y back in contract byte order (LSB-first):
//        contract_y[i] = uncompressed[64 - i]   (line 237-241)
//   4. Returns hex(contract_x_bytes) + hex(contract_y) (128 chars, no 0x prefix)
//
// CONSEQUENCE: the bytes32 sgnsDestination passed to bridgeOut() is consumed
// DIRECTLY as contract_x_bytes — so it must already be in LSB-first order.
// node->GetAddress() returns the 128-char hex X||Y where both halves are
// already LSB-first.  Therefore:
//   sgnsDestination (bytes32) = first 64 hex chars of GetAddress()  (no reversal)
//   destinationYOdd = (low bit of first byte of Y half) =
//                     (second hex char of Y half is odd-valued)
```

Real bridge burn entrypoint (TokenContracts/gnus-ai GNUSBridge.sol, from verified_findings):
```solidity
function bridgeOut(uint256 amount, uint256 id, uint256 destChainID,
                   bytes32 sgnsDestination, bool destinationYOdd)
// emits BridgeOutInitiated(address indexed sender, uint256 id, uint256 amount,
//   uint256 srcChainID, uint256 destChainID, bytes32 sgnsDestination, bool destinationYOdd)
// Requires: token id created, sender balanceOf(id) >= amount, destChainID != srcChainID.
// GNUS token id = 0 (GNUS_TOKEN_ID).
```
</interfaces>

<tasks>

<task type="auto">
  <name>Task 1: Add bridgeOut helpers + corrected topic0 to anvil_fixture.hpp</name>
  <files>test/src/bridge_e2e/anvil_fixture.hpp</files>
  <behavior>
    - kDestChainId is a constexpr string literal != "11155111" (selected fixed value documented).
    - kBridgeEventTopic0 is REPLACED by a derived `BridgeEventTopic0()` function that returns
      eth::abi::event_signature_hash(kBridgeOutInitiatedSig) hex-encoded — no hardcoded hash.
    - BridgeDestinationFromSgnsAddress("00...99" 128-char X||Y) returns {"0x" + X_half_64chars, parity_bool}.
    - For an address whose Y half's first byte is even, destinationYOdd is false; odd => true.
    - SendBridgeOutBurn returns a non-empty 0x-prefixed tx hash on rc==0, empty string on failure.
  </behavior>
  <action>
Apply THREE minimal, surgical edits to `test/src/bridge_e2e/anvil_fixture.hpp`. Do NOT refactor
any existing helper; only add the new constant/helpers and replace the one-line value of
`kBridgeEventTopic0`.

(1) DERIVE the topic0 — do NOT hardcode the hash. Replace the `inline constexpr const char *kBridgeEventTopic0`
    constant with a derived free function so the topic0 stays in lockstep with the relayer and the
    contract (no magic hash that can drift). The codebase already derives this exact value at runtime
    in `src/account/ChainRpcEndpointProvider.cpp:52` and `src/account/GeniusNode.cpp:2763` via
    `eth::abi::event_signature_hash(std::string(kBridgeOutInitiatedSig))` — mirror that pattern.

    Add the includes needed at the top of anvil_fixture.hpp:
      #include <eth/abi_decoder.hpp>            // eth::abi::event_signature_hash
      #include <base/parse_utility.hpp>         // rlp::base::parse::hex_bytes
      #include "account/BridgeEventTypes.hpp"   // sgns::kBridgeOutInitiatedSig (canonical sig string)

    Replace the `kBridgeEventTopic0` constant declaration with:

        /**
         * @brief BridgeOutInitiated event topic0 (v2), DERIVED from the canonical signature
         *        via keccak256 — never a hardcoded hash.
         *
         * Computed as eth::abi::event_signature_hash(kBridgeOutInitiatedSig) (the same call the
         * relayer and ChainRpcEndpointProvider use), hex-encoded with the 0x prefix. Both
         * downstream test files build accepted_topic0_hashes from this, so the v2 topic0
         * propagates automatically and cannot drift from the contract/relayer definition.
         */
        static inline std::string BridgeEventTopic0()
        {
            const auto hash = eth::abi::event_signature_hash( std::string( sgns::kBridgeOutInitiatedSig ) );
            return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
        }

    Then UPDATE BOTH CALL SITES (one per test file) where they currently build
    `accepted_topic0_hashes = { sgns::test::anvil::kBridgeEventTopic0 };` → change to
    `{ sgns::test::anvil::BridgeEventTopic0() };`. These are the ONLY two references to the old
    constant name; both become function calls. (Confirm with `grep -rn kBridgeEventTopic0 test/src/bridge_e2e/`
    that no other references remain after the rename.) Call this derivation + the two call-site
    updates out in the task summary.

(2) Add a new `kDestChainId` constexpr immediately AFTER `kSepoliaChainId` (around line 54):
    `inline constexpr const char *kDestChainId = "1";`
    Document why in a Doxygen comment: "Destination chain ID used in the bridgeOut() test burn —
    Ethereum mainnet (1). MUST differ from kSepoliaChainId (\"11155111\") because GNUSBridge.sol
    requires destChainID != srcChainID; mainnet is the natural canonical pairing and is never
    confused with the test's Sepolia fork."

(3) Add TWO new free helper functions in the "Free helper functions" section (immediately AFTER
    the existing `ParseTxHashFromCastJson` definition, BEFORE `FundAccount0WithGnus`).
    Allman bracing, 4-space indent, 120-char lines, kCamelCase constexpr, camelCase vars, no
    magic numbers (use named constexpr), Doxygen headers, noexcept where appropriate.

    Helper A: `BridgeDestinationFromSgnsAddress`
    Signature:
    `static inline std::pair<std::string,bool> BridgeDestinationFromSgnsAddress( const std::string &sgns_address_128 ) noexcept`
    Behavior (CRITICAL — verify each step against the byte-order contract in <interfaces>):
      - Define `constexpr unsigned int kSgnsAddressHexLen = 128u;`,
        `constexpr unsigned int kHalfLen = 64u;`, `constexpr unsigned int kByteHexChars = 2u;`.
      - If `sgns_address_128.size() != kSgnsAddressHexLen` return `{ "", false }`.
      - Validate every char of `sgns_address_128` is xdigit; on failure return `{ "", false }`.
      - `x_half = sgns_address_128.substr( 0, kHalfLen );` (first 64 hex chars = X in LSB-first
        contract byte order — this is EXACTLY what the relayer consumes as contract_x_bytes, so
        NO reversal is performed).
      - `y_half = sgns_address_128.substr( kHalfLen, kHalfLen );` (last 64 hex chars = Y in
        LSB-first; first byte of Y half = chars [0..1] of y_half, whose LSB encodes Y parity).
      - `y_first_byte_hex = y_half.substr( 0, kByteHexChars );`
      - Parse `y_first_byte_hex` to an unsigned int via `std::stoul` with base 16 (wrap in
        try/catch to satisfy noexcept; on exception return `{ "", false }`).
      - `destination_y_odd = ( y_first_byte & 1u ) != 0u;` (matches the relayer's prefix selection:
        false=even Y/0x02, true=odd Y/0x03, per secp256k1_utility.cpp:190).
      - Return `{ "0x" + x_half, destination_y_odd }`. The "0x" prefix is required because cast
        sends bytes32 args as 0x-prefixed hex.
    Doxygen: explain that this is the inverse of the relayer's v2 decompression contract —
    sgns_address_128 is the bare 128-char hex returned by node->GetAddress(), and the bytes32
    passed to bridgeOut must equal node X in contract (LSB-first) byte order, which is the first
    64 chars of GetAddress() unchanged.

    Helper B: `SendBridgeOutBurn`
    Signature:
    `static inline std::string SendBridgeOutBurn( const std::string &anvil_rpc_url,
                                                  uint64_t           amount,
                                                  const std::string &sgns_destination_128 )`
    Behavior:
      - Define `constexpr unsigned int kGnusTokenId = 0u;` (GNUS_TOKEN_ID in TokenContracts).
        Reuse `kAnvilAccount0Address`, `kAnvilAccount0PrivateKey`, `kSepoliaBridgeContractLower`,
        `kDestChainId` from this header.
      - Call `BridgeDestinationFromSgnsAddress( sgns_destination_128 )`; if the returned
        `first` is empty, `spdlog::error` and return `{}`.
      - Construct the cast send command exactly:
          `cast send <kSepoliaBridgeContractLower> "bridgeOut(uint256,uint256,uint256,bytes32,bool)" <amount> <kGnusTokenId> <kDestChainId> <dest_bytes32_0x> <true|false> --unlocked --from <kAnvilAccount0Address> --rpc-url <anvil_rpc_url> --json 2>&1`
        where `<dest_bytes32_0x>` is the helper-A returned string, and `<true|false>` is
        `"true"` if the parity bool is true else `"false"`. Use `--unlocked` + `--from` to
        match the just-verified FundAccount0WithGnus pattern (DO NOT use --private-key here;
        account #0 is unlocked by Anvil's default mnemonic mode and the existing funding path
        already uses --unlocked, verified correct in finding #5).
      - `RunShellCapture( cmd, rc )`. If `rc != 0`, `spdlog::error( "SendBridgeOutBurn: cast send failed rc={} output={}", rc, output )` and return `{}`.
      - `tx_hash = ParseTxHashFromCastJson( output )`. If empty, spdlog::error and return `{}`.
      - spdlog::info the parsed tx hash and return it.
    Doxygen: state that this replaces the prior safeTransferFrom burn-seeding (which was NOT a
    burn on the hybrid ERC-20/ERC-1155 GNUS contract and reverted at gas estimate) and exercises
    the production burn path consumed by BridgeRelayer's v2 parsing branch.

Use the existing `#include` set; do NOT add new headers (std::pair is brought in transitively
via <string> + the existing utility includes; if the compiler complains, add <utility> — but
try without first per minimal-change philosophy). Add only TWO named numeric constants
(kGnusTokenId inside SendBridgeOutBurn's body, the constexpr block at the top of helper A) —
no other magic numbers anywhere.
  </action>
  <verify>
    <automated>cd build/OSX/Debug && cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1; ninja bridge_anvil_e2e_test bridge_anvil_catchup_e2e_test 2>&1 | tail -40</automated>
  </verify>
  <done>
kBridgeEventTopic0 line value is the v2 BridgeOutInitiated topic0. kDestChainId is added.
BridgeDestinationFromSgnsAddress and SendBridgeOutBurn are added with full Doxygen headers,
Allman bracing, no magic numbers, and compile cleanly. Both downstream test targets still
build (the test files do not yet reference the new helpers, so they compile unchanged).
  </done>
</task>

<task type="auto">
  <name>Task 2: Replace safeTransferFrom burn-seeding in both test files</name>
  <files>test/src/bridge_e2e/bridge_anvil_e2e_test.cpp, test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp</files>
  <behavior>
    - cast send for burns invokes bridgeOut(...) with a non-Sepolia destChainID, succeeds, returns a tx hash.
    - Burns seed sgnsDestination/destinationYOdd from node_main->GetAddress().
    - accepted_topic0_hashes in both files still references kBridgeEventTopic0 (now v2) — no separate topic0 edits.
  </behavior>
  <action>
Minimal-change edits to TWO test files. Do NOT refactor any unrelated code; do NOT remove the
file-local anonymous-namespace helpers (Base64Decode, BytesToHex, NormalizePrivateKey, WriteSgnsConfig)
in bridge_anvil_e2e_test.cpp — leave them in place even if now unused (the linter can flag them
later; removing them is out of scope for this rework).

=== test/src/bridge_e2e/bridge_anvil_e2e_test.cpp ===

Edit (a) — `BridgeAnvilE2ETest` class members (around lines 213-219):
  - Delete the two static constexpr members `kSepoliaContract` and `kTransferSig`. They are now
    unused inside this fixture (the burn call goes through SendBridgeOutBurn which uses
    kSepoliaBridgeContractLower from the fixture header). If any later test in this fixture
    still references kSepoliaContract (search the file before deleting), KEEP it — but the
    only references are in the two burn command strings being replaced.

Edit (b) — `AnvilBurnToMintPipeline` TEST_F body (lines ~388-405):
  Replace the cast send block:
    ```
    std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) + " \"" + kTransferSig + "\" " + sender_addr +
                           " " + sender_addr + " 0 " + std::to_string( kMintAmount ) + " 0x --private-key " +
                           sgns::test::anvil::kAnvilAccount0PrivateKey + " --rpc-url " + s_anvil.RpcUrl() + " --json 2>&1";
    int cast_rc = -1;
    std::string cast_output = sgns::test::anvil::RunShellCapture( cast_cmd, cast_rc );
    spdlog::info( "bridge_anvil: cast send output: {}", cast_output );
    ASSERT_EQ( cast_rc, 0 ) << "cast send failed with exit code " << cast_rc;
    const std::string tx_hash = sgns::test::anvil::ParseTxHashFromCastJson( cast_output );
    ASSERT_FALSE( tx_hash.empty() ) << "Could not parse transactionHash from cast output";
    ```
  with:
    ```
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, node_main->GetAddress() );
    spdlog::info( "bridge_anvil: burn tx hash = {}", tx_hash );
    ASSERT_FALSE( tx_hash.empty() ) << "bridgeOut burn-seeding failed (cast send rejected the call)";
    ```
  KEEP the surrounding `sender_addr` declaration only if it is still used elsewhere in the test
  body (it is referenced in the spdlog::info at the top of the test — keep that line, or remove
  the sender_addr declaration and the spdlog line together if sender_addr becomes unused; prefer
  keeping the spdlog info line and dropping sender_addr only if truly unreferenced).

Edit (c) — `AnvilReplayRejection` TEST_F body (lines ~442-456):
  Replace the equivalent cast send block with the same `SendBridgeOutBurn` call form, using
  `node_main->GetAddress()` as the destination source. Replace the ASSERT_EQ(cast_rc,0) /
  ParseTxHashFromCastJson pair with the single ASSERT_FALSE(tx_hash.empty()) check.

Edit (d) — `BridgeSepoliaDirectFallbackTest` class:
  LEAVE UNCHANGED. This test uses a user-supplied PRIVATE_KEY against live Sepolia and is
  gated by RUN_E2E_BRIDGE — out of scope for this rework (the verified findings scope this
  plan to the Anvil-path tests; the Sepolia-direct path is not currently runnable in CI and
  its safeTransferFrom call can be reworked in a follow-up when the bridgeOut path is
  confirmed working on Anvil). DO NOT touch lines 499-761.

=== test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp ===

Edit (e) — `BridgeAnvilCatchupE2ETest` class members (around lines 90-99):
  - Delete the two static constexpr members `kSepoliaContract` and `kTransferSig`.
    Verify the file has no remaining references before deleting (they appear only inside
    `SendOneAnvilBurn`).

Edit (f) — Replace the entire `SendOneAnvilBurn` static method body (lines ~228-249) with
  a thin wrapper around the fixture helper. The catch-up test seeds burns BEFORE node_main
  exists, so the destination cannot come from node_main->GetAddress() at seeding time. Use a
  known-fixture SG address instead: since the catch-up fixture's node_main is created from
  `kAnvilAccount0HexKey` (the deterministic Anvil key #0), its GetAddress() is deterministic
  and is the SAME across runs — but it is not known at compile time. So: refactor
  `SendOneAnvilBurn` to take the sgns_destination_128 as a parameter, OR keep it parameterless
  and require the caller to pass the destination. The MINIMAL change is:

  Change the signature of `SendOneAnvilBurn` to:
    `static std::string SendOneAnvilBurn( const std::string &sgns_destination_128 );`

  Replace the body with:
    ```
    return sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, sgns_destination_128 );
    ```

  Update the class declaration (around line 183) to match the new signature (one parameter).
  Update the Doxygen on SendOneAnvilBurn to reflect that it now wraps the fixture's
  bridgeOut helper and takes the SG destination as a 128-char hex X||Y string.

Edit (g) — `SetUpTestSuite` burn-seeding loop (lines ~300-312):
  The seeding loop currently runs BEFORE node_main is constructed. To seed against
  node_main's eventual address, REORDER so node_main is created BEFORE the burn-seeding loop,
  OR (preferred, minimal-change) seed against a placeholder that is guaranteed to match
  node_main->GetAddress(). The minimal-change option:

  - Move the `node_main = GeniusNode::NewFromPrivateKey( gGeniusNodeConfig, kAnvilAccount0HexKey, false, kNodeMainPort, true );`
    call to BEFORE the burn-seeding loop (it currently lives at line ~317). Do NOT move the
    poll-for-CREATING, ConfigureRpcEndpoint, SetAuthorizedFullNodeAddress, or READY-wait —
    only the NewFromPrivateKey call itself needs to precede the burn seeding so
    `node_main->GetAddress()` is available. The poll/prime/READY-wait sequence stays in its
    current order relative to each other.
  - In the burn-seeding loop, pass `node_main->GetAddress()`:
    ```
    const std::string tx_hash = SendOneAnvilBurn( node_main->GetAddress() );
    ```
  - Leave the `s_pre_node_burn_hashes.push_back` and ASSERT_EQ size checks unchanged.

  NOTE: This reordering is semantically safe — creating node_main earlier does not start any
  RPC traffic (the ConfigureRpcEndpoint / PubSub bootstrap happens later), and the burn
  transactions land on Anvil regardless of node_main's lifecycle. The TEST body assertion
  (EXPECT_WAIT_FOR_CONDITION on GetBalance(dest_addr)) is unaffected because dest_addr is
  read from node_main->GetAddress() in the TEST_F body, which already matches.

  Doxygen on SendOneAnvilBurn: add a one-line note that the seeding now uses the real
  bridgeOut entrypoint and node_main's deterministic SG address.

Use Allman bracing, 4-space indent, 120-char lines, no magic numbers in any new code. Do NOT
introduce new headers. Do NOT touch the Sepolia-direct fixture. Do NOT push.
  </action>
  <verify>
    <automated>cd build/OSX/Debug && ninja bridge_anvil_e2e_test bridge_anvil_catchup_e2e_test && ./bridge_anvil_e2e_test --gtest_filter='BridgeAnvilE2ETest.*' && ./bridge_anvil_catchup_e2e_test</automated>
  </verify>
  <done>
Both test files build. The Anvil burn-to-mint pipeline and replay-rejection tests use
SendBridgeOutBurn with node_main->GetAddress(); the catch-up test seeds via SendOneAnvilBurn(
node_main->GetAddress() ) after the minimal NewFromPrivateKey reorder. Running both binaries
shows: (1) bridge_anvil_e2e_test burns now succeed (no "execution reverted" gas-estimate
failure), MintTokens mints, balance increases; (2) bridge_anvil_catchup_e2e_test pre-node
burns seed successfully, the catch-up scan discovers BridgeOutInitiated events matching the
v2 topic0, and the auto-mint path fires (recipient balance increases by >= kNumCatchupBurns *
kMintAmount). The Sepolia-direct test file region is unchanged.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| Test process -> local Anvil fork | cast send calls cross into the local Anvil subprocess; the fork is local-only and isolated from live Sepolia state |
| Test process -> live Sepolia (Path B only) | The Sepolia-direct fixture (BridgeSepoliaDirectFallbackTest) is out of scope and unchanged; gating env vars (RUN_E2E_BRIDGE, PRIVATE_KEY) remain intact |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-IAL-01 | Tampering | cast send argument construction in SendBridgeOutBurn | mitigate | Argument order matches the verified GNUSBridge.sol signature exactly (amount, id, destChainID, sgnsDestination, destinationYOdd); use --unlocked + --from account #0 (verified-correct funding path) |
| T-IAL-02 | Tampering | bytes32 sgnsDestination byte order | mitigate | BridgeDestinationFromSgnsAddress performs NO reversal — first 64 hex chars of GetAddress() are passed through directly as the bytes32, matching DecompressXOnlyPubkey's LSB-first contract-x-bytes consumption (secp256k1_utility.cpp:194-198) |
| T-IAL-03 | Information | destinationYOdd parity derivation | mitigate | Parity read from low bit of the FIRST byte of the Y half (LSB-first storage), matching contract byte order; explicitly verified against secp256k1_utility.cpp:190 prefix selection |
| T-IAL-04 | Repudiation | destChainID selection | accept | Fixed constexpr "1" (Ethereum mainnet); cannot collide with Sepolia 11155111; documented rationale; low-impact test fixture |
| T-IAL-05 | Denial | Anvil subprocess not started when SendBridgeOutBurn called | mitigate | All call sites are downstream of ASSERT_TRUE(s_anvil.Start()) && WaitForReady() && FundAccount0WithGnus() in SetUpTestSuite; failure short-circuits via GTEST_SKIP before any burn call |
| T-IAL-SC | Tampering | (n/a — no package-manager installs in this plan) | accept | Plan touches no package.json / vcpkg / conan; all dependencies (cast, anvil, spdlog, gtest) are already in the project |

</threat_model>

<verification>
- `cd build/OSX/Debug && ninja bridge_anvil_e2e_test bridge_anvil_catchup_e2e_test` — both targets build cleanly.
- Run `./bridge_anvil_e2e_test --gtest_filter='BridgeAnvilE2ETest.*'` — both AnvilBurnToMintPipeline and AnvilReplayRejection pass; cast send no longer reverts; balance increases.
- Run `./bridge_anvil_catchup_e2e_test` — StartupCatchupScanAutoMintsHistoricalBurns passes; pre-node burns seed; the auto-mint path mints all kNumCatchupBurns.
- No new include directives; no new files; only the three files in files_modified are touched.
- Sepolia-direct test (BridgeSepoliaDirectFallbackTest) is unchanged and still skips under default env.
</verification>

<success_criteria>
- All three files compile; both test binaries link.
- The Anvil burn-to-mint pipeline end-to-end test passes against a local Sepolia-forking Anvil.
- The startup catch-up scan test passes — historical BridgeOutInitiated events are discovered via the v2 topic0 and auto-minted.
- kBridgeEventTopic0 is replaced by derived BridgeEventTopic0() (= keccak256 of the v2 BridgeOutInitiated signature); both test files' accepted_topic0_hashes call it.
- No safeTransferFrom burn-seeding remains in either Anvil-path test file.
- Commit only (no push, per constraint).
</success_criteria>

<output>
Create `.planning/quick/260704-ial-rework-bridge-e2e-tests-to-burn-via-real/260704-ial-SUMMARY.md`
when done.
</output>
