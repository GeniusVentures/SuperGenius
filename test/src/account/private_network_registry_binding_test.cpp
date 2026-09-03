#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "networkregistry/NetworkRegistry.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace sgns;

namespace
{
    // Same private key for every scene -> same account address -> the single-peer
    // trust genesis (this account is the genesis peer and bootstrapper) is known
    // before GeniusNode::New runs.
    constexpr const char *TEST_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    // Valid private_network_id per the Phase-15 encoding decision (0x-hex-32B).
    constexpr const char *VALID_PRIVATE_NETWORK_ID =
        "0x3c978f8d1e2a4b6f9c0d5e7a8b1c3d5f7a9b1c3d5e7f9a1b3c5d7e9f1b3d5a7c";

    // Valid network_key (pnet PSK) in the plain base16 32-byte encoding accepted by
    // Psk::fromBase16String. Intentionally a DIFFERENT value than the id (D-02).
    constexpr const char *VALID_NETWORK_KEY_BASE16 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

    constexpr const char *BOOTSTRAP_PEER_ONE = "QmYyQSo1c1Ym7orWxLYvCrM2EmxFTANf8wXmmE7DWjhx5N";
    constexpr const char *BOOTSTRAP_PEER_TWO = "QmV1b7QdXbxcdRdLhxZsPVDKZsE3TNxrNcv3L4wmydV3Yr";

    const auto TEST_TOKEN_ID = TokenID::FromBytes( { 0x00 } );

    // In-memory secure storage (no file/platform keychain access) - must be set before
    // any GeniusAccount/GeniusNode construction.
    void UseMemorySecureStorage()
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    boost::filesystem::path MakeTempDir( const std::string &name )
    {
        auto path = boost::dll::program_location().parent_path() / name;
        try
        {
            test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
        boost::filesystem::create_directories( path );
        return path;
    }

    // Options for the node-under-test scene.
    struct NodeScene
    {
        bool                         private_network  = false; ///< Provision network_key + private_network_id.
        std::vector<std::string>     bootstrap_peers  = {};    ///< network_bootstrap_peers membership.
    };

    // Creates a genesis-configured node (single-peer self-genesis, thresholds 1/1, the
    // same account_management_test shape) with the scene's private-network identity.
    // The authority account is created FIRST from the same key so its address can be
    // written into sgns_config.json before GeniusNode::New.
    std::shared_ptr<GeniusNode> MakeGenesisNode( const std::string &dir, const NodeScene &scene )
    {
        auto base       = MakeTempDir( dir );
        auto dev_config = GeniusNodeConfig{ "0xcafe", "0.65", "1.0", TEST_TOKEN_ID, base.generic_string() + '/' };

        if ( scene.private_network )
        {
            if ( GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath,
                                                 /*port_seed=*/0,
                                                 /*auto_dht=*/false,
                                                 VALID_NETWORK_KEY_BASE16,
                                                 VALID_PRIVATE_NETWORK_ID,
                                                 scene.bootstrap_peers )
                     .has_error() )
            {
                return nullptr;
            }
        }
        else if ( GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false )
                      .has_error() )
        {
            return nullptr;
        }

        auto authority = GeniusAccount::NewFromPrivateKey( TEST_TOKEN_ID, TEST_PRIVATE_KEY, base );
        if ( !authority )
        {
            return nullptr;
        }
        {
            std::ofstream config( ( base / "sgns_config.json" ).string() );
            if ( !config.good() )
            {
                return nullptr;
            }
            config << "{\"net_id\":144,\"subnet_id\":144,\"node_type\":\"Full\",\"is_processor\":false,"
                   << "\"rpc_catchup\":false,\"trusted_peers\":[\"" << authority->GetAddress()
                   << "\"],\"bootstrapper_node\":\"" << authority->GetAddress()
                   << "\",\"trusted_peer_quorum_threshold\":1,\"burn_config_quorum_threshold\":1}";
        }
        Blockchain::SetAuthorizedFullNodeAddress( authority->GetAddress() );
        return GeniusNode::New( dev_config, FromPrivateKey{ TEST_PRIVATE_KEY } );
    }

    // Waits for the restricted first-boot state, then confirms the single-peer genesis
    // LOCALLY: the node's own approval satisfies the threshold-1 manifest quorum, and
    // the controller's trusted-peer-genesis candidate callback triggers the refresh
    // that activates it. No pubsub peer is needed, so this also works for a pnet-mode
    // node (an outside genesis tool could not pass the PSK boundary).
    void ConfirmLocalSelfGenesis( const std::shared_ptr<GeniusNode> &node )
    {
        ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
            [&] { return node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
            std::chrono::seconds( 50 ),
            "node did not reach the restricted trust-genesis wait state" ) );

        const auto submitted = GeniusNodeTestAccess::ApproveConfiguredTrustGenesis( node );
        ASSERT_TRUE( submitted.has_value() ) << "local genesis self-approval failed: " << submitted.error().message();
    }
} // namespace

