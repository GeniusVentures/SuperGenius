/**
 * @file       MintTransaction.hpp
 * @brief      
 * @date       2024-03-15
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _MINT_TRANSACTION_HPP_
#define _MINT_TRANSACTION_HPP_

#include <vector>
#include <cstdint>

#include "account/IGeniusTransactions.hpp"

namespace sgns
{
    class MintTransaction : public IGeniusTransactions
    {
    public:
        MintTransaction( uint64_t new_amount, const SGTransaction::DAGStruct &dag );
        ~MintTransaction() override = default;

        std::vector<uint8_t>   SerializeByteVector() override;
        static MintTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        uint64_t               GetAmount() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

    private:
        uint64_t amount;
    };
}

#endif
