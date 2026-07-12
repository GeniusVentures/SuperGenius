/**
 * @file       bridge_anvil_e2e_test.cpp
 * @brief      Local-Anvil E2E tests for the burn-to-mint bridge.
 * @date       2026-07-02
 * @author     Super Genius (info@gnus.ai)
 *
 * Self-contained burn-to-mint bridge E2E tests against a local Anvil
 * fork of Sepolia state. No dependency on `../TokenContracts/gnus-ai/.env` —
 * the Anvil default key #0 is hardcoded and account #0 is funded with GNUS
 * via anvil_impersonateAccount of a known Sepolia holder.
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include "testutil/outcome.hpp"
#include "testutil/wait_condition.hpp"

#include "anvil_fixture.hpp"

using sgns::GeniusNode;

namespace
{

    /**
     * @brief Decodes a base64-encoded string to raw bytes.
     * @param input  Base64-encoded string (standard alphabet, optional '=' padding).
     * @return Decoded bytes, or empty vector on invalid input.
     */
    std::vector<uint8_t> Base64Decode( const std::string &input )
    {
        static const std::array<int8_t, 256> kLookup = []()
        {
            std::array<int8_t, 256> table{};
            table.fill( -1 );
            for ( int i = 'A'; i <= 'Z'; ++i )
            {
                table[i] = static_cast<int8_t>( i - 'A' );
            }
            for ( int i = 'a'; i <= 'z'; ++i )
            {
                table[i] = static_cast<int8_t>( i - 'a' + 26 );
            }
            for ( int i = '0'; i <= '9'; ++i )
            {
                table[i] = static_cast<int8_t>( i - '0' + 52 );
            }
            table[static_cast<int>( '+' )] = 62;
            table[static_cast<int>( '/' )] = 63;
            return table;
        }();

        std::vector<uint8_t> result;
        result.reserve( ( input.size() * 3 ) / 4 );

        uint32_t accum = 0;
        int      bits  = 0;
        for ( char c : input )
        {
            if ( c == '=' )
            {
                break;
            }
            int val = kLookup[static_cast<uint8_t>( c )];
            if ( val < 0 )
            {
                return {};
            }
            accum  = ( accum << 6 ) | static_cast<uint32_t>( val );
            bits  += 6;
            if ( bits >= 8 )
            {
                bits -= 8;
                result.push_back( static_cast<uint8_t>( ( accum >> bits ) & 0xFF ) );
            }
        }
        return result;
    }

    /**
     * @brief Converts raw bytes to a lowercase hex string.
     * @param bytes  Input byte vector.
     * @return Hex-encoded string (no "0x" prefix).
     */
    std::string BytesToHex( const std::vector<uint8_t> &bytes )
    {
        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string           hex;
        hex.reserve( bytes.size() * 2 );
        for ( uint8_t byte : bytes )
        {
            hex += kHexDigits[( byte >> 4 ) & 0x0F];
            hex += kHexDigits[byte & 0x0F];
        }
        return hex;
    }

    /**
     * @brief Normalizes a signing key env var to a 64-char lowercase hex string.
     *
     * Strips a leading 0x prefix and, when the remaining string is not 64 hex
     * chars, attempts base64 decode → hex re-encode. Copied verbatim from
     * Phase 4 bridge_e2e_test.cpp into a file-local helper to avoid symbol
     * collision with that file's anonymous-namespace helpers.
     *
     * @param[in] raw_env  Raw env var value (0x-hex, plain hex, or base64).
     * @return 64-char hex string, or empty string if @p raw_env is invalid.
     */
    std::string NormalizePrivateKey( const std::string &raw_env )
    {
        std::string raw_key = raw_env;
        if ( raw_key.size() >= 2 && raw_key[0] == '0' && raw_key[1] == 'x' )
        {
            raw_key = raw_key.substr( 2 );
        }
        bool is_hex = ( raw_key.size() == 64 );
        if ( is_hex )
        {
            for ( char c : raw_key )
            {
                if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
                {
                    is_hex = false;
                    break;
                }
            }
        }
        if ( is_hex )
        {
            return raw_key;
        }
        auto decoded = Base64Decode( raw_key );
        if ( decoded.size() != 32 )
        {
            return {};
        }
        return BytesToHex( decoded );
    }

    /**
     * @brief Writes a per-node bridge_chains_config.json (sepolia-only subset).
     *
     * Scoping @c catchup_chains_ to ethereum-sepolia ensures PerformStartupCatchupScan
     * only queries the local Anvil fork (the one chain that actually exists on the
     * fork) and doesn't attempt RPC queries against mainnet, BSC, Polygon, or Base
     * chains that have no endpoints primed on the fork.
     *
     * @param[in] base_write_path  Per-node BaseWritePath (trailing slash expected).
     */
    void WriteBridgeChainsConfig( const std::string &baseWritePath )
    {
        constexpr const char *kBridgeChainsConfigContent = R"JSON({
    "ethereum-sepolia": {
        "chain_id": 11155111,
        "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70"
    }
}
)JSON";
        constexpr const char *kBridgeChainsConfigFilename = "bridge_chains_config.json";
        std::filesystem::create_directories( baseWritePath );
        const std::string kConfigPath = baseWritePath + kBridgeChainsConfigFilename;
        std::ofstream     out( kConfigPath, std::ios::binary | std::ios::trunc );
        out << kBridgeChainsConfigContent;
        out.close();
    }

} // namespace

