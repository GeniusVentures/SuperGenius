#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <gtest/gtest.h>

using namespace sgns;

namespace
{
    // Same private key across scenes -> same account address -> deterministic behavior.
    constexpr const char *TEST_PRIVATE_KEY =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    DevConfig_st MakeDevConfig( const boost::filesystem::path &base )
    {
        return { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base.generic_string() + '/' };
    }

    boost::filesystem::path MakeTempDir( const std::string &name )
    {
        auto path = boost::dll::program_location().parent_path() / name;
        try
        {
            boost::filesystem::remove_all( path );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
        boost::filesystem::create_directories( path );
        return path;
    }

    void WriteSgnsConfig( const boost::filesystem::path &base, const std::string &json )
    {
        std::ofstream ofs( ( base / "sgns_config.json" ).generic_string() );
        ofs << json;
    }
} // namespace

// Scene A (CONTEXT D-02/CFG-03): the new canonical factory derives is_full_node_ from the
// "node_type" sgns_config.json key AFTER LoadSgnsConfig. "full" (lowercase) must parse
// case-insensitively to NodeType::Full -> is_full_node_=true. is_full_node_ is set in the ctor
// body before New() returns, so it is observable immediately (no READY wait needed).
TEST( NodeTypeDerivation, ConfigDrivenCaseInsensitive )
{
    auto base = MakeTempDir( "ntd_derivation" );
    WriteSgnsConfig( base, R"({"node_type": "full"})" );

    auto node = sgns::GeniusNode::New( MakeDevConfig( base ), FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    EXPECT_EQ( node->GetNodeType(), GeniusNode::NodeType::Full );
    EXPECT_TRUE( node->IsFullNode() );
}

// Scene B (CONTEXT D-04): New(dev_config, AccountSource) preserves nullptr-on-failure.
// An invalid private key -> GeniusAccount::NewFromPrivateKey returns nullptr -> the new ctor's
// std::visit returns nullptr -> ctor throws "Account creation failed" -> New() catches -> nullptr.
TEST( NodeTypeDerivation, NullptrOnAccountRestoreFailure )
{
    auto base = MakeTempDir( "ntd_failure" );
    WriteSgnsConfig( base, R"({})" );

    auto node = sgns::GeniusNode::New( MakeDevConfig( base ), FromPrivateKey{ "not-a-valid-hex-key" } );
    EXPECT_EQ( node, nullptr );
}
