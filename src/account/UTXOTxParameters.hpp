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

#include <ProofSystem/EthereumKeyGenerator.hpp>

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
                                                         std::string                    token_id = "" );
        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO>     &utxo_pool,
                                                         const std::string                 &src_address,
                                                         const std::vector<OutputDestInfo> &destinations,
                                                         std::string                        token_id = "" );

        static std::vector<GeniusUTXO> UpdateUTXOList( const std::vector<GeniusUTXO> &utxo_pool,
                                                       const UTXOTxParameters        &params );

        bool SignParameters( std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );

    private:
        UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool,
                          const std::string             &src_address,
                          uint64_t                       amount,
                          std::string                    dest_address,
                          std::string                    token_id );

        UTXOTxParameters( const std::vector<GeniusUTXO>     &utxo_pool,
                          const std::string                 &src_address,
                          const std::vector<OutputDestInfo> &destinations,
                          std::string                        token_id );
    };
}

#endif