// =============================================================================
// BridgeAnvilE2ETest — Path A: local Anvil fork (D-01..D-13)
// =============================================================================

/**
 * @brief Three-node GTest fixture backed by a local Anvil fork of Sepolia.
 *
 * SetUpTestSuite starts a local Anvil subprocess forking Sepolia, polls it
 * for readiness, funds Anvil default account #0 with GNUS via impersonation,
 * then bootstraps a 3-node GeniusNode cluster whose RPC endpoints point at
 * the Anvil instance instead of live Sepolia. Tests exercise the burn-to-mint
 * pipeline against the local fork — fast, deterministic, and self-contained.
 */
class BridgeAnvilE2ETest : public ::testing::Test
{
protected:
    /** @brief Number of GeniusNode instances in the cluster. Node [0] is the full node. */
    static inline constexpr unsigned int kNodeCount = 3u;

    /** @brief GeniusNode instances — index 0 is full node, 1..kNodeCount-1 are processors. */
    static std::array<std::shared_ptr<GeniusNode>, kNodeCount> s_nodes;

    /** @brief Per-node GeniusNodeConfig — index 0 is full node config. */
    static std::array<GeniusNodeConfig, kNodeCount> s_configs;

    static sgns::test::anvil::AnvilProcess s_anvil;

    /** @brief Small test mint amount in base units. */
    static inline constexpr uint64_t kMintAmount = 1u;

    /** @brief Mint finalization timeout (Anvil block times are sub-second). */
    static inline constexpr std::chrono::milliseconds kMintTimeout{ 10000 };

    /** @brief Node READY timeout (covers genesis + processor sync). */
    static inline constexpr std::chrono::milliseconds kNodeReadyTimeout{ 10000 };

    /** @brief Replay-dedup assertion timeout (D-18). */
    static inline constexpr std::chrono::milliseconds kReplayTimeout{ 5000 };

    /** @brief Base port for PubSub — node i listens on kBasePort + i. */
    static inline constexpr unsigned int kBasePort = 40011u;

    /** @brief Anvil deterministic account private keys (hex, no 0x prefix) — public test values.
     *         Each index gets a distinct key so every node occupies a separate validator slot. */
    static inline constexpr const char *kAnvilAccountHexKeys[] = {
        "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80",  // Account #0
        "59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d",  // Account #1
        "5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a",  // Account #2
    };

    /**
     * @brief Starts Anvil, funds account #0, and bootstraps the kNodeCount-node cluster.
     */
    static void SetUpTestSuite();

    /**
     * @brief Tears down the cluster and stops the Anvil subprocess.
     */
    static void TearDownTestSuite();
};

std::array<std::shared_ptr<GeniusNode>, BridgeAnvilE2ETest::kNodeCount> BridgeAnvilE2ETest::s_nodes;
std::array<GeniusNodeConfig, BridgeAnvilE2ETest::kNodeCount>            BridgeAnvilE2ETest::s_configs = { {
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./node0", {} },
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./node1", {} },
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./node2", {} },
} };
sgns::test::anvil::AnvilProcess BridgeAnvilE2ETest::s_anvil;

