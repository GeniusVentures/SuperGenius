#include "account/GeniusNode.hpp"
#include <gtest/gtest.h>

using namespace sgns;

namespace sgns
{
    class NetworkConfigPrecedenceTestAccess
    {
    public:
        struct ResolvedConfig
        {
            uint16_t                  port_seed;
            bool                      auto_dht;
            std::chrono::milliseconds vote_selection_window;
        };

        static ResolvedConfig Resolve( std::string_view json )
        {
            const auto resolved = GeniusNode::ResolveNetworkConsensusConfig(
                json, 40001, true, ConsensusConfig::DEFAULT_VOTE_SELECTION_WINDOW );
            return { resolved.port_seed, resolved.auto_dht, resolved.vote_selection_window };
        }
    };
} // namespace sgns

namespace
{
    std::chrono::milliseconds ResolveVoteWindow( std::string_view json )
    {
        return sgns::NetworkConfigPrecedenceTestAccess::Resolve( json ).vote_selection_window;
    }
} // namespace

// The friend-only observer calls the same private, side-effect-free resolver used by InitNetwork.
// These precedence tests therefore do not depend on a host interface, PubSub socket, or router.
TEST( NetworkConfigPrecedence, AutoDhtConfigDriven )
{
    const auto resolved = sgns::NetworkConfigPrecedenceTestAccess::Resolve(
        R"({"port_seed":40001,"auto_dht":false})" );
    EXPECT_FALSE( resolved.auto_dht );
}

TEST( NetworkConfigPrecedence, PortSeedConfigDriven )
{
    const auto resolved = sgns::NetworkConfigPrecedenceTestAccess::Resolve(
        R"({"port_seed":49999,"auto_dht":false})" );
    EXPECT_EQ( resolved.port_seed, 49999u );
}

TEST( NetworkConfigPrecedence, ConsensusVoteSelectionWindowValidValueWins )
{
    EXPECT_EQ( ResolveVoteWindow(
                   R"({"port_seed":41001,"auto_dht":false,"consensus_vote_selection_window_ms":1234})" ),
               std::chrono::milliseconds( 1234 ) );
}

TEST( NetworkConfigPrecedence, ConsensusVoteSelectionWindowInvalidValuesUseCompiledDefault )
{
    const std::vector<std::pair<std::string, std::string>> cases{
        { "missing", R"({"port_seed":42001,"auto_dht":false})" },
        { "zero", R"({"consensus_vote_selection_window_ms":0})" },
        { "negative", R"({"consensus_vote_selection_window_ms":-1})" },
        { "fraction", R"({"consensus_vote_selection_window_ms":1.5})" },
        { "excessive", R"({"consensus_vote_selection_window_ms":30001})" },
    };
    for ( const auto &[name, json] : cases )
    {
        EXPECT_EQ( ResolveVoteWindow( json ),
                   sgns::ConsensusConfig::DEFAULT_VOTE_SELECTION_WINDOW )
            << name;
    }
}
