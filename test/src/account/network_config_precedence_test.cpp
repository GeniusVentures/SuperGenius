#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <gtest/gtest.h>

using namespace sgns;

namespace
{
    // Same private key across every scene -> same account address ->
    // GenerateRandomPort(seed, address) is deterministic. The only variable between scenes
    // is the config file (the canonical New(dev_config, AccountSource) factory has no
    // autodht/port_seed params — those come only from network_config.json).
    constexpr const char *TEST_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    GeniusNodeConfig MakeDevConfig( const boost::filesystem::path &base )
    {
        return { "0xcafe", 0.35, "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base.generic_string() + '/' };
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

    // In-memory secure storage (no file/platform keychain access) — must be set before any
    // GeniusNode construction so account creation uses the test backend.
    void UseMemorySecureStorage()
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    void WaitForReady( const std::shared_ptr<GeniusNode> &node )
    {
        // New() starts database initialization asynchronously. Let it finish before
        // releasing the last node reference at the end of these short config tests.
        test::assertWaitForCondition( [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::seconds( 50 ),
                                      "network-config test node did not finish initialization" );
    }
} // namespace

// Scene A (reframed in Phase 3): the canonical New(dev_config, AccountSource) factory has no
// autodht param — auto_dht comes only from network_config.json. Writing "auto_dht": false and
// constructing via New proves the resolved autodht_ is config-driven (false).
TEST( NetworkConfigPrecedence, AutoDhtConfigDriven )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncp_autodht" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    // config "auto_dht": false drives the resolved value.
    EXPECT_FALSE( node->IsAutodhtEnabled() );
    ASSERT_NO_FATAL_FAILURE( WaitForReady( node ) );
}

// Scene B (reframed in Phase 3): port_seed comes only from network_config.json. pubsubport_ is
// resolved synchronously in InitNetwork; with no "pubsub_port" key, the else-branch runs
// GenerateRandomPort(port_seed, address), which returns a value in [base, base+300]. So:
//   - default port_seed (40001) resolves into [40001, 40301]
//   - config port_seed=20000 resolves into [20000, 20300]
// These ranges do not overlap, so a resolved port in the latter range unambiguously proves
// port_seed is config-driven at 20000. Single construction avoids second-port-bind flakiness.
TEST( NetworkConfigPrecedence, PortSeedConfigDriven )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncp_port_seed" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/20000, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    const auto resolved = node->GetPubsubPort();
    EXPECT_GE( resolved, 20000u );
    EXPECT_LE( resolved, 20300u );
    ASSERT_NO_FATAL_FAILURE( WaitForReady( node ) );
}

TEST( NetworkConfigPrecedence, ZeroPortSeedUsesOsAssignedPort )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncp_ephemeral_port" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    EXPECT_GT( node->GetPubsubPort(), 0u );
    EXPECT_EQ( node->GetPubSub()->GetInterfaceAddress().find( "/tcp/0/" ), std::string::npos );
    ASSERT_NO_FATAL_FAILURE( WaitForReady( node ) );
}

namespace
{
    // WriteNetworkConfig only emits port_seed/auto_dht/upnp_enabled, so a scene needing any other
    // key must hand-roll the JSON. Same approach as genius_node_bootstrap_reconnect_test.
    void WriteNetworkConfigWithMultiplier( const std::string &base_path, const std::string &multiplier_literal )
    {
        std::ofstream config( base_path + "network_config.json" );
        ASSERT_TRUE( config.good() );
        config << R"({ "port_seed": 0, "auto_dht": false, "upnp_enabled": false, )"
               << R"("bootstrap_background_multiplier": )" << multiplier_literal << " }";
    }

    double MultiplierAfterLoad( const std::string &dir, const std::string &multiplier_literal )
    {
        UseMemorySecureStorage();
        auto       base       = MakeTempDir( dir );
        const auto dev_config = MakeDevConfig( base );
        WriteNetworkConfigWithMultiplier( dev_config.BaseWritePath, multiplier_literal );
        sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                           /*node_type=*/"Full",
                                           /*is_processor=*/false,
                                           /*rpc_catchup=*/false );

        auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
        EXPECT_NE( node, nullptr );
        if ( !node )
        {
            return -1.0;
        }
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        const double value = sgns::GeniusNodeTestAccess::BootstrapBackgroundMultiplier( node );
        WaitForReady( node );
        return value;
    }
} // namespace

// "bootstrap_background_multiplier" is the only double-valued key in network_config.json, so it is
// the sole exercise of the read() lambda's IsNumber()/GetDouble() branch.
//
// NOTE: the key is currently dormant. Commit 0e1cfb069 removed background_mode_ and both places
// that applied the multiplier, leaving the field and the config read in place ("may want it back
// later"). So this asserts the resolved value, not any runtime effect — do not go looking for
// behavior that no longer exists.
//
// The INTEGER case is the load-bearing one: Is<double> maps to rapidjson's IsDouble(), which
// rejects an integer-valued literal, so "7" would be silently dropped. A fractional literal would
// pass either way and would not guard the fix.
TEST( NetworkConfigPrecedence, BackgroundMultiplierAcceptsAnyJsonNumber )
{
    EXPECT_DOUBLE_EQ( MultiplierAfterLoad( "ncp_multiplier_int", "7" ), 7.0 )
        << "integer-valued JSON number must be accepted (IsNumber, not IsDouble)";
}

TEST( NetworkConfigPrecedence, BackgroundMultiplierAcceptsFractional )
{
    EXPECT_DOUBLE_EQ( MultiplierAfterLoad( "ncp_multiplier_frac", "2.5" ), 2.5 );
}

// An ill-typed value must leave the in-class default (3.0) untouched rather than coercing.
TEST( NetworkConfigPrecedence, BackgroundMultiplierIgnoresIllTypedValue )
{
    EXPECT_DOUBLE_EQ( MultiplierAfterLoad( "ncp_multiplier_string", R"("3")" ), 3.0 );
}
