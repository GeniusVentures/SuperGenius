/**
 * @file       BridgeRelayer.cpp
 * @brief      Processes bridge burn claims and submits MintFunds transactions.
 * @date       2026-05-30
 */
#include "account/BridgeRelayer.hpp"

#include <sstream>
#include <iomanip>

namespace sgns
{
    namespace
    {
        std::string HashToHex( const eth::Hash256 &hash )
        {
            std::ostringstream ss;
            ss << std::hex << std::setfill( '0' );
            for ( auto byte : hash )
            {
                ss << std::setw( 2 ) << static_cast<unsigned int>( byte );
            }
            return ss.str();
        }

        std::string AddressToHex( const eth::Address &addr )
        {
            std::ostringstream ss;
            ss << std::hex << std::setfill( '0' );
            for ( auto byte : addr )
            {
                ss << std::setw( 2 ) << static_cast<unsigned int>( byte );
            }
            return ss.str();
        }
    } // namespace

    BridgeRelayer::BridgeRelayer( std::shared_ptr<TransactionManager> tx_manager, base::Logger logger ) :
        tx_manager_( std::move( tx_manager ) ), logger_( std::move( logger ) )
    {
    }

    void BridgeRelayer::OnBridgeClaim( const eth::BridgeEventClaim &claim )
    {
        const std::string tx_hash_str = HashToHex( claim.tx_hash );
        const std::string recipient   = AddressToHex( claim.recipient );

        logger_->info( "BridgeRelayer: burn detected chain={} tx={} recipient={}",
                       claim.src_chain_id,
                       tx_hash_str.substr( 0, 16 ),
                       recipient.substr( 0, 16 ) );

        if ( !tx_manager_ )
        {
            logger_->error( "BridgeRelayer: no TransactionManager" );
            return;
        }

        // Map BridgeEventClaim → MintFunds parameters
        const std::string chain_id = std::to_string( claim.src_chain_id );

        // ERC-1155 token ID from the burn event — consensus validators verify
        // this matches the on-chain log via RPC (PublicChainInputValidator).
        // intx::uint256 → 32 bytes big-endian → TokenID
        std::array<uint8_t, 32> token_bytes{};
        for ( size_t i = 0; i < 32; ++i )
        {
            token_bytes[i] = static_cast<uint8_t>( ( claim.token_id_or_nonce >> ( 248 - i * 8 ) ) & 0xFF );
        }
        const TokenID token_id = TokenID::FromBytes( token_bytes.data(), token_bytes.size() );

        // Amount: uint256 → uint64 (bridge amounts must fit)
        if ( claim.amount > std::numeric_limits<uint64_t>::max() )
        {
            logger_->error( "BridgeRelayer: amount exceeds uint64 for tx={}", tx_hash_str.substr( 0, 16 ) );
            return;
        }
        const uint64_t amount = static_cast<uint64_t>( claim.amount );

        auto result = tx_manager_->MintFunds( amount, tx_hash_str, chain_id, token_id, recipient );
        if ( result.has_error() )
        {
            if ( result.error() == std::errc::already_connected )
            {
                logger_->debug( "BridgeRelayer: duplicate burn rejected tx={}", tx_hash_str.substr( 0, 16 ) );
            }
            else
            {
                logger_->error( "BridgeRelayer: MintFunds failed for tx={} error={}",
                                tx_hash_str.substr( 0, 16 ),
                                result.error().message() );
            }
            return;
        }

        logger_->info( "BridgeRelayer: mint submitted tx_hash={} mint_id={}",
                       tx_hash_str.substr( 0, 16 ),
                       result.value().substr( 0, 16 ) );
    }

    std::function<void( const eth::BridgeEventClaim & )> BridgeRelayer::GetClaimCallback()
    {
        return [this]( const eth::BridgeEventClaim &claim )
        {
            OnBridgeClaim( claim );
        };
    }
} // namespace sgns
