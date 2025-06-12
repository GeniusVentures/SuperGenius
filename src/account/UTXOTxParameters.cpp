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
