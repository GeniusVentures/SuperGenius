/**
 * @file       bridge_race_fixture.hpp
 * @brief      Phase 8 11-node (1 Full + 10 Light) mint-race e2e fixture, reused by all
 *             bridge_race test binaries.
 * @date       2026-07-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * SetUpTestSuite bootstraps an 11-node cluster (index 0 = Full node, indices 1-10 =
 * Light nodes) against a local Anvil fork of Sepolia, exactly like
 * BridgeAnvilCatchupE2ETest but WITHOUT calling ConfigureRpcEndpoint — that call is the
 * race-window trigger and is deliberately left to each TEST_F body (D-03: seed burns
 * before ConfigureRpcEndpoint, then release all 11 nodes' RPC endpoints together with no
 * per-node waits in between).
 *
 * Node identity keys are derived PROGRAMMATICALLY at runtime via DeriveNodeKey() (D-06)
 * rather than a hardcoded key literal array — only Anvil account #0 (the burn sender)
 * needs on-chain funding; the 11 node identities are libp2p/consensus keys and need none.
 */
#ifndef SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP
#define SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include <ProofSystem/EthereumKeyGenerator.hpp>
#include "account/ChainContractPair.hpp"
#include "crypto/hasher.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include "testutil/wait_condition.hpp"

#include "../bridge_e2e/anvil_fixture.hpp"

using sgns::GeniusNode;

/**
 * @brief Reusable 11-node (1 Full + 10 Light) fixture for the mint-race e2e suite.
 *
 * Derived test files add TEST_F bodies that seed burn(s) BEFORE calling
 * ConfigureRpcEndpoint on all 11 nodes back-to-back (no waits in between), proving the
 * watcher-driven exactly-once mint invariant holds under genuine concurrent discovery.
 */
class BridgeRaceE2ETest : public ::testing::Test
{
protected:
    /** @brief Number of GeniusNode instances in the cluster (index 0 = Full node). */
    static constexpr unsigned int kNodeCount = 11u;

    /** @brief Developer payout address (DevConfig::Addr) shared by all race-test nodes. */
    static constexpr std::string_view kDevPayoutAddr = "0xcafe";

    /** @brief Developer cut fraction (DevConfig::Cut) shared by all race-test nodes. */
    static constexpr std::string_view kDevCutFraction = "0.65";

    /** @brief Child-token conversion rate in GNUS (DevConfig::TokenValueInGNUS) shared by all race-test nodes. */
    static constexpr std::string_view kDevTokenValue = "1.0";

    /** @brief Base mint amount per burn (base units). */
    static constexpr unsigned int kMintAmount = 1u;

    /** @brief PubSub port base for the race fixture (next free block after catchup suite's 40031-40033). */
    static constexpr unsigned int kNodePortBase = 40041u;

    /**
     * @brief Node READY timeout (D-15/Pitfall 2 — 11-node startup cost exceeds the
     *        3-node catchup suite's 60000ms; bump initial budget, adjust upward if
     *        measured runs exceed it).
     */
    static constexpr std::chrono::milliseconds kRaceNodeReadyTimeout{ 90000 };

    /**
     * @brief Stability window a test waits after observing the expected mint, to catch
     *        a delayed double-mint on the watcher's next poll cycle (> production poll
     *        interval).
     */
    static constexpr std::chrono::milliseconds kRaceStabilityWindow{ 16000 };

    /**
     * @brief Per-node config — index 0 is the Full node, indices 1-10 are Light nodes.
     *
     * Declared `inline static` (C++17) so this header-only fixture stays ODR-safe when
     * `#include`d by multiple bridge_race test binaries — no out-of-class definition
     * needed/permitted.
     */
    static inline std::array<GeniusNodeConfig, kNodeCount> s_configs{};

    /** @brief All 11 node instances (index 0 = Full node, 1-10 = Light nodes). */
    static inline std::array<std::shared_ptr<GeniusNode>, kNodeCount> s_nodes{};

    static inline sgns::test::anvil::AnvilProcess s_anvil{};

    /** @brief Anvil block captured after funding and before any test burn is submitted. */
    static inline uint64_t s_pre_burn_block = 0ull;

    /** @brief Per-node bridge config filename (must match ResolveBridgeChainsConfigPath priority 1). */
    static constexpr std::string_view kBridgeChainsConfigFilename = "bridge_chains_config.json";

