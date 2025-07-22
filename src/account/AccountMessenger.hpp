/**
 * @file       AccountMessenger.hpp
 * @brief      
 * @date       2025-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <cstdlib>
#include <boost/optional.hpp>
#include "ipfs_pubsub/gossip_pubsub.hpp"

#include "outcome/outcome.hpp"

#include <future>

class AccountMessenger : public std::enable_shared_from_this<AccountMessenger>
{
private:
    const std::string                                address_;
    const std::string                                account_comm_topic_;
    const std::string                                full_node_comm_topic_;
    const bool                                       is_full_node_;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_;
    std::future<libp2p::protocol::Subscription>      subs_acc_future_;
    std::future<libp2p::protocol::Subscription>      subs_full_future_;

    std::optional<uint64_t> current_nonce_request_id_;
    std::promise<uint64_t>  nonce_result_promise_;
    std::mutex              nonce_mutex_;

    struct InterfaceMethods
    {
        std::function<std::vector<uint8_t>( std::vector<uint8_t> data )>                       sign_;
        std::function<bool( std::string address, std::string sig, std::vector<uint8_t> data )> verify_signature_;
        std::function<uint64_t()>                                                              get_local_nonce_;
    };

    InterfaceMethods methods_;

    AccountMessenger( std::string                                      address,
                      std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                      InterfaceMethods                                 methods,
                      bool                                             is_full_node );

    void                  OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &topic );
    outcome::result<void> SendMessage( const base::Buffer &buff );

    void HandleNonceRequest( const account::NonceRequest &req );
    void HandleNonceResponse( const account::NonceResponse &resp );

public:
    static constexpr std::string_view        ACCOUNT_COMM   = ".comm.%02d";
    static constexpr std::string_view        FULL_NODE_COMM = "SGNUSFullNode.comm.%02d";
    static std::shared_ptr<AccountMessenger> New( std::string                                      address,
                                                  std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                                                  InterfaceMethods methods bool                    is_full_node );
    ~AccountMessenger();
    outcome::result<uint64_t> GetLatestNonce( /* timeout maybe?*/ ); //TODO
    outcome::result<void>     RequestNonce();                        //TODO
};

AccountMessenger AccountMessenger::New( std::string                                      address,
                                        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                                        AccountMessenger::InterfaceMethods               methods,
                                        bool                                             is_full_node )
{
    if ( address.empty() )
    {
        return nullptr;
    }
    if ( !pubsub )
    {
        return nullptr;
    }
    if ( !methods.get_local_nonce_ || !methods.sign_ || !methods.verify_signature_ )
    {
        return nullptr;
    }
    auto instance = std::shared_ptr<AccountMessenger>( new AccountMessenger( std::move( address ),
                                                                             std::move( pubsub ),
                                                                             std::move( is_full_node ),
                                                                             std::move( methods ) ) );

    instance->subs_acc_future_ = std::move( instance->pubsub_->Subscribe(
        instance->account_comm_topic_,
        [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
            boost::optional<const GossipPubSub::Message &> message )
        {
            if ( auto self = weakptr.lock() )
            {
                self->m_logger->debug( "Message received on topic: {}", self->account_comm_topic_ );
                self->OnMessage( message, self->account_comm_topic_ );
            }
        } ) );
    if ( instance->is_full_node_ )
    {
        instance->subs_full_future_ = std::move( instance->pubsub_->Subscribe(
            instance->full_node_comm_topic_,
            [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                boost::optional<const GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->m_logger->debug( "Message received on topic: {}", self->full_node_comm_topic_ );
                    self->OnMessage( message, self->full_node_comm_topic_ );
                }
            } ) );
    }
}

