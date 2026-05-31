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

namespace sgns
{
    namespace
    {
        std::string AddressToHex( const eth::Address &addr )
        {
            return rlp::base::parse::hex_array_string( addr );
        }

        /// @brief Convert a uint256 ABI value to uint64, logging overflow.
        uint64_t Uint256ToUint64( const intx::uint256 &value, const base::Logger &logger, const char *field )
        {
            if ( value > std::numeric_limits<uint64_t>::max() )
            {
                logger->error( "BridgeRelayer: {} exceeds uint64", field );
                return 0;
            }
            return static_cast<uint64_t>( value );
        }
    } // namespace

    BridgeRelayer::BridgeRelayer( std::shared_ptr<TransactionManager>  tx_manager,
                                  std::shared_ptr<eth::EthWatchService> watch_service,
                                  base::Logger                          logger ) :
        tx_manager_( std::move( tx_manager ) ),
        watch_service_( std::move( watch_service ) ),
        logger_( std::move( logger ) )
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
        auto params = eth::cli::event_registry().params_for( event_sig );

        watch_id_ = watch_service_->watch_event(
            addr,
            event_sig,
            params,
            [this]( const eth::MatchedEvent &event, const std::vector<eth::abi::AbiValue> &values )
            {
                eth::WatchEventNotification notification;
                notification.event  = event;
                notification.values = values;
                OnWatchEvent( notification );
            } );

        logger_->info( "BridgeRelayer: watching {} contract={} watch_id={}",
                       chain_name, contract_address, watch_id_ );
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
            logger_->error( "BridgeRelayer: expected 5 event params, got {}",
                            notification.values.size() );
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
        const uint64_t amount = Uint256ToUint64( amount_val, logger_, "amount" );
        if ( amount == 0 && amount_val != 0 )
        {
            return; // overflow
        }

        // Chain ID
        const std::string chain_id = std::to_string( static_cast<uint64_t>( src_chain ) );

        // Token ID: uint256 → 32 bytes big-endian → TokenID
        TokenID::ByteArray token_bytes{};
        for ( size_t i = 0; i < token_bytes.size(); ++i )
        {
            token_bytes[i] = static_cast<uint8_t>(
                ( token_id >> ( ( token_bytes.size() - 1 - i ) * 8 ) ) & 0xFF );
        }
        const TokenID mint_token_id = TokenID::FromBytes( token_bytes.data(), token_bytes.size() );

        // Destination: sender of the burn (the user who bridged out)
        const std::string destination = AddressToHex( sender );

        logger_->info( "BridgeRelayer: burn detected chain={} tx={} token={} amount={} dest={}",
                       chain_id,
                       tx_hash.substr( 0, 16 ),
                       mint_token_id.ToHex().substr( 0, 16 ),
                       amount,
                       destination.substr( 0, 16 ) );

        if ( !tx_manager_ )
        {
            logger_->error( "BridgeRelayer: no TransactionManager" );
            return;
        }

        auto result = tx_manager_->MintFunds( amount, tx_hash, chain_id, mint_token_id, destination );
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