    /** @brief Content of the per-node bridge_chains_config.json (sepolia-only subset). */
    static constexpr std::string_view kBridgeChainsConfigTemplate = R"JSON({
    "ethereum-sepolia": {
        "chain_id": 11155111,
        "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70",
        "creation_block": __CREATION_BLOCK__
    }
}
)JSON";

    /**
     * @brief Writes a per-node bridge_chains_config.json pointing BridgeCatchupWatcher at
     *        the local Anvil fork (mirrors BridgeAnvilCatchupE2ETest::WriteBridgeChainsConfig).
     *
     * Without this file, a node's BridgeCatchupWatcher/BridgeRelayer fall back to their
     * default production chain list (real mainnet/Sepolia/etc. RPC endpoints) — a burn
     * seeded only on the local Anvil fork is then never observed by ANY node, and every
     * race test times out waiting for a mint that never starts. This is the actual
     * watcher-discovery wiring D-01 requires; ConfigureRpcEndpoint() alone only affects
     * the separate PublicChainInputValidator verification-quorum path.
     *
     * @param[in] base_write_path  Per-node BaseWritePath (trailing slash expected).
     */
    static void WriteBridgeChainsConfig( const std::string &base_write_path )
    {
        std::filesystem::create_directories( base_write_path );
        const std::string config_path = base_write_path + std::string( kBridgeChainsConfigFilename );

        // Race tests must expose only burns created by the current test run. The
        // scan floor is inclusive, so start one block after the pre-burn baseline.
        // Unlike the dedicated catch-up suite, this must not import history.
        const uint64_t creation_block = s_pre_burn_block + 1ull;

        std::string        config_json( kBridgeChainsConfigTemplate );
        const std::string  placeholder( "__CREATION_BLOCK__" );
        const auto         pos = config_json.find( placeholder );
        if ( pos != std::string::npos )
        {
            config_json.replace( pos, placeholder.size(), std::to_string( creation_block ) );
        }

        std::ofstream out( config_path, std::ios::binary | std::ios::trunc );
        out << config_json;
        out.close();
        spdlog::info( "bridge_race: wrote {} (creation_block={})", config_path, creation_block );
    }

    /**
     * @brief Deterministically derives a valid secp256k1 hex private key (no 0x prefix,
     *        64 hex chars) for the given node index (D-06 — programmatic, not a
     *        hardcoded literal array).
     *
     * Derivation: SHA-256("bridge_race_node_key_seed" || index), re-hashed with a
     * counter suffix on the (astronomically unlikely) all-zero digest. Deterministic:
     * the same index always yields the same key within and across runs; distinct
     * indices yield distinct keys.
     *
     * @param[in] index  Node index (0 = Full node, 1-10 = Light nodes).
     * @return 64-hex-char private key string (no 0x prefix).
     */
    static std::string DeriveNodeKey( unsigned int index )
    {
        static constexpr const char *kSeed = "bridge_race_node_key_seed";

        for ( unsigned int attempt = 0u; attempt < 8u; ++attempt )
        {
            const std::string input = std::string( kSeed ) + std::to_string( index ) + "#" + std::to_string( attempt );
            const sgns::base::Hash256 digest = sgns::crypto::sha2_256( input.data(), input.size() );

            bool all_zero = true;
            for ( unsigned char byte : digest )
            {
                if ( byte != 0u )
                {
                    all_zero = false;
                    break;
                }
            }
            if ( all_zero )
            {
                continue; // re-hash with a different attempt suffix (never observed in practice)
            }

            std::string hex;
            hex.reserve( digest.size() * 2u );
            static constexpr char kHexDigits[] = "0123456789abcdef";
            for ( unsigned char byte : digest )
            {
                hex.push_back( kHexDigits[( byte >> 4u ) & 0x0Fu] );
                hex.push_back( kHexDigits[byte & 0x0Fu] );
            }
            return hex;
        }
        // Unreachable in practice — SHA-256 producing an all-zero digest 8 times running
        // is not something any test run will encounter.
        return std::string( sgns::base::Hash256::size() * 2u, '1' );
    }

    /**
     * @brief Returns the SGNS destination address for a Light-node index (D-07 — burns
     *        must target Light-node addresses, not the Full node's own address).
     *
     * Must read the LIVE node's GetAddress() — a node's true SGNS address is NOT simply
     * EthereumKeyGenerator(private_key). GeniusAccount::GenerateGeniusAddress() signs a
     * predefined constant with the raw key and SHA-256s the signature to derive the
     * actual key seed, so the address can only be obtained from a constructed GeniusNode.
     *
     * @param[in] light_index  Light-node index (1-10).
     * @return SGNS destination string for that Light node's identity.
     */
    static std::string DeriveLightDestination( unsigned int light_index )
    {
        return s_nodes[light_index]->GetAddress();
    }

    /**
     * @brief Starts Anvil, funds account #0, and bootstraps the 11-node cluster.
     *
     * Deliberately does NOT call ConfigureRpcEndpoint — that call is the race-window
     * trigger and belongs in each TEST_F body (D-03).
     */
    static void SetUpTestSuite()
    {
        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
            { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

        // D-16: skip cleanly when Foundry binaries are missing.
        if ( !sgns::test::anvil::AnvilAvailable() || !sgns::test::anvil::CastAvailable() )
        {
            GTEST_SKIP() << "Install Foundry (anvil + cast): https://book.getfoundry.sh/getting-started/installation";
        }

        const std::string fork_url = sgns::test::anvil::SepoliaForkUrl();
        spdlog::info( "bridge_race: fork_url={}", fork_url );

        ASSERT_TRUE( s_anvil.Start( fork_url ) ) << "Failed to start anvil subprocess";
        ASSERT_TRUE( s_anvil.WaitForReady() ) << "Anvil did not become ready";

        if ( !sgns::test::anvil::FundAccount0WithGnus( s_anvil.RpcUrl() ) )
        {
            s_anvil.Stop();
            GTEST_SKIP() << "Could not fund Anvil account #0 via impersonation of "
                         << sgns::test::anvil::kGnusHolderSepolia << " — skipping";
        }

        // Capture the clean pre-burn baseline. Race-test watchers start here so they
        // see burns mined after setup without importing historical fork events.
        {
            int               exit_code      = 0;
            const std::string pre_burn_block_str = sgns::test::anvil::RunShellCapture(
                "cast block-number --rpc-url " + s_anvil.RpcUrl(), exit_code );
            ASSERT_EQ( exit_code, 0 ) << "Could not query Anvil fork block via cast block-number";
            ASSERT_FALSE( pre_burn_block_str.empty() ) << "cast block-number returned empty output";
            s_pre_burn_block = std::stoull( pre_burn_block_str );
            ASSERT_GT( s_pre_burn_block, 0ull ) << "Anvil pre-burn block must be non-zero";
            spdlog::info( "bridge_race: pre-burn baseline block = {}", s_pre_burn_block );
        }

        const std::string binary_path = boost::dll::program_location().parent_path().string();

        const std::string kAnvilRpcUrl = s_anvil.RpcUrl();
        auto chainlist_fetcher = [kAnvilRpcUrl]() -> std::optional<std::string>
        {
            return std::string( R"([{"name":"ethereum-sepolia","chainId":11155111,"rpc":[")" ) +
                   kAnvilRpcUrl + R"("],"status":"active"}])";
        };

        // Bootstrap order: create the 10 Light nodes FIRST and register their REAL
        // addresses via SetAdditionalGenesisValidatorAddresses, THEN create the Full node
        // LAST and register its REAL address via SetAuthorizedFullNodeAddress immediately
        // afterward (no other node creation in between).
        //
        // Why: a node's true SGNS address is NOT simply EthereumKeyGenerator(private_key)
        // — GeniusAccount::GenerateGeniusAddress() signs a predefined constant with the
        // raw key and SHA-256s the signature to derive the actual key seed. Addresses can
        // only be obtained from a LIVE GeniusNode's GetAddress(), not precomputed from the
        // key alone. And Blockchain::New() — which bakes GetAuthorizedFullNodeAddress()/
        // GetAdditionalGenesisValidatorAddresses() into the ValidatorRegistry's
        // genesis_authority and additional-validator list — runs ASYNCHRONOUSLY per node
        // (via the INITIALIZING_BLOCKCHAIN state transition, after DB migration), so
        // registering addresses only after creating ALL 11 nodes races every node's async
        // blockchain init against the registration call. This ordering removes the race
        // for both statics: the light-address list is fully known and registered before
        // the Full node (the only node that ever writes the genesis registry) is even
        // constructed, and the Full node's own registration follows its creation with no
        // intervening node-creation work — the same "single node, immediate registration"
        // timing already proven safe by the existing 3-node bridge_anvil_e2e fixture.
        // Proactively remove any stale per-node data directory left over from a PRIOR run
        // that didn't exit cleanly (e.g. a crash/SEGFAULT skips TearDownTestSuite's own
        // remove_all entirely). Without this, a fresh run can start against stale
        // RocksDB/CRDT state from an old process, producing confusing, nondeterministic
        // behavior that has nothing to do with the current run's actual logic.
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            const std::string stale_path = binary_path + "/bridge_race_node" + std::to_string( i + 1u ) + "/";
            std::error_code   ec;
            std::filesystem::remove_all( stale_path, ec );
        }

        auto write_node_config = [&]( unsigned int i, const char *node_type )
        {
            const std::string base_write_path = binary_path + "/bridge_race_node" + std::to_string( i + 1u ) + "/";
            s_configs[i].Addr             = kDevPayoutAddr;
            s_configs[i].Cut              = kDevCutFraction;
            s_configs[i].TokenValueInGNUS = kDevTokenValue;
            s_configs[i].TokenID          = sgns::TokenID::FromBytes( { 0x00 } );
            s_configs[i].BaseWritePath    = base_write_path;

            const unsigned int port = kNodePortBase + i;
            sgns::GeniusNode::WriteNetworkConfig( base_write_path, static_cast<uint16_t>( port ), /*auto_dht=*/true );
            sgns::GeniusNode::WriteSgnsConfig( base_write_path, node_type, /*is_processor=*/false );
            WriteBridgeChainsConfig( base_write_path );
        };

        std::vector<std::string> light_addresses;
        light_addresses.reserve( kNodeCount - 1u );
        for ( unsigned int i = 1u; i < kNodeCount; ++i )
        {
            write_node_config( i, "Light" );
            s_nodes[i] = GeniusNode::New( s_configs[i], sgns::FromPrivateKey{ DeriveNodeKey( i ) } );
            ASSERT_NE( s_nodes[i], nullptr ) << "Failed to create node index " << i;
            s_nodes[i]->SetChainlistFetcher( chainlist_fetcher );
            light_addresses.push_back( s_nodes[i]->GetAddress() );
        }

        sgns::Blockchain::SetAdditionalGenesisValidatorAddresses( light_addresses );
        spdlog::info( "bridge_race: registered {} additional genesis validators", light_addresses.size() );

        write_node_config( 0u, "Full" );
        s_nodes[0] = GeniusNode::New( s_configs[0], sgns::FromPrivateKey{ DeriveNodeKey( 0u ) } );
        ASSERT_NE( s_nodes[0], nullptr ) << "Failed to create Full node";
        s_nodes[0]->SetChainlistFetcher( chainlist_fetcher );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( s_nodes[0]->GetAddress() );
        spdlog::info( "bridge_race: authorized full node = {}", s_nodes[0]->GetAddress().substr( 0, 16 ) );

        // Star-topology PubSub mesh bootstrap: each Light node peers directly with the
        // Full node (sufficient for CRDT sync; a full 11x11 mesh is unnecessary).
        const std::string full_node_pubsub_addr = s_nodes[0]->GetPubSub()->GetLocalAddress();
        for ( unsigned int i = 1u; i < kNodeCount; ++i )
        {
            s_nodes[i]->GetPubSub()->AddPeers( { full_node_pubsub_addr } );
        }

        // All transaction managers must be READY before a TEST_F body attempts the
        // deliberately back-to-back ConfigureRpcEndpoint calls. The burn is still seeded
        // before endpoint configuration in each test, preserving the D-03 race trigger.
        ASSERT_WAIT_FOR_CONDITION(
            [&]()
            {
                return std::all_of(
                    s_nodes.begin(),
                    s_nodes.end(),
                    []( const std::shared_ptr<GeniusNode> &node )
                    { return node && node->GetState() == GeniusNode::NodeState::READY; } );
            },
            kRaceNodeReadyTimeout,
            "all 11 bridge-race nodes READY",
            nullptr );

        spdlog::info( "bridge_race: all {} nodes READY (RPC endpoints not yet configured)", kNodeCount );
    }

    /**
     * @brief Tears down the cluster, stops Anvil, and removes per-node data directories.
     */
    static void TearDownTestSuite()
    {
        spdlog::info( "bridge_race: tearing down nodes" );
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            s_nodes[i].reset();
        }
        s_anvil.Stop();
        std::error_code ec;
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            std::filesystem::remove_all( s_configs[i].BaseWritePath, ec );
        }
    }
};

#endif // SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP
