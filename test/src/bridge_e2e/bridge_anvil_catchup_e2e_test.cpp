/**
 * @file       bridge_anvil_catchup_e2e_test.cpp
 * @brief      Phase 5 D-20 startup catch-up scan auto-mint verification against a local Anvil fork.
 * @date       2026-07-03
 * @author     Super Genius (info@gnus.ai)
 *
 * Self-contained E2E test that proves Phase 5's startup catch-up scan auto-detects
 * historical bridge burns and auto-mints them with zero manual MintTokens() intervention.
 *
 * The test seeds N=kNumCatchupBurns ERC-1155 burn transactions against the local
 * Anvil fork BEFORE any GeniusNode starts, then bootstraps a 3-node cluster whose
 * per-node bridge_chains_config.json points the sepolia entry at the local Anvil RPC.
 * On node startup, GeniusNode::InitializeAndStartBridge() reads the config -> fires
 * OnRpcEndpointsReady -> populates catchup_chains_ -> when TransactionManager reaches
 * READY, PerformStartupCatchupScan() queries eth_getLogs against Anvil, finds the N
 * historical burns, and calls MintFunds() for each.
 *
 * INVARIANT: This test makes zero manual MintTokens() calls. The only path by which
 * the recipient balance can increase is GeniusNode::PerformStartupCatchupScan -> MintFunds().
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

#include "testutil/wait_condition.hpp"

#include "anvil_fixture.hpp"

using sgns::GeniusNode;

// =============================================================================
// BridgeAnvilCatchupE2ETest — startup catch-up scan auto-mint (Phase 5 D-20)
// =============================================================================

/**
 * @brief Three-node fixture verifying the startup catch-up scan auto-mints historical burns.
 *
 * SetUpTestSuite seeds kNumCatchupBurns ERC-1155 burns on the local Anvil fork
 * BEFORE creating any node, then bootstraps a 3-node cluster. Each node has a
 * bridge_chains_config.json in its BaseWritePath so ResolveBridgeChainsConfigPath()
 * finds it at priority 1, populating catchup_chains_ via OnRpcEndpointsReady.
 * ConfigureRpcEndpoint primes the validator's URL map (chain_id 11155111 -> local
 * Anvil) before the auto-scan queries it via GetFirstRpcUrl.
 *
 * ORDERING RATIONALE: The catch-up scan reads validator.GetFirstRpcUrl(chain_id)
 * at runtime, and is auto-triggered when (a) catchup_chains_ is populated AND
 * (b) TransactionManager reaches READY. To make the test deterministic, the
 * fixture:
 *   1. Writes per-node bridge_chains_config.json (drives catchup_chains_).
 *   2. Seeds pre-node burns.
 *   3. Creates node_main.
 *   4. Polls node_main out of INITIALIZING (validator is constructible).
 *   5. Calls ConfigureRpcEndpoint to prime the URL map.
 *   6. Polls node_main to READY (the auto-scan can now run with a primed URL).
 *   7. Creates the two processor nodes and bootstraps PubSub.
 */
class BridgeAnvilCatchupE2ETest : public ::testing::Test
{
protected:
    static std::shared_ptr<GeniusNode> node_main;
    static std::shared_ptr<GeniusNode> node_proc1;
    static std::shared_ptr<GeniusNode> node_proc2;

    /** @brief Number of GeniusNode instances in the cluster (index 0 is the full node). */
    static inline constexpr unsigned int kNodeCount = 3u;

    /** @brief Per-node config — index 0 is the full node (node_main), 1/2 are processors. */
    static std::array<GeniusNodeConfig, kNodeCount> s_configs;

    static sgns::test::anvil::AnvilProcess s_anvil;

    /** @brief Pre-node burn tx hashes seeded on the local Anvil fork before any node starts. */
    static std::vector<std::string> s_pre_node_burn_hashes;

    /** @brief Base mint amount per burn (base units). */
    static inline constexpr unsigned int kMintAmount = 1u;

    /** @brief Number of historical burns seeded on the Anvil fork before node start (D-17 precondition). */
    static inline constexpr unsigned int kNumCatchupBurns = 3u;

    /** @brief Per-node BaseWritePath subdirectory names (avoid collision with Plan 04.1-01 dirs). */
    static inline constexpr const char *kNode1Dir = "/catchup_node1/";
    static inline constexpr const char *kNode2Dir = "/catchup_node2/";
    static inline constexpr const char *kNode3Dir = "/catchup_node3/";

    /** @brief PubSub port base for the catch-up fixture (Plan 04.1-01 used 40011..40013). */
    static inline constexpr unsigned int kNodeMainPort = 40031u;
    static inline constexpr unsigned int kNodeProc1Port = 40032u;
    static inline constexpr unsigned int kNodeProc2Port = 40033u;

