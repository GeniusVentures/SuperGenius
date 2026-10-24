/**
 * @file       account_messenger_test.cpp
 * @brief      Methods-wired AccountMessenger serving tests over real pubsub.
 * @details    SC-5 (19-03) AccountMessenger-after-map-deletion row: a node
 *             whose tx->CID map no longer carries a requested transaction
 *             (the post-prune local miss) must answer the request with a
 *             SIGNED EMPTY BlockResponse -- responder/requester/request_id
 *             set, no block payload -- without crashing or propagating an
 *             error, and the requester's bounded cycle must complete
 *             cleanly. Scope note: HandleBlockLikeRequest's transaction leg
 *             resolves only the CID (the block itself is fetched later via
 *             graphsync by the requester), so there is no second
 *             block-retrieval leg to wire -- the CID-miss leg is the entire
 *             serving surface under audit. The two messengers run on real
 *             loopback GossipPubSub instances; InterfaceMethods carries all
 *             injectable behavior and the responder's sign_ callback is the
 *             capture point for the outgoing response bytes (it receives the
 *             serialized BlockResponse proto). No AccountMessenger production
 *             file is modified.
 * @date       2026-10-23
 * @author     Henrique A Klein (hklein@gnus.ai)
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/host/host.hpp>

#include "account/AccountMessenger.hpp"
#include "account/proto/SGAccountComm.pb.h"
#include "base/sgns_version.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace
{
    /// Fixed test addresses; the account comm topic is derived from these.
    constexpr char kRequesterAddress[] = "am-test-requester-7f3a";
    constexpr char kResponderAddress[] = "am-test-responder-b29c";

    /// The requested hash: any string -- the responder's map lookup misses it.
    constexpr char kPrunedTxHash[] = "0000pruned-tx-hash-0000";
} // namespace

/**
 * @brief One requester messenger (on the fixture's pubsub) plus one
 *        responder messenger on a second real pubsub node.
 */
class AccountMessengerPrunedServingTest : public ::test::CRDTFixture
{
public:
    AccountMessengerPrunedServingTest() : ::test::CRDTFixture( "account_messenger_test" )
    {
    }

    ~AccountMessengerPrunedServingTest() override
    {
        // Messenger workers drain and release their subscriptions BEFORE the
        // pubsubs stop (the fixture destructor stops the requester's pubsub).
        if ( responder_ )
        {
            responder_->Stop();
            responder_.reset();
        }
        if ( requester_ )
        {
            requester_->Stop();
            requester_.reset();
        }
        if ( responder_pubsub_ )
        {
            responder_pubsub_->Stop();
        }
    }

protected:
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> responder_pubsub_;
    std::shared_ptr<sgns::AccountMessenger>          requester_;
    std::shared_ptr<sgns::AccountMessenger>          responder_;
};

/**
 * SC-5: a transaction request for a locally-pruned tx hash is answered with a
 * signed EMPTY BlockResponse. The responder's get_transaction_cid_ returns
 * the post-map-deletion error exactly as the production wiring does on a
 * pruned node; its sign_ callback captures the serialized response bytes it
 * is asked to sign. The requester's full public cycle must complete: it
 * receives the empty response, records it, and settles its callback with a
 * bounded error outcome (empty CID set) -- no crash, no hang, no error
 * propagation to the responder.
 */
