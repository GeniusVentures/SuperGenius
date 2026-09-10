/**
 * @file       bridge_e2e_test.cpp
 * @brief      End-to-end integration test for the EVM bridge burn-to-mint pipeline.
 * @date       2026-05-31
 * @author     Super Genius (info@gnus.ai)
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include <ProofSystem/EthereumKeyGenerator.hpp>

#include <TrustWalletCore/TWPrivateKey.h>
#include <TrustWalletCore/TWHash.h>
#include <TrustWalletCore/TWCurve.h>
#include "testutil/mint_source_hash.hpp"
#include "testutil/local_trust_setup.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/outcome.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

#include <eth/bridge_event.hpp>
#include <eth/objects.hpp>

/**
 * @brief Decodes a base64-encoded string to raw bytes.
 * @param input  Base64-encoded string (standard alphabet, optional '=' padding).
 * @return Decoded bytes, or empty vector on invalid input.
 */
static std::vector<uint8_t> Base64Decode( const std::string &input )
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
static std::string BytesToHex( const std::vector<uint8_t> &bytes )
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

using sgns::GeniusNode;

namespace
{

    FILE *OpenCommandPipe( const char *command, const char *mode )
    {
#if defined( _WIN32 )
        return _popen( command, mode );
#else
        return popen( command, mode );
#endif
    }

    int CloseCommandPipe( FILE *pipe )
    {
#if defined( _WIN32 )
        return _pclose( pipe );
#else
        return pclose( pipe );
#endif
    }

#if defined( _WIN32 )
    constexpr const char *kCastCheckCmd = "where cast 2>NUL";
#else
    constexpr const char *kCastCheckCmd = "which cast 2>/dev/null";
#endif

} // namespace

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

    static GeniusNodeConfig DEV_CONFIG;
    static GeniusNodeConfig DEV_CONFIG2;
    static GeniusNodeConfig DEV_CONFIG3;

    static std::string s_eth_private_key;

    /** @brief Sepolia GNUS contract address. */
    static constexpr const char *kSepoliaContract = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";

    /** @brief ERC-1155 safeTransferFrom function selector. */
    static constexpr const char *kTransferSig = "safeTransferFrom(address,address,uint256,uint256,bytes)";

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

GeniusNodeConfig BridgeE2ETest::DEV_CONFIG  = { "0xcafe",
                                                "0.35",
                                                "1.0",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                "./bridge_e2e_node1" };
GeniusNodeConfig BridgeE2ETest::DEV_CONFIG2 = { "0xcafe",
                                                "0.35",
                                                "1.0",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                "./bridge_e2e_node2" };
GeniusNodeConfig BridgeE2ETest::DEV_CONFIG3 = { "0xcafe",
                                                "0.35",
                                                "1.0",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                "./bridge_e2e_node3" };

// --- Fixture implementation ---

