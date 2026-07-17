/**
 * @file       bridge_race_fixture.hpp
 * @brief      Phase 8 11-node (1 Full + 10 Light) mint-race e2e fixture, reused by all
 *             bridge_race test binaries.
 * @date       2026-07-16
 * @author     Super Genius (info@gnus.ai)
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

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <openssl/sha.h>
#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include <ProofSystem/EthereumKeyGenerator.hpp>
#include "account/ChainContractPair.hpp"
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
    static inline constexpr unsigned int kNodeCount = 11u;

    /** @brief Developer payout address (DevConfig::Addr) shared by all race-test nodes. */
    static inline constexpr const char *kDevPayoutAddr = "0xcafe";

    /** @brief Developer cut fraction (DevConfig::Cut) shared by all race-test nodes. */
    static inline constexpr const char *kDevCutFraction = "0.65";

    /** @brief Child-token conversion rate in GNUS (DevConfig::TokenValueInGNUS) shared by all race-test nodes. */
    static inline constexpr const char *kDevTokenValue = "1.0";

    /** @brief Base mint amount per burn (base units). */
    static inline constexpr unsigned int kMintAmount = 1u;

    /** @brief PubSub port base for the race fixture (next free block after catchup suite's 40031-40033). */
    static inline constexpr unsigned int kNodePortBase = 40041u;

    /**
     * @brief Node READY timeout (D-15/Pitfall 2 — 11-node startup cost exceeds the
     *        3-node catchup suite's 60000ms; bump initial budget, adjust upward if
     *        measured runs exceed it).
     */
    static inline constexpr std::chrono::milliseconds kRaceNodeReadyTimeout{ 90000 };

    /**
     * @brief Stability window a test waits after observing the expected mint, to catch
     *        a delayed double-mint on the watcher's next poll cycle (> production poll
     *        interval).
     */
    static inline constexpr std::chrono::milliseconds kRaceStabilityWindow{ 16000 };

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
            std::vector<unsigned char> input_bytes( input.begin(), input.end() );
            std::vector<unsigned char> digest( SHA256_DIGEST_LENGTH );
            SHA256( input_bytes.data(), input_bytes.size(), digest.data() );

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
        return std::string( SHA256_DIGEST_LENGTH * 2u, '1' );
    }

    /**
     * @brief Derives the SGNS destination address for a Light-node index (D-07 — burns
     *        must target Light-node addresses, not the Full node's own address).
     * @param[in] light_index  Light-node index (1-10).
     * @return SGNS destination string derived from that Light node's identity key.
     */
    static std::string DeriveLightDestination( unsigned int light_index )
    {
        ethereum::EthereumKeyGenerator key_gen( DeriveNodeKey( light_index ) );
        return key_gen.GetEntirePubValue();
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

        const std::string binary_path = boost::dll::program_location().parent_path().string();

        const std::string kAnvilRpcUrl = s_anvil.RpcUrl();
        auto chainlist_fetcher = [kAnvilRpcUrl]() -> std::optional<std::string>
        {
            return std::string( R"([{"name":"ethereum-sepolia","chainId":11155111,"rpc":[")" ) +
                   kAnvilRpcUrl + R"("],"status":"active"}])";
        };

        // Create all 11 nodes FIRST (node 0 = Full, 1-10 = Light) so genesis validator
        // registration has every address before the ValidatorRegistry initializes.
        std::vector<std::string> light_addresses;
        light_addresses.reserve( kNodeCount - 1u );

        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            const std::string base_write_path = binary_path + "/bridge_race_node" + std::to_string( i + 1u ) + "/";
            s_configs[i].Addr             = kDevPayoutAddr;
            s_configs[i].Cut              = kDevCutFraction;
            s_configs[i].TokenValueInGNUS = kDevTokenValue;
            s_configs[i].TokenID          = sgns::TokenID::FromBytes( { 0x00 } );
            s_configs[i].BaseWritePath    = base_write_path;

            const unsigned int port      = kNodePortBase + i;
            const char        *node_type = ( i == 0u ) ? "Full" : "Light";

            sgns::GeniusNode::WriteNetworkConfig( base_write_path, static_cast<uint16_t>( port ), /*auto_dht=*/true );
            sgns::GeniusNode::WriteSgnsConfig( base_write_path, node_type, /*is_processor=*/false );

            s_nodes[i] = GeniusNode::New( s_configs[i], sgns::FromPrivateKey{ DeriveNodeKey( i ) } );
            ASSERT_NE( s_nodes[i], nullptr ) << "Failed to create node index " << i;
            s_nodes[i]->SetChainlistFetcher( chainlist_fetcher );

            if ( i != 0u )
            {
                light_addresses.push_back( s_nodes[i]->GetAddress() );
            }
        }

        // Register genesis validators IMMEDIATELY after node creation, BEFORE any burn
        // seeding or RPC configuration (matches catchup-suite ordering).
        sgns::Blockchain::SetAuthorizedFullNodeAddress( s_nodes[0]->GetAddress() );
        sgns::Blockchain::SetAdditionalGenesisValidatorAddresses( light_addresses );
        spdlog::info( "bridge_race: authorized full node = {}, +{} additional genesis validators",
                      s_nodes[0]->GetAddress().substr( 0, 16 ),
                      light_addresses.size() );

        // Star-topology PubSub mesh bootstrap: each Light node peers directly with the
        // Full node (sufficient for CRDT sync; a full 11x11 mesh is unnecessary).
        const std::string full_node_pubsub_addr = s_nodes[0]->GetPubSub()->GetLocalAddress();
        for ( unsigned int i = 1u; i < kNodeCount; ++i )
        {
            s_nodes[i]->GetPubSub()->AddPeers( { full_node_pubsub_addr } );
        }

        // Wait for the Full node to reach READY before proceeding. Each TEST_F body will
        // separately wait for the Light nodes after seeding burns and calling
        // ConfigureRpcEndpoint (D-03 — this fixture does not call ConfigureRpcEndpoint).
        ASSERT_WAIT_FOR_CONDITION(
            [&]() { return s_nodes[0]->GetState() == GeniusNode::NodeState::READY; },
            kRaceNodeReadyTimeout,
            "s_nodes[0] (Full node) READY",
            nullptr );

        spdlog::info( "bridge_race: {}-node cluster bootstrapped (RPC endpoints not yet configured)", kNodeCount );
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
