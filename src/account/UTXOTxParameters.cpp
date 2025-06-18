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
    if ( ( selected_amount < amount ) || ( selected_utxos.empty() ) )
    {
        inputs_.clear();
        outputs_.clear();
        return;
    }

    // Aggregate destination amounts by TokenID
    std::unordered_map<std::string, uint64_t> send_totals;
    uint64_t                                  used_amount = 0;
    for ( const auto &utxo : selected_utxos )
    {
        const auto &token    = utxo.GetTokenID();
        uint64_t    utxo_amt = utxo.GetAmount();

        if ( used_amount + utxo_amt <= amount )
        {
            send_totals[token] += utxo_amt;
            used_amount        += utxo_amt;
        }
        else
        {
            uint64_t needed     = amount - used_amount;
            send_totals[token] += needed;
            used_amount        += needed;
            break;
        }
    }

    // Reserve space: one output per token plus possible change
    outputs_.reserve( send_totals.size() + 1 );

    // Create one output per TokenID for the destination
    for ( const auto &entry : send_totals )
    {
        outputs_.push_back( { entry.second, dest_address, entry.first } );
    }

    // Single change output (always from the last UTXO's token)
    uint64_t change = selected_amount - amount;
    if ( change > 0 )
    {
        const auto &last_utxo = selected_utxos.back();
        outputs_.push_back( { change, src_address, last_utxo.GetTokenID() } );
    }
}
