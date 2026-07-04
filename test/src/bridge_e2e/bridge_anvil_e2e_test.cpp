/**
 * @file       bridge_anvil_e2e_test.cpp
 * @brief      Local-Anvil and Sepolia-direct E2E tests for the burn-to-mint bridge.
 * @date       2026-07-02
 * @author     Super Genius (info@gnus.ai)
 *
 * Self-contained burn-to-mint bridge E2E test exercising a local Anvil
 * instance that forks Sepolia state. No dependency on
 * `../TokenContracts/gnus-ai/.env` — the Anvil default key #0 is hardcoded
 * as a public test constant, and account #0 is funded with GNUS via
 * anvil_impersonateAccount of a known Sepolia holder. A Sepolia-direct
 * fallback test class reproduces the original Phase 4 live-Sepolia approach
 * gated by RUN_E2E_BRIDGE + PRIVATE_KEY env vars.
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
#include "base/hexutil.hpp"
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
     * @brief Writes a per-node sgns_config.json setting is_processor=false.
     *
     * The develop refactor moved is_processor out of the GeniusNode::New() signature into
     * sgns_config.json (read by GeniusNode::LoadSgnsConfig). All nodes in these E2E fixtures
     * are non-processors, so each node's BaseWritePath gets this file to preserve the
     * pre-develop New() isprocessor=false behavior.
     *
     * @param[in] base_write_path  Per-node BaseWritePath (trailing slash expected).
     */
    void WriteSgnsConfig( const std::string &baseWritePath )
    {
        constexpr const char *kSgnsConfigContent  = R"({"is_processor": false})";
        constexpr const char *kSgnsConfigFilename = "sgns_config.json";
        std::filesystem::create_directories( baseWritePath );
        const std::string kConfigPath = baseWritePath + kSgnsConfigFilename;
        std::ofstream     out( kConfigPath, std::ios::binary | std::ios::trunc );
        out << kSgnsConfigContent;
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
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static sgns::test::anvil::AnvilProcess s_anvil;

    /** @brief Small test mint amount in base units. */
    static inline constexpr uint64_t kMintAmount = 1u;

    /** @brief Mint finalization timeout (Anvil block times are sub-second). */
    static inline constexpr std::chrono::milliseconds kMintTimeout{ 10000 };

    /** @brief Node READY timeout (covers genesis + processor sync). */
    static inline constexpr std::chrono::milliseconds kNodeReadyTimeout{ 60000 };

    /** @brief Replay-dedup assertion timeout (D-18). */
    static inline constexpr std::chrono::milliseconds kReplayTimeout{ 5000 };

    /** @brief Anvil-path signing key (hex, no 0x prefix) — public test value. */
    static inline constexpr const char *kAnvilAccount0HexKey =
        "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";

    /**
     * @brief Starts Anvil, funds account #0, and bootstraps the 3-node cluster.
     */
    static void SetUpTestSuite();

    /**
     * @brief Tears down the cluster and stops the Anvil subprocess.
     */
    static void TearDownTestSuite();
};

std::shared_ptr<GeniusNode>     BridgeAnvilE2ETest::node_main  = nullptr;
std::shared_ptr<GeniusNode>     BridgeAnvilE2ETest::node_proc1 = nullptr;
std::shared_ptr<GeniusNode>     BridgeAnvilE2ETest::node_proc2 = nullptr;
sgns::test::anvil::AnvilProcess BridgeAnvilE2ETest::s_anvil;

DevConfig_st BridgeAnvilE2ETest::DEV_CONFIG  = { "0xcafe",
                                                 "0.65",
                                                 "1.0",
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 "./anvil_node1" };
DevConfig_st BridgeAnvilE2ETest::DEV_CONFIG2 = { "0xcafe",
                                                 "0.65",
                                                 "1.0",
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 "./anvil_node2" };
DevConfig_st BridgeAnvilE2ETest::DEV_CONFIG3 = { "0xcafe",
                                                 "0.65",
                                                 "1.0",
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 "./anvil_node3" };

