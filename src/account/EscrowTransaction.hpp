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
namespace sgns
{
    class EscrowTransaction : public IGeniusTransactions
    {
    public:
        EscrowTransaction( const uint64_t &hold_amount, const std::string &job_id, const SGTransaction::DAGStruct &dag );
        EscrowTransaction( const std::string &job_id, const SGTransaction::DAGStruct &dag );
        ~EscrowTransaction() = default;

        std::vector<uint8_t>     SerializeByteVector() override;
        static EscrowTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        uint64_t                 GetAmount() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

        bool IsRelease() const
        {
            return is_release_;
        }

    private:
        uint64_t    amount_;
        std::string job_id_;
        bool        is_release_;
    };
}

#endif
