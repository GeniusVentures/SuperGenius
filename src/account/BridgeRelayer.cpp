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
#include "eth/secp256k1_utility.hpp"
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
                                  std::shared_ptr<eth::EthWatchService> watch_service,
                                  base::Logger                          logger ) :
        tx_manager_( std::move( tx_manager ) ),
        watch_service_( std::move( watch_service ) ),
        logger_( logger ? std::move( logger ) : std::move( BridgeRelayerLogger() ) )
    {
    }

    void BridgeRelayer::OnRpcEndpointsReady( std::vector<ChainContractPair> chains )
    {
        logger_->info( "BridgeRelayer: observer-driven startup for {} chain(s)", chains.size() );
        Start( std::move( chains ) );
    }

    void BridgeRelayer::Start( std::vector<ChainContractPair> chains )
    {
        if ( !watch_service_ )
        {
            logger_->error( "BridgeRelayer: no EthWatchService" );
            return;
        }

        // v1: BridgeSourceBurned(address indexed sender, uint256 id, uint256 amount,
        //                         uint256 srcChainID, uint256 destChainID, bytes sgnsDestination)
        const std::string event_sig_v1 = "BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)";
        auto              params_v1    = eth::cli::event_registry().params_for( event_sig_v1 );

        // v2: BridgeOutInitiated(address indexed sender, uint256 id, uint256 amount,
        //                         uint256 srcChainID, uint256 destChainID,
        //                         bytes32 sgnsDestination, bool destinationYOdd)
        // Param 5 is a 32-byte X-only key (decoded as codec::Hash256) and param 6
        // carries the Y parity needed for deterministic decompression (D-06/D-07).
        const std::string event_sig_v2 = "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)";
        auto              params_v2    = eth::cli::event_registry().params_for( event_sig_v2 );

        size_t registered = 0;
        size_t skipped    = 0;

        for ( const auto &chain : chains )
        {
            // Skip chains with empty or whitespace-only contract address
            if ( chain.chain_name.empty() )
            {
                logger_->warn( "BridgeRelayer: skipping chain with empty name" );
                ++skipped;
                continue;
            }

            if ( chain.contract_address.empty() )
            {
                logger_->debug( "BridgeRelayer: no contract address for chain {}, skipping", chain.chain_name );
                ++skipped;
                continue;
            }

            // Parse contract address
            eth::Address addr{};
            if ( !rlp::base::parse::hex_array( chain.contract_address, addr ) )
            {
                logger_->warn( "BridgeRelayer: invalid contract address {} for chain {}, skipping",
                               chain.contract_address,
                               chain.chain_name );
                ++skipped;
                continue;
            }

            const auto chain_name = chain.chain_name;

            // Shared callback for both v1 and v2 events — OnWatchEvent dispatches
            // on the variant type of values[5] (D-06).
            auto callback = [ weakptr{ weak_from_this() }, chain_name ]( const eth::MatchedEvent               &event,
                                                                          const std::vector<eth::abi::AbiValue> &values )
            {
                eth::WatchEventNotification notification;
                notification.event  = event;
                notification.values = values;
                auto self           = weakptr.lock();
                if ( self )
                {
                    self->OnWatchEvent( notification, chain_name );
                }
            };

            eth::EventWatchId watch_id_v1 = 0;
            eth::EventWatchId watch_id_v2 = 0;
            bool              got_v1      = false;
            bool              got_v2      = false;

            // Register v1 watch (best-effort per D-21)
            try
            {
                watch_id_v1 = watch_service_->watch_event( addr, event_sig_v1, params_v1, callback );
                got_v1      = true;
                ++registered;
                logger_->info( "BridgeRelayer: watching {} v1 contract={} watch_id={}",
                               chain_name,
                               chain.contract_address,
                               watch_id_v1 );
            }
            catch ( const std::exception &e )
            {
                logger_->warn( "BridgeRelayer: failed to watch {} v1 ({}) — skipping v1",
                               chain.chain_name,
                               e.what() );
            }

            // Register v2 watch (best-effort per D-21)
            try
            {
                watch_id_v2 = watch_service_->watch_event( addr, event_sig_v2, params_v2, callback );
                got_v2      = true;
                ++registered;
                logger_->info( "BridgeRelayer: watching {} v2 contract={} watch_id={}",
                               chain_name,
                               chain.contract_address,
                               watch_id_v2 );
            }
            catch ( const std::exception &e )
            {
                logger_->warn( "BridgeRelayer: failed to watch {} v2 ({}) — skipping v2",
                               chain.chain_name,
                               e.what() );
            }

            if ( !got_v1 && !got_v2 )
            {
                // Neither watch registered for this chain — count as skipped.
                ++skipped;
                continue;
            }

            chain_watches_[chain_name] = { watch_id_v1, watch_id_v2 };
        }

        logger_->info( "BridgeRelayer: started {} watch(es) across {} chain(s) ({} skipped)",
                       registered,
                       chains.size(),
                       skipped );
    }

    void BridgeRelayer::Stop()
    {
        // EthWatchService lifecycle is managed externally.
        // We could call watch_service_->unwatch(watch_id_) if needed.
        logger_->info( "BridgeRelayer: stopped" );
    }

    void BridgeRelayer::OnWatchEvent( const eth::WatchEventNotification &notification,
                                       const std::string                 &chain_name )
    {
        // v1 (BridgeSourceBurned) and v2 (BridgeOutInitiated) share params 0..4;
        // param 5 differs by type (ByteBuffer for v1, Hash256 for v2) and is the
        // dispatch discriminator (D-06). v2 adds param 6 (bool destinationYOdd).
        static constexpr size_t kCommonParamCount         = 6;
        static constexpr size_t kV2DestinationYOddIndex   = 6;
        if ( notification.values.size() < kCommonParamCount )
        {
            logger_->error( "BridgeRelayer: expected at least {} event params for chain {}, got {}",
                            kCommonParamCount,
                            chain_name,
                            notification.values.size() );
            return;
        }

        // Common params (identical types in both v1 and v2):
        //   values[0]: sender (address)
        //   values[1]: id (uint256) — ERC-1155 token ID
        //   values[2]: amount (uint256)
        //   values[3]: srcChainID (uint256)
        //   values[4]: destChainID (uint256)
        //   values[5]: sgnsDestination — v1: bytes (64-byte full pubkey),
        //                               v2: bytes32 (32-byte X-only key, D-06).
        //   values[6]: v2 only — destinationYOdd (bool), Y parity for decompression.
        const auto &sender     = std::get<eth::codec::Address>( notification.values[0] );
        const auto &token_id   = std::get<intx::uint256>( notification.values[1] );
        const auto &amount_val = std::get<intx::uint256>( notification.values[2] );
        const auto &src_chain  = std::get<intx::uint256>( notification.values[3] );
        const auto &dest_chain = std::get<intx::uint256>( notification.values[4] );

        // Transaction hash from the event
        const std::string tx_hash = rlp::base::parse::hex_array_string( notification.event.tx_hash );

        // Amount
        const auto amount_result = Uint256ToUint64( amount_val, "amount" );
        if ( !amount_result )
        {
            logger_->error( "BridgeRelayer: {} exceeds uint64 for chain {}", "amount", chain_name );
            return;
        }
        const uint64_t amount = amount_result.value();

        // Chain ID
        const std::string chain_id = std::to_string( static_cast<uint64_t>( src_chain ) );

        const TokenID mint_token_id = TokenID::FromUint256( token_id, TokenID::Endianness::BIG );

        // Destination: dispatch on the variant type of values[5] (D-06).
        //   ByteBuffer -> v1: full 64-byte destination (128 hex chars).
        //   Hash256    -> v2: 32-byte X-only key, decompressed via DecompressXOnlyPubkey
        //                using the Y-parity bool from values[6] (D-07).
        std::string destination;
        if ( std::holds_alternative<eth::codec::ByteBuffer>( notification.values[5] ) )
        {
            const auto &sgns_dest_bytes = std::get<eth::codec::ByteBuffer>( notification.values[5] );
            destination = rlp::base::parse::hex_bytes( sgns_dest_bytes.data(), sgns_dest_bytes.size() );
        }
        else if ( std::holds_alternative<eth::codec::Hash256>( notification.values[5] ) )
        {
            if ( notification.values.size() <= kV2DestinationYOddIndex )
            {
                logger_->error( "BridgeRelayer: v2 event missing destinationYOdd param for chain {}", chain_name );
                return;
            }
            const auto &x_bytes     = std::get<eth::codec::Hash256>( notification.values[5] );
            const auto &y_odd_value = notification.values[kV2DestinationYOddIndex];
            if ( !std::holds_alternative<bool>( y_odd_value ) )
            {
                logger_->error( "BridgeRelayer: v2 destinationYOdd param not bool for chain {}", chain_name );
                return;
            }
            const bool destination_y_odd = std::get<bool>( y_odd_value );
            auto       dest_opt          = eth::DecompressXOnlyPubkey( x_bytes, destination_y_odd );
            if ( !dest_opt )
            {
                logger_->error( "BridgeRelayer: X-only decompression failed for chain {} tx={}",
                                chain_name,
                                tx_hash.substr( 0, 16 ) );
                return;
            }
            destination = std::move( *dest_opt );
        }
        else
        {
            logger_->error( "BridgeRelayer: unexpected type for param 5 on chain {}", chain_name );
            return;
        }

        logger_->info( "BridgeRelayer: burn detected chain={} chain_name={} tx={} token={} amount={} dest={}",
                       chain_id,
                       chain_name,
                       tx_hash.substr( 0, 16 ),
                       mint_token_id.ToHex().substr( 0, 16 ),
                       amount,
                       destination.substr( 0, 16 ) );

        auto strong_tx_manager = tx_manager_.lock();
        if ( !strong_tx_manager )
        {
            logger_->error( "BridgeRelayer: no TransactionManager available for chain {}", chain_name );
            return;
        }

        auto result = strong_tx_manager->MintFunds( amount, tx_hash, chain_id, mint_token_id, destination );
        if ( result.has_error() )
        {
            if ( result.error() == std::errc::already_connected )
            {
                logger_->debug( "BridgeRelayer: duplicate burn rejected chain={} tx={}", chain_name, tx_hash.substr( 0, 16 ) );
            }
            else
            {
                logger_->error( "BridgeRelayer: MintFunds failed for chain={} tx={} error={}",
                                chain_name,
                                tx_hash.substr( 0, 16 ),
                                result.error().message() );
            }
            return;
        }

        logger_->info( "BridgeRelayer: mint submitted chain={} tx_hash={} mint_id={}",
                       chain_name,
                       tx_hash.substr( 0, 16 ),
                       result.value().substr( 0, 16 ) );
    }
} // namespace sgns