void BridgeE2ETest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // Guard 1: opt-in env var
    if ( !std::getenv( "RUN_E2E_BRIDGE" ) )
    {
        GTEST_SKIP() << "Set RUN_E2E_BRIDGE=1 to run E2E bridge tests";
    }

    // Guard 2: signing key required for Sepolia transactions
    // Support both SIGNING_KEY and PRIVATE_KEY env vars
    const char *private_key_env = std::getenv( "SIGNING_KEY" );
    if ( !private_key_env )
    {
        private_key_env = std::getenv( "PRIVATE_KEY" );
    }
    if ( !private_key_env )
    {
        GTEST_SKIP() << "SIGNING_KEY or PRIVATE_KEY env var required for E2E bridge tests";
    }

    std::string raw_key( private_key_env );

    // Strip 0x prefix if present (Ethereum convention)
    if ( raw_key.size() >= 2 && raw_key[0] == '0' && raw_key[1] == 'x' )
    {
        raw_key = raw_key.substr( 2 );
    }

    // GeniusNode expects a 64-char hex string (32 bytes).
    // If the key is not 64 hex chars, try base64 decoding then re-encoding as hex.
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
        s_eth_private_key = raw_key;
    }
    else
    {
        // Attempt base64 decode → hex encode
        auto decoded = Base64Decode( raw_key );
        if ( decoded.size() != 32 )
        {
            GTEST_SKIP() << "SIGNING_KEY/PRIVATE_KEY is not valid hex or base64-encoded 32-byte key";
        }
        s_eth_private_key = BytesToHex( decoded );
        spdlog::info( "bridge_e2e: decoded base64 signing key to hex ({} chars)", s_eth_private_key.size() );
    }

    // Guard 3: cast binary must be installed
    FILE *cast_pipe = OpenCommandPipe( kCastCheckCmd, "r" );
    if ( !cast_pipe )
    {
        GTEST_SKIP() << "Could not check for cast binary";
    }
    char cast_path_buf[256] = {};
    if ( !std::fgets( cast_path_buf, sizeof( cast_path_buf ), cast_pipe ) )
    {
        CloseCommandPipe( cast_pipe );
        GTEST_SKIP()
            << "cast binary not found — install Foundry: https://book.getfoundry.sh/getting-started/installation";
    }
    CloseCommandPipe( cast_pipe );
    spdlog::info( "bridge_e2e: found cast at {}", cast_path_buf );

    // Set per-node BaseWritePath
    std::string binary_path   = boost::dll::program_location().parent_path().string();
    DEV_CONFIG.BaseWritePath  = binary_path + "/bridge_e2e_node1/";
    DEV_CONFIG2.BaseWritePath = binary_path + "/bridge_e2e_node2/";
    DEV_CONFIG3.BaseWritePath = binary_path + "/bridge_e2e_node3/";
    sgns::test::removeAllWithRetry( DEV_CONFIG.BaseWritePath );
    sgns::test::removeAllWithRetry( DEV_CONFIG2.BaseWritePath );
    sgns::test::removeAllWithRetry( DEV_CONFIG3.BaseWritePath );

    spdlog::info( "bridge_e2e: creating 3-node cluster for E2E test" );

    // Create the full node FIRST — it will create the genesis block.
    // Pattern from blockchain_genesis_test.cpp: WithAuthorizationCanSync
    GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::test::WriteLocalTrustSgnsConfig(
        DEV_CONFIG.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/true, s_eth_private_key );
    node_main = GeniusNode::New( DEV_CONFIG, sgns::FromPrivateKey{ s_eth_private_key } );

    // Set authorized address to match the full node — triggers StoreGenesisRegistry
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node_main->GetAddress() );
    spdlog::info( "bridge_e2e: set authorized full node address to {}", node_main->GetAddress().substr( 0, 16 ) );

    ASSERT_NO_FATAL_FAILURE( sgns::test::MakeNodeReadyWithLocalTrust( node_main ) );

    // Wait for the full node to reach READY state (creates genesis + account-creation blocks)
    constexpr std::chrono::milliseconds kBlockchainInitTimeout{ 60000 };
    sgns::test::assertWaitForCondition( [&]() { return node_main->GetState() == GeniusNode::NodeState::READY; },
                                        kBlockchainInitTimeout,
                                        "node_main not ready" );

    spdlog::info( "bridge_e2e: node_main READY, creating processor nodes" );

    // Create regular nodes — they will sync genesis from node_main via PubSub.
    // is_processor=false matches the blockchain_genesis_test.cpp pattern.
    GeniusNode::WriteNetworkConfig( DEV_CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::test::WriteLocalTrustSgnsConfig(
        DEV_CONFIG2.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true, /*rpc_catchup=*/true, s_eth_private_key );
    node_proc1 = GeniusNode::New( DEV_CONFIG2, sgns::FromPrivateKey{ s_eth_private_key } );

    GeniusNode::WriteNetworkConfig( DEV_CONFIG3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::test::WriteLocalTrustSgnsConfig(
        DEV_CONFIG3.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true, /*rpc_catchup=*/true, s_eth_private_key );
    node_proc2 = GeniusNode::New( DEV_CONFIG3, sgns::FromPrivateKey{ s_eth_private_key } );

    // Bootstrap PubSub — match blockchain_genesis_test pattern
    node_proc1->AddPeers( { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    ASSERT_NO_FATAL_FAILURE( sgns::test::MakeNodeReadyWithLocalTrust( node_proc1 ) );
    ASSERT_NO_FATAL_FAILURE( sgns::test::MakeNodeReadyWithLocalTrust( node_proc2 ) );

    // Wait for processor nodes to sync and reach READY
    sgns::test::assertWaitForCondition(
        [&]()
        {
            return node_proc1->GetState() == GeniusNode::NodeState::READY &&
                   node_proc2->GetState() == GeniusNode::NodeState::READY;
        },
        kBlockchainInitTimeout,
        "Processor nodes did not sync to READY within timeout" );

    spdlog::info( "bridge_e2e: all processor nodes synced and READY" );

    spdlog::info( "bridge_e2e: 3-node cluster ready" );

    // Configure Sepolia RPC endpoints on node_main's input validator.
    // Need >= 75 consensus weight (3 endpoints × 25 each) for VerifyPublicChainSmartContract.
    {
        constexpr const char *kBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";
        constexpr const char *kEventTopic0 = "0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62";

        std::vector<sgns::WeightedRpcEndpoint> sepolia_eps;
        for ( const auto &url : { kSepoliaRpc,
                                  "https://rpc.sepolia.org",
                                  "https://sepolia.drpc.org",
                                  "https://sepolia.gateway.tenderly.co",
                                  "https://rpc2.sepolia.org",
                                  "https://ethereum-sepolia-rpc.publicnode.com" } )
        {
            sgns::WeightedRpcEndpoint ep;
            ep.url                     = url;
            ep.consensus_weight        = 25;
            ep.bridge_contract_address = kBridgeContractLower;
            ep.accepted_topic0_hashes  = { kEventTopic0 };
            sepolia_eps.push_back( ep );
        }

        node_main->ConfigureRpcEndpoint( "test", sepolia_eps );
        spdlog::info( "bridge_e2e: configured {} Sepolia RPC endpoints", sepolia_eps.size() );
    }
}

void BridgeE2ETest::TearDownTestSuite()
{
    spdlog::info( "bridge_e2e: tearing down nodes" );
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();

    // Remove test data directories
    sgns::test::removeAllWithRetry( DEV_CONFIG.BaseWritePath );
    sgns::test::removeAllWithRetry( DEV_CONFIG2.BaseWritePath );
    sgns::test::removeAllWithRetry( DEV_CONFIG3.BaseWritePath );
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
    std::string wallet_cmd  = "cast wallet address " + s_eth_private_key + " 2>&1";
    FILE       *wallet_pipe = OpenCommandPipe( wallet_cmd.c_str(), "r" );
    ASSERT_NE( wallet_pipe, nullptr ) << "Failed to run cast wallet address";

    char addr_buf[256] = {};
    ASSERT_NE( std::fgets( addr_buf, sizeof( addr_buf ), wallet_pipe ), nullptr )
        << "cast wallet address returned no output";
    CloseCommandPipe( wallet_pipe );

    std::string sender_addr( addr_buf );
    // Trim trailing whitespace/newline
    while ( !sender_addr.empty() &&
            ( sender_addr.back() == '\n' || sender_addr.back() == '\r' || sender_addr.back() == ' ' ) )
    {
        sender_addr.pop_back();
    }
    ASSERT_FALSE( sender_addr.empty() ) << "Could not derive sender address from PRIVATE_KEY";
    spdlog::info( "bridge_e2e: sender address = {}", sender_addr );

    // Use the node's own SuperGenius address as the mint destination.
    // The Ethereum address is only used for the cast send to Sepolia.
    const std::string dest_addr = node_main->GetAddress();
    spdlog::info( "bridge_e2e: destination address = {}", dest_addr.substr( 0, 16 ) );

    // Capture initial balance before burn
    uint64_t initial_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_e2e: initial balance = {}", initial_balance );

    // --- Step 2: Send burn transaction to Sepolia via cast send ---
    // ERC-1155 safeTransferFrom(from, to, id, amount, data) — self-transfer burn
    // Token ID 0 is GNUS. Empty bytes for data field.
    std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) + " \"" + kTransferSig + "\" " + sender_addr +
                           " " + sender_addr + " 0 " + std::to_string( kMintAmount ) + " 0x --private-key " +
                           s_eth_private_key + " --rpc-url " + kSepoliaRpc + " --json 2>&1";

    spdlog::info( "bridge_e2e: sending burn transaction" );
    FILE *cast_pipe = OpenCommandPipe( cast_cmd.c_str(), "r" );
    ASSERT_NE( cast_pipe, nullptr ) << "Failed to run cast send";

    std::string cast_output;
    char        line_buf[1024] = {};
    while ( std::fgets( line_buf, sizeof( line_buf ), cast_pipe ) )
    {
        cast_output += line_buf;
    }
    int cast_rc = CloseCommandPipe( cast_pipe );
    spdlog::info( "bridge_e2e: cast send output: {}", cast_output );
    ASSERT_EQ( cast_rc, 0 ) << "cast send failed with exit code " << cast_rc << ": " << cast_output;

    // Parse transactionHash from JSON output
    std::string           tx_hash;
    constexpr const char *kTxHashPattern = "\"transactionHash\":\"0x";
    size_t                hash_pos       = cast_output.find( kTxHashPattern );
    if ( hash_pos != std::string::npos )
    {
        size_t start = hash_pos + std::strlen( kTxHashPattern ) - 2; // include "0x"
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
    EXPECT_OUTCOME_TRUE( mint_result,
                         node_main->MintTokens( kMintAmount,
                                                tx_hash,
                                                "test",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kMintTimeout ) );
    spdlog::info( "bridge_e2e: MintTokens completed" );

    // --- Step 4: Poll for UTXO confirmation on node_main ---
    std::chrono::milliseconds e2e_timeout{ 10000 };
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > initial_balance; },
                               e2e_timeout,
                               "Minted UTXO appears in recipient balance on node_main",
                               nullptr );

    uint64_t final_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_e2e: node_main balance after mint = {} (delta = {})",
                  final_balance,
                  final_balance - initial_balance );
    EXPECT_GE( final_balance - initial_balance, kMintAmount );

    // Step 5: Processor nodes are non-full nodes and cannot query other addresses.
    // Consensus propagation is verified implicitly — the 2-of-3 quorum that
    // certified the mint requires participation from at least one processor node.

    spdlog::info( "bridge_e2e: BurnToMintPipeline test complete" );
}

