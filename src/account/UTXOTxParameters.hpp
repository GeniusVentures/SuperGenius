/**
 * @file       UTXOTxParameters.hpp
 * @brief      Helpers to build UTXO-based transaction parameters
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

    /**
     * @brief   Raw UTXO input data for a transaction
     */
    struct InputUTXOInfo
    {
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        std::string   signature_;
    };

    /**
     * @brief   Single output entry
     */
    struct OutputDestInfo
    {
        uint64_t    encrypted_amount; ///< El Gamal encrypted amount
        std::string dest_address;     ///< Destination node address
        std::string token_id;         ///< Token identifier
    };

    struct UTXOTxParameters
    {
        std::vector<InputUTXOInfo>  inputs_;  ///< Selected inputs after UTXO filtering
        std::vector<OutputDestInfo> outputs_; ///< Final outputs (destinations + change)

        UTXOTxParameters( const std::vector<InputUTXOInfo> &inputs, const std::vector<OutputDestInfo> &outputs ) :
            inputs_( inputs ), outputs_( outputs )
        {
        }

        /**
         * @brief     Single-destination UTXO transaction parameters
         * @param[in] utxo_pool    Vector of available UTXOs
         * @param[in] src_address  Sender address (for inputs/change)
         * @param[in] amount       Amount to send to the destination
         * @param[in] dest_address Destination address for the payment
         * @param[in] token_id     Token identifier to filter UTXOs; if empty, all token types are accepted
         * @return outcome::result<UTXOTxParameters>
         *         On success, a fully-constructed parameter object;
         *         on failure (e.g. insufficient funds), an error result.
         */
        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO> &utxo_pool,
                                                         const std::string             &src_address,
                                                         uint64_t                       amount,
                                                         std::string                    dest_address,
                                                         std::string                    token_id );
        /**
         * @brief Multi-destination UTXO transaction parameters
         * @param utxo_pool    Vector of available UTXOs
         * @param src_address  Sender address (for inputs/change)
         * @param destinations List of (amount, address) outputs
         * @param token_id     Token identifier to filter UTXOs; if empty, all token types are accepted
         * @return outcome::result<UTXOTxParameters>
         */
        static outcome::result<UTXOTxParameters> create( const std::vector<GeniusUTXO>     &utxo_pool,
                                                         const std::string                 &src_address,
                                                         const std::vector<OutputDestInfo> &destinations,
                                                         std::string                        token_id );
        /**
         * @brief       Lock spent UTXOs from a given pool.
         * @param[in]   utxo_pool  Original UTXO list
         * @param[in]   params     Transaction parameters that consumed some UTXOs
         * @return      New pool with all inputs from @p params.inputs_ locked
         */
        static std::vector<GeniusUTXO> UpdateUTXOList( const std::vector<GeniusUTXO> &utxo_pool,
                                                       const UTXOTxParameters        &params );

        bool SignParameters( std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );

    private:
        /**
         * @brief   Internal constructor for single-destination factory.
         * @note    Use create(...) instead.
         */
        UTXOTxParameters( const std::vector<GeniusUTXO> &utxo_pool,
                          const std::string             &src_address,
                          uint64_t                       amount,
                          std::string                    dest_address,
                          std::string                    token_id );

        /**
         * @brief   Internal constructor for multi-destination factory.
         * @note    Use create(...) instead.
         */
        UTXOTxParameters( const std::vector<GeniusUTXO>     &utxo_pool,
                          const std::string                 &src_address,
                          const std::vector<OutputDestInfo> &destinations,
                          std::string                        token_id );
    };
}

#endif
