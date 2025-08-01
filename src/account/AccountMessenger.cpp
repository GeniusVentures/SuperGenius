/**
 * @file       AccountMessenger.cpp
 * @brief      
 * @date       2025-07-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/format.hpp>
#include "AccountMessenger.hpp"
#include "base/sgns_version.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, AccountMessenger::Error, e )
{
    using AccountCommError = sgns::AccountMessenger::Error;
    switch ( e )
    {
        case AccountCommError::PROTO_DESERIALIZATION:
            return "Error in protobuf data deserialization";
        case AccountCommError::PROTO_SERIALIZATION:
            return "Error in protobuf data serialization";
        case AccountCommError::NONCE_REQUEST_IN_PROGRESS:
            return "Nonce request already in progress";
        case AccountCommError::NONCE_FUTURE_ERROR:
            return "Error in setting the future value of the nonce";
        case AccountCommError::NONCE_GET_ERROR:
            return "Nonce couldn't be fetched";
    }
    return "Unknown error";
}

namespace sgns
{
    std::shared_ptr<AccountMessenger> AccountMessenger::New( std::string                                address,
                                                             std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                             InterfaceMethods                           methods,
                                                             bool                                       is_full_node )
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
                                                                                 std::move( methods ),
                                                                                 std::move( is_full_node ) ) );

        instance->subs_acc_future_ = std::move( instance->pubsub_->Subscribe(
            instance->account_comm_topic_,
            [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->logger_->trace( "Message received on topic: {}", self->account_comm_topic_ );
                    self->OnMessage( message, self->account_comm_topic_ );
                }
            } ) );
        if ( instance->is_full_node_ )
        {
            instance->subs_full_future_ = std::move( instance->pubsub_->Subscribe(
                instance->full_node_comm_topic_,
                [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                    boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->logger_->debug( "Message received on topic: {}", self->full_node_comm_topic_ );
                        self->OnMessage( message, self->full_node_comm_topic_ );
                    }
                } ) );
        }
        return instance;
    }

    AccountMessenger::AccountMessenger( std::string                                address,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        InterfaceMethods                           methods,
                                        bool                                       is_full_node ) :
        address_( std::move( address ) ),
        account_comm_topic_(
            address_ +
            ( ( boost::format( std::string( ACCOUNT_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ) ),
        full_node_comm_topic_(
            ( boost::format( std::string( FULL_NODE_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ),
        is_full_node_( std::move( is_full_node ) ),
        pubsub_( std::move( pubsub ) ),
        methods_( std::move( methods ) )
    {
    }

    AccountMessenger::~AccountMessenger() {}

    void AccountMessenger::OnMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message,
                                      const std::string                                          &topic )
    {
        if ( message )
        {
            logger_->trace( "[{}] Valid message on topic ", topic, is_full_node_ );
            accountComm::AccountMessage acc_msg;
            if ( !acc_msg.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                logger_->warn( "Failed to parse AccountMessage from topic: {}", topic );
                return;
            }

            switch ( acc_msg.payload_case() )
            {
                case accountComm::AccountMessage::kNonceRequest:
                    HandleNonceRequest( acc_msg.nonce_request() );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                    HandleNonceResponse( acc_msg.nonce_response() );
                    break;
                default:
                    logger_->warn( "Unknown AccountMessage type received on {}", topic );
                    break;
            }
        }
    }

    outcome::result<void> AccountMessenger::RequestNonce( uint64_t req_id )
    {
        accountComm::NonceRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( req_id );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        std::string encoded;
        if ( !req.SerializeToString( &encoded ) )
        {
            return outcome::failure( Error::PROTO_DESERIALIZATION );
        }

        std::vector<uint8_t> serialized_vec( encoded.begin(), encoded.end() );
        OUTCOME_TRY( auto &&signature, methods_.sign_( serialized_vec ) );
        accountComm::SignedNonceRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_nonce_request() = signed_req;
        current_nonce_request_id_         = req_id;

        auto send_ret = SendAccountMessage( envelope, { account_comm_topic_, full_node_comm_topic_ } );

        if ( send_ret.has_error() )
        {
            current_nonce_request_id_.reset();
        }

        return send_ret;
    }

    outcome::result<uint64_t> AccountMessenger::GetLatestNonce( uint64_t timeout_ms )
    {
        logger_->debug( "[{} - full: {}] Requesting nonce", address_.substr( 0, 8 ), is_full_node_ );

        uint64_t req_id = static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() );

        std::future<uint64_t> fut;
        {
            std::lock_guard lock( nonce_mutex_ );
            if ( current_nonce_request_id_ )
            {
                logger_->warn( "[{} - full: {}] Nonce request already in progress with ID: {}",
                               address_.substr( 0, 8 ),
                               is_full_node_,
                               *current_nonce_request_id_ );
                return outcome::failure( Error::NONCE_REQUEST_IN_PROGRESS );
            }

            nonce_result_promise_ = std::promise<uint64_t>{};
            fut                   = nonce_result_promise_.get_future();

        }


        OUTCOME_TRY( RequestNonce( req_id ) );

        logger_->debug( "[{} - full: {}] Nonce request sent with ID: {}, waiting for response...",
                        address_.substr( 0, 8 ),
                        is_full_node_,
                        req_id );
        auto status = fut.wait_for( std::chrono::milliseconds( timeout_ms ) );


        {
            std::lock_guard lock( nonce_mutex_ );

            if ( status == std::future_status::ready )
            {
                try
                {
                    uint64_t result = fut.get();
                    logger_->debug( "[{} - full: {}] Successfully received nonce: {}",
                                    address_.substr( 0, 8 ),
                                    is_full_node_,
                                    result );
                    current_nonce_request_id_.reset();
                    return result;
                }
                catch ( const std::future_error &e )
                {
                    logger_->error( "[{} - full: {}] Future error when getting nonce: {} (code: {})",
                                    address_.substr( 0, 8 ),
                                    is_full_node_,
                                    e.what(),
                                    static_cast<int>( e.code().value()) );
                    current_nonce_request_id_.reset();
                    return outcome::failure( Error::NONCE_FUTURE_ERROR );
                }
            }

            logger_->error( "[{} - full: {}] Nonce request timed out after {}ms",
                            address_.substr( 0, 8 ),
                            is_full_node_,
                            timeout_ms );
            current_nonce_request_id_.reset();
            return outcome::failure( Error::NONCE_GET_ERROR );
        }
    }

    outcome::result<void> AccountMessenger::SendAccountMessage( const accountComm::AccountMessage &msg,
                                                                std::set<std::string>              topics )
    {
        size_t               size = msg.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        if ( !msg.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            logger_->warn( "Failed to serialize AccountMessage for NonceResponse" );
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }
        for ( auto &topic : topics )
        {
            pubsub_->Publish( topic, serialized_proto );
        }
        return outcome::success();
    }

    void AccountMessenger::HandleNonceRequest( const accountComm::SignedNonceRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{} - {}] Received a Nonce request", address_.substr( 0, 8 ), is_full_node_ );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->warn( "Failed to serialize NonceRequest for signature check" );
            return;
        }

        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );

        auto verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() )
        {
            logger_->error( "No verify method" );
            return;
        }
        if ( !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on NonceRequest from {}", req.requester_address() );
            return;
        }

        auto local_nonce_result = methods_.get_local_nonce_( req.requester_address() );

        if ( local_nonce_result.has_error() )
        {
            logger_->debug( "[{}] I don't have the nonce for the address {}",
                            address_.substr( 0, 8 ),
                            req.requester_address() );

            return;
        }
        uint64_t local_nonce = local_nonce_result.value();

        accountComm::NonceResponse resp;

        resp.set_responder_address( address_ );
        resp.set_requester_address( req.requester_address() );
        resp.set_request_id( req.request_id() );
        resp.set_known_nonce( local_nonce );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->error( "Failed to serialize NonceResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
        auto                 signature_result = methods_.sign_( resp_bytes );
        if ( signature_result.has_error() )
        {
            logger_->error( "Failed to send NonceResponse" );
            return;
        }
        auto signature = signature_result.value();

        accountComm::SignedNonceResponse signed_resp;
        *signed_resp.mutable_data() = resp;
        signed_resp.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage msg;
        *msg.mutable_nonce_response() = signed_resp;
        auto send_ret                 = SendAccountMessage(
            msg,
            { ( req.requester_address() +
                ( ( boost::format( std::string( ACCOUNT_COMM ) ) % sgns::version::SuperGeniusVersionMajor() )
                      .str() ) ) } );

        logger_->debug( "[{} - {}] Sending back the nonce {}", address_.substr( 0, 8 ), is_full_node_, local_nonce );

        if ( send_ret.has_error() )
        {
            logger_->warn( "Failed to send NonceResponse" );
            return;
        }
    }

    void AccountMessenger::HandleNonceResponse( const accountComm::SignedNonceResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{} - {}] Received a Nonce response from {}",
                        address_.substr( 0, 8 ),
                        is_full_node_,
                        resp.responder_address() );

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->warn( "Failed to serialize NonceResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );

        auto verify_signature_result = methods_.verify_signature_( resp.responder_address(),
                                                                   signed_resp.signature(),
                                                                   data_vec );
        if ( verify_signature_result.has_error() )
        {
            logger_->error( "No verify method" );
            return;
        }
        if ( !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on nonce response from {}", resp.responder_address() );
            return;
        }

        if ( resp.requester_address() != address_ )
        {
            logger_->debug( "[{} - full node: {}] Response for an address that is not me, I don't care",
                            address_.substr( 0, 8 ),
                            is_full_node_ );
            return;
        }

        std::lock_guard lock( nonce_mutex_ );

        if ( !current_nonce_request_id_ )
        {
            logger_->debug( "[{} - full node: {}] No active nonce request", address_.substr( 0, 8 ), is_full_node_ );
            return;
        }

        if ( resp.request_id() != *current_nonce_request_id_ )
        {
            logger_->debug( "[{} - full node: {}] Unrelated response - expected: {}, got: {}",
                            address_.substr( 0, 8 ),
                            is_full_node_,
                            *current_nonce_request_id_,
                            resp.request_id() );
            return; 
        }

        logger_->debug( "[{} - full node: {}] Setting the nonce value: {}",
                        address_.substr( 0, 8 ),
                        is_full_node_,
                        resp.known_nonce() );

        try
        {
            nonce_result_promise_.set_value( resp.known_nonce() );
            logger_->debug( "[{} - full node: {}] Successfully set nonce promise value",
                            address_.substr( 0, 8 ),
                            is_full_node_ );
        }
        catch ( const std::future_error &e )
        {
            logger_->error( "Future error when setting nonce value: {} (code: {})",
                            e.what(),
                            static_cast<int>( e.code().value() ) );
            return;
        }
        catch ( const std::exception &e )
        {
            logger_->error( "Exception when setting nonce value: {}", e.what() );
            return;
        }

        current_nonce_request_id_.reset();
    }
}
