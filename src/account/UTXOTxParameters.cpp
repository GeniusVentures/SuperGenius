#include "UTXOTxParameters.hpp"
#include <boost/multiprecision/fwd.hpp>
#include <cmath>

sgns::UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO>     &utxo_pool,
                                          const std::string                 &src_address,
                                          const std::vector<OutputDestInfo> &destinations,
                                          std::string                        signature )
{
    uint64_t    total_amount = 0;
    std::string change_token;
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
        InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), signature };
        used_amount += utxo.GetAmount();
        inputs_.push_back( curr_input );
        change_token = utxo.GetTokenID();
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

sgns::UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool,
                                          const std::string             &src_address,
                                          uint64_t                       amount,
                                          const std::string              dest_address,
                                          std::string                    signature )
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
        inputs_.push_back( { utxo.GetTxID(), utxo.GetOutputIdx(), signature } );
        selected_utxos.push_back( utxo );
        selected_amount += utxo.GetAmount();
    }

    // Abort if insufficient funds
    if ( ( selected_amount < amount ) || ( selected_utxos.size() == 0 ) )
    {
        inputs_.clear();
        outputs_.clear();
        return;
    }

    outputs_.reserve( selected_utxos.size() + 1 );
    uint64_t used_amount = 0;

    // Replicate each UTXO except the last one: send full amount to dest
    for ( size_t i = 0; i < selected_utxos.size() - 1; i++ )
    {
        const auto &utxo     = selected_utxos[i];
        uint64_t    utxo_amt = utxo.GetAmount();
        outputs_.push_back( { utxo_amt, dest_address, utxo.GetTokenID() } );
        used_amount += utxo_amt;
    }

    // Handle the last UTXO: send only what's needed, then change
    uint64_t    needed    = amount - used_amount;
    const auto &last_utxo = selected_utxos.back();

    outputs_.push_back( { needed, dest_address, last_utxo.GetTokenID() } );
    uint64_t change = last_utxo.GetAmount() - needed;
    if ( change > 0 )
    {
        outputs_.push_back( { change, src_address, last_utxo.GetTokenID() } );
    }
}