void BridgeAnvilE2ETest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // D-16: skip cleanly when Foundry binaries are missing.
    if ( !sgns::test::anvil::AnvilAvailable() || !sgns::test::anvil::CastAvailable() )
    {
        GTEST_SKIP() << "Install Foundry (anvil + cast): https://book.getfoundry.sh/getting-started/installation";
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
        s_anvil.Stop();
        GTEST_SKIP() << "Could not fund Anvil account #0 via impersonation of " << sgns::test::anvil::kGnusHolderSepolia
                     << " — skipping Anvil path; run Sepolia-direct test instead";
    }

    // Per-node BaseWritePath from binary location (Phase 4 pattern).
    std::string binary_path   = boost::dll::program_location().parent_path().string();
    DEV_CONFIG.BaseWritePath  = binary_path + "/anvil_node1/";
    DEV_CONFIG2.BaseWritePath = binary_path + "/anvil_node2/";
    DEV_CONFIG3.BaseWritePath = binary_path + "/anvil_node3/";

    // Write per-node sgns_config.json (is_processor=false) — the develop refactor moved
    // is_processor out of New() into this file. Matches the pre-develop isprocessor=false arg.
    WriteSgnsConfig( DEV_CONFIG.BaseWritePath );
    WriteSgnsConfig( DEV_CONFIG2.BaseWritePath );
    WriteSgnsConfig( DEV_CONFIG3.BaseWritePath );

    spdlog::info( "bridge_anvil: creating 3-node cluster against local Anvil" );

    // Create full node first — it builds the genesis block.
    node_main = GeniusNode::NewFromPrivateKey( DEV_CONFIG, kAnvilAccount0HexKey, false, 40011, true );

    // Trigger StoreGenesisRegistry on the full node's address.
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node_main->GetAddress() );
    spdlog::info( "bridge_anvil: authorized full node address = {}", node_main->GetAddress().substr( 0, 16 ) );

    // Wait for full node READY (genesis + account-creation blocks).
    ASSERT_WAIT_FOR_CONDITION( [&]() { return node_main->GetState() == GeniusNode::NodeState::READY; },
                               kNodeReadyTimeout,
                               "node_main READY",
                               nullptr );

    spdlog::info( "bridge_anvil: node_main READY, creating processor nodes" );

    // Regular nodes sync genesis via PubSub (Phase 4 pattern, ports 40012/40013).
    node_proc1 = GeniusNode::NewFromPrivateKey( DEV_CONFIG2, kAnvilAccount0HexKey, false, 40012 );
    node_proc2 = GeniusNode::NewFromPrivateKey( DEV_CONFIG3, kAnvilAccount0HexKey, false, 40013 );

    node_proc1->GetPubSub()->AddPeers(
        { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    // Wait for processor nodes to sync and reach READY.
    ASSERT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_proc1->GetState() == GeniusNode::NodeState::READY &&
                   node_proc2->GetState() == GeniusNode::NodeState::READY;
        },
        kNodeReadyTimeout,
        "processor nodes READY",
        nullptr );

    spdlog::info( "bridge_anvil: 3-node cluster ready" );

    // D-11: configure RPC endpoints pointing at the LOCAL Anvil instance.
    // Single local source — weight 100 alone satisfies the >= 75 consensus threshold.
    {
        sgns::WeightedRpcEndpoint ep;
        ep.url                     = s_anvil.RpcUrl();
        ep.consensus_weight        = 100;
        ep.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
        ep.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };

        std::vector<sgns::WeightedRpcEndpoint> anvil_eps{ ep };
        node_main->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps );
        spdlog::info( "bridge_anvil: configured {} RPC endpoint at {}", anvil_eps.size(), s_anvil.RpcUrl() );
    }
}

