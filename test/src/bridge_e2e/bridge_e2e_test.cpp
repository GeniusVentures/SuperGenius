/**
 * @file       bridge_e2e_test.cpp
 * @brief      End-to-end integration test for the EVM bridge burn-to-mint pipeline.
 * @date       2026-05-31
 * @author     Super Genius (info@gnus.ai)
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include "account/GeniusNode.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/outcome.hpp"
#include "testutil/wait_condition.hpp"

using sgns::GeniusNode;

/**
 * @brief Three-node GTest fixture for end-to-end bridge pipeline testing.
 *
 * Creates three GeniusNode instances bootstrapped via PubSub, then exercises
 * the full burn-to-mint pipeline against live Sepolia. Guarded by
 * RUN_E2E_BRIDGE, PRIVATE_KEY, and the cast binary.
 */
class BridgeE2ETest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static std::string s_eth_private_key;

    /** @brief Sepolia GNUS contract address. */
    static constexpr const char *kSepoliaContract = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";

    /** @brief ERC-20 transfer function selector. */
    static constexpr const char *kTransferSig = "transfer(address,uint256)";

    /** @brief Sepolia public RPC endpoint. */
    static constexpr const char *kSepoliaRpc = "https://ethereum-sepolia-rpc.publicnode.com";

    /** @brief Mint finalization timeout (accounts for Sepolia block confirmation). */
    static constexpr std::chrono::milliseconds kMintTimeout{ 10000 };

    /** @brief Small test mint amount in base units. */
    static constexpr uint64_t kMintAmount = 1;

    /**
     * @brief Sets up the three-node test suite.
     *
     * Guards: skips if RUN_E2E_BRIDGE is unset, PRIVATE_KEY is missing,
     * or the cast binary is not found.
     */
    static void SetUpTestSuite();

    /**
     * @brief Tears down the test suite and removes test data directories.
     */
    static void TearDownTestSuite();
};

// --- Static member initialization ---

std::shared_ptr<GeniusNode> BridgeE2ETest::node_main  = nullptr;
std::shared_ptr<GeniusNode> BridgeE2ETest::node_proc1 = nullptr;
std::shared_ptr<GeniusNode> BridgeE2ETest::node_proc2 = nullptr;

std::string BridgeE2ETest::s_eth_private_key;

DevConfig_st BridgeE2ETest::DEV_CONFIG  = { "0xcafe",
                                             "0.65",
                                             "1.0",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "./node1" };
DevConfig_st BridgeE2ETest::DEV_CONFIG2 = { "0xcafe",
                                             "0.65",
                                             "1.0",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "./node2" };
DevConfig_st BridgeE2ETest::DEV_CONFIG3 = { "0xcafe",
                                             "0.65",
                                             "1.0",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "./node3" };

// --- Fixture implementation ---

