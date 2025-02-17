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
        EscrowTransaction( UTXOTxParameters                params,
                           uint64_t                        num_chunks,
                           uint256_t                       dest_addr,
                           float                           cut,
                           const SGTransaction::DAGStruct &dag );
        ~EscrowTransaction() override = default;

        std::vector<uint8_t>     SerializeByteVector() override;
        static EscrowTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        uint64_t                 GetNumChunks() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

        uint256_t GetDestAddress() const
        {
            return dev_addr;
        }
        float GetDestCut() const
        {
            return dev_cut;
        }

    private:
        uint64_t         num_chunks_;
        uint256_t        dev_addr;
        float            dev_cut;
        UTXOTxParameters utxo_params_;
    };
}

#endif
