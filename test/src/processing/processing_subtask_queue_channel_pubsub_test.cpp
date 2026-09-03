#include "processing_service_test.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <gtest/gtest.h>
#include <thread>
#include <unordered_set>
#include <boost/chrono/duration.hpp>

#include "testutil/wait_condition.hpp"

#include "base/logger.hpp"

using namespace sgns::processing;
using namespace sgns::test;
using namespace sgns::base;

const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_subtask_queue_channel_pubsub_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

class ProcessingSubTaskChannelPubSubTest : public ProcessingServiceTest
{
public:
    virtual void SetUp() override
    {
        ProcessingServiceTest::SetUp( "processing_subtask_queue_manager_test", logger_config );
        ProcessingServiceTest::Initialize( 2, 50 );
    }

    const std::string nodeId1 = "NODE_1";
    const std::string nodeId2 = "NODE_2";
};

/**
 * @given 2 channels connected to a single pubsub host
 * @when A queue ownership request sent
 * @then Both channels receive the request.
 */
TEST_F( ProcessingSubTaskChannelPubSubTest, RequestTransmittingOnSinglePubSubHost )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "PROCESSING_CHANNEL_ID" );
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "PROCESSING_CHANNEL_ID" );

    std::atomic<size_t>      requestCount1{ 0 };
    std::vector<std::string> requestedNodeIds1;
    std::mutex               mutex1;
    queueChannel1->SetQueueRequestSink(
        [&requestCount1, &requestedNodeIds1, &mutex1]( const SGProcessing::SubTaskQueueRequest &request )
        {
            std::lock_guard lock( mutex1 );
            requestedNodeIds1.push_back( request.node_id() );
            ++requestCount1;
            return true;
        } );

    std::atomic<size_t>      requestCount2{ 0 };
    std::vector<std::string> requestedNodeIds2;
    std::mutex               mutex2;
    queueChannel2->SetQueueRequestSink(
        [&requestCount2, &requestedNodeIds2, &mutex2]( const SGProcessing::SubTaskQueueRequest &request )
        {
            std::lock_guard lock( mutex2 );
            requestedNodeIds2.push_back( request.node_id() );
            ++requestCount2;
            return true;
        } );

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 1 Subscription established after ", wait_time.count(), " ms" );
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 2 Subscription established after ", wait_time.count(), " ms" );
    }

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership( nodeId1 );
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION( ([&requestCount1, &requestCount2]() { return requestCount1 >= 1 && requestCount2 >= 1; }),
                               std::chrono::milliseconds( 2000 ),
                               "First request not received by both channels",
                               &waitTime1 );

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership( nodeId2 );
    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION( ([&requestCount1, &requestCount2]() { return requestCount1 >= 2 && requestCount2 >= 2; }),
                               std::chrono::milliseconds( 2000 ),
                               "Second request not received by both channels",
                               &waitTime2 );

    ASSERT_EQ( 2, requestedNodeIds1.size() );
    EXPECT_EQ( nodeId1, requestedNodeIds1[0] );
    EXPECT_EQ( nodeId2, requestedNodeIds1[1] );

    ASSERT_EQ( 2, requestedNodeIds2.size() );
    EXPECT_EQ( nodeId1, requestedNodeIds2[0] );
    EXPECT_EQ( nodeId2, requestedNodeIds2[1] );
}

/**
 * @given 2 channels connected to a different pubsub hosts
 * @when A queue ownership request sent
 * @then Both channels receive the request.
 */
TEST_F( ProcessingSubTaskChannelPubSubTest, RequestTransmittingOnDifferentPubSubHosts )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "PROCESSING_CHANNEL_ID" );
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs2, "PROCESSING_CHANNEL_ID" );

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // if there are multiple threads sending the QueueRequest, the counter should be wrapped in a mutex
    std::atomic<size_t>   requestCount1{ 0 };
    std::set<std::string> requestedNodeIds1;
    queueChannel1->SetQueueRequestSink(
        [&requestCount1, &requestedNodeIds1]( const SGProcessing::SubTaskQueueRequest &request )
        {
            requestedNodeIds1.insert( request.node_id() );
            // Properly update the atomic
            ++requestCount1;
            return true;
        } );

    // if there are multiple threads sending the QueueRequest, the counter should be wrapped in a mutex
    std::atomic<size_t>   requestCount2{ 0 };
    std::set<std::string> requestedNodeIds2;
    queueChannel2->SetQueueRequestSink(
        [&requestCount2, &requestedNodeIds2]( const SGProcessing::SubTaskQueueRequest &request )
        {
            requestedNodeIds2.insert( request.node_id() );
            ++requestCount2;
            return true;
        } );

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 1 Subscription established after ", wait_time.count(), " ms" );
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 2 Subscription established after ", wait_time.count(), " ms" );
    }

    std::string nodeId1 = "NODE1_ID";
    queueChannel1->RequestQueueOwnership( nodeId1 );
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION( ([&requestCount1, &requestCount2]() { return requestCount1 >= 1 && requestCount2 >= 1; }),
                               std::chrono::milliseconds( 2000 ),
                               "First request not received by both channels",
                               &waitTime1 );

    std::string nodeId2 = "NODE2_ID";
    queueChannel2->RequestQueueOwnership( nodeId2 );

    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION( ([&requestCount1, &requestCount2]() { return requestCount1 >= 2 && requestCount2 >= 2; }),
                               std::chrono::milliseconds( 2000 ),
                               "Second request not received by both channels",
                               &waitTime2 );

    // Requests are received by both channel endpoints.
    ASSERT_EQ( 2, requestedNodeIds1.size() );
    ASSERT_EQ( 2, requestedNodeIds2.size() );
}

