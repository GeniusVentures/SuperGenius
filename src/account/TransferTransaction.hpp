/**
 * @file       TransferTransaction.hpp
 * @brief      Transaction of currency transfer
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSFER_TRANSACTION_HPP_
#define _TRANSFER_TRANSACTION_HPP_
#include "account/IGeniusTransactions.hpp"
#include <boost/multiprecision/cpp_int.hpp>

namespace sgns
{
    class TransferTransaction : public IGeniusTransactions
    {
    public:
        const std::string GetType() const override
        {
            return "transfer";
        };
        TransferTransaction( ){};
        ~TransferTransaction(){};

    private:
        uint256_t              encrypted_amount; ///< El Gamal encrypted amount
        uint256_t              dest_address;     ///< Destination node address

    };

}

#endif
