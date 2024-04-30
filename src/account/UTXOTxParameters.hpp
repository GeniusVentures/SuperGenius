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

        UTXOTxParameters( const std::vector<InputUTXOInfo> &inputs, const std::vector<OutputDestInfo> &outputs ) :
            inputs_( inputs ), outputs_( outputs )
        {
        }

        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO> &utxo_pool, const uint256_t &src_address,
                                                         const uint64_t &amount, const uint256_t &dest_address, const std::string signature = "" )
        {
            UTXOTxParameters instance( utxo_pool, src_address, amount, dest_address, signature );

            if ( instance.inputs_.size() )
            {
                return instance;
            }
            else
            {
                return outcome::failure( boost::system::error_code{} );
            }
        }
        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO> &utxo_pool, const uint256_t &src_address,
                                                         const std::vector<OutputDestInfo> &destinations, const std::string signature = "" )
        {
            UTXOTxParameters instance( utxo_pool, src_address, destinations, signature );

            if ( instance.inputs_.size() )
            {
                return instance;
            }
            else
            {
                return outcome::failure( boost::system::error_code{} );
            }
        }

        static std::vector<GeniusUTXO> UpdateUTXOList( const std::vector<GeniusUTXO> &utxo_pool, const UTXOTxParameters &params )
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

    private:
        UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool, const uint256_t &src_address, const uint64_t &amount,
                          const uint256_t &dest_address, const std::string signature ) :
            UTXOTxParameters( utxo_pool, src_address, { OutputDestInfo{ uint256_t{ amount }, dest_address } }, signature )
        {
        }

        UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool, const uint256_t &src_address, const std::vector<OutputDestInfo> &destinations,
                          const std::string signature )
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
                InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), signature };
                remain -= utxo.GetAmount();

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
                outputs_.push_back( { change, src_address } );
            }
        }
    };
}

#endif
