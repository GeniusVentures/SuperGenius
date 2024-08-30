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

using namespace boost::multiprecision;
namespace sgns
{
    class TransferTransaction : public IGeniusTransactions
    {
    public:
        //TODO - El Gamal encrypt the amount. Now only copying
        /**
         * @brief       Construct a new Transfer Transaction object
         * @param[in]   amount: Raw amount of the transaction
         * @param[in]   destination: Address of the destination
         */
        TransferTransaction( const std::vector<OutputDestInfo> &destinations, const std::vector<InputUTXOInfo> &inputs,
                             const SGTransaction::DAGStruct &dag );

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
        static TransferTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );

        std::vector<OutputDestInfo> GetDstInfos() const;
        std::vector<InputUTXOInfo>  GetInputInfos() const;

        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

    private:
        std::vector<InputUTXOInfo>  input_tx_;
        std::vector<OutputDestInfo> outputs_;
    };

}

#endif
