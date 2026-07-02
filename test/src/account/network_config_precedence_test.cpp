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
    // Same private key across every scene -> same account address ->
    // GenerateRandomPort(seed, address) is deterministic. autodht/is_full_node flags do
    // not affect port derivation, so the only variable between scenes is the config file.
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

    void WriteNetworkConfig( const boost::filesystem::path &base, const std::string &json )
    {
        std::ofstream ofs( ( base / "network_config.json" ).generic_string() );
        ofs << json;
    }
} // namespace

// Scene A (CONTEXT D-01/D-02): config "auto_dht": false overrides constructor autodht=true.
// autodht_ is initialised from the param in the ctor init-list, then reassigned from the
// config key inside InitNetwork when present. A false getter result proves the override.
TEST( NetworkConfigPrecedence, AutoDhtConfigOverridesParam )
{
    auto base = MakeTempDir( "ncp_autodht" );
    WriteNetworkConfig( base, R"({"auto_dht": false})" );

    auto node = sgns::GeniusNode::NewFromPrivateKey( MakeDevConfig( base ),
                                                     TEST_PRIVATE_KEY,
                                                     /*autodht=*/true,
                                                     /*port_seed=*/40001,
                                                     /*is_full_node=*/true );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    // Param was true; config "auto_dht": false must win.
    EXPECT_FALSE( node->IsAutodhtEnabled() );
}

// Scene B (CONTEXT D-01/D-02): config "port_seed": 49999 overrides constructor port_seed=40001.
// pubsubport_ is resolved synchronously in InitNetwork. With no "pubsub_port" key present,
// the else-branch runs GenerateRandomPort(port_seed, address). GenerateRandomPort returns a
// value in [base, base+300], so:
//   - param-only (40001) would resolve into [40001, 40301]
//   - config-overridden (49999) resolves into [49999, 50299]
// These ranges do not overlap, so a resolved port >= 49999 unambiguously proves the config
// key overrode the param. Single construction avoids second-port-bind flakiness.
TEST( NetworkConfigPrecedence, PortSeedConfigOverridesParam )
{
    auto base = MakeTempDir( "ncp_port_seed" );
    WriteNetworkConfig( base, R"({"port_seed": 49999})" );

    auto node = sgns::GeniusNode::NewFromPrivateKey( MakeDevConfig( base ),
                                                     TEST_PRIVATE_KEY,
                                                     /*autodht=*/false,
                                                     /*port_seed=*/40001,
                                                     /*is_full_node=*/true );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    const auto resolved = node->GetPubsubPort();
    // Impossible if the param (40001) had been used; only reachable via the config override.
    EXPECT_GE( resolved, 49999u );
}