    /** @brief Per-node bridge config filename (must match ResolveBridgeChainsConfigPath priority 1). */
    static inline constexpr const char *kBridgeChainsConfigFilename = "bridge_chains_config.json";

    /** @brief Content of the per-node bridge_chains_config.json (sepolia-only subset). */
    static inline constexpr const char *kBridgeChainsConfigContent = R"JSON({
    "ethereum-sepolia": {
        "chain_id": 11155111,
        "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70"
    }
}
)JSON";

    /** @brief Timeout for the auto-mint path after node READY (scan runs after CRDT sync). */
    static inline constexpr std::chrono::milliseconds kCatchupMintTimeout{ 30000 };

    /** @brief Node READY timeout (covers genesis + processor sync). */
    static inline constexpr std::chrono::milliseconds kNodeReadyTimeout{ 60000 };

    /** @brief Poll interval when waiting for node_main to leave INITIALIZING before priming the URL map. */
    static inline constexpr std::chrono::milliseconds kValidatorReadyPollInterval{ 50 };

    /** @brief Anvil deterministic account private keys (hex, no 0x prefix) — public test values.
     *         Each index gets a distinct key so every node occupies a separate validator slot
     *         and PubSub peer id (mirrors bridge_anvil_e2e_test.cpp). */
    static inline constexpr const char *kAnvilAccountHexKeys[] = {
        "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80",  // Account #0
        "59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d",  // Account #1
        "5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a",  // Account #2
    };

    /**
     * @brief Starts Anvil, funds account #0, seeds pre-node burns, and bootstraps the 3-node cluster.
     */
    static void SetUpTestSuite();

    /**
     * @brief Tears down the cluster, stops Anvil, and removes per-node data directories.
     */
    static void TearDownTestSuite();

    /**
     * @brief Writes kBridgeChainsConfigContent into the given BaseWritePath directory.
     *
     * Creates the directory if missing. The file is read by ResolveBridgeChainsConfigPath()
     * at priority 1 (BaseWritePath/bridge_chains_config.json) and drives catchup_chains_
     * population via OnRpcEndpointsReady.
     *
     * @param[in] base_write_path  Per-node BaseWritePath (trailing slash expected).
     */
    static void WriteBridgeChainsConfig( const std::string &base_write_path );

    /**
     * @brief Sends one real GNUS bridgeOut() burn to the local Anvil fork.
     *
     * Uses sgns::test::anvil::SendBridgeOutBurn, which exercises the production
     * burn path consumed by the catch-up scan (emits BridgeOutInitiated, the
     * only topic0 PerformStartupCatchupScan filters for besides BridgeSourceBurned).
     * The destination is node_main's SGNS address — node_main MUST be created
     * before this helper is called.
     *
     * @return Parsed 0x-prefixed transaction hash, or empty string on failure.
     */
    static std::string SendOneAnvilBurn();
};

std::shared_ptr<GeniusNode>     BridgeAnvilCatchupE2ETest::node_main  = nullptr;
std::shared_ptr<GeniusNode>     BridgeAnvilCatchupE2ETest::node_proc1 = nullptr;
std::shared_ptr<GeniusNode>     BridgeAnvilCatchupE2ETest::node_proc2 = nullptr;
sgns::test::anvil::AnvilProcess BridgeAnvilCatchupE2ETest::s_anvil;
std::vector<std::string>        BridgeAnvilCatchupE2ETest::s_pre_node_burn_hashes;

std::array<GeniusNodeConfig, BridgeAnvilCatchupE2ETest::kNodeCount> BridgeAnvilCatchupE2ETest::s_configs = { {
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./catchup_node1", {} },
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./catchup_node2", {} },
    { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./catchup_node3", {} },
} };

void BridgeAnvilCatchupE2ETest::WriteBridgeChainsConfig( const std::string &base_write_path )
{
    std::filesystem::create_directories( base_write_path );
    const std::string config_path = base_write_path + kBridgeChainsConfigFilename;
    std::ofstream     out( config_path, std::ios::binary | std::ios::trunc );
    out << kBridgeChainsConfigContent;
    out.close();
    spdlog::info( "catchup_e2e: wrote {}", config_path );
}

std::string BridgeAnvilCatchupE2ETest::SendOneAnvilBurn()
{
    // node_main is the destination; it is created in SetUpTestSuite before this
    // helper is called. SendBridgeOutBurn exercises the production bridgeOut()
    // path, emitting BridgeOutInitiated — the topic0 PerformStartupCatchupScan
    // filters eth_getLogs by. The prior safeTransferFrom(self,self,0,amount,0x)
    // form emitted TransferSingle, which never matched the scan's EventFilter
    // and left the central balance assertion unsatisfiable (see CR-01).
    if ( !node_main )
    {
        spdlog::error( "catchup_e2e: SendOneAnvilBurn called before node_main was created" );
        return {};
    }
    return sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), node_main->GetAddress() );
}