AccountMessenger::AccountMessenger( std::string                                      address,
                                    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                                    AccountMessenger::InterfaceMethods               methods,
                                    bool                                             is_full_node ) :
    address_( std::move( address ) ),
    account_comm_topic_(
        address_ +
        ( ( boost::format( std::string( ACCOUNT_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ) ),
    full_node_comm_topic_(
        ( boost::format( std::string( FULL_NODE_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ),
    pubsub_( std::move( pubsub ) ),
    methods_( std::move( methods ) )
{
}

AccountMessenger::~AccountMessenger() {}

void AccountMessenger::OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &topic )
{
    m_logger->trace( "[{}] On Message on topic", topic );
    if ( message )
    {
        m_logger->trace( "[{}] Valid message on topic ", topic );
        accountComm::AccountMessage acc_msg;
        if ( !acc_msg.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
        {
            m_logger->warn( "Failed to parse AccountMessage from topic: {}", topic );
            return;
        }

        switch ( acc_msg.payload_case() )
        {
            case account::AccountMessage::kNonceRequest:
                HandleNonceRequest( acc_msg.nonce_request() );
                break;
            case account::AccountMessage::kNonceResponse:
                HandleNonceResponse( acc_msg.nonce_response() );
                break;
            default:
                m_logger->warn( "Unknown AccountMessage type received on {}", topic );
                break;
        }
    }
}

outcome::result<void> AccountMessenger::RequestNonce( uint64_t req_id )
{
    accountComm::NonceRequest req;
    req.set_requester_address( address_ );
    req.set_request_id( req_id );
    req.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );

    accountComm::AccountMessage envelope;
    *envelope.mutable_nonce_request() = req;

    std::string encoded;
    if ( !envelope.SerializeToString( &encoded ) )
    {
        return outcome::failure( /* error_code */ );
    }

    std::vector<uint8_t>        serialized_vec( encoded.begin(), encoded.end() );
    auto                        signature = methods_.sign_( serialized_vec );
    account::SignedNonceRequest signed_req;
    *signed_req.mutable_data() = req;
    signed_req.set_signature( signature );

    account::AccountMessage envelope;
    *envelope.mutable_nonce_request() = signed_req;

    std::string acc_message_serialized;
    if ( !envelope.SerializeToString( &acc_message_serialized ) )
    {
        current_nonce_request_id_.reset();
        return outcome::failure( /* envelope serialization error */ );
    }
    current_nonce_request_id_ = req_id;
    nonce_result_promise_     = std::promise<uint64_t>();

    base::Buffer buffer( reinterpret_cast<const uint8_t *>( acc_message_serialized.data() ),
                         acc_message_serialized.size() );
    auto         send_res = SendMessage( buffer );
    if ( !send_res )
    {
        current_nonce_request_id_.reset();
        return send_res.as_failure();
    }
    return SendMessage( buffer );
}

outcome::result<uint64_t> AccountMessenger::GetLatestNonce( uint64_t timeout_ms )
{
    std::lock_guard lock( nonce_mutex_ );

    uint64_t req_id = static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() );
    if ( current_nonce_request_id_ )
    {
        return outcome::failure( /* nonce request already in progress error */ );
    }

    OUTCOME_TRY( RequestNonce( req_id ) );

    // Wait for response (blocking) or timeout
    auto fut    = nonce_result_promise_.get_future();
    auto status = fut.wait_for( std::chrono::milliseconds( timeout_ms ) );

    if ( status == std::future_status::ready )
    {
        try
        {
            uint64_t result = fut.get();
            current_nonce_request_id_.reset();
            return result;
        }
        catch ( ... )
        {
            current_nonce_request_id_.reset();
            return outcome::failure( /* set_value failed? */ );
        }
    }

    current_nonce_request_id_.reset();
    return outcome::failure( /* timeout */ );
}

void AccountMessenger::HandleNonceRequest( const account::NonceRequest &req )
{
    const auto &req = signed_req.data();

    // 1. Signature check
    std::string serialized;
    if ( !req.SerializeToString( &serialized ) )
    {
        m_logger->warn( "Failed to serialize NonceRequest for signature check" );
        return;
    }

    std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
    if ( !methods.verify_signature_( req.requester_address(), serialized_vec, signed_req.signature() ) )
    {
        m_logger->warn( "Invalid signature on NonceRequest from {}", req.requester_address() );
        return;
    }

    uint64_t local_nonce = methods.get_local_nonce_();

    // 2. Build NonceResponse
    account::NonceResponse resp;
    resp.set_responder_address( address_ );
    resp.set_request_id( req.request_id() );
    resp.set_known_nonce( local_nonce );
    resp.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );

    std::string resp_serialized;
    if ( !resp.SerializeToString( &resp_serialized ) )
    {
        m_logger->warn( "Failed to serialize NonceResponse" );
        return;
    }

    std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
    std::vector<uint8_t> signature = methods_.sign_( resp_bytes );

    account::SignedNonceResponse signed_resp;
    *signed_resp.mutable_data() = resp;
    signed_resp.set_signature( signature.data(), signature.size() );

    account::AccountMessage msg;
    *msg.mutable_nonce_response() = signed_resp;

    std::string encoded;
    if ( !msg.SerializeToString( &encoded ) )
    {
        m_logger->warn( "Failed to serialize AccountMessage for NonceResponse" );
        return;
    }

    base::Buffer buffer( reinterpret_cast<const uint8_t *>( encoded.data() ), encoded.size() );
    auto         res = SendMessage( buffer );
    if ( !res )
    {
        m_logger->warn( "Failed to send NonceResponse: {}", res.error().message() );
    }
}

void AccountMessenger::HandleNonceResponse( const account::NonceResponse &resp )
{
    const auto &resp = signed_resp.data();

    std::vector<uint8_t> data_vec( resp.cbegin(), resp.cend() );

    if ( !methods.verify_signature_( resp.responder_address(), signed_resp.signature(), data_vec ) )
    {
        m_logger->warn( "Invalid signature on nonce response from {}", resp.responder_address() );
        return;
    }

    std::lock_guard lock( nonce_mutex_ );
    if ( !current_nonce_request_id_ || resp.request_id() != *current_nonce_request_id_ )
    {
        return; // ignore unrelated response
    }

    try
    {
        nonce_result_promise_.set_value( resp.known_nonce() );
    }
    catch ( const std::future_error &e )
    {
        m_logger->trace( "Duplicate or late response, ignoring: {}", e.what() );
    }

    current_nonce_request_id_.reset();
}