void BridgeAnvilE2ETest::TearDownTestSuite()
{
    spdlog::info( "bridge_anvil: tearing down nodes" );
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();
    s_anvil.Stop();
    std::filesystem::remove_all( DEV_CONFIG.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath );
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
    const std::string dest_addr   = node_main->GetAddress();
    spdlog::info( "bridge_anvil: sender={}, dest={}", sender_addr, dest_addr.substr( 0, 16 ) );

    const uint64_t initial_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_anvil: initial balance = {}", initial_balance );

    // Send a real bridgeOut() burn targeting the LOCAL Anvil RPC (D-14 Path A).
    spdlog::info( "bridge_anvil: sending burn transaction to local Anvil" );
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, node_main->GetAddress() );
    spdlog::info( "bridge_anvil: burn tx hash = {}", tx_hash );
    ASSERT_FALSE( tx_hash.empty() ) << "bridgeOut burn-seeding failed (cast send rejected the call)";

    // Feed burn tx hash into the MintTokens pipeline.
    spdlog::info( "bridge_anvil: triggering MintTokens on node_main" );
    EXPECT_OUTCOME_TRUE( mint_result,
                         node_main->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kMintTimeout ) );
    spdlog::info( "bridge_anvil: MintTokens completed" );

    // D-17a: assert minted UTXO appears in recipient balance.
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > initial_balance; },
                               kMintTimeout,
                               "Anvil burn minted UTXO appears in recipient balance",
                               nullptr );

    const uint64_t final_balance = node_main->GetBalance( dest_addr );
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
    const std::string dest_addr = node_main->GetAddress();

    // Send a fresh bridgeOut() burn tx to local Anvil.
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), kMintAmount, node_main->GetAddress() );
    ASSERT_FALSE( tx_hash.empty() ) << "bridgeOut burn-seeding failed (cast send rejected the call)";
    spdlog::info( "bridge_anvil: replay-test burn tx hash = {}", tx_hash );

    // First mint should succeed.
    EXPECT_OUTCOME_TRUE( first_result,
                         node_main->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout ) );
    spdlog::info( "bridge_anvil: replay-test first mint submitted" );

    const uint64_t balance_before = node_main->GetBalance( dest_addr );
    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > balance_before; },
                               kReplayTimeout,
                               "First Anvil mint balance increase",
                               nullptr );

    // Second mint with the SAME tx hash must be rejected by the dedup cache.
    auto second_result = node_main->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kReplayTimeout );

    bool replay_rejected = second_result.has_error();
    if ( !replay_rejected )
    {
        const uint64_t balance_after_replay = node_main->GetBalance( dest_addr );
        EXPECT_EQ( balance_after_replay, node_main->GetBalance( dest_addr ) )
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

// =============================================================================
// BridgeSepoliaDirectFallbackTest — Path B: live Sepolia (D-14 Path B)
// =============================================================================

/**
 * @brief Three-node GTest fixture for the live-Sepolia fallback path.
 *
 * Reproduces the original Phase 4 burn-to-mint pipeline against real Sepolia
 * using a user-supplied PRIVATE_KEY. Gated by RUN_E2E_BRIDGE + PRIVATE_KEY
 * env vars — no silent execution against live Sepolia.
 */
class BridgeSepoliaDirectFallbackTest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static std::string s_eth_private_key;

    /** @brief Sepolia GNUS contract address (mixed-case EIP-55). */
    static inline constexpr const char *kSepoliaContract = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";

    /** @brief ERC-1155 safeTransferFrom function selector. */
    static inline constexpr const char *kTransferSig = "safeTransferFrom(address,address,uint256,uint256,bytes)";

    /** @brief Live Sepolia public RPC endpoint. */
    static inline constexpr const char *kSepoliaRpc = "https://ethereum-sepolia-rpc.publicnode.com";

    /** @brief Lowercase bridge contract address used in RPC endpoint configuration. */
    static inline constexpr const char *kBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

    /** @brief Bridge event topic0 (BridgeSourceBurned). */
    static inline constexpr const char *kEventTopic0 =
        "0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62";

    /** @brief Mint finalization timeout (Sepolia block confirmation). */
    static inline constexpr std::chrono::milliseconds kMintTimeout{ 10000 };

    /** @brief Node READY timeout. */
    static inline constexpr std::chrono::milliseconds kNodeReadyTimeout{ 60000 };

    /** @brief Small test mint amount in base units. */
    static inline constexpr uint64_t kMintAmount = 1u;

    /**
     * @brief Bootstraps the 3-node cluster against live Sepolia (Path B).
     */
    static void SetUpTestSuite();

    /**
     * @brief Tears down the cluster and removes test data directories.
     */
    static void TearDownTestSuite();
};

std::shared_ptr<GeniusNode> BridgeSepoliaDirectFallbackTest::node_main  = nullptr;
std::shared_ptr<GeniusNode> BridgeSepoliaDirectFallbackTest::node_proc1 = nullptr;
std::shared_ptr<GeniusNode> BridgeSepoliaDirectFallbackTest::node_proc2 = nullptr;
std::string                 BridgeSepoliaDirectFallbackTest::s_eth_private_key;