void BridgeE2ETest::SetUpTestSuite()
{
    // Guard 1: opt-in env var
    if ( !std::getenv( "RUN_E2E_BRIDGE" ) )
    {
        GTEST_SKIP() << "Set RUN_E2E_BRIDGE=1 to run E2E bridge tests";
    }

    // Guard 2: PRIVATE_KEY required for Sepolia transactions
    const char *private_key_env = std::getenv( "PRIVATE_KEY" );
    if ( !private_key_env )
    {
        GTEST_SKIP() << "PRIVATE_KEY env var required for E2E bridge tests";
    }
    s_eth_private_key = private_key_env;

    // Guard 3: cast binary must be installed
    constexpr const char *kCastCheckCmd = "which cast 2>/dev/null";
    FILE *cast_pipe = popen( kCastCheckCmd, "r" );
    if ( !cast_pipe )
    {
        GTEST_SKIP() << "Could not check for cast binary";
    }
    char cast_path_buf[256] = {};
    if ( !std::fgets( cast_path_buf, sizeof( cast_path_buf ), cast_pipe ) )
    {
        pclose( cast_pipe );
        GTEST_SKIP() << "cast binary not found — install Foundry: https://book.getfoundry.sh/getting-started/installation";
    }
    pclose( cast_pipe );
    spdlog::info( "bridge_e2e: found cast at {}", cast_path_buf );

    // Set per-node BaseWritePath
    std::string binary_path          = boost::dll::program_location().parent_path().string();
    DEV_CONFIG.BaseWritePath         = binary_path + "/node1/";
    DEV_CONFIG2.BaseWritePath        = binary_path + "/node2/";
    DEV_CONFIG3.BaseWritePath        = binary_path + "/node3/";

    spdlog::info( "bridge_e2e: creating 3-node cluster for E2E test" );

    // Create nodes — main node (non-processor)
    node_main = GeniusNode::New( DEV_CONFIG, s_eth_private_key.c_str(), false, false );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // Processor nodes (isprocessor=true per processing_multi pattern)
    node_proc1 = GeniusNode::New( DEV_CONFIG2, s_eth_private_key.c_str(), false, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    node_proc2 = GeniusNode::New( DEV_CONFIG3, s_eth_private_key.c_str(), false, true );

    node_proc1->StopProcessing();
    node_proc2->StopProcessing();

    // Bootstrap PubSub mesh — match processing_multi_test pattern exactly
    std::vector bootstrappers = { node_proc1->GetPubSub()->GetLocalAddress(),
                                  node_proc2->GetPubSub()->GetLocalAddress() };
    node_main->GetPubSub()->AddPeers( bootstrappers );

    bootstrappers = { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() };
    node_proc1->GetPubSub()->AddPeers( bootstrappers );

    spdlog::info( "bridge_e2e: 3-node cluster ready" );
}

void BridgeE2ETest::TearDownTestSuite()
{
    spdlog::info( "bridge_e2e: tearing down nodes" );
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();

    // Remove test data directories
    std::filesystem::remove_all( DEV_CONFIG.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath );
}

/**
 * @brief Exercises the full burn-to-mint bridge pipeline against live Sepolia.
 *
 * Steps:
 * 1. Derive sender address from PRIVATE_KEY via cast.
 * 2. Send a burn transaction (ERC-20 transfer) to the Sepolia GNUS contract.
 * 3. Trigger MintTokens on node_main with the burn tx hash.
 * 4. Poll for UTXO confirmation on node_main.
 * 5. Verify consensus propagation on processor nodes.
 */
TEST_F( BridgeE2ETest, BurnToMintPipeline )
{
    // --- Step 1: Derive sender address from PRIVATE_KEY ---
    std::string wallet_cmd = "cast wallet address " + s_eth_private_key + " 2>&1";
    FILE       *wallet_pipe = popen( wallet_cmd.c_str(), "r" );
    ASSERT_NE( wallet_pipe, nullptr ) << "Failed to run cast wallet address";

    char addr_buf[256] = {};
    ASSERT_NE( std::fgets( addr_buf, sizeof( addr_buf ), wallet_pipe ), nullptr )
        << "cast wallet address returned no output";
    pclose( wallet_pipe );

    std::string sender_addr( addr_buf );
    // Trim trailing whitespace/newline
    while ( !sender_addr.empty() && ( sender_addr.back() == '\n' || sender_addr.back() == '\r' ||
                                      sender_addr.back() == ' ' ) )
    {
        sender_addr.pop_back();
    }
    ASSERT_FALSE( sender_addr.empty() ) << "Could not derive sender address from PRIVATE_KEY";
    spdlog::info( "bridge_e2e: sender address = {}", sender_addr );

    // Capture initial balance before burn
    uint64_t initial_balance = node_main->GetBalance( sender_addr );
    spdlog::info( "bridge_e2e: initial balance = {}", initial_balance );

    // --- Step 2: Send burn transaction to Sepolia via cast send ---
    std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) + " \"" + kTransferSig + "\" " +
                           sender_addr + " " + std::to_string( kMintAmount ) + " --private-key " +
                           s_eth_private_key + " --rpc-url " + kSepoliaRpc + " --json 2>&1";

    spdlog::info( "bridge_e2e: sending burn transaction" );
    FILE *cast_pipe = popen( cast_cmd.c_str(), "r" );
    ASSERT_NE( cast_pipe, nullptr ) << "Failed to run cast send";

    std::string cast_output;
    char        line_buf[1024] = {};
    while ( std::fgets( line_buf, sizeof( line_buf ), cast_pipe ) )
    {
        cast_output += line_buf;
    }
    int cast_rc = pclose( cast_pipe );
    spdlog::info( "bridge_e2e: cast send output: {}", cast_output );
    ASSERT_EQ( cast_rc, 0 ) << "cast send failed with exit code " << cast_rc << ": " << cast_output;

    // Parse transactionHash from JSON output
    std::string tx_hash;
    constexpr const char *kTxHashPattern = "\"transactionHash\":\"0x";
    size_t                hash_pos       = cast_output.find( kTxHashPattern );
    if ( hash_pos != std::string::npos )
    {
        size_t start = hash_pos + std::strlen( kTxHashPattern ) - 2;  // include "0x"
        size_t end   = cast_output.find( '"', start );
        if ( end != std::string::npos )
        {
            tx_hash = cast_output.substr( start, end - start );
        }
    }
    ASSERT_FALSE( tx_hash.empty() ) << "Could not parse transactionHash from cast output";
    spdlog::info( "bridge_e2e: burn tx hash = {}", tx_hash );

    // --- Step 3: Trigger mint on node_main ---
    spdlog::info( "bridge_e2e: triggering MintTokens on node_main" );
    EXPECT_OUTCOME_TRUE(
        mint_result,
        node_main->MintTokens( kMintAmount, tx_hash, "11155111", sgns::TokenID::FromBytes( { 0x00 } ), sender_addr, kMintTimeout ) );
    spdlog::info( "bridge_e2e: MintTokens completed" );

    // --- Step 4: Poll for UTXO confirmation on node_main ---
    std::chrono::milliseconds e2e_timeout{ 10000 };
    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_main->GetBalance( sender_addr ) > initial_balance;
        },
        e2e_timeout,
        "Minted UTXO appears in recipient balance on node_main",
        nullptr );

    uint64_t final_balance = node_main->GetBalance( sender_addr );
    spdlog::info( "bridge_e2e: node_main balance after mint = {} (delta = {})", final_balance, final_balance - initial_balance );
    EXPECT_GE( final_balance - initial_balance, kMintAmount );

    // --- Step 5: Verify consensus propagation on processor nodes ---
    std::chrono::milliseconds propagation_timeout{ 5000 };

    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_proc1->GetBalance( sender_addr ) > initial_balance;
        },
        propagation_timeout,
        "Minted UTXO propagated to node_proc1",
        nullptr );

    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_proc2->GetBalance( sender_addr ) > initial_balance;
        },
        propagation_timeout,
        "Minted UTXO propagated to node_proc2",
        nullptr );

    spdlog::info( "bridge_e2e: BurnToMintPipeline test complete" );
}