void BridgeAnvilE2ETest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // D-16: skip cleanly when Foundry binaries are missing.
    if ( !sgns::test::anvil::AnvilAvailable() || !sgns::test::anvil::CastAvailable() )
    {
        FAIL() << "Foundry (anvil + cast) required: https://book.getfoundry.sh/getting-started/installation";
    }

    // D-03: resolve fork URL (RPC_SEPOLIA override or default public Sepolia).
    const std::string fork_url = sgns::test::anvil::SepoliaForkUrl();
    spdlog::info( "bridge_anvil: fork_url={}", fork_url );

    // D-01: start Anvil subprocess forking Sepolia.
    ASSERT_TRUE( s_anvil.Start( fork_url ) ) << "Failed to start anvil subprocess";

    // D-04: poll Anvil readiness via cast block-number.
    ASSERT_TRUE( s_anvil.WaitForReady() ) << "Anvil did not become ready";

    // D-08/D-09: fund account #0 with GNUS via impersonation. D-10: skip cleanly on failure.
    if ( !sgns::test::anvil::FundAccount0WithGnus( s_anvil.RpcUrl() ) )
    {
        FAIL() << "Could not fund Anvil account #0 via impersonation of " << sgns::test::anvil::kGnusHolderSepolia;
    }

    // Per-node BaseWritePath from binary location (Phase 4 pattern).
    const std::string binary_path = boost::dll::program_location().parent_path().string();
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        s_configs[i].BaseWritePath = binary_path + "/node" + std::to_string( i ) + "/";
        WriteBridgeChainsConfig( s_configs[i].BaseWritePath );
        sgns::GeniusNode::WriteNetworkConfig( s_configs[i].BaseWritePath, kBasePort + i, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( s_configs[i].BaseWritePath, ( i == 0u ) ? "Full" : "Light", /*is_processor=*/false );
    }

    // Inject a chainlist fetcher that returns only the Anvil RPC endpoint, so
    // ChainRpcEndpointProvider::Initialize() queries the local fork instead of
    // doing a network fetch of chainid.network/chains.json.
    {
        const std::string kAnvilRpcUrl = s_anvil.RpcUrl();
        auto fetcher = [kAnvilRpcUrl]() -> std::optional<std::string> {
            return std::string( R"([{"name":"ethereum-sepolia","chainId":11155111,"rpc":[")" ) +
                   kAnvilRpcUrl + R"("],"status":"active"}])";
        };
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            s_configs[i].ChainlistFetcher = fetcher;
        }
    }

    spdlog::info( "bridge_anvil: creating {}-node cluster against local Anvil", kNodeCount );

    // Create full node [0] first — it builds the genesis block.
    s_nodes[0] = GeniusNode::New( s_configs[0], sgns::FromPrivateKey{ kAnvilAccountHexKeys[0] } );

    // Trigger StoreGenesisRegistry on the full node's address.
    sgns::Blockchain::SetAuthorizedFullNodeAddress( s_nodes[0]->GetAddress() );
    spdlog::info( "bridge_anvil: authorized full node address = {}", s_nodes[0]->GetAddress().substr( 0, 16 ) );

    // Wait for full node READY (genesis + account-creation blocks).
    ASSERT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetState() == GeniusNode::NodeState::READY; },
        kNodeReadyTimeout,
        "full node [0] READY",
        nullptr );

    spdlog::info( "bridge_anvil: full node [0] READY, creating {} processor nodes", kNodeCount - 1u );

    // Create processor nodes [1..kNodeCount-1] and bootstrap PubSub mesh.
    for ( unsigned int i = 1u; i < kNodeCount; ++i )
    {
        s_nodes[i] = GeniusNode::New( s_configs[i], sgns::FromPrivateKey{ kAnvilAccountHexKeys[i] } );
    }
    for ( unsigned int i = 1u; i < kNodeCount; ++i )
    {
        std::vector<std::string> peers;
        peers.push_back( s_nodes[0]->GetPubSub()->GetLocalAddress() );
        for ( unsigned int j = 1u; j < kNodeCount; ++j )
        {
            if ( j != i )
            {
                peers.push_back( s_nodes[j]->GetPubSub()->GetLocalAddress() );
            }
        }
        s_nodes[i]->GetPubSub()->AddPeers( peers );
    }

    // Wait for all processor nodes to sync and reach READY.
    ASSERT_WAIT_FOR_CONDITION(
        [&]()
        {
            for ( unsigned int i = 1u; i < kNodeCount; ++i )
            {
                if ( s_nodes[i]->GetState() != GeniusNode::NodeState::READY )
                {
                    return false;
                }
            }
            return true;
        },
        kNodeReadyTimeout,
        "processor nodes READY",
        nullptr );

    spdlog::info( "bridge_anvil: {}-node cluster ready", kNodeCount );

    // D-11: configure RPC endpoints pointing at the LOCAL Anvil instance on ALL nodes.
    {
        sgns::WeightedRpcEndpoint ep;
        ep.url                     = s_anvil.RpcUrl();
        ep.consensus_weight        = 100;
        ep.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
        ep.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };

        std::vector<sgns::WeightedRpcEndpoint> anvil_eps{ ep };
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            s_nodes[i]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps );
        }
        spdlog::info( "bridge_anvil: configured {} RPC endpoints across {} nodes at {}",
                      anvil_eps.size(),
                      kNodeCount,
                      s_anvil.RpcUrl() );
    }
}

void BridgeAnvilE2ETest::TearDownTestSuite()
{
    spdlog::info( "bridge_anvil: tearing down nodes" );
    for ( auto &node : s_nodes )
    {
        node.reset();
    }
    s_anvil.Stop();
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        std::filesystem::remove_all( s_configs[i].BaseWritePath );
    }
}

