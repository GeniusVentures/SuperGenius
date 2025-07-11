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
        static EscrowTransaction New( UTXOTxParameters         params,
                                      uint64_t                 amount,
                                      std::string              dev_addr,
                                      uint64_t                 peers_cut,
                                      SGTransaction::DAGStruct dag,
                                      std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );

        static std::shared_ptr<EscrowTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        ~EscrowTransaction() override = default;

        std::vector<uint8_t> SerializeByteVector() override;
        uint64_t             GetNumChunks() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

        std::string GetDevAddress() const
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
        EscrowTransaction( UTXOTxParameters         params,
                           uint64_t                 amount,
                           std::string              dev_addr,
                           uint64_t                 peers_cut,
                           SGTransaction::DAGStruct dag );

        UTXOTxParameters utxo_params_;
        uint64_t         amount_;
        std::string      dev_addr_;
        uint64_t         peers_cut_;

        /**
         * @brief       Registers the deserializer for the transfer transaction type.
         * @return      A boolean indicating successful registration.
         */
        static bool Register()
        {
            RegisterDeserializer( "escrow-hold", &EscrowTransaction::DeSerializeByteVector );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
