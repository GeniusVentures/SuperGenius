#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>

#include <boost/functional/hash.hpp>
#include <thread>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "base/logger.hpp"


namespace
{
    const std::string logger_config( R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: pubsub_count_test
        sink: console
        level: info
        children:
          - name: libp2p
          - name: Gossip
    # ----------------
      )" );

    class PubsubCounts : public ::testing::Test
    {
    public:
        virtual void SetUp() override {

                    // prepare log system
                    auto logging_system = std::make_shared<soralog::LoggingSystem>(
                        std::make_shared<soralog::ConfiguratorFromYAML>(
                            // Original LibP2P logging config
                            std::make_shared<libp2p::log::Configurator>(),
                            // Additional logging config for application
                            logger_config ) );
                    logging_system->configure();
                    
                    libp2p::log::setLoggingSystem( logging_system );
                    libp2p::log::setLevelOfGroup( "pubsub_count_test", soralog::Level::OFF );


        }
    };

    /**
 * @given A node is subscribed to result channel
 * @when A result is published to the channel
 * @then The node receives the result
 */
    TEST_F( PubsubCounts, CheckSubscriptionCount )
    {
        auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();

        auto future = pubs1->Start( 40081, {} );
        future.wait();
        auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
        pubs2->Start( 40002, {} );

        pubs1->AddPeers( { pubs2->GetLocalAddress() } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        pubs2->AddPeers( { pubs1->GetLocalAddress() } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

        sgns::ipfs_pubsub::GossipPubSubTopic resultChannel( pubs1, "CountTest" );
        resultChannel.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {}, true );
        sgns::ipfs_pubsub::GossipPubSubTopic resultChannel2( pubs2, "CountTest" );
        resultChannel2.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},true );
        //resultChannel2.Publish();
        std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
        //std::string test = "CountTest";
        std::cout << "Count Of peers: " << resultChannel.getPeerCount() << std::endl;
        std::cout << "Count Of peers: " << resultChannel2.getPeerCount() << std::endl;
        ASSERT_EQ( resultChannel.getPeerCount(), resultChannel2.getPeerCount() );
        ASSERT_EQ( resultChannel.getAllPeers().size(), resultChannel2.getAllPeers().size() );

        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

        // Explicitly call Stop() and give time for cleanup before destruction
        pubs1->Stop();
        pubs2->Stop();
    }

}
