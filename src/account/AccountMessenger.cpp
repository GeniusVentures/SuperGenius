/**
 * @file       AccountMessenger.cpp
 * @brief
 * @date       2025-07-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <thread>
#include <random>
#include <boost/format.hpp>
#include <future>
#include <algorithm>
#include <type_traits>
#include "AccountMessenger.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher.hpp"
#include "primitives/cid/cid.hpp"

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
        case AccountCommError::NO_RESPONSE_RECEIVED:
            return "No response received from network";
        case AccountCommError::RESPONSE_WITHOUT_NONCE:
            return "Response received but without nonce data";
        case AccountCommError::GENESIS_REQUEST_ERROR:
            return "Genesis request failed";
        case AccountCommError::UTXO_REQUEST_ERROR:
            return "UTXO request failed";
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
        worker_thread_ = std::thread( &AccountMessenger::WorkerLoop, this );
    }

    AccountMessenger::~AccountMessenger()
    {
        Stop();
    }

    void AccountMessenger::Stop()
    {
        stop_worker_.store( true );
        queue_cv_.notify_one();
        if ( worker_thread_.joinable() )
        {
            worker_thread_.join();
        }
    }

    void AccountMessenger::RegisterBlockResponseHandler( BlockResponseHandler handler )
    {
        std::lock_guard lock( global_handler_mutex_ );
        global_block_handler_ = std::move( handler );
    }

    void AccountMessenger::ClearBlockResponseHandler()
    {
        std::lock_guard lock( global_handler_mutex_ );
        global_block_handler_ = nullptr;
    }

    void AccountMessenger::RegisterHeadRequestHandler( HeadRequestHandler handler )
    {
        std::lock_guard lock( head_handler_mutex_ );
        head_request_handler_ = std::move( handler );
    }

    void AccountMessenger::ClearHeadRequestHandler()
    {
        std::lock_guard lock( head_handler_mutex_ );
        head_request_handler_ = nullptr;
    }

    outcome::result<void> AccountMessenger::RequestHeads( const std::unordered_set<std::string> &topics )
    {
        if ( topics.empty() )
        {
            logger_->debug( "[{}] RequestHeads called with empty topics list", address_.substr( 0, 8 ) );
            return outcome::success();
        }

        if ( !HasRequestPeers() )
        {
            logger_->debug( "[{}] Head request deferred until a request-topic peer is available",
                            address_.substr( 0, 8 ) );
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        accountComm::HeadRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( rd_() );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        for ( const auto &topic : topics )
        {
            req.add_topics( topic );
        }

        accountComm::SignedHeadRequest signed_req;
        signed_req.mutable_data()->CopyFrom( req );

        std::string req_string;
        if ( !req.SerializeToString( &req_string ) )
        {
            logger_->error( "[{}] Failed to serialize HeadRequest", address_.substr( 0, 8 ) );
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }

        auto sign_result = methods_.sign_( std::vector<uint8_t>( req_string.begin(), req_string.end() ) );
        if ( sign_result.has_error() )
        {
            logger_->error( "[{}] Failed to sign HeadRequest", address_.substr( 0, 8 ) );
            return outcome::failure( sign_result.error() );
        }

        signed_req.set_signature( std::string( sign_result.value().begin(), sign_result.value().end() ) );

        accountComm::AccountMessage envelope;
        envelope.mutable_head_request()->CopyFrom( signed_req );

        logger_->debug( "[{}] Sending HeadRequest for {} topics", address_.substr( 0, 8 ), topics.size() );

        return SendAccountMessage( envelope, { requests_topic_ } );
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
                case accountComm::AccountMessage::kHeadRequest:
                    HandleHeadRequest( acc_msg.head_request() );
                    break;
                case accountComm::AccountMessage::kBlockCidRequest:
                    HandleBlockCidRequest( acc_msg.block_cid_request() );
                    break;
                case accountComm::AccountMessage::kTransactionRequest:
                    HandleTransactionRequest( acc_msg.transaction_request() );
                    break;
                case accountComm::AccountMessage::kUtxoRequest:
                    HandleUTXORequest( acc_msg.utxo_request() );
                    break;
                case accountComm::AccountMessage::kNonceResponse:
                case accountComm::AccountMessage::kBlockResponse:
                case accountComm::AccountMessage::kUtxoResponse:
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
                case accountComm::AccountMessage::kBlockCidRequest:
                    logger_->error( "{}: Unexpected response received ", __func__ );
                    break;
                case accountComm::AccountMessage::kUtxoResponse:
                    HandleUTXOResponse( acc_msg.utxo_response() );
                    break;
                case accountComm::AccountMessage::kTransactionRequest:
                    logger_->error( "{}: Unexpected response received ", __func__ );
                    break;
                case accountComm::AccountMessage::kUtxoRequest:
                    logger_->error( "{}: Unexpected response received ", __func__ );
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
        BOOST_OUTCOME_TRY( auto signature, methods_.sign_( serialized_vec ) );
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
        auto promise = std::make_shared<std::promise<outcome::result<uint64_t>>>();
        auto future  = promise->get_future();

        EnqueueTask( { RequestType::Nonce,
                       timeout_ms,
                       silent_time_ms,
                       0,
                       std::string{},
                       std::string{},
                       nullptr,
                       std::move( promise ),
                       nullptr } );

        return future.get();
    }

    outcome::result<void> AccountMessenger::RequestGenesis(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback )
    {
        EnqueueTask( { RequestType::Genesis,
                       timeout_ms,
                       150,
                       0,
                       std::string{},
                       std::string{},
                       std::move( callback ),
                       nullptr,
                       nullptr } );
        return outcome::success();
    }

    outcome::result<void> AccountMessenger::RequestRegularBlock(
        uint64_t                                            timeout_ms,
        std::string                                         cid,
        std::function<void( outcome::result<std::string> )> callback )
    {
        EnqueueTask( { RequestType::BlockByCid,
                       timeout_ms,
                       150,
                       0,
                       std::move( cid ),
                       std::string{},
                       std::move( callback ),
                       nullptr,
                       nullptr } );
        return outcome::success();
    }

    outcome::result<void> AccountMessenger::RequestTransaction(
        uint64_t                                            timeout_ms,
        std::string                                         tx_hash,
        std::function<void( outcome::result<std::string> )> callback )
    {
        EnqueueTask( { RequestType::Transaction,
                       timeout_ms,
                       150,
                       0,
                       std::move( tx_hash ),
                       std::string{},
                       std::move( callback ),
                       nullptr,
                       nullptr } );
        return outcome::success();
    }

    outcome::result<std::unordered_set<std::string>> AccountMessenger::RequestUTXOs( uint64_t           timeout_ms,
                                                                                     const std::string &address,
                                                                                     uint64_t           silent_time_ms )
    {
        auto promise = std::make_shared<std::promise<outcome::result<std::unordered_set<std::string>>>>();
        auto future  = promise->get_future();

        EnqueueTask( { RequestType::UTXO,
                       timeout_ms,
                       silent_time_ms,
                       0,
                       std::string{},
                       address,
                       nullptr,
                       nullptr,
                       std::move( promise ) } );

        return future.get();
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
        BOOST_OUTCOME_TRY( auto signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedBlockRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_block_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    outcome::result<void> AccountMessenger::RequestBlockByCid( uint64_t req_id, const std::string &cid )
    {
        accountComm::BlockCidRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( req_id );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        req.set_cid( cid );
        logger_->debug( "[{}] Requesting block by CID {} with req_id {}", address_.substr( 0, 8 ), cid, req_id );
        std::string encoded;
        if ( !req.SerializeToString( &encoded ) )
        {
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }

        std::vector<uint8_t> serialized_vec( encoded.begin(), encoded.end() );
        BOOST_OUTCOME_TRY( auto signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedBlockCidRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_block_cid_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    outcome::result<void> AccountMessenger::RequestBlockByHash( uint64_t req_id, const std::string &tx_hash )
    {
        accountComm::TransactionRequest req;
        req.set_requester_address( address_ );
        req.set_request_id( req_id );
        req.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        req.set_tx_hash( tx_hash );
        logger_->debug( "[{}] Requesting transaction {} with req_id {}", address_.substr( 0, 8 ), tx_hash, req_id );

        std::string encoded;
        if ( !req.SerializeToString( &encoded ) )
        {
            return outcome::failure( Error::PROTO_SERIALIZATION );
        }

        std::vector<uint8_t> serialized_vec( encoded.begin(), encoded.end() );
        BOOST_OUTCOME_TRY( auto signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedTransactionRequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_transaction_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    outcome::result<void> AccountMessenger::RequestUTXO( uint64_t req_id, const std::string &address )
    {
        accountComm::UTXORequest req;
        req.set_requester_address( address_ );
        req.set_target_address( address );
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
        BOOST_OUTCOME_TRY( auto signature, methods_.sign_( serialized_vec ) );

        accountComm::SignedUTXORequest signed_req;
        *signed_req.mutable_data() = req;
        signed_req.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage envelope;
        *envelope.mutable_utxo_request() = signed_req;

        return SendAccountMessage( envelope, { requests_topic_ } );
    }

    void AccountMessenger::HandleBlockRequest( const accountComm::SignedBlockRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Block request req_id {} index {}",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.block_index() );

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

        HandleBlockLikeRequest( BlockIndexRequest{ static_cast<uint8_t>( req.block_index() ) },
                                req.requester_address(),
                                req.request_id() );
    }

    void AccountMessenger::HandleBlockCidRequest( const accountComm::SignedBlockCidRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Block-by-CID request req_id {} cid {}",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.cid() );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize BlockCidRequest for signature check" );
            return;
        }
        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on BlockCidRequest from {}", req.requester_address() );
            return;
        }

        HandleBlockLikeRequest( BlockCidRequest{ req.cid() }, req.requester_address(), req.request_id() );
    }

    void AccountMessenger::HandleTransactionRequest( const accountComm::SignedTransactionRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Transaction request req_id {} hash {}",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.tx_hash() );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize TransactionRequest for signature check" );
            return;
        }
        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on TransactionRequest from {}", req.requester_address() );
            return;
        }

        HandleBlockLikeRequest( TransactionHashRequest{ req.tx_hash() }, req.requester_address(), req.request_id() );
    }

    void AccountMessenger::HandleBlockLikeRequest( const BlockQuery  &query,
                                                   const std::string &requester_address,
                                                   uint64_t           request_id )
    {
        bool        have_cid = false;
        std::string cid;
        std::string label;
        std::string target;

        std::visit(
            [&]( const auto &q )
            {
                using T = std::decay_t<decltype( q )>;
                if constexpr ( std::is_same_v<T, BlockIndexRequest> )
                {
                    label           = "block";
                    target          = std::to_string( q.block_index );
                    auto cid_result = methods_.get_block_cid_( q.block_index, requester_address );
                    have_cid        = !cid_result.has_error();
                    if ( have_cid )
                    {
                        cid = cid_result.value();
                    }
                }
                else if constexpr ( std::is_same_v<T, BlockCidRequest> )
                {
                    label  = "block CID";
                    target = q.cid;
                    if ( methods_.has_block_cid_ )
                    {
                        auto has_cid_result = methods_.has_block_cid_( q.cid );
                        have_cid            = has_cid_result.has_value() && has_cid_result.value();
                        if ( have_cid )
                        {
                            cid = q.cid;
                        }
                    }
                    else
                    {
                        logger_->error( "No has_block_cid_ method configured" );
                    }
                }
                else
                {
                    label  = "transaction";
                    target = q.tx_hash;
                    if ( methods_.get_transaction_cid_ )
                    {
                        auto tx_cid_result = methods_.get_transaction_cid_( q.tx_hash );
                        have_cid           = tx_cid_result.has_value();
                        if ( have_cid )
                        {
                            cid = tx_cid_result.value();
                        }
                    }
                    else
                    {
                        logger_->error( "No get_transaction_cid_ method configured" );
                    }
                }
            },
            query );

        if ( !have_cid )
        {
            logger_->debug( "[{}] I don't have {} {}, will send empty BlockResponse",
                            address_.substr( 0, 8 ),
                            label,
                            target );
        }

        accountComm::BlockResponse resp;
        resp.set_responder_address( address_ );
        resp.set_requester_address( requester_address );
        resp.set_request_id( request_id );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        logger_->debug( "[{}] Preparing BlockResponse for {} req_id {}", address_.substr( 0, 8 ), label, request_id );

        if ( have_cid )
        {
            auto *info = resp.add_blocks();
            info->set_cid( cid );

            auto peer_info = pubsub_->GetHost()->getPeerInfo();
            info->set_peer_id( std::string( peer_info.id.toVector().begin(), peer_info.id.toVector().end() ) );

            auto pubsubObserved = pubsub_->GetHost()->getObservedAddressesReal();
            for ( auto &addr : pubsubObserved )
            {
                info->add_addresses( addr.getStringAddress() );
                logger_->debug( "Address Broadcast: {}", addr.getStringAddress() );
            }
            for ( auto &addr : peer_info.addresses )
            {
                info->add_addresses( addr.getStringAddress() );
                logger_->debug( "Address Broadcast: {}", addr.getStringAddress() );
            }

            if ( info->addresses_size() == 0 )
            {
                logger_->error( "No addresses found for BlockResponse" );
            }
        }

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->error( "Failed to serialize BlockResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
        auto                 signature_res = methods_.sign_( resp_bytes );
        if ( signature_res.has_error() )
        {
            logger_->error( "Failed to sign BlockResponse" );
            return;
        }

        accountComm::SignedBlockResponse signed_resp;
        *signed_resp.mutable_data() = resp;
        auto signature              = signature_res.value();
        signed_resp.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage msg;
        *msg.mutable_block_response() = signed_resp;

        auto account_topic = requester_address + std::string( ACCOUNT_COMM ) +
                             sgns::version::GetNetAndVersionAppendix();

        auto send_ret = SendAccountMessage( msg, { account_topic } );
        if ( send_ret.has_error() )
        {
            logger_->error( "[{}] Failed to send BlockResponse for req_id {}", address_.substr( 0, 8 ), request_id );
        }
        else
        {
            logger_->debug( "[{}] Sent BlockResponse ({} block entries) to {} (req_id {})",
                            address_.substr( 0, 8 ),
                            resp.blocks_size(),
                            requester_address.substr( 0, 8 ),
                            request_id );
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

        // Verify signature
        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize BlockResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( resp.responder_address(),
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

        // ------------------------------------------------------------
        // Store response information — even if blocks_size() == 0
        // ------------------------------------------------------------
        bool has_valid_cid = false;
        {
            std::lock_guard lock( block_responses_mutex_ );
            auto           &set_ref = block_responses_[resp.request_id()];

            if ( set_ref.empty() )
            {
                // mark first time we saw any response for this req_id
                block_first_response_time_[resp.request_id()] = std::chrono::steady_clock::now();
            }

            for ( const auto &b : resp.blocks() )
            {
                if ( !b.cid().empty() )
                {
                    set_ref.insert( b.cid() );
                    has_valid_cid = true;
                }
            }

            // even if no cids, we keep the key entry in block_responses_
            // to signal "a response was received but empty"
        }

        if ( !has_valid_cid && resp.blocks_size() == 0 )
        {
            logger_->trace( "[{}] Received empty BlockResponse from {}, marking as 'empty response received'",
                            address_.substr( 0, 8 ),
                            resp.responder_address().substr( 0, 8 ) );
            return; // do not trigger global handler
        }

        // ------------------------------------------------------------
        // Notify global handler only for valid CIDs
        // ------------------------------------------------------------
        std::lock_guard lock( global_handler_mutex_ );
        if ( global_block_handler_ )
        {
            for ( const auto &block_info : resp.blocks() )
            {
                if ( block_info.cid().empty() )
                {
                    continue;
                }

                std::string first_address;
                if ( block_info.addresses_size() > 0 )
                {
                    first_address = block_info.addresses( 0 );
                }

                global_block_handler_( block_info.cid(), block_info.peer_id(), first_address );
            }
        }
    }

    outcome::result<void> AccountMessenger::RequestAccountCreation(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback )
    {
        EnqueueTask( { RequestType::AccountCreation,
                       timeout_ms,
                       150,
                       1,
                       std::string{},
                       std::string{},
                       std::move( callback ),
                       nullptr,
                       nullptr } );
        return outcome::success();
    }

    outcome::result<void> AccountMessenger::RequestValidatorRegistry(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback )
    {
        EnqueueTask( { RequestType::ValidatorRegistry,
                       timeout_ms,
                       150,
                       2,
                       std::string{},
                       std::string{},
                       std::move( callback ),
                       nullptr,
                       nullptr } );
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

    void AccountMessenger::WorkerLoop()
    {
        while ( true )
        {
            RequestTask task;
            {
                std::unique_lock lock( queue_mutex_ );
                queue_cv_.wait( lock, [this]() { return stop_worker_.load() || !request_queue_.empty(); } );
                if ( stop_worker_.load() && request_queue_.empty() )
                {
                    break;
                }
                task = std::move( request_queue_.front() );
                request_queue_.pop();
            }

            switch ( task.type )
            {
                case RequestType::Nonce:
                {
                    auto res = PerformNonceRequest( task.timeout_ms, task.silent_time_ms );
                    if ( task.nonce_promise )
                    {
                        task.nonce_promise->set_value( res );
                    }
                    break;
                }
                case RequestType::Genesis:
                case RequestType::AccountCreation:
                case RequestType::ValidatorRegistry:
                {
                    auto res = PerformBlockRequest( task.timeout_ms, BlockIndexRequest{ task.block_index } );
                    if ( task.callback )
                    {
                        if ( res.has_error() )
                        {
                            task.callback( outcome::failure( res.error() ) );
                        }
                        else
                        {
                            const auto &cids = res.value();
                            if ( cids.empty() )
                            {
                                task.callback( outcome::failure( boost::system::error_code{} ) );
                            }
                            else
                            {
                                for ( const auto &cid : cids )
                                {
                                    task.callback( outcome::success( cid ) );
                                }
                            }
                        }
                    }
                    break;
                }
                case RequestType::BlockByCid:
                {
                    auto res = PerformBlockRequest( task.timeout_ms, BlockCidRequest{ task.cid } );
                    if ( task.callback )
                    {
                        if ( res.has_error() )
                        {
                            task.callback( outcome::failure( res.error() ) );
                        }
                        else
                        {
                            const auto &cids = res.value();
                            if ( cids.empty() )
                            {
                                task.callback( outcome::failure( boost::system::error_code{} ) );
                            }
                            else
                            {
                                for ( const auto &cid : cids )
                                {
                                    task.callback( outcome::success( cid ) );
                                }
                            }
                        }
                    }
                    break;
                }
                case RequestType::Transaction:
                {
                    auto res = PerformBlockRequest( task.timeout_ms, TransactionHashRequest{ task.cid } );
                    if ( task.callback )
                    {
                        if ( res.has_error() )
                        {
                            task.callback( outcome::failure( res.error() ) );
                        }
                        else
                        {
                            const auto &cids = res.value();
                            if ( cids.empty() )
                            {
                                task.callback( outcome::failure( boost::system::error_code{} ) );
                            }
                            else
                            {
                                for ( const auto &cid : cids )
                                {
                                    task.callback( outcome::success( cid ) );
                                }
                            }
                        }
                    }
                    break;
                }
                case RequestType::UTXO:
                {
                    auto res = PerformUTXORequest( task.timeout_ms, task.utxo_address, task.silent_time_ms );
                    if ( task.utxo_promise )
                    {
                        task.utxo_promise->set_value( res );
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    void AccountMessenger::EnqueueTask( RequestTask task )
    {
        {
            std::lock_guard lock( queue_mutex_ );
            request_queue_.push( std::move( task ) );
        }
        queue_cv_.notify_one();
    }

    bool AccountMessenger::HasRequestPeers() const
    {
        return pubsub_->getPeerCount( requests_topic_ ) != 0;
    }

    outcome::result<uint64_t> AccountMessenger::PerformNonceRequest( uint64_t timeout_ms, uint64_t silent_time_ms )
    {
        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        std::string              to_hash = address_ + std::to_string( random_value );
        auto hash = sgns::crypto::sha2_256( to_hash.data(), to_hash.size() );

        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        logger_->debug( "[{}] Requesting nonce with timeout {} and req_id {} ",
                        address_.substr( 0, 8 ),
                        timeout_ms,
                        req_id );

        {
            std::lock_guard lock( nonce_responses_mutex_ );
            nonce_responses_.erase( req_id );
            no_nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        BOOST_OUTCOME_TRY( RequestNonce( req_id ) );
        if ( !HasRequestPeers() )
        {
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        const auto silent_time  = std::chrono::milliseconds( silent_time_ms );

        bool first_seen = false;

        while ( true )
        {
            {
                std::lock_guard lock( nonce_responses_mutex_ );
                auto            nonce_it    = nonce_responses_.find( req_id );
                auto            no_nonce_it = no_nonce_responses_.find( req_id );

                bool has_nonce_responses    = ( nonce_it != nonce_responses_.end() && !nonce_it->second.empty() );
                bool has_no_nonce_responses = ( no_nonce_it != no_nonce_responses_.end() &&
                                                !no_nonce_it->second.empty() );

                if ( has_nonce_responses || has_no_nonce_responses )
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

        uint64_t max_nonce        = 0;
        bool     has_any_nonce    = false;
        bool     has_any_response = false;

        {
            std::lock_guard lock( nonce_responses_mutex_ );
            auto            nonce_it    = nonce_responses_.find( req_id );
            auto            no_nonce_it = no_nonce_responses_.find( req_id );

            if ( nonce_it != nonce_responses_.end() && !nonce_it->second.empty() )
            {
                has_any_nonce    = true;
                has_any_response = true;
                max_nonce        = *nonce_it->second.rbegin();
            }

            if ( no_nonce_it != no_nonce_responses_.end() && !no_nonce_it->second.empty() )
            {
                has_any_response = true;
            }

            nonce_responses_.erase( req_id );
            no_nonce_responses_.erase( req_id );
            first_response_time_.erase( req_id );
        }

        if ( !has_any_response )
        {
            logger_->debug( "[{}] No response received within timeout for req_id {}", address_.substr( 0, 8 ), req_id );
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        if ( !has_any_nonce )
        {
            logger_->debug( "[{}] Response received but without nonce data for req_id {}",
                            address_.substr( 0, 8 ),
                            req_id );
            return outcome::failure( Error::RESPONSE_WITHOUT_NONCE );
        }

        logger_->debug( "[{}] Returning highest collected nonce for req_id {}: {}",
                        address_.substr( 0, 8 ),
                        req_id,
                        max_nonce );
        return max_nonce;
    }

    outcome::result<std::set<std::string>> AccountMessenger::PerformBlockRequest( uint64_t          timeout_ms,
                                                                                  const BlockQuery &query )
    {
        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        std::string              to_hash = address_ + std::to_string( random_value );
        auto hash = sgns::crypto::sha2_256( to_hash.data(), to_hash.size() );

        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        std::string label;
        std::string target;
        auto        send_request = [&]( uint64_t id ) -> outcome::result<void>
        {
            return std::visit(
                [&]( const auto &q ) -> outcome::result<void>
                {
                    using T = std::decay_t<decltype( q )>;
                    if constexpr ( std::is_same_v<T, BlockIndexRequest> )
                    {
                        label  = "block";
                        target = std::to_string( q.block_index );
                        return RequestBlock( id, q.block_index );
                    }
                    else if constexpr ( std::is_same_v<T, BlockCidRequest> )
                    {
                        label  = "block CID";
                        target = q.cid;
                        return RequestBlockByCid( id, q.cid );
                    }
                    else
                    {
                        label  = "transaction";
                        target = q.tx_hash;
                        return RequestBlockByHash( id, q.tx_hash );
                    }
                },
                query );
        };

        {
            std::lock_guard lock( block_responses_mutex_ );
            block_responses_.erase( req_id );
            block_first_response_time_.erase( req_id );
        }

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        while ( !HasRequestPeers() )
        {
            if ( std::chrono::steady_clock::now() - start_time >= full_timeout )
            {
                return outcome::failure( Error::GENESIS_REQUEST_ERROR );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        auto request_result = send_request( req_id );
        logger_->debug( "[{}] Requesting {} {} with req_id {} and timeout {}",
                        address_.substr( 0, 8 ),
                        label,
                        target,
                        req_id,
                        timeout_ms );

        if ( request_result.has_error() )
        {
            logger_->error( "[{}] Failed to request {} {}", address_.substr( 0, 8 ), label, target );
            return request_result.error();
        }
        const auto silent_time  = std::chrono::milliseconds( 150 );

        bool first_seen = false;

        while ( true )
        {
            {
                std::lock_guard lock( block_responses_mutex_ );
                auto            it = block_responses_.find( req_id );
                if ( it != block_responses_.end() )
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
                            break;
                        }
                    }
                }
            }

            if ( std::chrono::steady_clock::now() - start_time >= full_timeout )
            {
                logger_->debug( "[{}] Timeout: no BlockResponse received for req_id {}",
                                address_.substr( 0, 8 ),
                                req_id );
                return outcome::failure( Error::GENESIS_REQUEST_ERROR );
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        std::set<std::string> cids;
        bool                  any_response = false;
        {
            std::lock_guard lock( block_responses_mutex_ );
            auto            it = block_responses_.find( req_id );
            if ( it != block_responses_.end() )
            {
                any_response = true;
                cids         = it->second;
            }
            block_responses_.erase( req_id );
            block_first_response_time_.erase( req_id );
        }

        if ( !any_response )
        {
            logger_->warn( "[{}] No responses recorded for req_id {}", address_.substr( 0, 8 ), req_id );
            return outcome::failure( Error::GENESIS_REQUEST_ERROR );
        }

        return outcome::success( cids );
    }

    outcome::result<std::unordered_set<std::string>> AccountMessenger::PerformUTXORequest( uint64_t timeout_ms,
                                                                                           const std::string &address,
                                                                                           uint64_t silent_time_ms )
    {
        std::mt19937_64 gen( rd_() );
        uint64_t        random_value = gen();

        std::string              to_hash = address_ + std::to_string( random_value );
        auto hash = sgns::crypto::sha2_256( to_hash.data(), to_hash.size() );

        uint64_t req_id = 0;
        std::memcpy( &req_id, hash.data(), sizeof( req_id ) );

        logger_->debug( "[{}] Requesting UTXOs for {} with req_id {} and timeout {}",
                        address_.substr( 0, 8 ),
                        address.substr( 0, 8 ),
                        req_id,
                        timeout_ms );

        {
            std::lock_guard lock( utxo_responses_mutex_ );
            utxo_responses_.erase( req_id );
            utxo_first_response_time_.erase( req_id );
        }

        auto request_result = RequestUTXO( req_id, address );
        if ( request_result.has_error() )
        {
            logger_->error( "[{}] Failed to request UTXOs for {}", address_.substr( 0, 8 ), address.substr( 0, 8 ) );
            return request_result.error();
        }
        if ( !HasRequestPeers() )
        {
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        const auto start_time   = std::chrono::steady_clock::now();
        const auto full_timeout = std::chrono::milliseconds( timeout_ms );
        const auto silent_time  = std::chrono::milliseconds( silent_time_ms );

        bool first_seen = false;

        while ( true )
        {
            {
                std::lock_guard lock( utxo_responses_mutex_ );
                auto            it = utxo_responses_.find( req_id );
                if ( it != utxo_responses_.end() && !it->second.empty() )
                {
                    if ( !first_seen )
                    {
                        first_seen                        = true;
                        utxo_first_response_time_[req_id] = std::chrono::steady_clock::now();
                    }
                    else
                    {
                        auto elapsed = std::chrono::steady_clock::now() - utxo_first_response_time_[req_id];
                        if ( elapsed >= silent_time )
                        {
                            break;
                        }
                    }
                }
            }

            if ( std::chrono::steady_clock::now() - start_time >= full_timeout )
            {
                logger_->debug( "[{}] Timeout: no UTXOResponse received for req_id {}",
                                address_.substr( 0, 8 ),
                                req_id );
                return outcome::failure( Error::NO_RESPONSE_RECEIVED );
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        std::vector<UTXOResponseData> responses;
        {
            std::lock_guard lock( utxo_responses_mutex_ );
            auto            it = utxo_responses_.find( req_id );
            if ( it != utxo_responses_.end() )
            {
                responses = std::move( it->second );
            }
            utxo_responses_.erase( req_id );
            utxo_first_response_time_.erase( req_id );
        }

        if ( responses.empty() )
        {
            logger_->warn( "[{}] No UTXO responses recorded for req_id {}", address_.substr( 0, 8 ), req_id );
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        struct Vote
        {
            uint64_t                 total_weight{ 0 };
            uint64_t                 count{ 0 };
            bool                     has_utxos{ false };
            std::vector<std::string> utxos; // canonical sorted list
        };

        auto make_key = []( const std::vector<std::string> &utxos ) -> std::string
        {
            std::string key;
            for ( const auto &u : utxos )
            {
                key.append( u );
            }
            return key;
        };

        struct WeightedResponse
        {
            UTXOResponseData        data;
            std::optional<uint64_t> validator_weight;
        };

        std::vector<WeightedResponse> weighted;
        weighted.reserve( responses.size() );

        bool any_validator = false;
        for ( auto &resp : responses )
        {
            if ( !resp.has_utxos )
            {
                resp.utxos.clear();
            }

            std::optional<uint64_t> weight;
            if ( methods_.get_validator_weight_ )
            {
                auto weight_res = methods_.get_validator_weight_( resp.responder_address );
                if ( weight_res.has_value() )
                {
                    weight = weight_res.value();
                }
            }

            if ( weight.has_value() && weight.value() > 0 )
            {
                any_validator = true;
            }

            weighted.push_back( { std::move( resp ), weight } );
        }

        std::unordered_map<std::string, Vote> votes;
        for ( const auto &entry : weighted )
        {
            const bool is_validator = entry.validator_weight.has_value() && entry.validator_weight.value() > 0;
            if ( any_validator && !is_validator )
            {
                continue;
            }

            const uint64_t weight = any_validator ? entry.validator_weight.value() : 1;

            if ( !entry.data.has_utxos )
            {
                continue;
            }

            std::vector<std::string> canonical_utxos;
            canonical_utxos.reserve( entry.data.utxos.size() );
            for ( const auto &u : entry.data.utxos )
            {
                canonical_utxos.push_back( u );
            }
            std::sort( canonical_utxos.begin(), canonical_utxos.end() );

            const auto key  = make_key( canonical_utxos );
            auto      &vote = votes[key];
            if ( vote.count == 0 )
            {
                vote.has_utxos = true;
                vote.utxos     = canonical_utxos;
            }
            vote.total_weight += weight;
            vote.count        += 1;
        }

        if ( votes.empty() )
        {
            logger_->warn( "[{}] No eligible UTXO responses after applying validator preference",
                           address_.substr( 0, 8 ) );
            return outcome::failure( Error::NO_RESPONSE_RECEIVED );
        }

        std::string best_key;
        Vote        best_vote;
        bool        first = true;

        for ( const auto &[key, vote] : votes )
        {
            if ( first || vote.total_weight > best_vote.total_weight ||
                 ( vote.total_weight == best_vote.total_weight && vote.count > best_vote.count ) ||
                 ( vote.total_weight == best_vote.total_weight && vote.count == best_vote.count && key < best_key ) )
            {
                best_key  = key;
                best_vote = vote;
                first     = false;
            }
        }

        std::unordered_set<std::string> result;
        if ( best_vote.has_utxos )
        {
            result.insert( best_vote.utxos.begin(), best_vote.utxos.end() );
        }

        return outcome::success( result );
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

        accountComm::NonceResponse resp;
        resp.set_responder_address( address_ );
        resp.set_requester_address( req.requester_address() );
        resp.set_request_id( req.request_id() );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        if ( local_nonce_result.has_error() )
        {
            logger_->debug( "[{}] I don't have the nonce for the address {}, responding with has_nonce=false",
                            address_.substr( 0, 8 ),
                            req.requester_address() );

            resp.set_has_nonce( false );
            resp.set_known_nonce( 0 ); // Set to 0 when no nonce available
        }
        else
        {
            uint64_t local_nonce = local_nonce_result.value();
            resp.set_has_nonce( true );
            resp.set_known_nonce( local_nonce );

            logger_->debug( "[{}] Sending back the nonce {} to {} with req_id {}",
                            address_.substr( 0, 8 ),
                            local_nonce,
                            req.requester_address().substr( 0, 8 ),
                            resp.request_id() );
        }

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

        if ( send_ret.has_error() )
        {
            logger_->error( "Failed to send NonceResponse" );
        }
    }

    void AccountMessenger::HandleNonceResponse( const accountComm::SignedNonceResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a Nonce response from {} (has_nonce={}, nonce={}) and req_id {}",
                        address_.substr( 0, 8 ),
                        resp.responder_address(),
                        resp.has_nonce(),
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

        std::lock_guard lock( nonce_responses_mutex_ );

        if ( resp.has_nonce() )
        {
            nonce_responses_[resp.request_id()].insert( resp.known_nonce() );
        }
        else
        {
            logger_->debug( "[{}] Responder {} has no nonce for our address",
                            address_.substr( 0, 8 ),
                            resp.responder_address().substr( 0, 8 ) );
            // Track addresses that responded with no nonce
            no_nonce_responses_[resp.request_id()].insert( resp.responder_address() );
        }
    }

    void AccountMessenger::HandleUTXORequest( const accountComm::SignedUTXORequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a UTXO request req_id {} for {}",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.target_address().substr( 0, 8 ) );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize UTXORequest for signature check" );
            return;
        }

        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on UTXORequest from {}", req.requester_address() );
            return;
        }

        accountComm::UTXOResponse resp;
        resp.set_responder_address( address_ );
        resp.set_requester_address( req.requester_address() );
        resp.set_target_address( req.target_address() );
        resp.set_request_id( req.request_id() );
        resp.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        bool have_utxos = false;

        auto utxos_res = methods_.get_utxos_( req.target_address() );
        if ( utxos_res.has_value() )
        {
            auto utxos = utxos_res.value();
            for ( auto &utxo : utxos )
            {
                resp.add_utxos( std::move( utxo ) );
            }
            have_utxos = !utxos.empty();
        }

        resp.set_has_utxos( have_utxos );

        std::string resp_serialized;
        if ( !resp.SerializeToString( &resp_serialized ) )
        {
            logger_->error( "Failed to serialize UTXOResponse" );
            return;
        }

        std::vector<uint8_t> resp_bytes( resp_serialized.begin(), resp_serialized.end() );
        auto                 signature_res = methods_.sign_( resp_bytes );
        if ( signature_res.has_error() )
        {
            logger_->error( "Failed to sign UTXOResponse" );
            return;
        }

        accountComm::SignedUTXOResponse signed_resp;
        *signed_resp.mutable_data() = resp;
        auto signature              = signature_res.value();
        signed_resp.set_signature( signature.data(), signature.size() );

        accountComm::AccountMessage msg;
        *msg.mutable_utxo_response() = signed_resp;

        auto account_topic = req.requester_address() + std::string( ACCOUNT_COMM ) +
                             sgns::version::GetNetAndVersionAppendix();

        auto send_ret = SendAccountMessage( msg, { account_topic } );
        if ( send_ret.has_error() )
        {
            logger_->error( "[{}] Failed to send UTXOResponse for req_id {}",
                            address_.substr( 0, 8 ),
                            req.request_id() );
        }
    }

    void AccountMessenger::HandleUTXOResponse( const accountComm::SignedUTXOResponse &signed_resp )
    {
        const auto &resp = signed_resp.data();

        logger_->debug( "[{}] Received a UTXO response from {} (has_utxos={}, count={}) and req_id {}",
                        address_.substr( 0, 8 ),
                        resp.responder_address().substr( 0, 8 ),
                        resp.has_utxos(),
                        resp.utxos_size(),
                        resp.request_id() );

        std::string serialized;
        if ( !resp.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize UTXOResponse for signature check" );
            return;
        }

        std::vector<uint8_t> data_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( resp.responder_address(),
                                                                   signed_resp.signature(),
                                                                   data_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on UTXOResponse from {}", resp.responder_address() );
            return;
        }

        UTXOResponseData entry;
        entry.responder_address = resp.responder_address();
        entry.has_utxos         = resp.has_utxos();
        for ( const auto &utxo : resp.utxos() )
        {
            entry.utxos.insert( utxo );
        }

        std::lock_guard lock( utxo_responses_mutex_ );
        auto           &list_ref = utxo_responses_[resp.request_id()];
        if ( list_ref.empty() )
        {
            utxo_first_response_time_[resp.request_id()] = std::chrono::steady_clock::now();
        }
        list_ref.push_back( std::move( entry ) );
    }

    void AccountMessenger::HandleHeadRequest( const accountComm::SignedHeadRequest &signed_req )
    {
        const auto &req = signed_req.data();

        logger_->debug( "[{}] Received a Head request req_id {} for {} topics",
                        address_.substr( 0, 8 ),
                        req.request_id(),
                        req.topics_size() );

        std::string serialized;
        if ( !req.SerializeToString( &serialized ) )
        {
            logger_->error( "Failed to serialize HeadRequest for signature check" );
            return;
        }

        std::vector<uint8_t> serialized_vec( serialized.begin(), serialized.end() );
        auto                 verify_signature_result = methods_.verify_signature_( req.requester_address(),
                                                                   signed_req.signature(),
                                                                   serialized_vec );
        if ( verify_signature_result.has_error() || !verify_signature_result.value() )
        {
            logger_->error( "Invalid signature on HeadRequest from {}", req.requester_address() );
            return;
        }

        // Get topics from request
        std::set<std::string> requested_topics;
        for ( int i = 0; i < req.topics_size(); ++i )
        {
            requested_topics.emplace( req.topics( i ) );
        }

        // Call the registered handler (typically will be handled by CrdtDatastore)
        std::lock_guard lock( head_handler_mutex_ );
        if ( head_request_handler_ )
        {
            logger_->debug( "[{}] Forwarding head request for {} topics to handler",
                            address_.substr( 0, 8 ),
                            requested_topics.size() );
            head_request_handler_( requested_topics );
        }
        else
        {
            logger_->warn( "[{}] No head request handler registered, ignoring HeadRequest", address_.substr( 0, 8 ) );
        }
    }

}
