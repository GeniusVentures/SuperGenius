/**
 * @file       AccountMessenger.cpp
 * @brief      
 * @date       2025-07-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <thread>
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
                                                             InterfaceMethods                           methods )
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
        auto instance = std::shared_ptr<AccountMessenger>(
            new AccountMessenger( std::move( address ), std::move( pubsub ), std::move( methods ) ) );

        instance->subs_acc_future_ = std::move(
            instance->pubsub_->Subscribe( instance->account_comm_topic_,
                                          [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                                              boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
                                          {
                                              if ( auto self = weakptr.lock() )
                                              {
                                                  self->logger_->trace( "Received Account response" );
                                                  self->OnResponse( message );
                                              }
                                          } ) );

        instance->subs_requests_future_ = std::move(
            instance->pubsub_->Subscribe( instance->requests_topic_,
                                          [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                                              boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
                                          {
                                              if ( auto self = weakptr.lock() )
                                              {
                                                  self->logger_->debug( "Received Account request" );
                                                  self->OnRequest( message );
                                              }
                                          } ) );

        return instance;
    }

    AccountMessenger::AccountMessenger( std::string                                address,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        InterfaceMethods                           methods ) :
        address_( std::move( address ) ),
        account_comm_topic_(
            address_ +
            ( ( boost::format( std::string( ACCOUNT_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ) ),
        requests_topic_(
            ( boost::format( std::string( REQUESTS_COMM ) ) % sgns::version::SuperGeniusVersionMajor() ).str() ),
        pubsub_( std::move( pubsub ) ),
        methods_( std::move( methods ) )
    {
    }

    AccountMessenger::~AccountMessenger() {}

    void AccountMessenger::OnRequest( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        if ( message )
        {
            logger_->trace( "{}: Valid message received", __func__ );
            accountComm::AccountMessage acc_msg;
            if ( !acc_msg.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                logger_->warn( "{}: Failed to parse AccountMessage ", __func__ );
                return;
            }

            switch ( acc_msg.payload_case() )
            {
                case accountComm::AccountMessage::kNonceRequest:
                    HandleNonceRequest( acc_msg.nonce_request() );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                    logger_->error( "{}: Unexpected response received ", __func__ );
                    break;
                default:
                    logger_->warn( "{}: Unknown AccountMessage type received on {}", __func__ );
                    break;
            }
        }
    }

    void AccountMessenger::OnResponse( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        if ( message )
        {
            logger_->trace( "{}: Valid message received", __func__ );
            accountComm::AccountMessage acc_msg;
            if ( !acc_msg.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
            {
                logger_->warn( "{}: Failed to parse AccountMessage ", __func__ );
                return;
            }

            switch ( acc_msg.payload_case() )
            {
                case accountComm::AccountMessage::kNonceRequest:
                    logger_->error( "{}: Unexpected response received ", __func__ );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                    HandleNonceResponse( acc_msg.nonce_response() );
                    break;
                default:
                    logger_->warn( "{}: Unknown AccountMessage type received on {}", __func__ );
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

        auto send_ret = SendAccountMessage( envelope, { account_comm_topic_, requests_topic_ } );

        return send_ret;
    }

    outcome::result<uint64_t> AccountMessenger::GetLatestNonce( uint64_t timeout_ms )
    {
        logger_->debug( "[{}] Requesting nonce with timeout {}", address_.substr( 0, 8 ), timeout_ms );

        uint64_t req_id = static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() );

        {
            std::lock_guard lock( nonce_responses_mutex_ );
            nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        OUTCOME_TRY( RequestNonce( req_id ) );

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        const auto silent_time  = std::chrono::milliseconds( 150 ); // Adjustable window

        bool first_seen = false;

        while ( true )
        {
            {
                std::lock_guard lock( nonce_responses_mutex_ );
                auto            it = nonce_responses_.find( req_id );
                if ( it != nonce_responses_.end() && !it->second.empty() )
                {
                    if ( !first_seen )
                    {
                        first_seen                   = true;
                        first_response_time_[req_id] = std::chrono::steady_clock::now();
                    }
                    else
                    {
                        auto elapsed = std::chrono::steady_clock::now() - first_response_time_[req_id];
                        if ( elapsed >= silent_time )
                        {
                            break; // silent window passed
                        }
                    }
                }
            }

            if ( std::chrono::steady_clock::now() - start_time >= full_timeout )
            {
                break; // total timeout reached
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        uint64_t max_nonce = 0;
        {
            std::lock_guard lock( nonce_responses_mutex_ );
            auto            it = nonce_responses_.find( req_id );
            if ( it == nonce_responses_.end() || it->second.empty() )
            {
                nonce_responses_.erase( req_id );
                first_response_time_.erase( req_id );
                logger_->debug( "[{}] No nonce received within timeout", address_.substr( 0, 8 ) );
                return outcome::failure( Error::NONCE_GET_ERROR );
            }
            max_nonce = *it->second.rbegin();
            nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        logger_->debug( "[{}] Returning highest collected nonce: {}", address_.substr( 0, 8 ), max_nonce );
        return max_nonce;
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
            logger_->trace( "Sending account packet to {}", topic );
            pubsub_->Publish( topic, serialized_proto );
        }
        return outcome::success();
    }

    void AccountMessenger::HandleNonceRequest( const accountComm::SignedNonceRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Nonce request", address_.substr( 0, 8 ) );

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
            logger_->error( "Failed to sign NonceResponse" );
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

        logger_->debug( "[{}] Sending back the nonce {} to {}",
                        address_.substr( 0, 8 ),
                        local_nonce,
                        req.requester_address().substr( 0, 8 ) );

        if ( send_ret.has_error() )
        {
            logger_->warn( "Failed to send NonceResponse" );
            return;
        }
    }

    void AccountMessenger::HandleNonceResponse( const accountComm::SignedNonceResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a Nonce response from {} (nonce={})",
                        address_.substr( 0, 8 ),
                        resp.responder_address(),
                        resp.known_nonce() );

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
            logger_->error( "No verify method for NonceResponse" );
            return;
        }
        if ( !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on NonceResponse from {}", resp.responder_address() );
            return;
        }

        // Store in set
        {
            std::lock_guard lock( nonce_responses_mutex_ );
            nonce_responses_[resp.request_id()].insert( resp.known_nonce() );
        }
    }

}
