#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <string_view>
#include <boost/functional/hash.hpp>
#include <thread>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

#include "base/logger.hpp"
#include "testutil/wait_condition.hpp"

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

    /// PSK shared by the two nodes inside the private network.
    constexpr std::string_view SWARM_KEY_PNET = "/key/swarm/psk/1.0.0/\n/base16/"
                                                "000102030405060708090a0b0c0d0e0f101112131415161718"
                                                "191a1b1c1d1e1f\n";

    /// Different PSK — the "outside" node must fail the pnet handshake.
    constexpr std::string_view SWARM_KEY_OUTSIDE = "/key/swarm/psk/1.0.0/\n/base16/"
                                                   "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0efeeedecebeae9"
                                                   "e8e7e6e5e4e3e2e1e0\n";

    /// Gossip config matching GossipPubSub's production defaults (the class'
    /// own default-config accessor is private, and the pnet constructor
    /// requires an explicit config).
    libp2p::protocol::gossip::Config MakeGossipConfig()
    {
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode       = false;
        config.sign_messages           = true;
        config.seen_cache_limit        = 10;
        config.heartbeat_interval_msec = std::chrono::milliseconds{ 100 };
        return config;
    }

    /// Generates a fresh Ed25519 key pair so every node has a distinct identity.
    libp2p::crypto::KeyPair GenerateKeyPair()
    {
        libp2p::crypto::ed25519::Ed25519ProviderImpl provider;
        auto keypair = provider.generate().value();
        libp2p::crypto::KeyPair result;
        result.publicKey  = { libp2p::crypto::Key::Type::Ed25519,
                             { keypair.public_key.begin(), keypair.public_key.end() } };
        result.privateKey = { libp2p::crypto::Key::Type::Ed25519,
                             { keypair.private_key.begin(), keypair.private_key.end() } };
        return result;
    }


    class PubsubCounts : public ::testing::Test
    {
    public:
        virtual void SetUp() override
        {
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

        auto future = pubs1->Start( 0, {} );
        future.wait();
        auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
        pubs2->Start( 0, {} );

        pubs1->AddPeers( { pubs2->GetInterfaceAddress() } );
        pubs2->AddPeers( { pubs1->GetInterfaceAddress() } );

        sgns::ipfs_pubsub::GossipPubSubTopic resultChannel( pubs1, "CountTest" );
        resultChannel.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                                 true );
        sgns::ipfs_pubsub::GossipPubSubTopic resultChannel2( pubs2, "CountTest" );
        resultChannel2.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                                  true );
        std::cout << "Count Of peers: " << resultChannel.getPeerCount() << std::endl;
        std::cout << "Count Of peers: " << resultChannel2.getPeerCount() << std::endl;
        ASSERT_WAIT_FOR_CONDITION(
            [&]() { return resultChannel.getPeerCount() == resultChannel2.getPeerCount(); },
            std::chrono::milliseconds( 9000 ),
            "Peer counts did not converge within timeout",
            nullptr );
        ASSERT_EQ( resultChannel.getAllPeers().size(), resultChannel2.getAllPeers().size() );

        // Explicitly call Stop() and give time for cleanup before destruction
        pubs1->Stop();
        pubs2->Stop();
    }

    /**
 * @given Four gossip pubsub nodes:
 *        - pnetA and pnetB share a private network key (same PSK)
 *        - outside uses a different PSK, so it is not part of the private network
 *        - blocked shares the pnet PSK but is in pnetA's connection-gater deny list
 * @when All nodes are created once, started, and pnetA attempts to mesh with
 *        each of the other three on a gossip topic
 * @then Only pnetB connects to pnetA; the outside and blocked nodes never
 *        establish a usable connection
 */
    TEST_F( PubsubCounts, PnetIsolationAndGaterBlocking )
    {
        // Nodes are created exactly once and reused for the whole test --
        // recreating pubsubs (Stop() + new GossipPubSub) tends to cause
        // port races and dead listeners, so everything below operates on
        // these four instances only.
        auto pnetA = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_PNET ) );
        auto pnetB = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_PNET ) );
        auto outside = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_OUTSIDE ) );
        auto blocked = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_PNET ) );

        auto futureA = pnetA->Start( 0, {} );
        auto futureB = pnetB->Start( 0, {} );
        auto futureOutside = outside->Start( 0, {} );
        auto futureBlocked = blocked->Start( 0, {} );
        futureA.wait();
        futureB.wait();
        futureOutside.wait();
        futureBlocked.wait();
        ASSERT_FALSE( futureA.get() ) << "pnetA failed to start";
        ASSERT_FALSE( futureB.get() ) << "pnetB failed to start";
        ASSERT_FALSE( futureOutside.get() ) << "outside failed to start";
        ASSERT_FALSE( futureBlocked.get() ) << "blocked failed to start";

        // Sanity: the DI graph must produce four genuinely distinct nodes.
        // (Same-signature Boost.DI injector calls have historically aliased
        // Host instances -- see the libp2p pnet two-node test notes.)
        const auto idA     = pnetA->GetHost()->getId();
        const auto idB     = pnetB->GetHost()->getId();
        const auto idOut   = outside->GetHost()->getId();
        const auto idBlock = blocked->GetHost()->getId();
        std::set<libp2p::peer::PeerId> uniqueIds = { idA, idB, idOut, idBlock };
        ASSERT_EQ( uniqueIds.size(), 4 ) << "pubsub nodes silently share a host";

        // pnetA blocks the "blocked" node via the connection gater.
        pnetA->BlockPeer( idBlock );
        ASSERT_TRUE( pnetA->IsPeerBlocked( idBlock ) );

        // Everyone tries to reach pnetA.
        pnetA->AddPeers( { pnetB->GetInterfaceAddress() } );
        pnetA->AddPeers( { outside->GetInterfaceAddress() } );
        pnetA->AddPeers( { blocked->GetInterfaceAddress() } );

        sgns::ipfs_pubsub::GossipPubSubTopic channelA( pnetA, "PnetTest" );
        channelA.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                             true );
        sgns::ipfs_pubsub::GossipPubSubTopic channelB( pnetB, "PnetTest" );
        channelB.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                             true );
        sgns::ipfs_pubsub::GossipPubSubTopic channelOut( outside, "PnetTest" );
        channelOut.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                              true );
        sgns::ipfs_pubsub::GossipPubSubTopic channelBlocked( blocked, "PnetTest" );
        channelBlocked.Subscribe( []( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message ) {},
                                  true );

        // The two pnet members must mesh with each other.
        ASSERT_WAIT_FOR_CONDITION(
            [&]() { return channelA.getPeerCount() >= 1 && channelB.getPeerCount() >= 1; },
            std::chrono::milliseconds( 15000 ),
            "pnet peers did not connect within timeout",
            nullptr );

        // Message flows between pnet members (separate topic so the counted
        // subscription is the only receiver there).
        std::atomic_size_t receivedOnB{ 0 };
        {
            sgns::ipfs_pubsub::GossipPubSubTopic countedChannelB( pnetB, "PnetTestCounted" );
            countedChannelB.Subscribe(
                [&]( boost::optional<const sgns::ipfs_pubsub::GossipPubSub::Message &> message )
                {
                    if ( message )
                    {
                        ++receivedOnB;
                    }
                },
                true );
            pnetA->Publish( "PnetTestCounted", std::vector<uint8_t>{ 1, 2, 3 } );

            ASSERT_WAIT_FOR_CONDITION(
                [&]() { return receivedOnB.load() >= 1; },
                std::chrono::milliseconds( 10000 ),
                "pnetB never received the pnetA message",
                nullptr );
        }

        // The outside node (wrong PSK) and the gater-blocked node must never
        // appear as peers of pnetA. Negative conditions cannot be proven
        // positively, so give them a grace window in which they would have
        // connected if the protections were broken, then assert they stayed
        // absent and non-connected the whole time.
        const auto gracePeriod = std::chrono::milliseconds( 3000 );
        const auto deadline    = std::chrono::steady_clock::now() + gracePeriod;
        while ( std::chrono::steady_clock::now() < deadline )
        {
            ASSERT_EQ( channelOut.getPeerCount(), 0 ) << "outside node meshed despite pnet mismatch";
            ASSERT_EQ( channelBlocked.getPeerCount(), 0 ) << "blocked node meshed despite gater deny list";
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }

        const auto &hostA = pnetA->GetHost();
        const auto notConnected = [ &hostA ]( const libp2p::peer::PeerId &id )
        {
            libp2p::peer::PeerInfo info{ id, {} };
            auto connectedness = hostA->connectedness( info );
            return connectedness != libp2p::Host::Connectedness::CONNECTED;
        };
        EXPECT_TRUE( notConnected( idOut ) ) << "outside node connected despite pnet mismatch";
        EXPECT_TRUE( notConnected( idBlock ) ) << "blocked node connected despite gater deny list";

        EXPECT_EQ( channelA.getPeerCount(), 1 ) << "pnetA should be meshed with exactly one peer";

        // Explicitly call Stop() and give time for cleanup before destruction
        pnetA->Stop();
        pnetB->Stop();
        outside->Stop();
        blocked->Stop();
    }

}