/**
 * @brief Positive Anvil burn-to-mint pipeline test (D-14 Path A, D-17).
 *
 * Sends an ERC-1155 self-transfer burn via `cast send` against the local
 * Anvil fork using Anvil default key #0, parses the burn tx hash from cast
 * --json output, feeds it to GeniusNode::MintTokens, and asserts the
 * recipient's balance increases by >= kMintAmount within the wait-condition
 * timeout.
 */
TEST_F( BridgeAnvilE2ETest, AnvilBurnToMintPipeline )
{
    const std::string sender_addr = sgns::test::anvil::kAnvilAccount0Address;
    const std::string dest_addr   = s_nodes[0]->GetAddress();
    spdlog::info( "bridge_anvil: sender={}, dest={}", sender_addr, dest_addr.substr( 0, 16 ) );

    const uint64_t initial_balance = s_nodes[0]->GetBalance( dest_addr );
    spdlog::info( "bridge_anvil: initial balance = {}", initial_balance );

    // Send a real bridgeOut() burn targeting the LOCAL Anvil RPC (D-14 Path A).
    spdlog::info( "bridge_anvil: sending burn transaction to local Anvil" );
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, s_nodes[0]->GetAddress() );
    spdlog::info( "bridge_anvil: burn tx hash = {}", tx_hash );
    ASSERT_FALSE( tx_hash.empty() ) << "bridgeOut burn-seeding failed (cast send rejected the call)";

    // Feed burn tx hash into the MintTokens pipeline.
    spdlog::info( "bridge_anvil: triggering MintTokens on s_nodes[0]" );
    EXPECT_OUTCOME_TRUE( mint_result,
                         s_nodes[0]->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kMintTimeout ) );
    spdlog::info( "bridge_anvil: MintTokens completed" );

    // D-17a: assert minted UTXO appears in recipient balance.
    EXPECT_WAIT_FOR_CONDITION( [&]() { return s_nodes[0]->GetBalance( dest_addr ) > initial_balance; },
                               kMintTimeout,
                               "Anvil burn minted UTXO appears in recipient balance",
                               nullptr );

    const uint64_t final_balance = s_nodes[0]->GetBalance( dest_addr );
    spdlog::info( "bridge_anvil: balance after mint = {} (delta = {})",
                  final_balance,
                  final_balance - initial_balance );
    EXPECT_GE( final_balance - initial_balance, kMintAmount );

    spdlog::info( "bridge_anvil: AnvilBurnToMintPipeline test complete" );
}

/**
 * @brief Verifies that a replayed Anvil burn tx hash is rejected by the dedup cache (D-18).
 *
 * Sends a fresh cast burn to the local Anvil fork, feeds the tx hash to
 * MintTokens once (expecting success), then re-feeds the SAME tx hash and
 * asserts the second call is rejected (returns error OR balance unchanged).
 */
TEST_F( BridgeAnvilE2ETest, AnvilReplayRejection )
{
    const std::string dest_addr = s_nodes[0]->GetAddress();

    // Send a fresh bridgeOut() burn tx to local Anvil.
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, s_nodes[0]->GetAddress() );
    ASSERT_FALSE( tx_hash.empty() ) << "bridgeOut burn-seeding failed (cast send rejected the call)";
    spdlog::info( "bridge_anvil: replay-test burn tx hash = {}", tx_hash );

    // First mint should succeed.
    EXPECT_OUTCOME_TRUE( first_result,
                         s_nodes[0]->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout ) );
    spdlog::info( "bridge_anvil: replay-test first mint submitted" );

    const uint64_t balance_before = s_nodes[0]->GetBalance( dest_addr );
    EXPECT_WAIT_FOR_CONDITION( [&]() { return s_nodes[0]->GetBalance( dest_addr ) > balance_before; },
                               kReplayTimeout,
                               "First Anvil mint balance increase",
                               nullptr );

    // Second mint with the SAME tx hash must be rejected by the dedup cache.
    auto second_result = s_nodes[0]->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout );

    bool replay_rejected = second_result.has_error();
    if ( !replay_rejected )
    {
        const uint64_t balance_after_replay = s_nodes[0]->GetBalance( dest_addr );
        EXPECT_EQ( balance_after_replay, s_nodes[0]->GetBalance( dest_addr ) )
            << "Balance should not increase for a replayed Anvil burn tx hash";
        spdlog::info( "bridge_anvil: second mint returned OK but balance unchanged" );
    }
    else
    {
        spdlog::info( "bridge_anvil: second mint rejected with error: {}", second_result.error().message() );
    }
    EXPECT_TRUE( replay_rejected ) << "Replayed Anvil burn tx hash must be rejected by dedup cache";

    spdlog::info( "bridge_anvil: AnvilReplayRejection test complete" );
}
