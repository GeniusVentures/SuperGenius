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
#include "account/TokenID.hpp"

namespace sgns
{
    class MintTransaction : public IGeniusTransactions
    {
    public:
        ~MintTransaction() override = default;

        static std::shared_ptr<MintTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        static MintTransaction New( uint64_t                                        new_amount,
                                    std::string                                     chain_id,
                                    TokenID                                         token_id,
                                    SGTransaction::DAGStruct                        dag,
                                    std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );

        std::vector<uint8_t> SerializeByteVector() override;

        uint64_t GetAmount() const;

        TokenID GetTokenID() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

    private:
        MintTransaction( uint64_t new_amount, std::string chain_id, TokenID token_id, SGTransaction::DAGStruct dag );

        uint64_t    amount;
        std::string chain_id;
        TokenID     token_id;

        /**
         * @brief       Registers the deserializer for the transfer transaction type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( "mint", &MintTransaction::DeSerializeByteVector );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
