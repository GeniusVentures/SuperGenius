/**
 * @file       AccountMessenger.cpp
 * @brief      
 * @date       2025-07-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
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
                    self->logger_->debug( "Message received on topic: {}", self->account_comm_topic_ );
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
        logger_->trace( "[{}] On Message on topic", topic );
        if ( message )
        {
            logger_->trace( "[{}] Valid message on topic ", topic );
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

        std::vector<uint8_t>            serialized_vec( encoded.begin(), encoded.end() );
        auto                            signature = methods_.sign_( serialized_vec );
        accountComm::SignedNonceRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_nonce_request() = signed_req;

        auto send_ret = SendAccountMessage( envelope );

        if ( send_ret.has_error() )
        {
            current_nonce_request_id_.reset();
        }

        return send_ret;
    }

    outcome::result<uint64_t> AccountMessenger::GetLatestNonce( uint64_t timeout_ms )
    {
        std::lock_guard lock( nonce_mutex_ );

        uint64_t req_id = static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() );
        if ( current_nonce_request_id_ )
        {
            return outcome::failure( Error::NONCE_REQUEST_IN_PROGRESS );
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
                return outcome::failure( Error::NONCE_FUTURE_ERROR );
            }
        }

        current_nonce_request_id_.reset();
        return outcome::failure( Error::NONCE_GET_ERROR );
    }

    outcome::result<void> AccountMessenger::SendAccountMessage( const accountComm::AccountMessage &msg )
    {
        size_t               size = msg.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        if ( !msg.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            logger_->warn( "Failed to serialize AccountMessage for NonceResponse" );
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }
        pubsub_->Publish( account_comm_topic_, serialized_proto );
        return outcome::success();
    }

    void AccountMessenger::HandleNonceRequest( const accountComm::SignedNonceRequest &signed_req )
    {
        const auto &req = signed_req.data();

        // 1. Signature check
        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->warn( "Failed to serialize NonceRequest for signature check" );
            return;
        }

        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        if ( !methods_.verify_signature_( req.requester_address(), signed_req.signature(), serialized_vec ) )
        {
            logger_->warn( "Invalid signature on NonceRequest from {}", req.requester_address() );
            return;
        }

        uint64_t local_nonce = methods_.get_local_nonce_();

        // 2. Build NonceResponse
        accountComm::NonceResponse resp;

        resp.set_responder_address( address_ );
        resp.set_request_id( req.request_id() );
        resp.set_known_nonce( local_nonce );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->warn( "Failed to serialize NonceResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
        std::vector<uint8_t> signature = methods_.sign_( resp_bytes );

        accountComm::SignedNonceResponse signed_resp;
        *signed_resp.mutable_data() = resp;
        signed_resp.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage msg;
        *msg.mutable_nonce_response() = signed_resp;
        auto send_ret                 = SendAccountMessage( msg );

        if ( send_ret.has_error() )
        {
            logger_->warn( "Failed to send NonceResponse" );
            return;
        }
    }

    void AccountMessenger::HandleNonceResponse( const accountComm::SignedNonceResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->warn( "Failed to serialize NonceResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );

        if ( !methods_.verify_signature_( resp.responder_address(), signed_resp.signature(), data_vec ) )
        {
            logger_->warn( "Invalid signature on nonce response from {}", resp.responder_address() );
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
            logger_->trace( "Duplicate or late response, ignoring: {}", e.what() );
        }

        current_nonce_request_id_.reset();
    }
}