TEST_F( AccountMessengerPrunedServingTest, PrunedTxRequestIsAnsweredWithEmptySignedBlockResponse )
{
    using sgns::AccountMessenger;

    // The responder node: real pubsub on loopback, advertised so the
    // requester can dial it (the erase-suite address-advertisement lesson).
    const std::string responder_key_path = ( base_path / "responder_key" ).string();
    auto              responder_key      = sgns::crdt::KeyPairFileStorage( responder_key_path ).GetKeyPair();
    ASSERT_TRUE( responder_key.has_value() );
    responder_pubsub_      = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( responder_key.value() );
    const auto start_error = responder_pubsub_->Start( 0, {}, "0.0.0.0", { "127.0.0.1" } ).get();
    ASSERT_FALSE( start_error ) << "responder pubsub failed to start: " << start_error.message();

    // One-direction dial (requester -> responder): two dials inside one
    // gossip heartbeat collide on the shared source tuple.
    pubs_->AddPeers( { responder_pubsub_->GetInterfaceAddress() } );
    auto       requester_host = pubs_->GetHost();
    const auto responder_info = responder_pubsub_->GetHost()->getPeerInfo();
    ASSERT_WAIT_FOR_CONDITION(
        [&] { return requester_host->connectedness( responder_info ) == libp2p::Host::Connectedness::CONNECTED; },
        std::chrono::seconds( 10 ),
        "messenger test nodes form one connected libp2p pair",
        nullptr );

    // Responder capture state: the sign_ callback receives the serialized
    // BlockResponse bytes it is asked to sign.
    std::mutex                                captured_response_mutex;
    std::optional<accountComm::BlockResponse> captured_response;
    std::atomic<int>                          captured_count{ 0 };
    std::atomic<int>                          tx_lookup_calls{ 0 };

    // Requester callback state: the public cycle settles it exactly once.
    std::mutex                                  callback_mutex;
    std::optional<outcome::result<std::string>> callback_result;

    AccountMessenger::InterfaceMethods requester_methods;
    requester_methods.get_local_nonce_ = []( const std::string & ) -> outcome::result<uint64_t>
    { return outcome::success( uint64_t{ 0 } ); };
    requester_methods.sign_ = []( std::vector<uint8_t> data ) -> outcome::result<std::vector<uint8_t>>
    { return outcome::success( std::move( data ) ); };
    requester_methods.verify_signature_ =
        []( const std::string &, const std::string &, std::vector<uint8_t> ) -> outcome::result<bool>
    { return outcome::success( true ); };

    AccountMessenger::InterfaceMethods responder_methods;
    responder_methods.get_local_nonce_ = []( const std::string & ) -> outcome::result<uint64_t>
    { return outcome::success( uint64_t{ 0 } ); };
    responder_methods.sign_ = [&]( std::vector<uint8_t> data ) -> outcome::result<std::vector<uint8_t>>
    {
        accountComm::BlockResponse response;
        if ( response.ParseFromArray( data.data(), static_cast<int>( data.size() ) ) &&
             !response.responder_address().empty() )
        {
            std::lock_guard lock( captured_response_mutex );
            captured_response = response;
        }
        captured_count.fetch_add( 1 );
        return outcome::success( std::move( data ) );
    };
    responder_methods.verify_signature_ =
        []( const std::string &, const std::string &, std::vector<uint8_t> ) -> outcome::result<bool>
    { return outcome::success( true ); };
    // The audited leg: the post-map-deletion local miss this node would see
    // after pruning -- an error result, exactly as the production wiring
    // surfaces a pruned/absent tx->CID entry.
    responder_methods.get_transaction_cid_ = [&]( const std::string &tx_hash ) -> outcome::result<std::string>
    {
        EXPECT_EQ( tx_hash, std::string( kPrunedTxHash ) );
        tx_lookup_calls.fetch_add( 1 );
        return outcome::failure( std::errc::no_such_file_or_directory );
    };

    requester_ = AccountMessenger::New( kRequesterAddress, pubs_, std::move( requester_methods ) );
    responder_ = AccountMessenger::New( kResponderAddress, responder_pubsub_, std::move( responder_methods ) );
    ASSERT_TRUE( requester_ );
    ASSERT_TRUE( responder_ );

    // The public cycle: request the pruned tx from the network.
    ASSERT_TRUE( requester_
                     ->RequestTransaction( std::chrono::milliseconds( 10000 ),
                                           kPrunedTxHash,
                                           [&]( outcome::result<std::string> result )
                                           {
                                               std::lock_guard lock( callback_mutex );
                                               if ( !callback_result.has_value() )
                                               {
                                                   callback_result = std::move( result );
                                               }
                                           } )
                     .has_value() );

    // The responder answered: it signed exactly one BlockResponse.
    ASSERT_TRUE(
        ::waitForCondition( [&] { return captured_count.load() >= 1; }, std::chrono::milliseconds( 15000 ), nullptr ) )
        << "responder never signed a BlockResponse for the pruned-tx request";
    ASSERT_TRUE( ::waitForCondition(
        [&]
        {
            std::lock_guard lock( callback_mutex );
            return callback_result.has_value();
        },
        std::chrono::milliseconds( 15000 ),
        nullptr ) )
        << "requester cycle never settled its callback";

    {
        std::lock_guard lock( captured_response_mutex );
        ASSERT_TRUE( captured_response.has_value() );
        // Presence: the envelope names both parties and the request.
        EXPECT_EQ( captured_response->responder_address(), std::string( kResponderAddress ) );
        EXPECT_EQ( captured_response->requester_address(), std::string( kRequesterAddress ) );
        EXPECT_NE( captured_response->request_id(), 0u );
        EXPECT_NE( captured_response->timestamp(), 0u );
        // Absence: no tx/block payload -- the graceful empty answer.
        EXPECT_EQ( captured_response->blocks_size(), 0 );
    }

    // The lookup leg fired exactly once, for the requested hash; no retry
    // storm at this layer.
    EXPECT_EQ( tx_lookup_calls.load(), 1 );

    // The requester's bounded outcome: the empty response is recorded (the
    // cycle completed rather than timing out) and settles as a clean error
    // with no CID -- graceful degradation, not a crash or hang.
    {
        std::lock_guard lock( callback_mutex );
        ASSERT_TRUE( callback_result.has_value() );
        EXPECT_TRUE( callback_result->has_error() );
    }
}
