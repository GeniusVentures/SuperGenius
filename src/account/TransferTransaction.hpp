/**
 * @file       TransferTransaction.hpp
 * @brief      Transaction of currency transfer
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSFER_TRANSACTION_HPP_
#define _TRANSFER_TRANSACTION_HPP_
#include <boost/multiprecision/cpp_int.hpp>

#include "account/IGeniusTransactions.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/UTXOTxParameters.hpp"

namespace sgns
{
    class TransferTransaction : public IGeniusTransactions
    {
    public:
        static TransferTransaction New( std::vector<OutputDestInfo>                     destinations,
                                        std::vector<InputUTXOInfo>                      inputs,
                                        SGTransaction::DAGStruct                        dag,
                                        std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );
        /**
         * @brief      Default Transfer Transaction destructor
         */
        ~TransferTransaction() override = default;

        /**
         * @brief      
         * @return      A @ref std::vector<uint8_t> 
         */
        std::vector<uint8_t> SerializeByteVector() override;

        /**
         * @brief      
         * @param[in]   data 
         * @return      A @ref TransferTransaction 
         */
        static std::shared_ptr<TransferTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        std::vector<OutputDestInfo> GetDstInfos() const;
        std::vector<InputUTXOInfo>  GetInputInfos() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

    private:
        //TODO - El Gamal encrypt the amount. Now only copying
        /**
         * @brief       Construct a new Transfer Transaction object
         * @param[in]   amount: Raw amount of the transaction
         * @param[in]   destination: Address of the destination
         */
        TransferTransaction( std::vector<OutputDestInfo> destinations,
                             std::vector<InputUTXOInfo>  inputs,
                             SGTransaction::DAGStruct    dag );

        std::vector<InputUTXOInfo>  input_tx_;
        std::vector<OutputDestInfo> outputs_;

        /**
         * @brief       Registers the deserializer for the transfer transaction type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( "transfer", &TransferTransaction::DeSerializeByteVector );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };

}

#endif
