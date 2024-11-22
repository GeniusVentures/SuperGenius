#include "UTXOTxParameters.hpp"
#include <boost/multiprecision/fwd.hpp>

sgns::UTXOTxParameters::UTXOTxParameters( const std::vector<GeniusUTXO>          &utxo_pool,
                                          const KeyGenerator::ElGamal::PublicKey &src_address,
                                          const std::vector<OutputDestInfo>      &destinations,
                                          std::string                             signature )
{
    int64_t total_amount = 0;

    for ( auto &dest_info : destinations )
    {
        total_amount += static_cast<int64_t>( dest_info.encrypted_amount );
    }

    int64_t remain = total_amount;

    for ( auto &utxo : utxo_pool )
    {
        if ( remain <= 0 )
        {
            break;
        }
        if ( utxo.GetLock() )
        {
            continue;
        }
        InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), signature };
        remain -= static_cast<int64_t>( utxo.GetAmount() );

        inputs_.push_back( curr_input );
    }
    if ( remain > 0 )
    {
        inputs_.clear();
        outputs_.clear();
    }
    else
    {
        outputs_ = destinations;
    }

    if ( remain < 0 )
    {
        uint256_t change( std::abs( remain ) );
        auto      str_address = src_address.public_key_value.str();
        outputs_.push_back( { change, uint256_t( str_address ) } );
    }
}
