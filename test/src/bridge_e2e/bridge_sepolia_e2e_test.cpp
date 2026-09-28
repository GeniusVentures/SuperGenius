/**
 * @file       bridge_sepolia_e2e_test.cpp
 * @brief      Live-Sepolia E2E tests for the burn-to-mint bridge.
 * @date       2026-07-08
 * @author     Super Genius (info@gnus.ai)
 *
 * Live-Sepolia burn-to-mint E2E tests. Requires RUN_E2E_BRIDGE=1 + PRIVATE_KEY
 * env vars — no silent execution against real Sepolia.
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
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
#include "testutil/local_trust_setup.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

#include "anvil_fixture.hpp" // CastAvailable, RunShellCapture, ParseTxHashFromCastJson, kSepoliaChainId

using sgns::GeniusNode;

namespace
{

    std::vector<uint8_t> Base64Decode( const std::string &input )
    {
        static const std::array<int8_t, 256> kLookup = []()
        {
            std::array<int8_t, 256> table{};
            table.fill( -1 );
            for ( int i = 'A'; i <= 'Z'; ++i ) { table[i] = static_cast<int8_t>( i - 'A' ); }
            for ( int i = 'a'; i <= 'z'; ++i ) { table[i] = static_cast<int8_t>( i - 'a' + 26 ); }
            for ( int i = '0'; i <= '9'; ++i ) { table[i] = static_cast<int8_t>( i - '0' + 52 ); }
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
            if ( c == '=' ) { break; }
            int val = kLookup[static_cast<uint8_t>( c )];
            if ( val < 0 ) { return {}; }
            accum = ( accum << 6 ) | static_cast<uint32_t>( val );
            bits += 6;
            if ( bits >= 8 )
            {
                bits -= 8;
                result.push_back( static_cast<uint8_t>( ( accum >> bits ) & 0xFF ) );
            }
        }
        return result;
    }

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
                if ( !std::isxdigit( static_cast<unsigned char>( c ) ) ) { is_hex = false; break; }
            }
        }
        if ( is_hex ) { return raw_key; }
        auto decoded = Base64Decode( raw_key );
        if ( decoded.size() != 32 ) { return {}; }
        return BytesToHex( decoded );
    }

} // namespace

// =============================================================================
// BridgeSepoliaE2ETest — live Sepolia burn-to-mint
// =============================================================================

class BridgeSepoliaE2ETest : public ::testing::Test
{
protected:
    static inline constexpr unsigned int kNodeCount = 3u;

    static std::array<std::shared_ptr<GeniusNode>, kNodeCount> s_nodes;
    static std::array<GeniusNodeConfig, kNodeCount>                s_configs;
    static std::string                                          s_eth_private_key;

    static inline constexpr const char *kSepoliaContract     = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";
    static inline constexpr const char *kBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";
    static inline constexpr const char *kEventTopic0 =
        "0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62";
    static inline constexpr const char *kSepoliaRpc = "https://ethereum-sepolia-rpc.publicnode.com";
    static inline constexpr std::chrono::milliseconds kMintTimeout{ 10000 };
    static inline constexpr std::chrono::milliseconds kNodeReadyTimeout{ 60000 };
    static inline constexpr uint64_t kMintAmount = 1u;
    static void SetUpTestSuite();
    static void TearDownTestSuite();
};

std::array<std::shared_ptr<GeniusNode>, BridgeSepoliaE2ETest::kNodeCount> BridgeSepoliaE2ETest::s_nodes;
std::array<GeniusNodeConfig, BridgeSepoliaE2ETest::kNodeCount>                BridgeSepoliaE2ETest::s_configs = { {
    { "0xcafe", "0.35", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./bridge_sepolia_node0" },
    { "0xcafe", "0.35", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./bridge_sepolia_node1" },
    { "0xcafe", "0.35", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./bridge_sepolia_node2" },
} };
std::string BridgeSepoliaE2ETest::s_eth_private_key;

void BridgeSepoliaE2ETest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    if ( !std::getenv( "RUN_E2E_BRIDGE" ) )
    {
        FAIL() << "RUN_E2E_BRIDGE=1 required for live Sepolia E2E";
    }

    const char *pk_env = std::getenv( "PRIVATE_KEY" );
    if ( !pk_env ) { pk_env = std::getenv( "SIGNING_KEY" ); }
    if ( !pk_env )
    {
        FAIL() << "PRIVATE_KEY env var required for live Sepolia E2E";
    }

    s_eth_private_key = NormalizePrivateKey( pk_env );
    if ( s_eth_private_key.empty() )
    {
        FAIL() << "PRIVATE_KEY is not a valid hex or base64-encoded 32-byte key";
    }

    if ( !sgns::test::anvil::CastAvailable() )
    {
        FAIL() << "cast binary not found — install Foundry: https://book.getfoundry.sh/getting-started/installation";
    }

    const std::string binary_path = boost::dll::program_location().parent_path().string();
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        s_configs[i].BaseWritePath = binary_path + "/bridge_sepolia_node" + std::to_string( i ) + "/";
        sgns::test::removeAllWithRetry( s_configs[i].BaseWritePath );
        std::filesystem::create_directories( s_configs[i].BaseWritePath );
        sgns::GeniusNode::WriteNetworkConfig( s_configs[i].BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::test::WriteLocalTrustSgnsConfig( s_configs[i].BaseWritePath,
                                               ( i == 0u ) ? "Full" : "Light",
                                               /*is_processor=*/false,
                                               /*rpc_catchup=*/true,
                                               s_eth_private_key );
    }

    spdlog::info( "bridge_sepolia: creating {}-node cluster against live Sepolia", kNodeCount );

    s_nodes[0] = GeniusNode::New( s_configs[0], sgns::FromPrivateKey{ s_eth_private_key } );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( s_nodes[0]->GetAddress() );
    spdlog::info( "bridge_sepolia: authorized full node = {}", s_nodes[0]->GetAddress().substr( 0, 16 ) );

    ASSERT_NO_FATAL_FAILURE( sgns::test::MakeNodeReadyWithLocalTrust( s_nodes[0] ) );

    ASSERT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetState() == GeniusNode::NodeState::READY; },
        kNodeReadyTimeout, "full node [0] READY", nullptr );

    for ( unsigned int i = 1u; i < kNodeCount; ++i )
    {
        s_nodes[i] = GeniusNode::New( s_configs[i], sgns::FromPrivateKey{ s_eth_private_key } );
    }
    for ( unsigned int i = 1u; i < kNodeCount; ++i )
    {
        std::vector<std::string> peers;
        peers.push_back( s_nodes[0]->GetPubSub()->GetLocalAddress() );
        for ( unsigned int j = 1u; j < kNodeCount; ++j )
        {
            if ( j != i ) { peers.push_back( s_nodes[j]->GetPubSub()->GetLocalAddress() ); }
        }
        s_nodes[i]->AddPeers( peers );
    }

    for ( unsigned int i = 1u; i < kNodeCount; ++i )
    {
        ASSERT_NO_FATAL_FAILURE( sgns::test::MakeNodeReadyWithLocalTrust( s_nodes[i] ) );
    }

    ASSERT_WAIT_FOR_CONDITION(
        [&]()
        {
            for ( unsigned int i = 1u; i < kNodeCount; ++i )
            {
                if ( s_nodes[i]->GetState() != GeniusNode::NodeState::READY ) { return false; }
            }
            return true;
        },
        kNodeReadyTimeout, "processor nodes READY", nullptr );

    spdlog::info( "bridge_sepolia: {}-node cluster ready", kNodeCount );

    {
        std::vector<sgns::WeightedRpcEndpoint> sepolia_eps;
        for ( const auto &url : { std::string( kSepoliaRpc ),
                                  std::string( "https://rpc.sepolia.org" ),
                                  std::string( "https://sepolia.drpc.org" ),
                                  std::string( "https://sepolia.gateway.tenderly.co" ),
                                  std::string( "https://rpc2.sepolia.org" ),
                                  std::string( "https://ethereum-sepolia-rpc.publicnode.com" ) } )
        {
            sgns::WeightedRpcEndpoint ep;
            ep.url                     = url;
            ep.consensus_weight        = 25;
            ep.bridge_contract_address = kBridgeContractLower;
            ep.accepted_topic0_hashes  = { kEventTopic0 };
            sepolia_eps.push_back( ep );
        }
        s_nodes[0]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, sepolia_eps );
        spdlog::info( "bridge_sepolia: configured {} live Sepolia RPC endpoints", sepolia_eps.size() );
    }
}

