/**
 * @file       bridge_rlpx_e2e_test.cpp
 * @brief      Live-Sepolia RLPx burn-event-gossip end-to-end test (Phase 04.2).
 * @date       2026-07-14
 * @author     Super Genius (info@gnus.ai)
 *
 * Constructs three SuperGenius nodes each with an independent EthWatchService in
 * production RLPx mode, wires a BridgeRelayer per node, streams 10 burns to live
 * Sepolia at a 1-2s cadence, and asserts every burn mints a UTXO on all three
 * nodes autonomously — no manual MintTokens call.
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include "account/BridgeRelayer.hpp"
#include "account/ChainContractPair.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "eth/eth_watch_service.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/outcome.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

#include "anvil_fixture.hpp"
#include "../../../evmrelay/examples/chain_config.hpp"

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

namespace
{

    /**
 * @brief Reads an unsigned integer env var with fallback.
 */
    static uint64_t EnvUint64( const char *name, uint64_t fallback )
    {
        const char *value = std::getenv( name );
        if ( !value || value[0] == '\0' )
        {
            return fallback;
        }
        try
        {
            unsigned long long parsed = std::stoull( value );
            return parsed == 0ULL ? fallback : static_cast<uint64_t>( parsed );
        }
        catch ( ... )
        {
            return fallback;
        }
    }

    /**
 * @brief Records one burn iteration: tx hash and the balance baselines at burn time.
 */
    struct BurnRecord
    {
        std::string tx_hash;
        uint64_t    baseline_main  = 0;
        uint64_t    baseline_proc1 = 0;
        uint64_t    baseline_proc2 = 0;
    };

} // namespace

using sgns::GeniusNode;

/**
 * @brief Three-node GTest fixture for live-Sepolia RLPx burn-event-gossip testing.
 *
 * Creates three GeniusNode instances, each with its own EthWatchService in
 * production RLPx mode and its own BridgeRelayer. Streams burns to live Sepolia
 * and asserts every burn produces a minted UTXO on all three nodes autonomously.
 */
class BridgeRlpxE2ETest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    static GeniusNodeConfig gGeniusNodeConfig;
    static GeniusNodeConfig gGeniusNodeConfig2;
    static GeniusNodeConfig gGeniusNodeConfig3;

    static std::string s_eth_private_key;

    /** @brief Per-node RLPx EthWatchService instances. */
    static std::shared_ptr<eth::EthWatchService> rlpx_service_main;
    static std::shared_ptr<eth::EthWatchService> rlpx_service_proc1;
    static std::shared_ptr<eth::EthWatchService> rlpx_service_proc2;

    /** @brief Per-node BridgeRelayer instances. */
    static std::shared_ptr<sgns::BridgeRelayer> relayer_main;
    static std::shared_ptr<sgns::BridgeRelayer> relayer_proc1;
    static std::shared_ptr<sgns::BridgeRelayer> relayer_proc2;

    /** @brief Per-node io_context + thread for RLPx service. */
    static std::unique_ptr<boost::asio::io_context> io_main;
    static std::unique_ptr<boost::asio::io_context> io_proc1;
    static std::unique_ptr<boost::asio::io_context> io_proc2;
    static std::unique_ptr<std::thread>             io_thread_main;
    static std::unique_ptr<std::thread>             io_thread_proc1;
    static std::unique_ptr<std::thread>             io_thread_proc2;

    /** @brief Sepolia GNUS bridge contract address. */
    static constexpr const char *kSepoliaContract = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";

    /** @brief Sepolia public RPC endpoint. */
    static constexpr const char *kSepoliaRpc = "https://ethereum-sepolia-rpc.publicnode.com";

    /** @brief ERC-1155 safeTransferFrom function selector. */
    static constexpr const char *kTransferSig = "safeTransferFrom(address,address,uint256,uint256,bytes)";

    /** @brief Small test mint amount in base units. */
    static constexpr uint64_t kMintAmount = 1;

    /** @brief Discovery + handshake settle window (default 30s; SGNS_RLPX_SETTLE_SECONDS overrides). */
    static constexpr uint64_t kRlpxSettleSeconds = 30;

    /** @brief Number of burn transactions (default 10; SGNS_RLPX_BURN_COUNT overrides). */
    static constexpr uint64_t kBurnCount = 10;

    /** @brief Mint appearance timeout per burn (45s). */
    static constexpr std::chrono::milliseconds kMintTimeoutMs{ 45000 };

    static void SetUpTestSuite();
    static void TearDownTestSuite();
};