void BridgeAnvilCatchupE2ETest::SetUpTestSuite()
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
    spdlog::info( "catchup_e2e: fork_url={}", fork_url );

    // D-01: start Anvil subprocess forking Sepolia.
    ASSERT_TRUE( s_anvil.Start( fork_url ) ) << "Failed to start anvil subprocess";

    // D-04: poll Anvil readiness via cast block-number.
    ASSERT_TRUE( s_anvil.WaitForReady() ) << "Anvil did not become ready";

    // D-08/D-09: fund account #0 with GNUS via impersonation. D-10: skip cleanly on failure.
    if ( !sgns::test::anvil::FundAccount0WithGnus( s_anvil.RpcUrl() ) )
    {
        s_anvil.Stop();
        GTEST_SKIP() << "Could not fund Anvil account #0 via impersonation of "
                     << sgns::test::anvil::kGnusHolderSepolia << " — skipping";
    }

    // Per-node BaseWritePath from binary location (Plan 04.1-01 pattern), distinct subdirs.
    const std::string binary_path   = boost::dll::program_location().parent_path().string();
    s_configs[0].BaseWritePath = binary_path + kNode1Dir;
    s_configs[1].BaseWritePath = binary_path + kNode2Dir;
    s_configs[2].BaseWritePath = binary_path + kNode3Dir;

    // Write per-node bridge_chains_config.json so ResolveBridgeChainsConfigPath() finds it at
    // priority 1 and OnRpcEndpointsReady populates catchup_chains_.
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        WriteBridgeChainsConfig( s_configs[i].BaseWritePath );
    }

    // Inject a chainlist fetcher returning only the Anvil RPC endpoint, so the
    // catch-up scan queries the local fork instead of chainid.network.
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

    // Create node_main FIRST so its SGNS address is available as the bridgeOut()
    // destination for the pre-node burn seeding below. The catch-up scan only
    // fires after the full cluster reaches READY (the wait-for-condition at the
    // end of SetUpTestSuite), so node_main will not advance past INITIALIZING
    // before the burns are seeded — the seeded burns are still historical by
    // scan time.
    spdlog::info( "catchup_e2e: creating node_main (full node, port {})", kNodeMainPort );
    sgns::GeniusNode::WriteNetworkConfig( s_configs[0].BaseWritePath, kNodeMainPort, /*auto_dht=*/true );
    sgns::GeniusNode::WriteSgnsConfig( s_configs[0].BaseWritePath, "Full", /*is_processor=*/false );
    node_main = GeniusNode::New( s_configs[0], sgns::FromPrivateKey{ kAnvilAccountHexKeys[0] } );

    // PRE-NODE BURN SEEDING (the heart of this test): send kNumCatchupBurns real
    // bridgeOut() burns to the local Anvil fork targeting node_main's SGNS
    // address, so the burns are historical by the time the catch-up scan runs
    // and emit the BridgeOutInitiated topic0 that PerformStartupCatchupScan
    // filters eth_getLogs by.
    spdlog::info( "catchup_e2e: seeding {} pre-node burns against local Anvil", kNumCatchupBurns );
    for ( unsigned int i = 0u; i < kNumCatchupBurns; ++i )
    {
        const std::string tx_hash = SendOneAnvilBurn();
        ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed pre-node burn #" << i;
        s_pre_node_burn_hashes.push_back( tx_hash );
        spdlog::info( "catchup_e2e: pre-node burn #{} tx_hash={}", i, tx_hash );
    }
    ASSERT_EQ( s_pre_node_burn_hashes.size(), kNumCatchupBurns )
        << "Did not seed the expected number of pre-node burns";

    // Create the two processor nodes so ValidatorRegistry syncs via CRDT between peers.
    // The pre-node burns persist on Anvil's fork state until Stop().
    sgns::GeniusNode::WriteNetworkConfig( s_configs[1].BaseWritePath, kNodeProc1Port, /*auto_dht=*/true );
    sgns::GeniusNode::WriteSgnsConfig( s_configs[1].BaseWritePath, "Light", /*is_processor=*/false );
    node_proc1 = GeniusNode::New( s_configs[1], sgns::FromPrivateKey{ kAnvilAccountHexKeys[1] } );

    sgns::GeniusNode::WriteNetworkConfig( s_configs[2].BaseWritePath, kNodeProc2Port, /*auto_dht=*/true );
    sgns::GeniusNode::WriteSgnsConfig( s_configs[2].BaseWritePath, "Light", /*is_processor=*/false );
    node_proc2 = GeniusNode::New( s_configs[2], sgns::FromPrivateKey{ kAnvilAccountHexKeys[2] } );

    // Prime the validator URL map with the local Anvil endpoint BEFORE the catch-up
    // scan fires at READY, so PerformStartupCatchupScan queries eth_getLogs against
    // http://127.0.0.1:18545 instead of real Sepolia.
    {
        sgns::WeightedRpcEndpoint ep;
        ep.url                     = s_anvil.RpcUrl();
        ep.consensus_weight        = 100;
        ep.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
        ep.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };
        std::vector<sgns::WeightedRpcEndpoint> anvil_eps{ ep };
        node_main->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps );
        spdlog::info( "catchup_e2e: primed validator with {} Anvil RPC endpoint at {}",
                      anvil_eps.size(),
                      s_anvil.RpcUrl() );
    }

    sgns::Blockchain::SetAuthorizedFullNodeAddress( node_main->GetAddress() );
    spdlog::info( "catchup_e2e: authorized full node address = {}", node_main->GetAddress().substr( 0, 16 ) );

    // Bootstrap PubSub mesh so ValidatorRegistry synces via CRDT — all three nodes
    // share the genesis registry, avoiding the single-node self-bootstrap timeout.
    node_proc1->GetPubSub()->AddPeers(
        { node_main->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    // Wait for all three nodes to reach READY. After READY the auto-scan fires with
    // a primed URL map, so eth_getLogs hits the local Anvil.
    ASSERT_WAIT_FOR_CONDITION(
        [&]()
        {
            return node_main->GetState()  == GeniusNode::NodeState::READY &&
                   node_proc1->GetState() == GeniusNode::NodeState::READY &&
                   node_proc2->GetState() == GeniusNode::NodeState::READY;
        },
        kNodeReadyTimeout,
        "3-node cluster READY",
        nullptr );

    spdlog::info( "catchup_e2e: 3-node cluster ready; auto-mint path armed" );
}

void BridgeAnvilCatchupE2ETest::TearDownTestSuite()
{
    spdlog::info( "catchup_e2e: tearing down nodes" );
    node_main.reset();
    node_proc1.reset();
    node_proc2.reset();
    s_anvil.Stop();
    std::error_code ec;
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        std::filesystem::remove_all( s_configs[i].BaseWritePath, ec );
    }
}

