/**
 * @file       AccountMessenger.cpp
 * @brief      
 * @date       2025-07-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <thread>
#include <random>
#include <boost/format.hpp>
#include "AccountMessenger.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher/hasher_impl.hpp"

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
        case AccountCommError::NONCE_GET_ERROR:
            return "Nonce couldn't be fetched";
        case AccountCommError::GENESIS_REQUEST_ERROR:
            return "Genesis request failed";
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

        instance->subs_acc_future_ = std::move( instance->pubsub_->Subscribe(
            instance->account_comm_topic_,
            [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->logger_->trace( "[{}] Received Account response", self->address_.substr( 0, 8 ) );
                    self->OnResponse( message );
                }
            } ) );
        instance->logger_->debug( "[{}] Subscribed to Account topic {}",
                                  instance->address_.substr( 0, 8 ),
                                  instance->account_comm_topic_ );

        instance->subs_requests_future_ = std::move( instance->pubsub_->Subscribe(
            instance->requests_topic_,
            [weakptr( std::weak_ptr<AccountMessenger>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->logger_->debug( "[{}] Received Account request", self->address_.substr( 0, 8 ) );
                    self->OnRequest( message );
                }
            } ) );
        instance->logger_->debug( "[{}] Subscribed to Requests topic {}",
                                  instance->address_.substr( 0, 8 ),
                                  instance->requests_topic_ );

        return instance;
    }

    AccountMessenger::AccountMessenger( std::string                                address,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        InterfaceMethods                           methods ) :
        address_( std::move( address ) ),
        account_comm_topic_( address_ + std::string( ACCOUNT_COMM ) + sgns::version::GetNetAndVersionAppendix() ),
        requests_topic_( std::string( REQUESTS_COMM ) + sgns::version::GetNetAndVersionAppendix() ),
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
                logger_->error( "{}: Failed to parse AccountMessage ", __func__ );
                return;
            }

            switch ( acc_msg.payload_case() )
            {
                case accountComm::AccountMessage::kNonceRequest:
                    HandleNonceRequest( acc_msg.nonce_request() );
                    break;
                case accountComm::AccountMessage::kGenesisRequest:
                    HandleGenesisRequest( acc_msg.genesis_request() );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                case accountComm::AccountMessage::kGenesisResponse:
                    logger_->error( "{}: Unexpected response received ", __func__ );
                    break;
                default:
                    logger_->error( "{}: Unknown AccountMessage type received on {}", __func__ );
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
                logger_->error( "{}: Failed to parse AccountMessage ", __func__ );
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
                case accountComm::AccountMessage::kGenesisResponse:
                    HandleGenesisResponse( acc_msg.genesis_response() );
                    break;
                default:
                    logger_->error( "{}: Unknown AccountMessage type received on {}", __func__ );
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

        auto send_ret = SendAccountMessage( envelope, { requests_topic_ } );

        return send_ret;
    }

    outcome::result<uint64_t> AccountMessenger::GetLatestNonce( uint64_t timeout_ms, uint64_t silent_time_ms )
    {
        // Generate a random value

        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        // Concatenate address and random value
        std::string to_hash = address_ + std::to_string( random_value );

        // Use HasherImpl to hash the concatenated string
        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( to_hash.data() ), to_hash.size() ) );

        // Use the first 8 bytes of the hash as req_id
        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        logger_->debug( "[{}] Requesting nonce with timeout {} and req_id {} ",
                        address_.substr( 0, 8 ),
                        timeout_ms,
                        req_id );

        {
            std::lock_guard lock( nonce_responses_mutex_ );
            nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        OUTCOME_TRY( RequestNonce( req_id ) );

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        const auto silent_time  = std::chrono::milliseconds( silent_time_ms );

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
                logger_->debug( "[{}] No nonce received within timeout for the req_rw {}",
                                address_.substr( 0, 8 ),
                                req_id );
                return outcome::failure( Error::NONCE_GET_ERROR );
            }
            max_nonce = *it->second.rbegin();
            nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        logger_->debug( "[{}] Returning highest collected nonce for req_id {}: {}",
                        address_.substr( 0, 8 ),
                        req_id,
                        max_nonce );
        return max_nonce;
    }

    void AccountMessenger::RequestGenesis( GenesisCallback callback )
    {
        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        std::string              to_hash = address_ + std::to_string( random_value );
        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( to_hash.data() ), to_hash.size() ) );

        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        logger_->debug( "[{}] Requesting genesis block with req_id {}", address_.substr( 0, 8 ), req_id );

        {
            std::lock_guard lock( genesis_callbacks_mutex_ );
            genesis_callbacks_[req_id] = std::move( callback );
        }

        auto request_result = RequestGenesisBlock( req_id );
        if ( request_result.has_error() )
        {
            std::lock_guard lock( genesis_callbacks_mutex_ );
            genesis_callbacks_.erase( req_id );
            logger_->error( "[{}] Failed to request genesis block", address_.substr( 0, 8 ) );
        }
    }

    outcome::result<void> AccountMessenger::RequestGenesisBlock( uint64_t req_id )
    {
        accountComm::GenesisRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( req_id );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        std::string encoded;
        if ( !req.SerializeToString( &encoded ) )
        {
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }

        std::vector<uint8_t> serialized_vec( encoded.begin(), encoded.end() );
        OUTCOME_TRY( auto &&signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedGenesisRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_genesis_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    void AccountMessenger::HandleGenesisRequest( const accountComm::SignedGenesisRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Genesis request req_id {}", address_.substr( 0, 8 ), req.request_id() );

        // verify genesis request signature (keep your existing checks)
        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize GenesisRequest for signature check" );
            return;
        }
        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on GenesisRequest from {}", req.requester_address() );
            return;
        }

        // get blocks with routing info (CID, peerID, addresses)
        auto genesis_cid_result = methods_.get_genesis_cid_();
        if ( genesis_cid_result.has_error() )
        {
            logger_->debug( "[{}] I don't have the genesis block share", address_.substr( 0, 8 ) );
            return;
        }

        // build generic BlockResponse (contains repeated BlockInfo with CIDs)
        accountComm::BlockResponse resp;
        resp.set_responder_address( address_ );
        resp.set_requester_address( req.requester_address() );
        resp.set_request_id( req.request_id() );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        for ( const auto &b : available_blocks )
        {
            auto *genesis_info = resp.add_blocks();
            genesis_info->set_cid( genesis_cid_result.value() );
            auto peer_info_result = pubsub_->GetHost()->getPeerInfo();
            if ( peer_info_result.has_error() )
            {
                logger_->error( "Failed to get peer info for GenesisResponse" );
                return;
            }
            auto peer_info = peer_info_res.value();
            genesis_info->set_peer_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );
            auto mas = peer_info.addresses;
            for ( auto &address : mas )
            {
                genesis_info->add_addresses( address.getStringAddress() );
                logger_->info( "Address Broadcast: {}", address.getStringAddress() );
            }
            if ( genesis_info->addresses_size() <= 0 )
            {  
                logger_->error( "No addresses found for GenesisResponse." );
                return;
            }
        }

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->error( "Failed to serialize GenesisResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
        OUTCOME_TRY( auto &&signature, methods_.sign_( resp_bytes ) );

        accountComm::SignedBlockResponse signed_resp;
        *signed_resp.mutable_data() = resp;
        signed_resp.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage msg;
        *msg.mutable_block_response() = signed_resp;

        auto account_topic = req.requester_address() + std::string( ACCOUNT_COMM ) +
                             sgns::version::GetNetAndVersionAppendix();

        auto send_ret = SendAccountMessage( msg, { account_topic } );
        if ( send_ret.has_error() )
        {
            logger_->error( "Failed to send BlockResponse" );
        }
        else
        {
            logger_->debug( "[{}] Sent {} Genesis BlockInfo entries to {} (req_id {})",
                            address_.substr( 0, 8 ),
                            resp.blocks_size(),
                            req.requester_address().substr( 0, 8 ),
                            resp.request_id() );
        }
    }

    void AccountMessenger::HandleGenesisResponse( const accountComm::SignedGenesisResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a Genesis response from {} with req_id {}",
                        address_.substr( 0, 8 ),
                        resp.responder_address().substr( 0, 8 ),
                        resp.request_id() );

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize GenesisResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );

        auto verify_signature_result = methods_.verify_signature_( resp.responder_address(),
                                                                   signed_resp.signature(),
                                                                   data_vec );
        if ( verify_signature_result.has_error() )
        {
            logger_->error( "No verify method for GenesisResponse" );
            return;
        }
        if ( !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on GenesisResponse from {}", resp.responder_address() );
            return;
        }

        {
            std::lock_guard lock( genesis_callbacks_mutex_ );
            auto            it = genesis_callbacks_.find( resp.request_id() );
            if ( it != genesis_callbacks_.end() )
            {
                auto callback = std::move( it->second );
                genesis_callbacks_.erase( it );
                callback( resp.genesis_block() );
            }
        }
    }

    outcome::result<void> AccountMessenger::SendAccountMessage( const accountComm::AccountMessage &msg,
                                                                const std::set<std::string>       &topics )
    {
        size_t               size = msg.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        if ( !msg.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            logger_->error( "Failed to serialize AccountMessage for NonceResponse" );
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }
        for ( auto &topic : topics )
        {
            logger_->debug( "Sending account packet to {}", topic );
            pubsub_->Publish( topic, serialized_proto );
        }
        return outcome::success();
    }

    void AccountMessenger::HandleNonceRequest( const accountComm::SignedNonceRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Nonce request req_id {}", address_.substr( 0, 8 ), req.request_id() );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize NonceRequest for signature check" );
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
        auto account_topic            = req.requester_address() + std::string( ACCOUNT_COMM ) +
                             sgns::version::GetNetAndVersionAppendix();

        auto send_ret = SendAccountMessage( msg, { account_topic } );

        logger_->debug( "[{}] Sending back the nonce {} to {} with req_id {}",
                        address_.substr( 0, 8 ),
                        local_nonce,
                        req.requester_address().substr( 0, 8 ),
                        resp.request_id() );

        if ( send_ret.has_error() )
        {
            logger_->error( "Failed to send NonceResponse" );
        }
    }

    void AccountMessenger::HandleNonceResponse( const accountComm::SignedNonceResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a Nonce response from {} (nonce={}) and req_id {}",
                        address_.substr( 0, 8 ),
                        resp.responder_address(),
                        resp.known_nonce(),
                        resp.request_id() );

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize NonceResponse for signature check" );
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