void BridgeSepoliaE2ETest::TearDownTestSuite()
{
    spdlog::info( "bridge_sepolia: tearing down nodes" );
    for ( auto &node : s_nodes ) { node.reset(); }
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        sgns::test::removeAllWithRetry( s_configs[i].BaseWritePath );
    }
}

/**
 * @brief Positive live-Sepolia burn-to-mint pipeline test.
 *
 * Derives the sender address from PRIVATE_KEY via `cast wallet address`,
 * sends a burn transaction to live Sepolia, feeds the tx hash to
 * MintTokens, and asserts the recipient balance increases.
 */
TEST_F( BridgeSepoliaE2ETest, DISABLED_BurnToMintPipeline )
{
    std::string wallet_cmd = "cast wallet address " + s_eth_private_key + " 2>&1";
    int         wallet_rc  = -1;
    std::string wallet_out = sgns::test::anvil::RunShellCapture( wallet_cmd, wallet_rc );
    ASSERT_EQ( wallet_rc, 0 ) << "cast wallet address failed";
    ASSERT_FALSE( wallet_out.empty() );

    std::string sender_addr = wallet_out;
    while ( !sender_addr.empty() &&
            ( sender_addr.back() == '\n' || sender_addr.back() == '\r' || sender_addr.back() == ' ' ) )
    {
        sender_addr.pop_back();
    }
    ASSERT_FALSE( sender_addr.empty() ) << "Could not derive sender address from PRIVATE_KEY";
    spdlog::info( "bridge_sepolia: sender = {}", sender_addr );

    const std::string dest_addr       = s_nodes[0]->GetAddress();
    const uint64_t    initial_balance = s_nodes[0]->GetBalance( dest_addr );
    spdlog::info( "bridge_sepolia: initial balance = {}", initial_balance );

    std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) +
                           " \"safeTransferFrom(address,address,uint256,uint256,bytes)\" " +
                           sender_addr + " " + sender_addr + " 0 " + std::to_string( kMintAmount ) +
                           " 0x --private-key " + s_eth_private_key + " --rpc-url " + kSepoliaRpc + " --json 2>&1";

    int         cast_rc     = -1;
    std::string cast_output = sgns::test::anvil::RunShellCapture( cast_cmd, cast_rc );
    ASSERT_EQ( cast_rc, 0 ) << "cast send failed: " << cast_output;

    const std::string tx_hash = sgns::test::anvil::ParseTxHashFromCastJson( cast_output );
    ASSERT_FALSE( tx_hash.empty() ) << "Could not parse transactionHash";
    spdlog::info( "bridge_sepolia: burn tx hash = {}", tx_hash );

    EXPECT_OUTCOME_TRUE( mint_result,
                         s_nodes[0]->MintTokens( kMintAmount,
                                                 tx_hash,
                                                 sgns::test::anvil::kSepoliaChainId,
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 dest_addr,
                                                 kMintTimeout ) );

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetBalance( dest_addr ) > initial_balance; },
        kMintTimeout,
        "live Sepolia minted UTXO appears in recipient balance",
        nullptr );

    const uint64_t final_balance = s_nodes[0]->GetBalance( dest_addr );
    spdlog::info( "bridge_sepolia: balance after mint = {} (delta = {})",
                  final_balance,
                  final_balance - initial_balance );
    EXPECT_GE( final_balance - initial_balance, kMintAmount );
}
