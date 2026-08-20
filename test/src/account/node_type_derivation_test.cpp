#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

using namespace sgns;

namespace
{
    // Same private key across scenes -> same account address -> deterministic behavior.
    constexpr const char *TEST_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

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
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    std::string ConfiguredFixtureAddress( const boost::filesystem::path &base )
    {
        const auto account = GeniusAccount::NewFromPrivateKey(
            sgns::TokenID::FromBytes( { 0x00 } ), TEST_PRIVATE_KEY, ( base / "configured-identity" ).string(), true );
        return account ? account->GetAddress() : std::string{};
    }
} // namespace

// Scene A (CONTEXT D-02/CFG-03): the new canonical factory derives is_full_node_ from the
// "node_type" sgns_config.json key AFTER LoadSgnsConfig. "full" (lowercase) must parse
// case-insensitively to NodeType::Full -> is_full_node_=true. is_full_node_ is set in the ctor
// body before New() returns, so it is observable immediately. The READY wait only lets New()'s
// asynchronous database initialization finish before the test process releases the node.
TEST( NodeTypeDerivation, ConfigDrivenCaseInsensitive )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ntd_derivation" );
    const auto dev_config = MakeDevConfig( base );
    GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                 /*node_type=*/"full",
                                 /*is_processor=*/true,
                                 /*rpc_catchup=*/false );

    auto node = sgns::GeniusNode::New( dev_config, FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    const auto configured_address = ConfiguredFixtureAddress( base );
    ASSERT_FALSE( configured_address.empty() );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( configured_address );

    EXPECT_EQ( node->GetNodeType(), GeniusNode::NodeType::Full );
    EXPECT_TRUE( node->IsFullNode() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "node-type test node did not finish initialization" ) );
}

// Scene B (CONTEXT D-04): New(dev_config, AccountSource) preserves nullptr-on-failure.
// An invalid private key -> GeniusAccount::NewFromPrivateKey returns nullptr -> the new ctor's
// std::visit returns nullptr -> ctor throws "Account creation failed" -> New() catches -> nullptr.
TEST( NodeTypeDerivation, NullptrOnAccountRestoreFailure )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ntd_failure" );
    const auto dev_config = MakeDevConfig( base );
    GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                 /*node_type=*/"Light",
                                 /*is_processor=*/true,
                                 /*rpc_catchup=*/false );

    auto node = sgns::GeniusNode::New( dev_config, FromPrivateKey{ "not-a-valid-hex-key" } );
    EXPECT_EQ( node, nullptr );
}