/**
 * @brief Positive auto-mint verification: catch-up scan discovers and mints all pre-node burns.
 *
 * This test makes ZERO manual MintTokens() calls. The only path by which the
 * recipient balance can increase is GeniusNode::PerformStartupCatchupScan ->
 * MintFunds(). The pre-node burns were seeded in SetUpTestSuite; the auto-scan
 * fires after node_main reaches READY and queries eth_getLogs against the local
 * Anvil (URL resolved via validator.GetFirstRpcUrl). We assert the recipient's
 * balance increases by >= (kNumCatchupBurns * kMintAmount) within the
 * catch-up mint timeout.
 */
TEST_F( BridgeAnvilCatchupE2ETest, StartupCatchupScanAutoMintsHistoricalBurns )
{
    const std::string dest_addr       = node_main->GetAddress();
    const uint64_t    initial_balance = node_main->GetBalance( dest_addr );
    spdlog::info( "catchup_e2e: dest={} initial_balance={}", dest_addr.substr( 0, 16 ), initial_balance );

    // Sanity log: list the pre-node burn hashes so a human running the test can
    // correlate with cast logs from the local Anvil fork.
    for ( unsigned int i = 0u; i < s_pre_node_burn_hashes.size(); ++i )
    {
        spdlog::info( "catchup_e2e: expected auto-mint source #{} = {}", i, s_pre_node_burn_hashes[i] );
    }

    // The single assertion that proves the catch-up scan ran, queried eth_getLogs
    // against Anvil, found all kNumCatchupBurns historical burns, and called
    // MintFunds() for each. The auto-mint path is the ONLY way the balance can
    // increase in this test.
    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return node_main->GetBalance( dest_addr ) >= initial_balance + ( kNumCatchupBurns * kMintAmount ); },
        kCatchupMintTimeout,
        "Catch-up scan auto-minted all pre-node burns",
        nullptr );

    const uint64_t final_balance = node_main->GetBalance( dest_addr );
    const uint64_t delta         = final_balance - initial_balance;
    spdlog::info( "catchup_e2e: balance {} -> {} (delta {}, expected >= {})",
                  initial_balance,
                  final_balance,
                  delta,
                  kNumCatchupBurns * kMintAmount );
    EXPECT_GE( delta, kNumCatchupBurns * kMintAmount )
        << "Catch-up scan failed to auto-mint all " << kNumCatchupBurns << " historical burns";

    spdlog::info( "catchup_e2e: StartupCatchupScanAutoMintsHistoricalBurns test complete" );
}