// --- Static member initialization ---

std::shared_ptr<GeniusNode> BridgeRlpxE2ETest::node_main  = nullptr;
std::shared_ptr<GeniusNode> BridgeRlpxE2ETest::node_proc1 = nullptr;
std::shared_ptr<GeniusNode> BridgeRlpxE2ETest::node_proc2 = nullptr;

std::string BridgeRlpxE2ETest::s_eth_private_key;

GeniusNodeConfig BridgeRlpxE2ETest::gGeniusNodeConfig  = { "0xcafe",
                                                           0.35,
                                                           "1.0",
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           "./bridge_rlpx_node1/" };
GeniusNodeConfig BridgeRlpxE2ETest::gGeniusNodeConfig2 = { "0xcafe",
                                                           0.35,
                                                           "1.0",
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           "./bridge_rlpx_node2/" };
GeniusNodeConfig BridgeRlpxE2ETest::gGeniusNodeConfig3 = { "0xcafe",
                                                           0.35,
                                                           "1.0",
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           "./bridge_rlpx_node3/" };

std::shared_ptr<eth::EthWatchService> BridgeRlpxE2ETest::rlpx_service_main  = nullptr;
std::shared_ptr<eth::EthWatchService> BridgeRlpxE2ETest::rlpx_service_proc1 = nullptr;
std::shared_ptr<eth::EthWatchService> BridgeRlpxE2ETest::rlpx_service_proc2 = nullptr;

std::shared_ptr<sgns::BridgeRelayer> BridgeRlpxE2ETest::relayer_main  = nullptr;
std::shared_ptr<sgns::BridgeRelayer> BridgeRlpxE2ETest::relayer_proc1 = nullptr;
std::shared_ptr<sgns::BridgeRelayer> BridgeRlpxE2ETest::relayer_proc2 = nullptr;

std::unique_ptr<boost::asio::io_context> BridgeRlpxE2ETest::io_main;
std::unique_ptr<boost::asio::io_context> BridgeRlpxE2ETest::io_proc1;
std::unique_ptr<boost::asio::io_context> BridgeRlpxE2ETest::io_proc2;
std::unique_ptr<std::thread>             BridgeRlpxE2ETest::io_thread_main;
std::unique_ptr<std::thread>             BridgeRlpxE2ETest::io_thread_proc1;
std::unique_ptr<std::thread>             BridgeRlpxE2ETest::io_thread_proc2;

// --- Fixture implementation ---

