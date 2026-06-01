/**
 * @file       TransferTransaction.hpp
 * @brief      Transaction of currency transfer
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _TRANSFER_TRANSACTION_HPP_
#define _TRANSFER_TRANSACTION_HPP_

#include "account/GeniusTransaction.hpp"
#include "UTXOStructs.hpp"
#include "account/proto/SGTransaction.pb.h"

namespace sgns
{
    /**
     * @brief Transaction for transferring funds between UTXO inputs and outputs.
     */
    class TransferTransaction final : public GeniusTransaction
    {
    public:
        static TransferTransaction New( std::vector<InputUTXOInfo>  inputs,
                                        std::vector<OutputDestInfo> destinations,
                                        SGTransaction::DAGStruct    dag );
        /**
         * @brief      Default Transfer Transaction destructor
         */
        ~TransferTransaction() override = default;

        /**
         * @brief      Serializes the transaction into a byte vector.
         * @return     Serialized bytes.
         */
        using GeniusTransaction::SerializeByteVector;
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief      Deserializes a TransferTransaction from bytes.
         * @param[in]  data Serialized bytes.
         * @return     Shared pointer to the deserialized transaction.
         */
        static std::shared_ptr<TransferTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        std::vector<OutputDestInfo> GetDstInfos() const;
        std::vector<InputUTXOInfo>  GetInputInfos() const;

        /**
         * @brief       Returns if transaction supports UTXOs
         * @return      True if supported, false otherwise
         */
        bool                        HasUTXOParameters() const override;

        /**
         * @brief       Returns the UTXOs
         * @return      If exists, returns the UTXOs of the transaction
         */
        std::optional<UTXOTxParameters> GetUTXOParametersOpt() const override;

        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        std::unordered_set<std::string> GetTopics() const override;

    private:
        /**
         * @brief       Construct a new TransferTransaction object.
         * @param[in]   destinations Destination outputs.
         * @param[in]   inputs Input UTXOs.
         * @param[in]   dag DAG struct describing the transaction graph.
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
        static bool Register()
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