// A private-network node (private_network_id + network_key + provisioned bootstrap
// membership) constructs its NetworkRegistry in the INITIALIZING_TRANSACTIONS path
// once the quorum trio is live, and the registry caches the provisioned membership.
TEST( PrivateNetworkRegistryBinding, PrivateNodeConstructsNetworkRegistryFromBootstrapMembership )
{
    UseMemorySecureStorage();
    auto node = MakeGenesisNode( "pnr_binding_private",
                                 { /*private_network=*/true, { BOOTSTRAP_PEER_ONE, BOOTSTRAP_PEER_TWO } } );
    ASSERT_NE( node, nullptr );

    // No registry before the trust genesis is confirmed (the quorum trio's trusted
    // peer registry is not live yet).
    ASSERT_NO_FATAL_FAILURE( ConfirmLocalSelfGenesis( node ) );

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return node->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "private-network node with provisioned membership did not reach READY" ) );

    const auto registry = GeniusNodeTestAccess::NetworkRegistry( node );
    ASSERT_NE( registry, nullptr ) << "private-network node reached READY without a NetworkRegistry";

    const auto membership = registry->GetCurrentPeers();
    ASSERT_EQ( membership.size(), 2u );
    EXPECT_EQ( membership[0], BOOTSTRAP_PEER_ONE );
    EXPECT_EQ( membership[1], BOOTSTRAP_PEER_TWO );

    // D-07 app-layer enforcement (15-12): the node's gossip ingest is
    // membership-filtered from the moment its NetworkRegistry is constructed —
    // cached registry membership authorizes every inbound replicated message.
    EXPECT_TRUE( GeniusNodeTestAccess::BroadcasterMembershipFilterInstalled( node ) )
        << "private node reached READY without the registry-backed gossip membership filter";
}

// A public node (no private_network_id) constructs NO NetworkRegistry even though its
// trust lifecycle is fully confirmed - the public startup path is unchanged.
TEST( PrivateNetworkRegistryBinding, PublicNodeConstructsNoNetworkRegistry )
{
    UseMemorySecureStorage();
    auto node = MakeGenesisNode( "pnr_binding_public", { /*private_network=*/false, {} } );
    ASSERT_NE( node, nullptr );

    ASSERT_NO_FATAL_FAILURE( ConfirmLocalSelfGenesis( node ) );

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return node->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "public node did not reach READY" ) );

    EXPECT_EQ( GeniusNodeTestAccess::NetworkRegistry( node ), nullptr )
        << "public node constructed a NetworkRegistry without a private_network_id";

    // Public path installs NOTHING on the gossip ingest (regression pin): public
    // nodes keep byte-identical pass-through broadcaster behavior.
    EXPECT_FALSE( GeniusNodeTestAccess::BroadcasterMembershipFilterInstalled( node ) )
        << "public node installed a gossip membership filter";
}