void BridgeRlpxE2ETest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // Guard 1: opt-in env var
    if ( !std::getenv( "RUN_E2E_RLPX" ) )
    {
        GTEST_SKIP() << "Set RUN_E2E_RLPX=1 to run RLPx E2E bridge tests";
    }

    // Guard 2: signing key required for Sepolia transactions
    const char *private_key_env = std::getenv( "PRIVATE_KEY" );
    if ( !private_key_env )
    {
        private_key_env = std::getenv( "SIGNING_KEY" );
    }
    if ( !private_key_env )
    {
        GTEST_SKIP() << "PRIVATE_KEY or SIGNING_KEY env var required for RLPx E2E tests";
    }

    std::string raw_key( private_key_env );

    // Strip 0x prefix if present
    if ( raw_key.size() >= 2 && raw_key[0] == '0' && raw_key[1] == 'x' )
    {
        raw_key = raw_key.substr( 2 );
    }

    // Normalize to 64-char hex
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
        auto decoded = Base64Decode( raw_key );
        if ( decoded.size() != 32 )
        {
            GTEST_SKIP() << "PRIVATE_KEY/SIGNING_KEY is not valid hex or base64-encoded 32-byte key";
        }
        s_eth_private_key = BytesToHex( decoded );
        spdlog::info( "rlpx_e2e: decoded base64 signing key to hex ({} chars)", s_eth_private_key.size() );
    }

    // Guard 3: cast binary must be installed
    if ( !sgns::test::anvil::CastAvailable() )
    {
        GTEST_SKIP()
            << "cast binary not found — install Foundry: https://book.getfoundry.sh/getting-started/installation";
    }
    spdlog::info( "rlpx_e2e: cast binary found on PATH" );

    // Set per-node BaseWritePath
    std::string binary_path          = boost::dll::program_location().parent_path().string();
    gGeniusNodeConfig.BaseWritePath  = binary_path + "/bridge_rlpx_node1/";
    gGeniusNodeConfig2.BaseWritePath = binary_path + "/bridge_rlpx_node2/";
    gGeniusNodeConfig3.BaseWritePath = binary_path + "/bridge_rlpx_node3/";
    sgns::test::removeAllWithRetry( gGeniusNodeConfig.BaseWritePath );
    sgns::test::removeAllWithRetry( gGeniusNodeConfig2.BaseWritePath );
    sgns::test::removeAllWithRetry( gGeniusNodeConfig3.BaseWritePath );

    spdlog::info( "rlpx_e2e: creating 3-node cluster for RLPx E2E test" );

    // Create the full node FIRST — it creates the genesis block.
    GeniusNode::WriteNetworkConfig( gGeniusNodeConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( gGeniusNodeConfig.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true );
    node_main = GeniusNode::New( gGeniusNodeConfig, sgns::FromPrivateKey{ s_eth_private_key } );

    // Wait for node to leave CREATING state (polling, no thread sleep).
    {
        constexpr std::chrono::milliseconds kPostCreateTimeout{ 3000 };
        sgns::test::assertWaitForCondition( [&]() { return node_main->GetState() != GeniusNode::NodeState::CREATING; },
                                            kPostCreateTimeout,
                                            "node_main did not leave CREATING state after construction" );
    }

    // Set authorized address to match the full node
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node_main->GetAddress() );
    spdlog::info( "rlpx_e2e: set authorized full node address to {}", node_main->GetAddress().substr( 0, 16 ) );

    // Wait for the full node to reach READY state
    constexpr std::chrono::milliseconds kBlockchainInitTimeout{ 60000 };
    sgns::test::assertWaitForCondition( [&]() { return node_main->GetState() == GeniusNode::NodeState::READY; },
                                        kBlockchainInitTimeout,
                                        "node_main not ready" );

    spdlog::info( "rlpx_e2e: node_main READY, creating processor nodes" );

    // Create processor nodes
    GeniusNode::WriteNetworkConfig( gGeniusNodeConfig2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( gGeniusNodeConfig2.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true );
    node_proc1 = GeniusNode::New( gGeniusNodeConfig2, sgns::FromPrivateKey{ s_eth_private_key } );

    GeniusNode::WriteNetworkConfig( gGeniusNodeConfig3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( gGeniusNodeConfig3.BaseWritePath, /*node_type=*/"Light", /*is_processor=*/true );
    node_proc2 = GeniusNode::New( gGeniusNodeConfig3, sgns::FromPrivateKey{ s_eth_private_key } );

    // Bootstrap PubSub
    node_proc1->AddPeers( { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    // Wait for processor nodes to sync and reach READY (polling only, no thread sleep)
    {
        auto sync_deadline = std::chrono::steady_clock::now() + kBlockchainInitTimeout;
        bool synced        = false;
        while ( std::chrono::steady_clock::now() < sync_deadline )
        {
            if ( node_proc1->GetState() == GeniusNode::NodeState::READY &&
                 node_proc2->GetState() == GeniusNode::NodeState::READY )
            {
                synced = true;
                spdlog::info( "rlpx_e2e: all processor nodes synced and READY" );
                break;
            }
            // Small busy-poll; the wait_condition primitive itself handles cadence
        }
        if ( !synced )
        {
            spdlog::warn( "rlpx_e2e: processor nodes did not sync within timeout — proceeding" );
        }
    }

    spdlog::info( "rlpx_e2e: 3-node cluster ready" );

    // Configure Sepolia RPC endpoints on ALL THREE nodes
    {
        constexpr const char *kBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

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
            ep.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };
            sepolia_eps.push_back( ep );
        }

        node_main->ConfigureRpcEndpoint( "test", sepolia_eps );
        node_proc1->ConfigureRpcEndpoint( "test", sepolia_eps );
        node_proc2->ConfigureRpcEndpoint( "test", sepolia_eps );
        spdlog::info( "rlpx_e2e: configured {} Sepolia RPC endpoints on all 3 nodes", sepolia_eps.size() );
    }

    // --- RLPx service construction per node ---

    const uint64_t    settle_secs = EnvUint64( "SGNS_RLPX_SETTLE_SECONDS", kRlpxSettleSeconds );
    const std::string argv0       = boost::dll::program_location().string();
    const char       *json_env    = std::getenv( "EVMRELAY_LIVE_SEPOLIA_JSON" );
    const std::string json_path   = ( json_env != nullptr ) ? std::string( json_env ) : std::string();

    // Load chain peer config once (same for all three services)
    auto chain_cfg = evmrelay::examples::load_chain_peer_config( "ethereum-sepolia",
                                                                 argv0,
                                                                 json_path,
                                                                 "https://enodes.gnus.ai/chain_enodes.json.gz",
                                                                 true );

    if ( !chain_cfg.has_value() )
    {
        GTEST_SKIP()
            << "Could not load ethereum-sepolia chain peer config — check EVMRELAY_LIVE_SEPOLIA_JSON or network";
    }

    spdlog::info( "rlpx_e2e: loaded sepolia chain config with {} cached nodes", chain_cfg->nodes.size() );

    // Lambda to build a per-node RLPx service + BridgeRelayer
    auto BuildRlpxService = [&]( std::shared_ptr<GeniusNode>              &node,
                                 std::shared_ptr<eth::EthWatchService>    &svc,
                                 std::shared_ptr<sgns::BridgeRelayer>     &relayer,
                                 std::unique_ptr<boost::asio::io_context> &io,
                                 std::unique_ptr<std::thread>             &io_thread )
    {
        // Build EthWatchServiceConfig
        eth::EthWatchServiceConfig config{};
        config.connection.max_connections_per_chain = 3;
        config.connection.max_total_connections     = 9;
        config.chains                               = { *chain_cfg };
        config.discovery_mode                       = eth::EthWatchDiscoveryMode::kDiscoverFirst;
        config.discovery.bind_port                  = 0U;
        config.discv5_discovery.bind_port           = 0U;

        svc = std::make_shared<eth::EthWatchService>();

        auto tx_mgr_result = node->GetTransactionManager();
        ASSERT_TRUE( tx_mgr_result.has_value() ) << "node transaction manager not ready";
        std::shared_ptr<sgns::TransactionManager> tx_mgr = tx_mgr_result.value();

        relayer = sgns::BridgeRelayer::Create( std::weak_ptr<sgns::TransactionManager>( tx_mgr ), svc );

        relayer->Start( { sgns::ChainContractPair{ "ethereum-sepolia",
                                                   sgns::test::anvil::kSepoliaBridgeContractLower,
                                                   11155111 } } );

        svc->initialize( std::move( config ), []( const eth::WatchEventNotification & ) {} );

        io = std::make_unique<boost::asio::io_context>();
        svc->run( *io );

        io_thread = std::make_unique<std::thread>( [raw_io = io.get()]() { raw_io->run(); } );
    };

    BuildRlpxService( node_main, rlpx_service_main, relayer_main, io_main, io_thread_main );
    BuildRlpxService( node_proc1, rlpx_service_proc1, relayer_proc1, io_proc1, io_thread_proc1 );
    BuildRlpxService( node_proc2, rlpx_service_proc2, relayer_proc2, io_proc2, io_thread_proc2 );

    spdlog::info( "rlpx_e2e: all three RLPx services initialized and running" );

    // --- RLPx settle wait ---
    {
        constexpr std::chrono::milliseconds kSettleTimeout{ kRlpxSettleSeconds * 1000 };
        bool                                settled = waitForCondition(
            [&]() { return rlpx_service_main->aggregate_connection_stats().remote_status_accepted >= 1; },
            std::chrono::milliseconds( settle_secs * 1000 ) );

        if ( !settled )
        {
            const auto stats = rlpx_service_main->aggregate_connection_stats();
            spdlog::warn( "rlpx_e2e: RLPx peers did not complete ETH Status handshake within {}s "
                          "(remote_status_accepted={}, discovered={}) — skipping (D-03)",
                          settle_secs,
                          stats.remote_status_accepted,
                          stats.peer_queue.discovered_peer_count );
            GTEST_SKIP() << "RLPx peers did not complete ETH Status handshake within " << settle_secs << "s";
        }

        const auto stats = rlpx_service_main->aggregate_connection_stats();
        spdlog::info( "rlpx_e2e: RLPx handshake complete — remote_status_accepted={}, discovered_peers={}",
                      stats.remote_status_accepted,
                      stats.peer_queue.discovered_peer_count );
    }
}

void BridgeRlpxE2ETest::TearDownTestSuite()
{
    spdlog::info( "rlpx_e2e: tearing down RLPx services and nodes" );

    // Stop RLPx services (best-effort; may throw forced_unwind)
    auto StopService = []( std::shared_ptr<eth::EthWatchService> &svc )
    {
        if ( svc )
        {
            try
            {
                svc->stop();
            }
            catch ( ... )
            {
                spdlog::warn( "rlpx_e2e: exception during RLPx service stop (forced_unwind expected)" );
            }
        }
    };
    StopService( rlpx_service_main );
    StopService( rlpx_service_proc1 );
    StopService( rlpx_service_proc2 );

    // Stop io_contexts and join threads
    auto StopIo = []( std::unique_ptr<boost::asio::io_context> &io, std::unique_ptr<std::thread> &thr )
    {
        if ( io )
        {
            io->stop();
        }
        if ( thr && thr->joinable() )
        {
            thr->join();
        }
    };
    StopIo( io_main, io_thread_main );
    StopIo( io_proc1, io_thread_proc1 );
    StopIo( io_proc2, io_thread_proc2 );

    // Reset nodes
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();

    // Remove test data directories (NOT the RLPx services — intentionally leaked per live-test pattern)
    sgns::test::removeAllWithRetry( gGeniusNodeConfig.BaseWritePath );
    sgns::test::removeAllWithRetry( gGeniusNodeConfig2.BaseWritePath );
    sgns::test::removeAllWithRetry( gGeniusNodeConfig3.BaseWritePath );

    spdlog::info( "rlpx_e2e: teardown complete" );
}

/**
 * @brief Exercises the full RLPx-burn-to-autonomous-mint pipeline against live Sepolia.
 *
 * Steps:
 * 1. Derive sender address from PRIVATE_KEY via cast.
 * 2. Record initial balances on all three nodes.
 * 3. Stream burn transactions to live Sepolia at 1-2s cadence.
 * 4. Poll each burn for balance increase on ALL THREE nodes within 45s.
 * 5. Assert final balance delta >= successful_burns * kMintAmount on all three nodes.
 */
TEST_F( BridgeRlpxE2ETest, RlpxBurnStreamAutoMints )
{
    // --- Step 1: Derive sender address ---
    std::string wallet_cmd  = "cast wallet address " + s_eth_private_key + " 2>&1";
    FILE       *wallet_pipe = sgns::test::anvil::OpenCommandPipe( wallet_cmd.c_str(), "r" );
    ASSERT_NE( wallet_pipe, nullptr ) << "Failed to run cast wallet address";

    char addr_buf[256] = {};
    ASSERT_NE( std::fgets( addr_buf, sizeof( addr_buf ), wallet_pipe ), nullptr )
        << "cast wallet address returned no output";
    sgns::test::anvil::CloseCommandPipe( wallet_pipe );

    std::string sender_addr( addr_buf );
    while ( !sender_addr.empty() &&
            ( sender_addr.back() == '\n' || sender_addr.back() == '\r' || sender_addr.back() == ' ' ) )
    {
        sender_addr.pop_back();
    }
    ASSERT_FALSE( sender_addr.empty() ) << "Could not derive sender address from PRIVATE_KEY";
    spdlog::info( "rlpx_e2e: sender address = {}", sender_addr );

    // Use each node's own SuperGenius address as mint destination
    const std::string dest_addr_main  = node_main->GetAddress();
    const std::string dest_addr_proc1 = node_proc1->GetAddress();
    const std::string dest_addr_proc2 = node_proc2->GetAddress();
    spdlog::info( "rlpx_e2e: destination addresses — main={}, proc1={}, proc2={}",
                  dest_addr_main.substr( 0, 16 ),
                  dest_addr_proc1.substr( 0, 16 ),
                  dest_addr_proc2.substr( 0, 16 ) );

    // --- Step 2: Record initial balances ---
    uint64_t initial_balance_main  = node_main->GetBalance( dest_addr_main );
    uint64_t initial_balance_proc1 = node_proc1->GetBalance( dest_addr_proc1 );
    uint64_t initial_balance_proc2 = node_proc2->GetBalance( dest_addr_proc2 );
    spdlog::info( "rlpx_e2e: initial balances — main={}, proc1={}, proc2={}",
                  initial_balance_main,
                  initial_balance_proc1,
                  initial_balance_proc2 );

    uint64_t baseline_main  = initial_balance_main;
    uint64_t baseline_proc1 = initial_balance_proc1;
    uint64_t baseline_proc2 = initial_balance_proc2;

    // --- Step 3: Burn stream ---
    const uint64_t          burn_count = EnvUint64( "SGNS_RLPX_BURN_COUNT", kBurnCount );
    std::vector<BurnRecord> burn_records;
    burn_records.reserve( burn_count );

    unsigned int successful_burns = 0;

    for ( uint64_t i = 0; i < burn_count; ++i )
    {
        // Cadence delay: enforce 1-2s floor via deadline polling (no thread sleep).
        // Use a deadline-based lambda polled by assertWaitForCondition.
        {
            const auto delay    = std::chrono::milliseconds( 1000 + static_cast<int>( ( i * 137 ) % 1000 ) );
            const auto deadline = std::chrono::steady_clock::now() + delay;
            sgns::test::assertWaitForCondition( [deadline]() { return std::chrono::steady_clock::now() >= deadline; },
                                                delay + std::chrono::milliseconds( 500 ),
                                                "burn cadence delay" );
        }

        // Build and execute cast send
        std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) + " \"" + kTransferSig + "\" " +
                               sender_addr + " " + sender_addr + " 0 " + std::to_string( kMintAmount ) +
                               " 0x --private-key " + s_eth_private_key + " --rpc-url " + kSepoliaRpc + " --json 2>&1";

        FILE *cast_pipe = sgns::test::anvil::OpenCommandPipe( cast_cmd.c_str(), "r" );
        ASSERT_NE( cast_pipe, nullptr ) << "Failed to run cast send";

        std::string cast_output;
        char        line_buf[1024] = {};
        while ( std::fgets( line_buf, sizeof( line_buf ), cast_pipe ) )
        {
            cast_output += line_buf;
        }
        int cast_rc = sgns::test::anvil::CloseCommandPipe( cast_pipe );

        if ( cast_rc != 0 )
        {
            spdlog::warn( "rlpx_e2e: burn {}/{} failed (cast rc={}), continuing — output: {}",
                          i + 1,
                          burn_count,
                          cast_rc,
                          cast_output );
            continue;
        }

        // Parse transaction hash
        std::string tx_hash = sgns::test::anvil::ParseTxHashFromCastJson( cast_output );
        if ( tx_hash.empty() )
        {
            spdlog::warn( "rlpx_e2e: burn {}/{} could not parse tx hash from cast output", i + 1, burn_count );
            continue;
        }

        spdlog::info( "rlpx_e2e: burn {}/{} tx_hash={}", i + 1, burn_count, tx_hash.substr( 0, 16 ) );

        BurnRecord rec;
        rec.tx_hash        = tx_hash;
        rec.baseline_main  = baseline_main;
        rec.baseline_proc1 = baseline_proc1;
        rec.baseline_proc2 = baseline_proc2;
        burn_records.push_back( rec );
        ++successful_burns;

        // Update baselines: record current balances as baseline for next burn
        baseline_main  = node_main->GetBalance( dest_addr_main );
        baseline_proc1 = node_proc1->GetBalance( dest_addr_proc1 );
        baseline_proc2 = node_proc2->GetBalance( dest_addr_proc2 );
    }

    spdlog::info( "rlpx_e2e: burn stream complete — {} / {} burns successful", successful_burns, burn_count );

    if ( successful_burns == 0 )
    {
        GTEST_SKIP() << "No successful burns in the stream — cannot verify RLPx gossip";
    }

    // --- Step 4: Verify each burn mints on all three nodes ---
    unsigned int minted_main  = 0;
    unsigned int minted_proc1 = 0;
    unsigned int minted_proc2 = 0;

    for ( const auto &rec : burn_records )
    {
        spdlog::info( "rlpx_e2e: verifying burn tx_hash={} (baselines: main={}, proc1={}, proc2={})",
                      rec.tx_hash.substr( 0, 16 ),
                      rec.baseline_main,
                      rec.baseline_proc1,
                      rec.baseline_proc2 );

        // Poll node_main
        {
            uint64_t current = 0;
            bool     ok      = waitForCondition(
                [&]()
                {
                    current = node_main->GetBalance( dest_addr_main );
                    return current >= rec.baseline_main + kMintAmount;
                },
                kMintTimeoutMs );
            if ( ok )
            {
                spdlog::info( "rlpx_e2e: node_main minted for tx_hash={} (balance={})",
                              rec.tx_hash.substr( 0, 16 ),
                              current );
                ++minted_main;
            }
            else
            {
                spdlog::error(
                    "rlpx_e2e: node_main did NOT mint for tx_hash={} within {}ms (balance={}, expected >= {})",
                    rec.tx_hash.substr( 0, 16 ),
                    kMintTimeoutMs.count(),
                    current,
                    rec.baseline_main + kMintAmount );
            }
        }

        // Poll node_proc1
        {
            uint64_t current = 0;
            bool     ok      = waitForCondition(
                [&]()
                {
                    current = node_proc1->GetBalance( dest_addr_proc1 );
                    return current >= rec.baseline_proc1 + kMintAmount;
                },
                kMintTimeoutMs );
            if ( ok )
            {
                spdlog::info( "rlpx_e2e: node_proc1 minted for tx_hash={} (balance={})",
                              rec.tx_hash.substr( 0, 16 ),
                              current );
                ++minted_proc1;
            }
            else
            {
                spdlog::error(
                    "rlpx_e2e: node_proc1 did NOT mint for tx_hash={} within {}ms (balance={}, expected >= {})",
                    rec.tx_hash.substr( 0, 16 ),
                    kMintTimeoutMs.count(),
                    current,
                    rec.baseline_proc1 + kMintAmount );
            }
        }

        // Poll node_proc2
        {
            uint64_t current = 0;
            bool     ok      = waitForCondition(
                [&]()
                {
                    current = node_proc2->GetBalance( dest_addr_proc2 );
                    return current >= rec.baseline_proc2 + kMintAmount;
                },
                kMintTimeoutMs );
            if ( ok )
            {
                spdlog::info( "rlpx_e2e: node_proc2 minted for tx_hash={} (balance={})",
                              rec.tx_hash.substr( 0, 16 ),
                              current );
                ++minted_proc2;
            }
            else
            {
                spdlog::error(
                    "rlpx_e2e: node_proc2 did NOT mint for tx_hash={} within {}ms (balance={}, expected >= {})",
                    rec.tx_hash.substr( 0, 16 ),
                    kMintTimeoutMs.count(),
                    current,
                    rec.baseline_proc2 + kMintAmount );
            }
        }
    }

    // --- Step 5: Final assertions ---
    uint64_t final_balance_main  = node_main->GetBalance( dest_addr_main );
    uint64_t final_balance_proc1 = node_proc1->GetBalance( dest_addr_proc1 );
    uint64_t final_balance_proc2 = node_proc2->GetBalance( dest_addr_proc2 );

    uint64_t delta_main  = ( final_balance_main > initial_balance_main ) ? ( final_balance_main - initial_balance_main )
                                                                         : 0;
    uint64_t delta_proc1 = ( final_balance_proc1 > initial_balance_proc1 )
                               ? ( final_balance_proc1 - initial_balance_proc1 )
                               : 0;
    uint64_t delta_proc2 = ( final_balance_proc2 > initial_balance_proc2 )
                               ? ( final_balance_proc2 - initial_balance_proc2 )
                               : 0;

    spdlog::info( "rlpx_e2e: final deltas — main={}, proc1={}, proc2={} (expected >= {})",
                  delta_main,
                  delta_proc1,
                  delta_proc2,
                  successful_burns * kMintAmount );
    spdlog::info( "rlpx_e2e: mints per node — main={}/{}, proc1={}/{}, proc2={}/{}",
                  minted_main,
                  successful_burns,
                  minted_proc1,
                  successful_burns,
                  minted_proc2,
                  successful_burns );

    EXPECT_GE( delta_main, successful_burns * kMintAmount )
        << "node_main balance delta too low for " << successful_burns << " burns";
    EXPECT_GE( delta_proc1, successful_burns * kMintAmount )
        << "node_proc1 balance delta too low for " << successful_burns << " burns";
    EXPECT_GE( delta_proc2, successful_burns * kMintAmount )
        << "node_proc2 balance delta too low for " << successful_burns << " burns";

    // D-12: assert each burn mints on all three nodes
    EXPECT_EQ( minted_main, successful_burns )
        << "node_main: some burns did not mint (RLPx gossip failure on main node)";
    EXPECT_EQ( minted_proc1, successful_burns )
        << "node_proc1: some burns did not mint (RLPx gossip failure on processor 1)";
    EXPECT_EQ( minted_proc2, successful_burns )
        << "node_proc2: some burns did not mint (RLPx gossip failure on processor 2)";

    spdlog::info( "rlpx_e2e: RlpxBurnStreamAutoMints test complete" );
}
