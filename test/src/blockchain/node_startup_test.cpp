// node_startup_test.cpp
//
// Node startup-timing tests. Verifies that both the genesis (full) node and a
// regular node reach the READY state quickly:
//   1. Genesis creator READY before the account-creation PubSub timeout.
//   2. Regular node READY shortly after the genesis node is ready.
//
// Both tests use bounded wait-conditions (no sleep_for) and report the measured
// latency so the startup budget can be tightened over time.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>

#include <boost/dll.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

namespace
{
    // Prefix for each node's working directory under the test-binary folder.
    constexpr const char *kNodeDirPrefix = "node_startup_";

    // Number of working directories swept clean in SetUp.
    constexpr int kNodeDirCleanupCount = 10;

    // Upper bound on how long a READY poll may wait before failing the test.
    constexpr int kReadyPollTimeoutMs = 30000;

    // 32-byte private key serialized as 64 hex characters.
    constexpr int kPrivateKeyHexLength = 64;

    // Size of the hex digit alphabet (0..15) used for deterministic key generation.
    constexpr int kHexAlphabetSize = 16;
} // namespace

class NodeStartupTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // Inject in-memory secure storage to avoid OS keychain prompts during tests.
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    // Create a node with a deterministic key derived from self_address. PubSub is
    // started synchronously in the constructor; callers wait for READY via a
    // wait-condition, so no fixed sleep is needed (a sleep would also obscure the
    // startup-timing measurements these tests make).
    std::shared_ptr<GeniusNode> CreateNode( const std::string &self_address,
                                            const std::string &dev_addr,
                                            const std::string &tokenValue,
                                            TokenID            tokenId,
                                            bool               isFullNode = false )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        auto        outPath    = binaryPath + "/" + kNodeDirPrefix + std::to_string( id ) + "/";

        GeniusNodeConfig devConfig = { dev_addr, "0.65", tokenValue, tokenId, outPath };

        std::hash<std::string>          hasher;
        size_t                          address_hash = hasher( self_address );
        std::mt19937                    rng( static_cast<uint32_t>( address_hash ) );
        std::uniform_int_distribution<> dist( 0, kHexAlphabetSize - 1 );
        std::string                     key;
        key.reserve( kPrivateKeyHexLength );
        std::generate_n( std::back_inserter( key ),
                         kPrivateKeyHexLength,
                         [&]()
                         {
                             static constexpr std::string_view hexChars = "0123456789abcdef";
                             return hexChars[dist( rng )];
                         } );

        GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath,
                                     /*node_type=*/isFullNode ? "Full" : "Light",
                                     /*is_processor=*/false, /*rpc_catchup=*/false );
        return GeniusNode::New( devConfig, sgns::FromPrivateKey{ key.c_str() } );
    }

    void SetUp() override
    {
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        for ( int i = 0; i < kNodeDirCleanupCount; ++i )
        {
            auto            dir = binaryPath + "/" + kNodeDirPrefix + std::to_string( i ) + "/";
            std::error_code ec;
            sgns::test::removeAllWithRetry( dir, ec );
        }
    }
};

// ---------------------------------------------------------------------------
// 1. GENESIS NODE STARTUP
// ---------------------------------------------------------------------------

// Reproduces the genesis-creator startup stall: the authorized full node creates
// its own account-creation block, so reaching READY must NOT be gated by the
// RequestAccountCreation PubSub timeout (TIMEOUT_ACC_CREATION_BLOCK_MS = 8000ms).
//
// Bug under test: Blockchain::GenesisReceivedCallback issued
// RequestAccountCreation(8000) and, with no peers, waited the full timeout
// before falling back to CreateAccountCreationBlock — adding ~8s to genesis-node
// startup. The budget below is intentionally under the 8s PubSub timeout, so the
// test fails while that timeout gates startup and passes once the genesis creator
// creates its account-creation block directly.
TEST_F( NodeStartupTest, GenesisCreatorReadyBeforeAccountCreationPubsubTimeout )
{
    std::cout << "=== Starting Genesis Creator Ready Before Account-Creation Pubsub Timeout Test ===" << std::endl;

    // Must stay below TIMEOUT_ACC_CREATION_BLOCK_MS (8000ms) to detect the
    // PubSub-timeout stall. Measured genesis-creator READY is ~1.2s (genesis +
    // account-creation logic ~0.2s; remainder is node init: PubSub/DHT/UPnP/DB
    // migration). The < 1s stretch goal is therefore bounded by node init, not
    // by the account-creation path fixed here.
    constexpr int kGenesisCreatorReadyBudgetMs = 7000;

    auto node_full = CreateNode( "full_node_acc_creation_timing",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 true );
    Blockchain::SetAuthorizedFullNodeAddress( node_full->GetAddress() );

    std::chrono::milliseconds ready_elapsed_ms;
    test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis creator never reached READY",
                                  &ready_elapsed_ms );

    std::cout << "Genesis creator reached READY in " << ready_elapsed_ms.count() << "ms" << std::endl;

    ASSERT_LT( ready_elapsed_ms.count(), kGenesisCreatorReadyBudgetMs )
        << "Genesis creator READY took " << ready_elapsed_ms.count()
        << "ms — startup is gated by the RequestAccountCreation PubSub timeout. "
        << "The genesis creator should create its own account-creation block directly "
        << "instead of waiting on a PubSub response that never arrives (no peers).";

    std::cout << "=== Genesis Creator Ready Before Account-Creation Pubsub Timeout Test Completed ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 2. REGULAR NODE STARTUP (after genesis is ready)
