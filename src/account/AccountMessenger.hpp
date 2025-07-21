/**
 * @file       AccountMessenger.hpp
 * @brief      
 * @date       2025-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <string>
#include <memory>
#include <boost/optional.hpp>
#include "ipfs_pubsub/gossip_pubsub.hpp"

#include "outcome/outcome.hpp"

#include <future>

class AccountMessenger : public std::enable_shared_from_this<AccountMessenger>
{
private:
    const std::string                                account_comm_topic_;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_;
    std::future<libp2p::protocol::Subscription>      subs_future_;

    AccountMessenger( std::string account_comm_topic, std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub );

    void                  OnMessage( boost::optional<const GossipPubSub::Message &> message, const std::string &topic );
    outcome::result<void> SendMessage( const base::Buffer &buff );

public:
    static std::shared_ptr<AccountMessenger> New( td::string                                       account_comm_topic,
                                                  std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub );

    outcome::result<uint64_t> GetLatestNonce( /* timeout maybe?*/ ); //TODO
    outcome::result<void>     RequestNonce();                        //TODO
    ~AccountMessenger();
};

AccountMessenger New( std::string account_comm_topic, std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub )
{
    if ( account_comm_topic.empty() )
    {
        return nullptr;
    }
    if ( !pubsub )
    {
        return nullptr;
    }
    auto instance = std::shared_ptr<AccountMessenger>(
        new AccountMessenger( std::move( account_comm_topic ), std::move( pubsub ) ) );

    instance->subs_future_ = std::move( instance->pubsub_->Subscribe(
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
}

AccountMessenger::AccountMessenger( std::string                                      account_comm_topic,
                                    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub ) :
    account_comm_topic_( std::move( account_comm_topic ) ), //
    pubsub_( std::move( pubsub ) )                          //
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

outcome::result<void> AccountMessenger::RequestNonce()
{
    account::NonceRequest req;
    req.set_requester_address( /* my address */ );
    req.set_request_id( static_cast<uint64_t>( std::chrono::system_clock::now().time_since_epoch().count() ) );

    account::AccountMessage envelope;
    *envelope.mutable_nonce_request() = req;

    std::string encoded;
    if ( !envelope.SerializeToString( &encoded ) )
    {
        return outcome::failure( /* error_code */ );
    }

    base::Buffer buffer( reinterpret_cast<const uint8_t *>( encoded.data() ), encoded.size() );
    return SendMessage( buffer );
}