DevConfig_st BridgeSepoliaDirectFallbackTest::DEV_CONFIG  = { "0xcafe",
                                                              "0.65",
                                                              "1.0",
                                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                                              "./sepolia_fb_node1" };
DevConfig_st BridgeSepoliaDirectFallbackTest::DEV_CONFIG2 = { "0xcafe",
                                                              "0.65",
                                                              "1.0",
                                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                                              "./sepolia_fb_node2" };
DevConfig_st BridgeSepoliaDirectFallbackTest::DEV_CONFIG3 = { "0xcafe",
                                                              "0.65",
                                                              "1.0",
                                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                                              "./sepolia_fb_node3" };

void BridgeSepoliaDirectFallbackTest::SetUpTestSuite()
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
        { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // D-14 Path B gating — explicit opt-in required.
    if ( !std::getenv( "RUN_E2E_BRIDGE" ) )
    {
        GTEST_SKIP() << "Set RUN_E2E_BRIDGE=1 to run Sepolia-direct fallback";
    }

    // D-10/D-14: Path B requires a user-supplied signing key (Anvil defaults don't exist on real Sepolia).
    const char *pk_env = std::getenv( "PRIVATE_KEY" );
    if ( !pk_env )
    {
        pk_env = std::getenv( "SIGNING_KEY" );
    }
    if ( !pk_env )
    {
        GTEST_SKIP() << "PRIVATE_KEY env var required for Sepolia-direct fallback (D-14 Path B)";
    }

    s_eth_private_key = NormalizePrivateKey( pk_env );
    if ( s_eth_private_key.empty() )
    {
        GTEST_SKIP() << "PRIVATE_KEY is not valid hex or base64-encoded 32-byte key";
    }

    // D-16: cast binary required.
    if ( !sgns::test::anvil::CastAvailable() )
    {
        GTEST_SKIP()
            << "cast binary not found — install Foundry: https://book.getfoundry.sh/getting-started/installation";
    }

    // Per-node BaseWritePath.
    std::string binary_path   = boost::dll::program_location().parent_path().string();
    DEV_CONFIG.BaseWritePath  = binary_path + "/sepolia_fb_node1/";
    DEV_CONFIG2.BaseWritePath = binary_path + "/sepolia_fb_node2/";
    DEV_CONFIG3.BaseWritePath = binary_path + "/sepolia_fb_node3/";

    // Write per-node sgns_config.json (is_processor=false) — the develop refactor moved
    // is_processor out of New() into this file. Matches the pre-develop isprocessor=false arg.
    WriteSgnsConfig( DEV_CONFIG.BaseWritePath );
    WriteSgnsConfig( DEV_CONFIG2.BaseWritePath );
    WriteSgnsConfig( DEV_CONFIG3.BaseWritePath );

    spdlog::info( "bridge_sepolia_fb: creating 3-node cluster against live Sepolia" );

    // Full node first (creates genesis). Ports 40021/40022/40023.
    node_main = GeniusNode::NewFromPrivateKey( DEV_CONFIG, s_eth_private_key.c_str(), false, 40021, true );

    sgns::Blockchain::SetAuthorizedFullNodeAddress( node_main->GetAddress() );
    spdlog::info( "bridge_sepolia_fb: authorized full node = {}", node_main->GetAddress().substr( 0, 16 ) );

    ASSERT_WAIT_FOR_CONDITION( [&]() { return node_main->GetState() == GeniusNode::NodeState::READY; },
                               kNodeReadyTimeout,
                               "node_main READY",
                               nullptr );

    node_proc1 = GeniusNode::NewFromPrivateKey( DEV_CONFIG2, s_eth_private_key.c_str(), false, 40022 );
    node_proc2 = GeniusNode::NewFromPrivateKey( DEV_CONFIG3, s_eth_private_key.c_str(), false, 40023 );

    node_proc1->GetPubSub()->AddPeers(
        { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    ASSERT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_proc1->GetState() == GeniusNode::NodeState::READY &&
                   node_proc2->GetState() == GeniusNode::NodeState::READY;
        },
        kNodeReadyTimeout,
        "processor nodes READY",
        nullptr );

    spdlog::info( "bridge_sepolia_fb: 3-node cluster ready" );

    // Configure live Sepolia RPC endpoints (6 public endpoints × weight 25 = 150 >= 75).
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
        node_main->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, sepolia_eps );
        spdlog::info( "bridge_sepolia_fb: configured {} Sepolia RPC endpoints", sepolia_eps.size() );
    }
}