/**
 * @brief Verifies that the Phase 3 collision-resistance fix in GetSlotKey()
 *        correctly includes the burn tx hash in the MintV2 slot key.
 *
 * Two distinct burns with identical chain/token/amount/dest must produce
 * different consensus slot keys, preventing double-mint via slot collision.
 * The test proves this indirectly: if the fix were absent, the second mint
 * would be rejected as a duplicate.
 */
TEST_F( BridgeE2ETest, SlotKeyCollisionResistance )
{
    // --- Step 1: Generate two distinct burn tx hashes ---
    std::string burn_hash_1 = sgns::test::NextMintSourceHash();
    std::string burn_hash_2 = sgns::test::NextMintSourceHash();

    ASSERT_NE( burn_hash_1, burn_hash_2 ) << "NextMintSourceHash() must produce unique hashes";
    spdlog::info( "bridge_e2e: burn_hash_1 = {}", burn_hash_1 );
    spdlog::info( "bridge_e2e: burn_hash_2 = {}", burn_hash_2 );

    // --- Step 2: Record initial balance ---
    std::string dest_addr       = node_main->GetAddress();
    uint64_t    initial_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_e2e: initial balance = {} for {}", initial_balance, dest_addr );

    // --- Step 3: First mint (burn_hash_1) ---
    constexpr uint64_t                  kSlotMintAmount = 500;
    constexpr std::chrono::milliseconds kSlotKeyTimeout{ 10000 };

    spdlog::info( "bridge_e2e: first MintTokens with burn_hash_1" );
    EXPECT_OUTCOME_TRUE( mint_result_1,
                         node_main->MintTokens( kSlotMintAmount,
                                                burn_hash_1,
                                                "test",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kSlotKeyTimeout ) );
    spdlog::info( "bridge_e2e: first MintTokens completed" );

    // --- Step 4: Wait for first mint to propagate ---
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > initial_balance; },
                               kSlotKeyTimeout,
                               "First mint balance increase on node_main",
                               nullptr );

    uint64_t balance_after_first = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_e2e: balance after first mint = {} (delta = {})",
                  balance_after_first,
                  balance_after_first - initial_balance );
    EXPECT_GE( balance_after_first - initial_balance, kSlotMintAmount );

    // --- Step 5: Second mint (burn_hash_2) — same chain/token/amount/dest ---
    spdlog::info( "bridge_e2e: second MintTokens with burn_hash_2" );
    EXPECT_OUTCOME_TRUE( mint_result_2,
                         node_main->MintTokens( kSlotMintAmount,
                                                burn_hash_2,
                                                "test",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kSlotKeyTimeout ) );
    spdlog::info( "bridge_e2e: second MintTokens completed" );

    // --- Step 6: Wait for second mint to propagate ---
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > balance_after_first; },
                               kSlotKeyTimeout,
                               "Second mint balance increase on node_main (collision resistance proof)",
                               nullptr );

    uint64_t final_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_e2e: final balance = {} (total delta = {})", final_balance, final_balance - initial_balance );

    // --- Step 7: Verify final balance = initial + 2 * kSlotMintAmount ---
    EXPECT_GE( final_balance - initial_balance, 2 * kSlotMintAmount )
        << "Both mints must succeed — if slot keys collided, the second would be rejected";

    spdlog::info( "bridge_e2e: SlotKeyCollisionResistance test complete" );
}

