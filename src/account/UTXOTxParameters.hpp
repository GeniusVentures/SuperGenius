/**
 * @file       UTXOTxParameters.hpp
 * @brief      
 * @date       2024-04-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _UTXO_TX_PARAMETERS_HPP_
#define _UTXO_TX_PARAMETERS_HPP_

#include "account/GeniusUTXO.hpp"
#include <vector>
#include <boost/outcome.hpp>

namespace sgns
{
    struct InputUTXOInfo
    {
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        std::string   signature_;
    };
    struct OutputDestInfo
    {
        uint256_t encrypted_amount; ///< El Gamal encrypted amount
        uint256_t dest_address;     ///< Destination node address
    };

    struct UTXOTxParameters
    {
        std::vector<InputUTXOInfo>  inputs_;
        std::vector<OutputDestInfo> outputs_;

        outcome::result<UTXOTxParameters> create( std::vector<GeniusUTXO> &utxo_pool, const uint64_t &amount, const uint256_t &dest_address,
                                                  const std::string signature = "" )
        {
            UTXOTxParameters instance( utxo_pool, amount, dest_address, signature );

            if ( instance.inputs_.size() )
            {
                return instance;
            }
            else
            {
                return outcome::failure( boost::system::error_code{} );
            }
        }
        outcome::result<UTXOTxParameters> create( std::vector<GeniusUTXO> &utxo_pool, const std::vector<OutputDestInfo> &destinations,
                                                  const std::string signature = "" )
        {
            UTXOTxParameters instance( utxo_pool, destinations, signature );

            if ( instance.inputs_.size() )
            {
                return instance;
            }
            else
            {
                return outcome::failure( boost::system::error_code{} );
            }
        }

    private:
        UTXOTxParameters( std::vector<GeniusUTXO> &utxo_pool, const uint64_t &amount, const uint256_t &dest_address, const std::string signature ) :
            UTXOTxParameters( utxo_pool, { OutputDestInfo{ uint256_t{ amount }, dest_address } }, signature )
        {
        }

        UTXOTxParameters( std::vector<GeniusUTXO> &utxo_pool, const std::vector<OutputDestInfo> &destinations, const std::string signature )
        {
            int64_t total_amount = 0;

            for ( auto &dest_info : destinations )
            {
                total_amount += static_cast<int64_t>( dest_info.encrypted_amount );
            }
            auto temp_utxos = utxo_pool; //so we don't change anything unless success;

            int64_t remain = total_amount;

            for ( auto &utxo : temp_utxos )
            {
                if ( remain <= 0 )
                {
                    break;
                }
                InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), signature };
                remain -= utxo.GetAmount();
                utxo.ToggleLock( true );
                inputs_.push_back( curr_input );
            }
            if ( remain <= 0 )
            {
                utxo_pool = temp_utxos;
                outputs_  = destinations;
                outputs_.push_back( { uint256_t( std::abs( remain ) ), destinations[0].dest_address } );
            }
            else
            {
                inputs_.clear();
                outputs_.clear();
            }
        }
    };
}

#endif
