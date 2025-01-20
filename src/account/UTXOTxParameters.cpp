#include "UTXOTxParameters.hpp"
#include <boost/multiprecision/fwd.hpp>
#include <cmath>


sgns::UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO>          &utxo_pool,
                                          const KeyGenerator::ElGamal::PublicKey &src_address,
                                          const std::vector<OutputDestInfo>      &destinations,
                                          std::string                             signature )
{
    double total_amount = 0.0;

    for ( auto &dest_info : destinations )
    {
        total_amount += dest_info.encrypted_amount;
    }

    double remain  = total_amount;
    double epsilon = 1e-9; // Define a tolerance

    for ( auto &utxo : utxo_pool )
    {
        if ( remain <= epsilon )
        {
            break;
        }
        if ( utxo.GetLock() )
        {
            continue;
        }
        InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), signature };
        remain -= utxo.GetAmount();

        inputs_.push_back( curr_input );
    }
    if ( remain > epsilon )
    {
        inputs_.clear();
        outputs_.clear();
    }
    else
    {
        outputs_ = destinations;
    }

    if ( remain < epsilon )
    {
        double change( std::abs( remain ) );
        auto   str_address = src_address.public_key_value.str();
        outputs_.push_back( { change, uint256_t( str_address ) } );
    }
}