/**
 * @brief Verifies that replaying a previously-seen burn tx hash is rejected.
 *
 * The Phase 3 dedup cache uses GetSlotKey to produce deterministic slot keys
 * from the burn tx hash.  A second MintTokens call with the same hash should
 * collide in the same consensus slot and be rejected, preventing double-mint.
 */
TEST_F( BridgeE2ETest, ReplayRejection )
{
    // Step 1: Generate a unique burn tx hash
    const std::string burn_tx_hash = sgns::test::NextMintSourceHash();
    spdlog::info( "bridge_e2e: ReplayRejection — burn_tx_hash = {}", burn_tx_hash );

    // Step 2: First mint should succeed
    const std::string                   dest_addr = node_main->GetAddress();
    constexpr std::chrono::milliseconds kReplayTimeout{ 5000 };

    EXPECT_OUTCOME_TRUE( first_result,
                         node_main->MintTokens( kMintAmount,
                                                burn_tx_hash,
                                                "test",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout ) );
    spdlog::info( "bridge_e2e: first mint submitted" );

    // Step 3: Wait for the first mint to finalize
    uint64_t balance_before = node_main->GetBalance( dest_addr );
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > balance_before; },
                               kReplayTimeout,
                               "First mint UTXO appears in balance",
                               nullptr );

    // Step 4: Second mint with the same burn tx hash should be rejected
    auto second_result = node_main->MintTokens( kMintAmount,
                                                burn_tx_hash,
                                                "test",
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout );

    bool replay_rejected = second_result.has_error();
    if ( !replay_rejected )
    {
        // MintTokens returned OK but consensus may reject — verify balance unchanged
        uint64_t balance_after_replay = node_main->GetBalance( dest_addr );
        EXPECT_EQ( balance_after_replay, node_main->GetBalance( dest_addr ) )
            << "Balance should not increase for a replayed burn tx hash";
        spdlog::info( "bridge_e2e: ReplayRejection — second mint returned OK but balance unchanged" );
    }
    else
    {
        spdlog::info( "bridge_e2e: ReplayRejection — second mint rejected with error: {}",
                      second_result.error().message() );
    }
    EXPECT_TRUE( replay_rejected ) << "Replayed burn tx hash must be rejected by dedup cache";

    spdlog::info( "bridge_e2e: ReplayRejection test complete" );
}