/**
 * @given 2 channels connected to a single pubsub host
 * @when A queue published to a channel
 * @then Both channels receive the queue.
 */
TEST_F( ProcessingSubTaskChannelPubSubTest, QueueTransmittingOnSinglePubSubHost )
{
    auto                      pubs1 = m_pubsub_nodes[0];
    std::chrono::milliseconds resultTime;

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "PROCESSING_CHANNEL_ID" );
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, "PROCESSING_CHANNEL_ID" );

    std::atomic<size_t>                                      queueCount1{ 0 };
    std::mutex                                               mutex1;
    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet1;
    queueChannel1->SetQueueUpdateSink(
        [&queueSnapshotSet1, &queueCount1, &mutex1]( SGProcessing::SubTaskQueue *queue )
        {
            std::lock_guard lock( mutex1 );
            auto                        queueCopy = std::make_shared<SGProcessing::SubTaskQueue>();
            queueCopy->CopyFrom( *queue );
            queueSnapshotSet1.push_back( queueCopy );
            queueCount1++;
            return true;
        } );

    std::atomic<size_t>                                      queueCount2{ 0 };
    std::mutex                                               mutex2;
    std::vector<std::shared_ptr<SGProcessing::SubTaskQueue>> queueSnapshotSet2;
    queueChannel2->SetQueueUpdateSink(
        [&queueSnapshotSet2, &queueCount2, &mutex2]( SGProcessing::SubTaskQueue *queue )
        {
            std::lock_guard lock( mutex2 );
            auto                        queueCopy = std::make_shared<SGProcessing::SubTaskQueue>();
            queueCopy->CopyFrom( *queue );
            queueSnapshotSet2.push_back( queueCopy );
            queueCount2++;
            return true;
        } );

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 1 Subscription established after ", wait_time.count(), " ms" );
    }

    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Channel subscription failed to establish within 2000ms";

    // Log the actual time if interested
    if ( listen_result && std::holds_alternative<std::chrono::milliseconds>( listen_result.value() ) )
    {
        auto wait_time = std::get<std::chrono::milliseconds>( listen_result.value() );
        Color::PrintInfo( "Channel 2 Subscription established after ", wait_time.count(), " ms" );
    }

    std::string nodeId1 = "NODE1_ID";
    auto        queue   = std::make_shared<SGProcessing::SubTaskQueue>();
    queue->mutable_processing_queue()->set_owner_node_id( nodeId1 );
    {
        auto subtask = queue->mutable_subtasks()->add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
    }
    {
        auto subtask = queue->mutable_subtasks()->add_items();
        subtask->set_subtaskid( "SUBTASK_2" );
    }

    queueChannel1->PublishQueue( queue );
    std::chrono::milliseconds waitTime1;
    ASSERT_WAIT_FOR_CONDITION( ([&queueCount1, &queueCount2]() { return queueCount1 >= 1 && queueCount2 >= 1; }),
                               std::chrono::milliseconds( 2000 ),
                               "First queue not received by both channels",
                               &waitTime1 );

    std::string nodeId2 = "NODE2_ID";
    queue->mutable_processing_queue()->set_owner_node_id( nodeId2 );
    queueChannel2->PublishQueue( queue );
    std::chrono::milliseconds waitTime2;
    ASSERT_WAIT_FOR_CONDITION( ([&queueCount1, &queueCount2]() { return queueCount1 >= 2 && queueCount2 >= 2; }),
                               std::chrono::milliseconds( 2000 ),
                               "Second queue not received by both channels",
                               &waitTime2 );

    ASSERT_EQ( 2, queueCount1.load() );
    EXPECT_EQ( nodeId1, queueSnapshotSet1[0]->processing_queue().owner_node_id() );
    EXPECT_EQ( nodeId2, queueSnapshotSet1[1]->processing_queue().owner_node_id() );

    ASSERT_EQ( 2, queueCount2.load() );
    EXPECT_EQ( nodeId1, queueSnapshotSet2[0]->processing_queue().owner_node_id() );
    EXPECT_EQ( nodeId2, queueSnapshotSet2[1]->processing_queue().owner_node_id() );
}

