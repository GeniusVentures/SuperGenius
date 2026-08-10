#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <boost/dll.hpp>
#include <gtest/gtest.h>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_id.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class GeniusNodeBootstrapReconnectTest : public ::testing::Test
    {
    protected:
        static constexpr std::string_view FULL_NODE_PRIVATE_KEY =
            "9389e5f08c01e791dc436abab7a61a502515ddc7f91cb09f10289e147c651780";
        static constexpr std::string_view CLIENT_PRIVATE_KEY =
            "19c2f2db8e7cb27e5438093cf377d27888ddd4b257827baddd0418eefacedd02";

        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            const auto binary_path       = boost::dll::program_location().parent_path().string();
            full_config_.BaseWritePath   = binary_path + "/bootstrap_reconnect_full/";
            client_config_.BaseWritePath = binary_path + "/bootstrap_reconnect_client/";

            test::removeAllWithRetry( full_config_.BaseWritePath );
            test::removeAllWithRetry( client_config_.BaseWritePath );

            ASSERT_TRUE( GeniusNode::WriteNetworkConfig( full_config_.BaseWritePath, 0, false ).has_value() );
            ASSERT_TRUE( GeniusNode::WriteSgnsConfig( full_config_.BaseWritePath, "Full", false ).has_value() );
            {
                std::ofstream config( full_config_.BaseWritePath + "bridge_chains_config.json" );
                ASSERT_TRUE( config.good() );
                config << "{}";
            }

            full_node_ = GeniusNode::New( full_config_, FromPrivateKey{ std::string( FULL_NODE_PRIVATE_KEY ) } );
            ASSERT_TRUE( full_node_ );
            Blockchain::SetAuthorizedFullNodeAddress( full_node_->GetAddress() );

            ASSERT_NO_FATAL_FAILURE(
                test::assertWaitForCondition( [&]() { return full_node_->GetState() == GeniusNode::NodeState::READY; },
                                              std::chrono::seconds( 50 ),
                                              "bootstrap full node did not become ready" ) );

            bootstrap_address_ = full_node_->GetPubSub()->GetInterfaceAddress();
            ASSERT_FALSE( bootstrap_address_.empty() );
            ASSERT_NE( full_node_->GetPubsubPort(), 0u );
            {
                std::ofstream config( full_config_.BaseWritePath + "network_config.json" );
                ASSERT_TRUE( config.good() );
                config << "{ \"pubsub_port\": \"" << full_node_->GetPubsubPort()
                       << "\", \"auto_dht\": false, \"upnp_enabled\": false }";
            }
            full_node_.reset();

            std::filesystem::create_directories( client_config_.BaseWritePath );
            {
                std::ofstream config( client_config_.BaseWritePath + "bridge_chains_config.json" );
                ASSERT_TRUE( config.good() );
                config << "{}";
            }
            {
                std::ofstream config( client_config_.BaseWritePath + "sgns_config.json" );
                ASSERT_TRUE( config.good() );
                config << "{ \"node_type\": \"Light\", \"is_processor\": false, \"bootstrap_fullnodes\": [\""
                       << bootstrap_address_ << "\"] }";
            }
            {
                std::ofstream config( client_config_.BaseWritePath + "network_config.json" );
                ASSERT_TRUE( config.good() );
                config << "{ \"port_seed\": 0, \"auto_dht\": false, \"upnp_enabled\": false, "
                          "\"bootstrap_reconnect_base_delay_sec\": 1, \"bootstrap_reconnect_max_delay_sec\": 1, "
                          "\"bootstrap_health_check_interval_sec\": 1, "
                          "\"bootstrap_health_check_disconnected_interval_sec\": 1 }";
            }

            client_node_ = GeniusNode::New( client_config_, FromPrivateKey{ std::string( CLIENT_PRIVATE_KEY ) } );
            ASSERT_TRUE( client_node_ );
        }

        void TearDown() override
        {
            client_node_.reset();
            full_node_.reset();
        }

        DevConfig                   full_config_   = { "0xcafe", "0.65", "1.0", TokenID::FromBytes( { 0x00 } ), {} };
        DevConfig                   client_config_ = { "0xcafe", "0.65", "1.0", TokenID::FromBytes( { 0x00 } ), {} };
        std::shared_ptr<GeniusNode> full_node_;
        std::shared_ptr<GeniusNode> client_node_;
        std::string                 bootstrap_address_;
    };

    TEST_F( GeniusNodeBootstrapReconnectTest, ConnectsWhenConfiguredBootstrapStartsLateAndReconnectsAfterRestart )
    {
        auto multiaddress_result = libp2p::multi::Multiaddress::create( bootstrap_address_ );
        ASSERT_TRUE( multiaddress_result.has_value() );
        auto multiaddress = std::move( multiaddress_result.value() );

        auto peer_id_text = multiaddress.getPeerId();
        ASSERT_TRUE( peer_id_text.has_value() );
        auto peer_id_result = libp2p::peer::PeerId::fromBase58( peer_id_text.value() );
        ASSERT_TRUE( peer_id_result.has_value() );
        const libp2p::peer::PeerInfo bootstrap_peer{ peer_id_result.value(), { multiaddress } };

        // Leave the configured bootstrap offline long enough for the initial transport dial
        // to fail. The client must retry without first handing that failure to GossipSub,
        // which bans failed peers for one minute.
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        EXPECT_NE( client_node_->GetPubSub()->GetHost()->connectedness( bootstrap_peer ),
                   libp2p::Host::Connectedness::CONNECTED );

        full_node_ = GeniusNode::New( full_config_, FromPrivateKey{ std::string( FULL_NODE_PRIVATE_KEY ) } );
        ASSERT_TRUE( full_node_ );
        Blockchain::SetAuthorizedFullNodeAddress( full_node_->GetAddress() );

        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]() { return full_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::seconds( 50 ),
            "late bootstrap full node did not become ready" ) );
        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]() { return client_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::seconds( 50 ),
            "configured bootstrap client did not become ready" ) );
        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]()
            {
                return client_node_->GetPubSub()->GetHost()->connectedness( bootstrap_peer ) ==
                       libp2p::Host::Connectedness::CONNECTED;
            },
            std::chrono::seconds( 20 ),
            "client did not connect to its configured bootstrap full node" ) );

        full_node_.reset();

        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]()
            {
                return client_node_->GetPubSub()->GetHost()->connectedness( bootstrap_peer ) !=
                       libp2p::Host::Connectedness::CONNECTED;
            },
            std::chrono::seconds( 20 ),
            "client did not observe the configured bootstrap going offline" ) );

        full_node_ = GeniusNode::New( full_config_, FromPrivateKey{ std::string( FULL_NODE_PRIVATE_KEY ) } );
        ASSERT_TRUE( full_node_ );
        Blockchain::SetAuthorizedFullNodeAddress( full_node_->GetAddress() );

        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]() { return full_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::seconds( 50 ),
            "restarted bootstrap full node did not become ready" ) );
        ASSERT_NO_FATAL_FAILURE( sgns::test::assertWaitForCondition(
            [&]()
            {
                return client_node_->GetPubSub()->GetHost()->connectedness( bootstrap_peer ) ==
                       libp2p::Host::Connectedness::CONNECTED;
            },
            std::chrono::seconds( 20 ),
            "client did not reconnect to the restarted configured bootstrap" ) );
    }
} // namespace sgns