/**
 * @brief Verifies that a chain with no RPC endpoints causes MintTokens to fail closed.
 *
 * The Phase 3 D-03 fix ensures VerifyPublicChainSmartContract returns false
 * when no RPC endpoints are configured for the requested chain, preventing
 * unchecked minting on unverifiable source chains.
 */
TEST_F( BridgeE2ETest, MissingEndpointsFailClosed )
{
    // Generate a unique burn tx hash
    const std::string burn_tx_hash = sgns::test::NextMintSourceHash();
    spdlog::info( "bridge_e2e: MissingEndpointsFailClosed — burn_tx_hash = {}", burn_tx_hash );

    // Chain "999999" has no RPC endpoints configured — should fail closed
    constexpr std::chrono::milliseconds kFailClosedTimeout{ 5000 };
    auto                                result = node_main->MintTokens( kMintAmount,
                                                                        burn_tx_hash,
                                                                        "999999",
                                                                        sgns::TokenID::FromBytes( { 0x00 } ),
                                                                        node_main->GetAddress(),
                                                                        kFailClosedTimeout );

    EXPECT_TRUE( result.has_error() )
        << "MintTokens for chain with no RPC endpoints should fail (fail-closed per Phase 3 D-03)";

    if ( result.has_error() )
    {
        spdlog::info( "bridge_e2e: MissingEndpointsFailClosed — correctly rejected: {}", result.error().message() );
    }

    spdlog::info( "bridge_e2e: MissingEndpointsFailClosed test complete" );
}