/**
 * @given 2 channels connected to different pubsub hosts; a deny-all membership
 *        filter installed on the receiving channel
 * @when The sender publishes a queue ownership request
 * @then The receiver's request sink is NOT invoked while the filter denies the
 *       sender; after the filter is REPLACED with one allowing the sender's peer id,
 *       the sender's next queue message propagates (set-time consultation at the
 *       processing layer — runtime admission with no reinstall).
 */
TEST_F( ProcessingSubTaskChannelPubSubTest, MembershipFilterBlocksNonMemberQueueMessages )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    const std::string queueChannelId = "PROCESSING_MEMBERSHIP_CHANNEL";

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, queueChannelId );
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs2, queueChannelId );

    std::atomic<size_t>   requestCount2{ 0 };
    std::set<std::string> requestedNodeIds2;
    std::mutex            mutex2;
    queueChannel2->SetQueueRequestSink(
        [&requestCount2, &requestedNodeIds2, &mutex2]( const SGProcessing::SubTaskQueueRequest &request )
        {
            std::lock_guard lock( mutex2 );
            requestedNodeIds2.insert( request.node_id() );
            ++requestCount2;
            return true;
        } );

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Sender channel subscription failed to establish";
    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Receiver channel subscription failed to establish";

    // Learn the sender's transport peer id from the receiver's topic view (bounded
    // wait for the mesh — the sender must be visible before the filter is built).
    std::vector<libp2p::peer::PeerId> senderPeers;
    ASSERT_WAIT_FOR_CONDITION(
        ( [&queueChannel2, &senderPeers]()
        {
            senderPeers = queueChannel2->GetActiveNodes();
            return !senderPeers.empty();
        } ),
        std::chrono::milliseconds( 5000 ),
        "Sender peer not visible on the receiver's queue topic",
        nullptr );
    ASSERT_FALSE( senderPeers.empty() );
    std::unordered_set<std::string> memberSenders;
    for ( const auto &peer : senderPeers )
    {
        memberSenders.insert( peer.toBase58() );
    }

    // CR-G01 fixture repair: the SENDER must seal (a gated receiver denies
    // raw data), so it installs the same membership shape + its own signing
    // key; the receiver is keyed too. The sender's envelope authenticates
    // fine (own key, own from) -- the deny phase is then attributable to the
    // receiver's MEMBERSHIP predicate alone.
    queueChannel1->SetMembershipFilter(
        [memberSenders]( const libp2p::peer::PeerId &peer )
        { return memberSenders.count( peer.toBase58() ) > 0; } );
    queueChannel1->SetGossipSigningKey( m_pubsub_keypairs[0] );
    queueChannel2->SetGossipSigningKey( m_pubsub_keypairs[1] );

    // Deny-all filter on the receiving channel
    queueChannel2->SetMembershipFilter(
        []( const libp2p::peer::PeerId & ) { return false; } );

    // The sender-side operation the passing tests above prove propagates
    queueChannel1->RequestQueueOwnership( "NODE_NON_MEMBER" );

    // Bounded negative window (grace-loop pattern): the receiver's observable must
    // NOT change while the filter denies the sender
    const auto denyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3000 );
    while ( std::chrono::steady_clock::now() < denyDeadline )
    {
        ASSERT_EQ( 0, requestCount2.load() )
            << "Non-member queue message reached the receiver's sink although the filter denies it";
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    EXPECT_EQ( 0, requestCount2.load() );
    {
        std::lock_guard lock( mutex2 );
        EXPECT_EQ( 0, requestedNodeIds2.size() );
    }

    // Widen the SAME live channel: replace the filter with one allowing the sender
    queueChannel2->SetMembershipFilter(
        [memberSenders]( const libp2p::peer::PeerId &peer )
        { return memberSenders.count( peer.toBase58() ) > 0; } );

    queueChannel1->RequestQueueOwnership( "NODE_MEMBER" );

    ASSERT_WAIT_FOR_CONDITION( [&requestCount2]() { return requestCount2.load() >= 1; },
                               std::chrono::milliseconds( 5000 ),
                               "Member's queue message was not received after the filter widened",
                               nullptr );

    {
        std::lock_guard lock( mutex2 );
        EXPECT_EQ( 1, requestedNodeIds2.count( "NODE_MEMBER" ) );
        EXPECT_EQ( 0, requestedNodeIds2.count( "NODE_NON_MEMBER" ) );
    }
}

