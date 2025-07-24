#include "UTXOTxParameters.hpp"
#include <boost/multiprecision/fwd.hpp>
#include <cmath>

#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

namespace sgns
{

    outcome::result<UTXOTxParameters> UTXOTxParameters::create( const std::vector<GeniusUTXO> &utxo_pool,
                                                                const std::string             &src_address,
                                                                uint64_t                       amount,
                                                                std::string                    dest_address,
                                                                TokenID                        token_id )
    {
        UTXOTxParameters instance( utxo_pool, src_address, amount, std::move( dest_address ), std::move( token_id ) );

        if ( !instance.inputs_.empty() )
        {
            return instance;
        }

        return outcome::failure( boost::system::error_code{} );
    }

    outcome::result<UTXOTxParameters> UTXOTxParameters::create( const std::vector<GeniusUTXO>     &utxo_pool,
                                                                const std::string                 &src_address,
                                                                const std::vector<OutputDestInfo> &destinations,
                                                                TokenID                            token_id )
    {
        UTXOTxParameters instance( utxo_pool, src_address, destinations, std::move( token_id ) );

        if ( !instance.inputs_.empty() )
        {
            return instance;
        }
        return outcome::failure( boost::system::error_code{} );
    }

    UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO>     &utxo_pool,
                                        const std::string                 &src_address,
                                        const std::vector<OutputDestInfo> &destinations,
                                        TokenID                            token_id )
    {
        uint64_t    total_amount = 0;
        TokenID change_token;
        for ( const auto &d : destinations )
        {
            total_amount += d.encrypted_amount;
        }

        uint64_t used_amount = 0;
        for ( const auto &utxo : utxo_pool )
        {
            if ( used_amount >= total_amount )
            {
                break;
            }
            if ( utxo.GetLock() )
            {
                continue;
            }
            if ( !token_id.Equals( utxo.GetTokenID() ) )
            {
                continue;
            }
            InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), "" };
            used_amount += utxo.GetAmount();
            inputs_.push_back( curr_input );
            change_token = token_id;
        }

        if ( used_amount < total_amount )
        {
            inputs_.clear();
            outputs_.clear();
        }
        else
        {
            outputs_ = destinations;
        }

        if ( used_amount > total_amount )
        {
            uint64_t change = used_amount - total_amount;
            outputs_.push_back( { change, src_address, change_token } );
        }
    }

    UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool,
                                        const std::string             &src_address,
                                        uint64_t                       amount,
                                        const std::string              dest_address,
                                        TokenID                        token_id )
    {
        // Select UTXOs until we cover the requested amount
        std::vector<GeniusUTXO> selected_utxos;
        uint64_t                selected_amount = 0;
        for ( const auto &utxo : utxo_pool )
        {
            if ( selected_amount >= amount )
            {
                break;
            }
            if ( utxo.GetLock() )
            {
                continue;
            }
            if ( !token_id.Equals( utxo.GetTokenID() ) )
            {
                continue;
            }
            inputs_.push_back( { utxo.GetTxID(), utxo.GetOutputIdx(), "" } );
            selected_utxos.push_back( utxo );
            selected_amount += utxo.GetAmount();
        }

        // Abort if insufficient funds
        if ( ( selected_amount < amount ) || ( selected_utxos.empty() ) )
        {
            inputs_.clear();
            outputs_.clear();
            return;
        }

        // Aggregate destination amounts by TokenID
        uint64_t send_totals = 0;
        uint64_t used_amount = 0;
        for ( const auto &utxo : selected_utxos )
        {
            uint64_t utxo_amt = utxo.GetAmount();

            if ( used_amount + utxo_amt <= amount )
            {
                send_totals += utxo_amt;
                used_amount += utxo_amt;
            }
            else
            {
                uint64_t needed  = amount - used_amount;
                send_totals     += needed;
                used_amount     += needed;
                break;
            }
        }

        // Reserve space: one output per token plus possible change
        outputs_.reserve( 2 );

        // Create one output per TokenID for the destination

        outputs_.push_back( { send_totals, dest_address, token_id } );

        // Single change output (always from the last UTXO's token)
        uint64_t change = selected_amount - amount;
        if ( change > 0 )
        {
            const auto &last_utxo = selected_utxos.back();
            outputs_.push_back( { change, src_address, token_id } );
        }
    }

    std::vector<GeniusUTXO> UTXOTxParameters::UpdateUTXOList( const std::vector<GeniusUTXO> &utxo_pool,
                                                              const UTXOTxParameters        &params )
    {
        auto updated_list = utxo_pool;

        for ( auto &input_utxo : params.inputs_ )
        {
            for ( auto &utxo : updated_list )
            {
                if ( input_utxo.txid_hash_ == utxo.GetTxID() )
                {
                    utxo.ToggleLock( true );
                }
            }
        }

        return updated_list;
    }

    bool UTXOTxParameters::SignParameters( std::shared_ptr<GeniusAccount> account )
    {
        //TODO -- Fill the signature field
        return true;
    }

}