// Fail-closed: a private-network node whose bootstrap membership is empty cannot
// establish its membership authority (NetworkRegistry::New rejects the empty set
// below the strict-majority floor). The state transition must FAIL rather than
// continue as if network enforcement were active: the node leaves the trust wait
// (genesis itself is confirmed - the local approval above succeeded) but stays in
// INITIALIZING_TRANSACTIONS, never reaches READY, and no registry exists.
TEST( PrivateNetworkRegistryBinding, PrivateNodeWithoutBootstrapMembershipFailsClosed )
{
    UseMemorySecureStorage();
    auto node = MakeGenesisNode( "pnr_binding_empty_membership", { /*private_network=*/true, {} } );
    ASSERT_NE( node, nullptr );

    ASSERT_NO_FATAL_FAILURE( ConfirmLocalSelfGenesis( node ) );

    // The trust-ready transition fires and INITIALIZING_TRANSACTIONS then fails inside
    // the NetworkRegistry construction. Poll a full window in which a healthy node
    // would have reached READY (the provisioned scene above does): READY must never
    // appear, and the node must end stalled in INITIALIZING_TRANSACTIONS (proving the
    // genesis itself confirmed and the stall is the fail-closed construction).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 15 );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        ASSERT_NE( node->GetState(), GeniusNode::NodeState::READY )
            << "node reached READY despite an unconstructable NetworkRegistry";
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    ASSERT_EQ( node->GetState(), GeniusNode::NodeState::INITIALIZING_TRANSACTIONS )
        << "empty-membership private node should be fail-closed in INITIALIZING_TRANSACTIONS (state was "
        << static_cast<int>( node->GetState() ) << ")";
    EXPECT_EQ( GeniusNodeTestAccess::NetworkRegistry( node ), nullptr )
        << "fail-closed node must not retain a NetworkRegistry";
}

// Teardown clears the broadcaster's membership filter: driven through the REAL
// destruction route (ShutdownForDestruction — PRIVATE at GeniusNode.hpp, hence the
// test-access friend route) and observed on the SAME broadcaster object.
//
// Non-vacuity (why the broadcaster handle is captured BEFORE shutdown):
// GlobalDB::ShutdownNow MOVES m_broadcaster out and Stops it, so GetBroadcaster()
// through the node AFTER shutdown returns null — a post-shutdown
// BroadcasterMembershipFilterInstalled check would pass vacuously (null broadcaster
// -> false). The test-held shared_ptr keeps the same broadcaster object alive;
// Stop() does not touch the membership filter, so HasMembershipFilter() going true
// -> false is attributable solely to the ClearMembershipFilter() call inside
// ShutdownNodePolicyServices (which ShutdownForDestruction reaches via
// ShutdownAccountBoundServices -> ShutdownNodePolicyServices -> tx_globaldb_->ShutdownNow).
//
// Double-shutdown safety: ~GeniusNode (when the node shared_ptr drops at test end)
// calls ShutdownForDestruction again; the shutdown_started_ compare_exchange makes
// that second call a no-op, so explicit-call-then-destroy is safe.
TEST( PrivateNetworkRegistryBinding, TeardownClearsBroadcasterMembershipFilter )
{
    UseMemorySecureStorage();
    auto node = MakeGenesisNode( "pnr_binding_teardown",
                                 { /*private_network=*/true, { BOOTSTRAP_PEER_ONE, BOOTSTRAP_PEER_TWO } } );
    ASSERT_NE( node, nullptr );

    ASSERT_NO_FATAL_FAILURE( ConfirmLocalSelfGenesis( node ) );

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return node->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "private-network node with provisioned membership did not reach READY" ) );

    // The filter is installed while the node is live (D-07 enforcement active).
    ASSERT_TRUE( GeniusNodeTestAccess::BroadcasterMembershipFilterInstalled( node ) )
        << "live private node has no membership filter on its gossip ingest";

    // Capture the broadcaster object BEFORE any shutdown: the held shared_ptr keeps
    // it alive across GlobalDB::ShutdownNow's move-out.
    auto broadcaster = GeniusNodeTestAccess::BroadcasterOf( node );
    ASSERT_NE( broadcaster, nullptr ) << "live node's GlobalDB exposed no broadcaster";
    ASSERT_TRUE( broadcaster->HasMembershipFilter() )
        << "captured broadcaster handle lacks the membership filter installed at construction";

    // Drive the real teardown route while the node shared_ptr is still alive.
    GeniusNodeTestAccess::RequestShutdownForDestruction( node );

    // Assert on the HELD handle — the same object that carried the filter before
    // shutdown; false here means ClearMembershipFilter ran, not that the
    // broadcaster vanished.
    EXPECT_FALSE( broadcaster->HasMembershipFilter() )
        << "teardown left a membership filter installed on the broadcaster (asserted on the "
           "pre-captured handle — not a null-broadcaster pass)";
}