/**
 * @given 2 channels on different pubsub hosts; the receiver's membership filter
 *        ALLOWS the sender (it is in the allow-set), and both sides are keyed
 *        for CR-G01 sealing
 * @when The sender's signing key is wired to ANOTHER member's keypair, so its
 *       envelope embeds a public key deriving a DIFFERENT member's PeerId than
 *       the transport from-field it actually publishes under (impersonation
 *       attempt), then the sender re-wires its OWN key and publishes again
 * @then The impostor envelope is dropped at the authentication check (the
 *       request sink stays at 0 although membership alone would admit the
 *       sender); the honestly-sealed request propagates (positive control).
 */
TEST_F( ProcessingSubTaskChannelPubSubTest, QueueChannelImpostorEnvelopeIgnored )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    const std::string queueChannelId = "PROCESSING_IMPOSTOR_CHANNEL";

    auto queueChannel1 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs1, queueChannelId );
    auto queueChannel2 = std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubs2, queueChannelId );

    std::atomic<size_t>   requestCount2{ 0 };
    std::set<std::string> requestedNodeIds2;
    std::mutex            mutex2;
    queueChannel2->SetQueueRequestSink(
        [&requestCount2, &requestedNodeIds2, &mutex2]( const SGProcessing::SubTaskQueueRequest &request )
        {
            std::lock_guard lock( mutex2 );
            requestedNodeIds2.insert( request.node_id() );
            ++requestCount2;
            return true;
        } );

    auto listen_result = queueChannel1->Listen();
    ASSERT_TRUE( listen_result ) << "Sender channel subscription failed to establish";
    listen_result = queueChannel2->Listen();
    ASSERT_TRUE( listen_result ) << "Receiver channel subscription failed to establish";

    // Learn the sender's transport peer id from the receiver's topic view: the
    // allow-set deliberately CONTAINS the sender (membership alone would admit
    // it -- only the authentication check can deny the impostor envelope).
    std::vector<libp2p::peer::PeerId> senderPeers;
    ASSERT_WAIT_FOR_CONDITION(
        ( [&queueChannel2, &senderPeers]()
        {
            senderPeers = queueChannel2->GetActiveNodes();
            return !senderPeers.empty();
        } ),
        std::chrono::milliseconds( 5000 ),
        "Sender peer not visible on the receiver's queue topic",
        nullptr );
    std::unordered_set<std::string> memberSenders;
    for ( const auto &peer : senderPeers )
    {
        memberSenders.insert( peer.toBase58() );
    }

    const auto senderAllowFilter = [memberSenders]( const libp2p::peer::PeerId &peer )
    { return memberSenders.count( peer.toBase58() ) > 0; };

    // Receiver: allow-set filter + own signing key.
    queueChannel2->SetMembershipFilter( senderAllowFilter );
    queueChannel2->SetGossipSigningKey( m_pubsub_keypairs[1] );

    // Sender: allow-set filter (production shape; triggers sealing) but the
    // signing key of the OTHER member -- every envelope it publishes claims
    // the other member's identity while its transport from stays its own.
    queueChannel1->SetMembershipFilter( senderAllowFilter );
    queueChannel1->SetGossipSigningKey( m_pubsub_keypairs[1] );

    queueChannel1->RequestQueueOwnership( "NODE_IMPOSTOR" );

    // Bounded negative window (grace-loop pattern): membership would admit the
    // sender; only the key<->from binding check denies the impostor envelope.
    const auto denyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3000 );
    while ( std::chrono::steady_clock::now() < denyDeadline )
    {
        ASSERT_EQ( 0, requestCount2.load() )
            << "Impostor envelope reached the receiver's sink although the from-field binding denies it";
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    EXPECT_EQ( 0, requestCount2.load() );
    {
        std::lock_guard lock( mutex2 );
        EXPECT_EQ( 0, requestedNodeIds2.size() );
    }

    // Positive control: re-wire the sender's OWN key and publish again -- the
    // honestly-sealed request propagates (the mesh and both gates are fine).
    queueChannel1->SetGossipSigningKey( m_pubsub_keypairs[0] );
    queueChannel1->RequestQueueOwnership( "NODE_HONEST" );

    ASSERT_WAIT_FOR_CONDITION( [&requestCount2]() { return requestCount2.load() >= 1; },
                               std::chrono::milliseconds( 5000 ),
                               "Honestly sealed queue message was not received",
                               nullptr );

    {
        std::lock_guard lock( mutex2 );
        EXPECT_EQ( 1, requestedNodeIds2.count( "NODE_HONEST" ) );
        EXPECT_EQ( 0, requestedNodeIds2.count( "NODE_IMPOSTOR" ) );
    }
}
