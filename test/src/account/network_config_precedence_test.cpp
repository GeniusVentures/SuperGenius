#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <gtest/gtest.h>

using namespace sgns;

namespace sgns
{
    class NetworkConfigPrecedenceTestAccess
    {
    public:
        static std::chrono::milliseconds VoteSelectionWindow( const std::shared_ptr<GeniusNode> &node )
        {
            return node->consensus_vote_selection_window_;
        }
    };
} // namespace sgns

namespace
{
    // Same private key across every scene -> same account address ->
    // GenerateRandomPort(seed, address) is deterministic. The only variable between scenes
    // is the config file (the canonical New(dev_config, AccountSource) factory has no
    // autodht/port_seed params — those come only from network_config.json).
    constexpr const char *TEST_PRIVATE_KEY =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    GeniusNodeConfig MakeDevConfig( const boost::filesystem::path &base )
    {
        return { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base.generic_string() + '/' };
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
        GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
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

    void WriteRawNetworkConfig( const boost::filesystem::path &base, std::string_view value )
    {
        std::ofstream stream( ( base / "network_config.json" ).string(), std::ios::trunc );
        ASSERT_TRUE( stream.good() );
        stream << value;
        ASSERT_TRUE( stream.good() );
    }

    std::chrono::milliseconds ResolveVoteWindow( const std::string &name, std::string_view json )
    {
        UseMemorySecureStorage();
        auto base = MakeTempDir( name );
        const auto dev_config = MakeDevConfig( base );
        WriteRawNetworkConfig( base, json );
        EXPECT_TRUE( sgns::GeniusNode::WriteSgnsConfig(
                         dev_config.BaseWritePath, "Full", true, false )
                         .has_value() );
        auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
        EXPECT_TRUE( node );
        return node ? sgns::NetworkConfigPrecedenceTestAccess::VoteSelectionWindow( node )
                    : std::chrono::milliseconds::zero();
    }
} // namespace

// Scene A (reframed in Phase 3): the canonical New(dev_config, AccountSource) factory has no
// autodht param — auto_dht comes only from network_config.json. Writing "auto_dht": false and
// constructing via New proves the resolved autodht_ is config-driven (false).
TEST( NetworkConfigPrecedence, AutoDhtConfigDriven )
{
    UseMemorySecureStorage();
    auto base = MakeTempDir( "ncp_autodht" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false );

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
    auto base = MakeTempDir( "ncp_port_seed" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/20000, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath, /*node_type=*/"Full", /*is_processor=*/true, /*rpc_catchup=*/false );

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

TEST( NetworkConfigPrecedence, ConsensusVoteSelectionWindowValidValueWins )
{
    EXPECT_EQ( ResolveVoteWindow( "ncp_vote_window_valid",
                                  R"({"port_seed":41001,"auto_dht":false,"upnp_enabled":false,"consensus_vote_selection_window_ms":1234})" ),
               std::chrono::milliseconds( 1234 ) );
}

TEST( NetworkConfigPrecedence, ConsensusVoteSelectionWindowInvalidValuesUseCompiledDefault )
{
    const std::vector<std::pair<std::string, std::string>> cases{
        { "missing", R"({"port_seed":42001,"auto_dht":false,"upnp_enabled":false})" },
        { "zero", R"({"port_seed":43001,"auto_dht":false,"upnp_enabled":false,"consensus_vote_selection_window_ms":0})" },
        { "negative", R"({"port_seed":44001,"auto_dht":false,"upnp_enabled":false,"consensus_vote_selection_window_ms":-1})" },
        { "fraction", R"({"port_seed":45001,"auto_dht":false,"upnp_enabled":false,"consensus_vote_selection_window_ms":1.5})" },
        { "excessive", R"({"port_seed":46001,"auto_dht":false,"upnp_enabled":false,"consensus_vote_selection_window_ms":30001})" },
    };
    for ( const auto &[name, json] : cases )
    {
        EXPECT_EQ( ResolveVoteWindow( "ncp_vote_window_" + name, json ),
                   sgns::ConsensusConfig::DEFAULT_VOTE_SELECTION_WINDOW )
            << name;
    }
}