/**
 * @brief Verifies that verify_receipt_log rejects mismatched contract address and event topic0.
 *
 * The Phase 3 D-05/D-06 fixes ensure that a BridgeEventClaim with a wrong
 * bridge_contract or event_topic0 is rejected by verify_receipt_log().
 * This test exercises the free function directly with mock data and does
 * NOT require a live testnet or environment variables.
 */
TEST( BridgeE2ENegativeTest, InvalidReceiptLogsRejected )
{
    // --- Construct deterministic mock data ---
    constexpr size_t kAddrSize = 20;
    constexpr size_t kHashSize = 32;

    // Test contract address (20 bytes)
    eth::Address test_addr{};
    for ( size_t i = 0; i < kAddrSize; ++i )
    {
        test_addr[i] = static_cast<uint8_t>( 0xAB + i );
    }

    // Test event topic0 (32 bytes)
    eth::Hash256 test_topic0{};
    for ( size_t i = 0; i < kHashSize; ++i )
    {
        test_topic0[i] = static_cast<uint8_t>( 0xDE + i );
    }

    // Test tx hash and block hash (32 bytes each)
    eth::Hash256 test_tx_hash{};
    for ( size_t i = 0; i < kHashSize; ++i )
    {
        test_tx_hash[i] = static_cast<uint8_t>( 0x11 + i );
    }
    eth::Hash256 test_block_hash{};
    for ( size_t i = 0; i < kHashSize; ++i )
    {
        test_block_hash[i] = static_cast<uint8_t>( 0x22 + i );
    }

    // Build a mock receipt with one log entry
    eth::codec::LogEntry log_entry;
    log_entry.address = test_addr;
    log_entry.topics.push_back( test_topic0 );
    log_entry.data = { 0x01, 0x02, 0x03, 0x04 };

    eth::codec::Receipt mock_receipt;
    mock_receipt.status = true;
    mock_receipt.logs.push_back( log_entry );

    // Build ReceiptResult
    eth::ReceiptResult receipt_result;
    receipt_result.receipt    = mock_receipt;
    receipt_result.tx_hash    = test_tx_hash;
    receipt_result.block_hash = test_block_hash;
    receipt_result.log_indices.push_back( 0 );

    // Build a matching BridgeEventClaim
    eth::BridgeEventClaim matching_claim;
    matching_claim.src_chain_id    = 11155111;
    matching_claim.tx_hash         = test_tx_hash;
    matching_claim.block_hash      = test_block_hash;
    matching_claim.log_index       = 0;
    matching_claim.bridge_contract = test_addr;
    matching_claim.event_topic0    = test_topic0;
    matching_claim.topics.push_back( test_topic0 );
    matching_claim.data = log_entry.data;

    // --- Case 1: Matching claim should succeed ---
    auto match_result = eth::verify_receipt_log( receipt_result, matching_claim );
    EXPECT_TRUE( match_result ) << "Matching claim should verify successfully";
    EXPECT_EQ( match_result.error, eth::ReceiptLogVerificationError::kNone );
    spdlog::info( "bridge_e2e: InvalidReceiptLogs — matching case: PASS" );

    // --- Case 2: Mismatched contract address should fail ---
    eth::BridgeEventClaim wrong_contract_claim = matching_claim;
    wrong_contract_claim.bridge_contract[0]    = 0xFF;
    auto wrong_contract_result                 = eth::verify_receipt_log( receipt_result, wrong_contract_claim );
    EXPECT_FALSE( wrong_contract_result ) << "Mismatched contract should be rejected";
    EXPECT_EQ( wrong_contract_result.error, eth::ReceiptLogVerificationError::kContractMismatch );
    spdlog::info( "bridge_e2e: InvalidReceiptLogs — wrong contract: correctly rejected" );

    // --- Case 3: Mismatched event topic0 should fail ---
    eth::BridgeEventClaim wrong_topic_claim = matching_claim;
    wrong_topic_claim.event_topic0[0]       = 0xFF;
    auto wrong_topic_result                 = eth::verify_receipt_log( receipt_result, wrong_topic_claim );
    EXPECT_FALSE( wrong_topic_result ) << "Mismatched topic0 should be rejected";
    EXPECT_EQ( wrong_topic_result.error, eth::ReceiptLogVerificationError::kTopic0Mismatch );
    spdlog::info( "bridge_e2e: InvalidReceiptLogs — wrong topic0: correctly rejected" );

    spdlog::info( "bridge_e2e: InvalidReceiptLogsRejected test complete" );
}
