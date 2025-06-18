/**
 * @file       UTXOTxParameters.hpp
 * @brief
 * @date       2024-04-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _UTXO_TX_PARAMETERS_HPP_
#define _UTXO_TX_PARAMETERS_HPP_

#include <utility>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

#include "account/GeniusUTXO.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    struct InputUTXOInfo
    {
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        std::string   signature_;
    };

    struct OutputDestInfo
    {
        uint64_t    encrypted_amount; ///< El Gamal encrypted amount
        std::string dest_address;     ///< Destination node address
        std::string token_id;         ///< Token identifier
    };

    struct UTXOTxParameters
    {
        std::vector<InputUTXOInfo>  inputs_;
        std::vector<OutputDestInfo> outputs_;

        UTXOTxParameters( const std::vector<InputUTXOInfo> &inputs, const std::vector<OutputDestInfo> &outputs ) :
            inputs_( inputs ), outputs_( outputs )
        {
        }

        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO> &utxo_pool,
                                                         const std::string             &src_address,
                                                         uint64_t                       amount,
                                                         std::string                    dest_address,
                                                         std::string                    signature = "" )
        {
            UTXOTxParameters instance( utxo_pool,
                                       src_address,
                                       amount,
                                       std::move( dest_address ),
                                       std::move( signature ) );

            if ( !instance.inputs_.empty() )
            {
                return instance;
            }

            return outcome::failure( boost::system::error_code{} );
        }

        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO>     &utxo_pool,
                                                         const std::string                 &src_address,
                                                         const std::vector<OutputDestInfo> &destinations,
                                                         std::string                        signature = "" )
        {
            UTXOTxParameters instance( utxo_pool, src_address, destinations, std::move( signature ) );

            if ( !instance.inputs_.empty() )
            {
                return instance;
            }
            return outcome::failure( boost::system::error_code{} );
        }

        static std::vector<GeniusUTXO> UpdateUTXOList( const std::vector<GeniusUTXO> &utxo_pool,
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

    private:
        UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool,
                          const std::string             &src_address,
                          uint64_t                       amount,
                          std::string                    dest_address,
                          std::string                    signature );

        UTXOTxParameters( const std::vector<GeniusUTXO>     &utxo_pool,
                          const std::string                 &src_address,
                          const std::vector<OutputDestInfo> &destinations,
                          std::string                        signature );
    };
}

#endif
