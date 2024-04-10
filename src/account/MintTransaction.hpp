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
        MintTransaction( const uint64_t &new_amount );
        ~MintTransaction() = default;

        std::vector<uint8_t> SerializeByteVector() override;
        static MintTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        const uint64_t GetAmount() const;

    private:
        uint64_t amount;
    };
}

#endif
