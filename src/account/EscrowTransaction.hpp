/**
 * @file       EscrowTransaction.hpp
 * @brief      
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ESCROW_TRANSACTION_HPP_
#define _ESCROW_TRANSACTION_HPP_
#include <cstdint>
#include <string>
#include "account/IGeniusTransactions.hpp"
#include "account/UTXOTxParameters.hpp"
namespace sgns
{
    class EscrowTransaction : public IGeniusTransactions
    {
    public:
        EscrowTransaction( const UTXOTxParameters &params, const uint64_t &num_chunks, const SGTransaction::DAGStruct &dag );
        ~EscrowTransaction() = default;

        std::vector<uint8_t>     SerializeByteVector() override;
        static EscrowTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        uint64_t                 GetAmount() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

    private:
        uint64_t         num_chunks_;
        UTXOTxParameters utxo_params_;
    };
}

#endif