void BridgeSepoliaDirectFallbackTest::TearDownTestSuite()
{
    spdlog::info( "bridge_sepolia_fb: tearing down nodes" );
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();
    std::filesystem::remove_all( DEV_CONFIG.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG2.BaseWritePath );
    std::filesystem::remove_all( DEV_CONFIG3.BaseWritePath );
}

/**
 * @brief Positive live-Sepolia burn-to-mint pipeline test (D-14 Path B).
 *
 * Derives the sender address from PRIVATE_KEY via `cast wallet address`,
 * sends a burn transaction to live Sepolia, parses the tx hash, feeds it
 * to MintTokens, and asserts the recipient balance increases. Skips
 * cleanly when RUN_E2E_BRIDGE or PRIVATE_KEY is absent.
 */
TEST_F( BridgeSepoliaDirectFallbackTest, BurnToMintPipeline )
{
    // Derive sender address from PRIVATE_KEY.
    std::string wallet_cmd = "cast wallet address " + s_eth_private_key + " 2>&1";
    int         wallet_rc  = -1;
    std::string wallet_out = sgns::test::anvil::RunShellCapture( wallet_cmd, wallet_rc );
    ASSERT_EQ( wallet_rc, 0 ) << "cast wallet address failed";
    ASSERT_FALSE( wallet_out.empty() ) << "cast wallet address returned no output";

    // Trim trailing whitespace.
    std::string sender_addr = wallet_out;
    while ( !sender_addr.empty() &&
            ( sender_addr.back() == '\n' || sender_addr.back() == '\r' || sender_addr.back() == ' ' ) )
    {
        sender_addr.pop_back();
    }
    ASSERT_FALSE( sender_addr.empty() ) << "Could not derive sender address from PRIVATE_KEY";
    spdlog::info( "bridge_sepolia_fb: sender address = {}", sender_addr );

    const std::string dest_addr       = node_main->GetAddress();
    const uint64_t    initial_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_sepolia_fb: initial balance = {}", initial_balance );

    // Send burn transaction to LIVE Sepolia.
    std::string cast_cmd = "cast send " + std::string( kSepoliaContract ) + " \"" + kTransferSig + "\" " + sender_addr +
                           " " + sender_addr + " 0 " + std::to_string( kMintAmount ) + " 0x --private-key " +
                           s_eth_private_key + " --rpc-url " + kSepoliaRpc + " --json 2>&1";

    spdlog::info( "bridge_sepolia_fb: sending burn transaction to live Sepolia" );
    int         cast_rc     = -1;
    std::string cast_output = sgns::test::anvil::RunShellCapture( cast_cmd, cast_rc );
    spdlog::info( "bridge_sepolia_fb: cast send output: {}", cast_output );
    ASSERT_EQ( cast_rc, 0 ) << "cast send failed: " << cast_output;

    const std::string tx_hash = sgns::test::anvil::ParseTxHashFromCastJson( cast_output );
    ASSERT_FALSE( tx_hash.empty() ) << "Could not parse transactionHash";
    spdlog::info( "bridge_sepolia_fb: burn tx hash = {}", tx_hash );

    EXPECT_OUTCOME_TRUE( mint_result,
                         node_main->MintTokens( kMintAmount,
                                                tx_hash,
                                                sgns::test::anvil::kSepoliaChainId,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                dest_addr,
                                                kMintTimeout ) );
    spdlog::info( "bridge_sepolia_fb: MintTokens completed" );

    EXPECT_WAIT_FOR_CONDITION( [&]() { return node_main->GetBalance( dest_addr ) > initial_balance; },
                               kMintTimeout,
                               "Sepolia-direct minted UTXO appears in recipient balance",
                               nullptr );

    const uint64_t final_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "bridge_sepolia_fb: balance after mint = {} (delta = {})",
                  final_balance,
                  final_balance - initial_balance );
    EXPECT_GE( final_balance - initial_balance, kMintAmount );

    spdlog::info( "bridge_sepolia_fb: BurnToMintPipeline test complete" );
}
