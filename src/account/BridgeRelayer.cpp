/**
 * @file       BridgeRelayer.cpp
 * @brief      Wires evmrelay burn events to MintFunds via shared EthWatchService.
 * @date       2026-05-30
 */
#include "account/BridgeRelayer.hpp"

#include <sstream>
#include <iomanip>

#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/eth_watch_cli.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @brief       Returns a new instance of the BridgeRelayer logger.
     * @return      Logger instance for BridgeRelayer.
     * @note        This is used for 2 reasons: (1) to enable logging on static methods, and (2) to avoid static initialization order issues
     *              when its created before the one with the same name on GeniusNode, which can have the output configured to file.
     *              If we initialize this logger statically it could end up outputing to console instead.
     */
    base::Logger BridgeRelayerLogger()
    {
        return base::createLogger( "BridgeRelayer" );
    }

    namespace
    {
        /**
         * @brief       Auxiliary function to convert an eth::Address to a hex string for logging.
         * @param[in]   addr The eth::Address to convert.
         * @return      Address as a hex string.
         */
        std::string AddressToHex( const eth::Address &addr )
        {
            return rlp::base::parse::hex_array_string( addr );
        }

        /**
         * @brief       Converts an intx::uint256 to uint64_t with overflow check, used for event parameters that must fit in uint64.
         * @param[in]   value The intx::uint256 value to convert.
         * @param[in]   field The name of the field being converted.
         * @return      The converted uint64_t value, or an error if the value exceeds uint64 limits.
         */
        outcome::result<uint64_t> Uint256ToUint64( const intx::uint256 &value, const std::string_view &field )
        {
            if ( value > std::numeric_limits<uint64_t>::max() )
            {
                sgns::BridgeRelayerLogger()->error( "BridgeRelayer: {} exceeds uint64", field );
                return outcome::failure( std::errc::value_too_large );
            }
            return static_cast<uint64_t>( value );
        }
    } // namespace

    std::shared_ptr<BridgeRelayer> BridgeRelayer::Create( std::weak_ptr<TransactionManager>     tx_manager,
                                                          std::shared_ptr<eth::EthWatchService> watch_service )
    {
        if ( !watch_service )
        {
            BridgeRelayerLogger()->error( "BridgeRelayer: null EthWatchService" );
            return nullptr;
        }
        return std::shared_ptr<BridgeRelayer>(
            new BridgeRelayer( std::move( tx_manager ), std::move( watch_service ) ) );
    }

    BridgeRelayer::BridgeRelayer( std::weak_ptr<TransactionManager>     tx_manager,
                                  std::shared_ptr<eth::EthWatchService> watch_service ) :
        tx_manager_( std::move( tx_manager ) ),
        watch_service_( std::move( watch_service ) ),
        logger_( std::move( BridgeRelayerLogger() ) )
    {
    }

    void BridgeRelayer::Start( const std::string &chain_name, const std::string &contract_address )
    {
        if ( !watch_service_ )
        {
            logger_->error( "BridgeRelayer: no EthWatchService" );
            return;
        }

        // Parse contract address
        eth::Address addr{};
        if ( !rlp::base::parse::hex_array( contract_address, addr ) )
        {
            logger_->error( "BridgeRelayer: invalid contract address {}", contract_address );
            return;
        }

        // BridgeSourceBurned(address indexed sender, uint256 id, uint256 amount,
        //                    uint256 srcChainID, uint256 destChainID)
        const std::string event_sig = "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)";
        auto              params    = eth::cli::event_registry().params_for( event_sig );

        watch_id_ = watch_service_->watch_event(
            addr,
            event_sig,
            params,
            [weakptr{ weak_from_this() }]( const eth::MatchedEvent               &event,
                                           const std::vector<eth::abi::AbiValue> &values )
            {
                eth::WatchEventNotification notification;
                notification.event  = event;
                notification.values = values;
                auto self           = weakptr.lock();
                if ( self )
                {
                    self->OnWatchEvent( notification );
                }
            } );

        logger_->info( "BridgeRelayer: watching {} contract={} watch_id={}", chain_name, contract_address, watch_id_ );
    }

    void BridgeRelayer::Stop()
    {
        // EthWatchService lifecycle is managed externally.
        // We could call watch_service_->unwatch(watch_id_) if needed.
        logger_->info( "BridgeRelayer: stopped" );
    }

    void BridgeRelayer::OnWatchEvent( const eth::WatchEventNotification &notification )
    {
        if ( notification.values.size() < 5 )
        {
            logger_->error( "BridgeRelayer: expected 5 event params, got {}", notification.values.size() );
            return;
        }

        // Decode BridgeSourceBurned params:
        //   values[0]: sender (address)
        //   values[1]: id (uint256) — ERC-1155 token ID
        //   values[2]: amount (uint256)
        //   values[3]: srcChainID (uint256)
        //   values[4]: destChainID (uint256)
        const auto &sender     = std::get<eth::codec::Address>( notification.values[0] );
        const auto &token_id   = std::get<intx::uint256>( notification.values[1] );
        const auto &amount_val = std::get<intx::uint256>( notification.values[2] );
        const auto &src_chain  = std::get<intx::uint256>( notification.values[3] );

        // Transaction hash from the event
        const std::string tx_hash = rlp::base::parse::hex_array_string( notification.event.tx_hash );

        // Amount
        const auto amount_result = Uint256ToUint64( amount_val, "amount" );
        if ( !amount_result )
        {
            logger_->error( "BridgeRelayer: {} exceeds uint64", "amount" );
            return;
        }
        const uint64_t amount = amount_result.value();

        // Chain ID
        const std::string chain_id = std::to_string( static_cast<uint64_t>( src_chain ) );

        const TokenID mint_token_id = TokenID::FromUint256( token_id, TokenID::Endianness::BIG );

        // Destination: sender of the burn (the user who bridged out)
        const std::string destination = AddressToHex( sender );

        logger_->info( "BridgeRelayer: burn detected chain={} tx={} token={} amount={} dest={}",
                       chain_id,
                       tx_hash.substr( 0, 16 ),
                       mint_token_id.ToHex().substr( 0, 16 ),
                       amount,
                       destination.substr( 0, 16 ) );

        auto strong_tx_manager = tx_manager_.lock();
        if ( !strong_tx_manager )
        {
            logger_->error( "BridgeRelayer: no TransactionManager available" );
            return;
        }

        auto result = strong_tx_manager->MintFunds( amount, tx_hash, chain_id, mint_token_id, destination );
        if ( result.has_error() )
        {
            if ( result.error() == std::errc::already_connected )
            {
                logger_->debug( "BridgeRelayer: duplicate burn rejected tx={}", tx_hash.substr( 0, 16 ) );
            }
            else
            {
                logger_->error( "BridgeRelayer: MintFunds failed for tx={} error={}",
                                tx_hash.substr( 0, 16 ),
                                result.error().message() );
            }
            return;
        }

        logger_->info( "BridgeRelayer: mint submitted tx_hash={} mint_id={}",
                       tx_hash.substr( 0, 16 ),
                       result.value().substr( 0, 16 ) );
    }
} // namespace sgns
