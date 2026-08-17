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
        const std::string event_sig_v1( kBridgeSourceBurnedSig );
        auto              params_v1    = eth::cli::event_registry().params_for( event_sig_v1 );

        // v2: BridgeOutInitiated(address indexed sender, uint256 id, uint256 amount,
        //                         uint256 srcChainID, uint256 destChainID,
        //                         bytes32 sgnsDestination, bool destinationYOdd)
        // Param 5 is a 32-byte X-only key (decoded as codec::Hash256) and param 6
        // carries the Y parity needed for deterministic decompression (D-06/D-07).
        const std::string event_sig_v2( kBridgeOutInitiatedSig );
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

    outcome::result<BurnEventParams> BridgeRelayer::ParseBurnEventValues(
        const std::vector<eth::abi::AbiValue> &values )
    {
        static constexpr size_t kExpectedMinParams    = 6;
        static constexpr size_t kTokenIdIndex         = 1;
        static constexpr size_t kAmountIndex          = 2;
        static constexpr size_t kSgnsDestinationIndex = 5;
        static constexpr size_t kDestinationYOddIndex = 6;
        // SG public key is the uncompressed X||Y coordinates (32 + 32 bytes).
        static constexpr size_t kSgnsPubKeyBytes      = 64;

        if ( values.size() < kExpectedMinParams )
        {
            BridgeRelayerLogger()->error( "ParseBurnEventValues: expected at least {} values, got {}",
                                          kExpectedMinParams,
                                          values.size() );
            return outcome::failure( std::errc::invalid_argument );
        }

        // Token ID (uint256 at index 1).
        if ( !std::holds_alternative<intx::uint256>( values[kTokenIdIndex] ) )
        {
            BridgeRelayerLogger()->error( "ParseBurnEventValues: token_id not uint256" );
            return outcome::failure( std::errc::invalid_argument );
        }
        const auto &token_id_uint = std::get<intx::uint256>( values[kTokenIdIndex] );
        TokenID     token_id = TokenID::FromUint256( token_id_uint, TokenID::Endianness::BIG );

        // Amount (uint256 at index 2).
        if ( !std::holds_alternative<intx::uint256>( values[kAmountIndex] ) )
        {
            BridgeRelayerLogger()->error( "ParseBurnEventValues: amount not uint256" );
            return outcome::failure( std::errc::invalid_argument );
        }
        const auto &amount_uint   = std::get<intx::uint256>( values[kAmountIndex] );
        auto        amount_result = Uint256ToUint64( amount_uint, "amount" );
        if ( !amount_result )
        {
            return outcome::failure( std::errc::value_too_large );
        }
        uint64_t amount = amount_result.value();

        // Destination: dispatch on variant type (v1 = ByteBuffer, v2 = Hash256).
        std::string destination;
        const auto &dest_val = values[kSgnsDestinationIndex];
        if ( std::holds_alternative<eth::codec::ByteBuffer>( dest_val ) )
        {
            const auto &dest_bytes = std::get<eth::codec::ByteBuffer>( dest_val );
            // Require exactly the 64-byte SG public key (128 hex chars). An empty
            // or wrong-length payload would otherwise yield "" (which MintFunds
            // silently credits to the relayer's own address) or a malformed
            // recipient — reject the burn instead.
            if ( dest_bytes.size() != kSgnsPubKeyBytes )
            {
                BridgeRelayerLogger()->error(
                    "ParseBurnEventValues: v1 sgnsDestination must be {} bytes, got {}",
                    kSgnsPubKeyBytes, dest_bytes.size() );
                return outcome::failure( std::errc::invalid_argument );
            }
            // hex_bytes() prepends "0x"; GetAddress() and the v2 decompression
            // path return a bare 128-char hex string, so strip the prefix —
            // otherwise v1 mints are addressed to "0x"+key and recipient
            // (non-full) nodes won't index them as spendable.
            destination = rlp::base::parse::hex_bytes( dest_bytes.data(), dest_bytes.size() )
                              .substr( rlp::base::parse::kHexCharsPerByte );
        }
        else if ( std::holds_alternative<eth::codec::Hash256>( dest_val ) )
        {
            if ( values.size() <= kDestinationYOddIndex )
            {
                BridgeRelayerLogger()->error( "ParseBurnEventValues: v2 event missing destinationYOdd" );
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto &x_bytes = std::get<eth::codec::Hash256>( dest_val );

            if ( !std::holds_alternative<bool>( values[kDestinationYOddIndex] ) )
            {
                BridgeRelayerLogger()->error( "ParseBurnEventValues: destinationYOdd not bool" );
                return outcome::failure( std::errc::invalid_argument );
            }
            const bool destination_y_odd = std::get<bool>( values[kDestinationYOddIndex] );

            auto dest_opt = eth::DecompressXOnlyPubkey( x_bytes, destination_y_odd );
            if ( !dest_opt )
            {
                BridgeRelayerLogger()->error( "ParseBurnEventValues: X-only decompression failed" );
                return outcome::failure( std::errc::invalid_argument );
            }
            destination = std::move( *dest_opt );
        }
        else
        {
            BridgeRelayerLogger()->error( "ParseBurnEventValues: unexpected type for sgnsDestination" );
            return outcome::failure( std::errc::invalid_argument );
        }

        return BurnEventParams{ token_id, amount, std::move( destination ) };
    }

    void BridgeRelayer::OnWatchEvent( const eth::WatchEventNotification &notification,
                                       const std::string                 &chain_name )
    {
        auto parsed = ParseBurnEventValues( notification.values );
        if ( !parsed )
        {
            logger_->error( "BridgeRelayer: failed to parse burn event for chain {}: {}",
                            chain_name,
                            parsed.error().message() );
            return;
        }

        auto                  &burn        = parsed.value();
        static constexpr size_t kSrcChainIndex = 3;

        if ( !notification.event.receipt_log_index.has_value() )
        {
            logger_->error( "BridgeRelayer: rejecting burn without receipt-local index for chain {}", chain_name );
            return;
        }
        const uint32_t receipt_log_index = *notification.event.receipt_log_index;

        // MintFunds persists the burn identity as canonical bare lowercase hex.
        // hex_array_string() is intended for JSON-RPC and prepends "0x", so
        // remove that wire-format prefix at this API boundary.
        std::string tx_hash = rlp::base::parse::hex_array_string( notification.event.tx_hash );
        if ( tx_hash.rfind( "0x", 0 ) == 0 )
        {
            tx_hash.erase( 0, 2 );
        }

        // Chain ID from the event's srcChainID
        const std::string chain_id = std::to_string(
            static_cast<uint64_t>( std::get<intx::uint256>( notification.values[kSrcChainIndex] ) ) );

        logger_->info( "BridgeRelayer: burn detected chain={} chain_name={} tx={} receipt_index={} token={} amount={} dest={}",
                       chain_id,
                       chain_name,
                       tx_hash.substr( 0, 16 ),
                       receipt_log_index,
                       burn.token_id.ToHex().substr( 0, 16 ),
                       burn.amount,
                       burn.destination.substr( 0, 16 ) );

        auto strong_tx_manager = tx_manager_.lock();
        if ( !mint_funds_override_ && !strong_tx_manager )
        {
            logger_->error( "BridgeRelayer: no TransactionManager available for chain {}", chain_name );
            return;
        }

        auto result = mint_funds_override_
                        ? mint_funds_override_( burn.amount,
                                                tx_hash,
                                                chain_id,
                                                receipt_log_index,
                                                burn.token_id,
                                                burn.destination )
                        : strong_tx_manager->MintFunds( burn.amount,
                                                        tx_hash,
                                                        chain_id,
                                                        receipt_log_index,
                                                        burn.token_id,
                                                        burn.destination );
        if ( result.has_error() )
        {
            if ( result.error() == std::errc::already_connected )
            {
                logger_->debug( "BridgeRelayer: duplicate burn rejected chain={} tx={}",
                                chain_name,
                                tx_hash.substr( 0, 16 ) );
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
