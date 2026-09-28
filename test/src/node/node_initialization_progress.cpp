#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include <chrono>
#include <thread>
#include "testutil/local_trust_setup.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include <boost/dll/runtime_symbol_info.hpp>
#include <gtest/gtest.h>

using namespace sgns;

TEST( GeniusNode, InitializationProgress )
{
    boost::filesystem::path path = boost::dll::program_location().parent_path() / "init_progress_node";

    try
    {
        sgns::test::removeAllWithRetry( path.string() );
    }
    catch ( ... ) //NOLINT(bugprone-empty-catch)
    {
    }

    GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                            { return std::make_shared<MemorySecureStorage>( identifier ); } );

    const auto base_write_path = path.generic_string() + '/';
    sgns::GeniusNode::WriteNetworkConfig( base_write_path, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::test::WriteLocalTrustSgnsConfig( base_write_path,
                                           /*node_type=*/"Full",
                                           /*is_processor=*/true,
                                           /*rpc_catchup=*/false,
                                           "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" );
    auto node = sgns::GeniusNode::New(
        { "0xcafe", "0.35", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base_write_path },
        sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    auto last_percentage = 0.0F;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    bool trust_approved = false;
    while ( std::chrono::steady_clock::now() < end && node->GetState() != GeniusNode::NodeState::READY )
    {
        // Drive the local trust genesis once the node parks in a restricted trust
        // state, then keep nudging the controller so init can proceed to READY.
        const auto state = node->GetState();
        if ( !trust_approved && ( state == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS ||
                                  state == GeniusNode::NodeState::WAITING_FOR_BURN_GENESIS ) )
        {
            ASSERT_FALSE( sgns::GeniusNodeTestAccess::ApproveConfiguredTrustGenesis( node ).has_error() );
            trust_approved = true;
        }
        if ( trust_approved )
        {
            sgns::GeniusNodeTestAccess::RefreshTrust( node );
        }
        auto percentage = node->GetInitializationStatus().first;
        ASSERT_GE( percentage, last_percentage );
        last_percentage = percentage;
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }

    ASSERT_EQ( node->GetInitializationStatus().first, 1.0 );
}
