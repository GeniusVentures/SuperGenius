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
                           uint64_t                        amount,
                           uint256_t                       dev_addr,
                           uint64_t                        peers_cut,
                           const SGTransaction::DAGStruct &dag );
        ~EscrowTransaction() override = default;

        std::vector<uint8_t>                      SerializeByteVector() override;
        static std::shared_ptr<EscrowTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );
        uint64_t                                  GetNumChunks() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

        uint256_t GetDevAddress() const
        {
            return dev_addr_;
        }

        uint64_t GetAmount() const
        {
            return amount_;
        }

        uint64_t GetPeersCut() const
        {
            return peers_cut_;
        }

    private:
        UTXOTxParameters utxo_params_;
        uint64_t         amount_;
        uint256_t        dev_addr_;
        uint64_t         peers_cut_;

        /**
         * @brief       Registers the deserializer for the transfer transaction type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( "escrow", &EscrowTransaction::DeSerializeByteVector );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