// ---------------------------------------------------------------------------

// Verifies a regular (non-full) node reaches READY quickly once the genesis node
// is already up. The genesis node is brought to READY first, then the regular
// node is created, connected, and its READY latency is measured.
TEST_F( NodeStartupTest, RegularNodeReadyQuicklyAfterGenesisReady )
{
    std::cout << "=== Starting Regular Node Ready Quickly After Genesis Ready Test ===" << std::endl;

    // Bring the genesis full node to READY first.
    auto genesisNode = CreateNode( "genesis_node_startup", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ), true );
    Blockchain::SetAuthorizedFullNodeAddress( genesisNode->GetAddress() );

    std::chrono::milliseconds genesis_ready_ms;
    test::assertWaitForCondition( [&]() { return genesisNode->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis node never reached READY",
                                  &genesis_ready_ms );
    std::cout << "Genesis node reached READY in " << genesis_ready_ms.count() << "ms" << std::endl;

    // Create a regular node and connect it to the genesis node.
    auto regularNode = CreateNode( "regular_node_startup", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ), false );
    // GetInterfaceAddress() returns the full multiaddr WITH peer ID (required for
    // GeniusNode::AddPeer to dial). GetLocalAddress() omits the peer ID, so the dial never
    // completes and the regular node can't reach the genesis node — the cause of
    // the 5s nonce-timeout stall. This matches every other multi-node E2E test.
    regularNode->AddPeer( genesisNode->GetPubSub()->GetInterfaceAddress() );

    // Measure the regular node's READY latency (connect -> READY). Generous budget
    // until a baseline is measured; tighten afterward.
    constexpr int kRegularReadyBudgetMs = 10000;

    std::chrono::milliseconds regular_ready_ms;
    test::assertWaitForCondition( [&]() { return regularNode->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "regular node never reached READY after the genesis node was ready",
                                  &regular_ready_ms );
    std::cout << "Regular node reached READY in " << regular_ready_ms.count() << "ms (after genesis was ready)"
              << std::endl;

    ASSERT_LT( regular_ready_ms.count(), kRegularReadyBudgetMs )
        << "Regular node READY took " << regular_ready_ms.count()
        << "ms after the genesis node was ready — startup latency is too high.";

    std::cout << "=== Regular Node Ready Quickly After Genesis Ready Test Completed ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 3. DEFAULT BURN RATE END-TO-END REGRESSION (BURN-03)
// ---------------------------------------------------------------------------

// Exercises the real INITIALIZING_TRANSACTIONS construction path end-to-end
// (SecureCrdt -> TrustedPeerRegistry -> BurnConfig -> TransactionManager, wired
// in Phase 11 Plan 02) via an actual running GeniusNode reaching READY, and
// confirms a freshly-seeded genesis node's default burn rate is unchanged: 1%
// (100 basis points out of 10000), matching pre-milestone behavior.
TEST_F( NodeStartupTest, GenesisNodeDefaultBurnRateIsOnePercent )
{
    std::cout << "=== Starting Genesis Node Default Burn Rate Is One Percent Test ===" << std::endl;

    auto node_full = CreateNode( "full_node_burn_rate_default",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 true );
    Blockchain::SetAuthorizedFullNodeAddress( node_full->GetAddress() );

    test::assertWaitForCondition( [&]() { return node_full->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis node never reached READY" );

    ASSERT_EQ( sgns::GeniusNode::GetBurnBasisPoints(), 100u )
        << "Genesis node's default burn rate must remain 1% (100 basis points) until a "
        << "quorum-signed BurnConfig update changes it (BURN-03 regression).";
    ASSERT_EQ( sgns::GeniusNode::GetBasisPointsTotal(), 10000u );

    std::cout << "=== Genesis Node Default Burn Rate Is One Percent Test Completed ===" << std::endl;
}
