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

    void AccountMessenger::RegisterBlockResponseHandler( BlockResponseHandler handler )
    {
        std::lock_guard lock( global_handler_mutex_ );
        global_block_handler_ = std::move( handler );
    }

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
                case accountComm::AccountMessage::kBlockRequest:
                    HandleBlockRequest( acc_msg.block_request() );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                case accountComm::AccountMessage::kBlockResponse:
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
                case accountComm::AccountMessage::kBlockResponse:
                    HandleBlockResponse( acc_msg.block_response() );
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

    void AccountMessenger::RequestGenesis()
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

        auto request_result = RequestBlock( req_id, 0 );
        if ( request_result.has_error() )
        {
            logger_->error( "[{}] Failed to request genesis block", address_.substr( 0, 8 ) );
        }
    }

    outcome::result<void> AccountMessenger::RequestBlock( uint64_t req_id, uint8_t block_index )
    {
        accountComm::BlockRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( req_id );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        req.set_block_index( static_cast<uint32_t>( block_index ) );

        std::string encoded;
        if ( !req.SerializeToString( &encoded ) )
        {
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }

        std::vector<uint8_t> serialized_vec( encoded.begin(), encoded.end() );
        OUTCOME_TRY( auto &&signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedBlockRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_block_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    void AccountMessenger::HandleBlockRequest( const accountComm::SignedBlockRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Block request req_id {} index {}",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.block_index() );

        // verify request signature
        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize BlockRequest for signature check" );
            return;
        }
        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on BlockRequest from {}", req.requester_address() );
            return;
        }

        // get block CID for requested index
        auto cid_result = methods_.get_block_cid_( static_cast<uint8_t>( req.block_index() ) );
        if ( cid_result.has_error() )
        {
            logger_->debug( "[{}] I don't have the block share for index {}",
                            address_.substr( 0, 8 ),
                            req.block_index() );
            return;
        }

        // build BlockResponse
        accountComm::BlockResponse resp;
        resp.set_responder_address( address_ );
        resp.set_requester_address( req.requester_address() );
        resp.set_request_id( req.request_id() );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        auto *info = resp.add_blocks();
        info->set_cid( cid_result.value() );
        auto peer_info = pubsub_->GetHost()->getPeerInfo();
        info->set_peer_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );
        auto mas = peer_info.addresses;
        for ( auto &address : mas )
        {
            info->add_addresses( address.getStringAddress() );
            logger_->info( "Address Broadcast: {}", address.getStringAddress() );
        }
        if ( info->addresses_size() <= 0 )
        {
            logger_->error( "No addresses found for BlockResponse." );
            return;
        }

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->error( "Failed to serialize BlockResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );

        auto signature_res = methods_.sign_( resp_bytes );
        if ( signature_res.has_error() )
        {
            logger_->error( "Failed to sign BlockResponse" );
            return;
        }
        auto signature = signature_res.value();

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
            logger_->debug( "[{}] Sent {} BlockInfo entries to {} (req_id {})",
                            address_.substr( 0, 8 ),
                            resp.blocks_size(),
                            req.requester_address().substr( 0, 8 ),
                            resp.request_id() );
        }
    }

    void AccountMessenger::HandleBlockResponse( const accountComm::SignedBlockResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a Block response from {} with {} blocks and req_id {}",
                        address_.substr( 0, 8 ),
                        resp.responder_address().substr( 0, 8 ),
                        resp.blocks_size(),
                        resp.request_id() );

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize BlockResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );

        auto verify_signature_result = methods_.verify_signature_( resp.responder_address(),
                                                                   signed_resp.signature(),
                                                                   data_vec );
        if ( verify_signature_result.has_error() )
        {
            logger_->error( "No verify method for BlockResponse" );
            return;
        }
        if ( !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on BlockResponse from {}", resp.responder_address() );
            return;
        }

        // Store block CIDs for any waiting RequestAccountCreation caller
        {
            std::lock_guard lock( block_responses_mutex_ );
            auto           &set_ref = block_responses_[resp.request_id()];
            if ( set_ref.empty() )
            {
                block_first_response_time_[resp.request_id()] = std::chrono::steady_clock::now();
            }
            for ( const auto &b : resp.blocks() )
            {
                set_ref.insert( b.cid() );
            }
        }

        // Call global handler for each BlockInfo if registered
        {
            std::lock_guard lock( global_handler_mutex_ );
            if ( global_block_handler_ )
            {
                for ( const auto &block_info : resp.blocks() )
                {
                    std::string first_address;
                    if ( block_info.addresses_size() > 0 )
                    {
                        first_address = block_info.addresses( 0 );
                    }

                    global_block_handler_( block_info.cid(), block_info.peer_id(), first_address );
                }
            }
            else
            {
                logger_->debug( "[{}] No global block response handler registered", address_.substr( 0, 8 ) );
            }
        }
    }

    outcome::result<void> AccountMessenger::RequestAccountCreation( uint64_t                           timeout_ms,
                                                                    std::function<void( std::string )> callback )
    {
        // create request id similarly to GetLatestNonce / RequestGenesis
        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        std::string              to_hash = address_ + std::to_string( random_value );
        sgns::crypto::HasherImpl hasher;
        auto                     hash = hasher.sha2_256(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( to_hash.data() ), to_hash.size() ) );

        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        logger_->debug( "[{}] Requesting account creation with req_id {} and timeout {}",
                        address_.substr( 0, 8 ),
                        req_id,
                        timeout_ms );

        {
            std::lock_guard lock( block_responses_mutex_ );
            block_responses_.erase( req_id );
            block_first_response_time_.erase( req_id );
        }

        // request block index 1 (account creation block)
        OUTCOME_TRY( RequestBlock( req_id, 1 ) );

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        const auto silent_time  = std::chrono::milliseconds( 150 ); // same silent window as GetLatestNonce

        bool first_seen = false;

        while ( true )
        {
            {
                std::lock_guard lock( block_responses_mutex_ );
                auto            it = block_responses_.find( req_id );
                if ( it != block_responses_.end() && !it->second.empty() )
                {
                    if ( !first_seen )
                    {
                        first_seen                         = true;
                        block_first_response_time_[req_id] = std::chrono::steady_clock::now();
                    }
                    else
                    {
                        auto elapsed = std::chrono::steady_clock::now() - block_first_response_time_[req_id];
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

        std::set<std::string> cids;
        {
            std::lock_guard lock( block_responses_mutex_ );
            auto            it = block_responses_.find( req_id );
            if ( it == block_responses_.end() || it->second.empty() )
            {
                block_responses_.erase( req_id );
                block_first_response_time_.erase( req_id );
                logger_->debug( "[{}] No block response received within timeout for req_id {}",
                                address_.substr( 0, 8 ),
                                req_id );
                return outcome::failure( Error::GENESIS_REQUEST_ERROR );
            }
            cids = it->second;
            block_responses_.erase( req_id );
            block_first_response_time_.erase( req_id );
        }

        // invoke callback for each collected CID
        for ( const auto &cid : cids )
        {
            try
            {
                callback( cid );
            }
            catch ( const std::exception &e )
            {
                logger_->error( "Callback threw exception: {}", e.what() );
            }
        }

        return outcome::success();
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
