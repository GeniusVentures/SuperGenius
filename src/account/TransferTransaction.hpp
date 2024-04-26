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
#include "account/proto/SGTransaction.pb.h"
#include "account/GeniusUTXO.hpp"

using namespace boost::multiprecision;
namespace sgns
{
    class TransferTransaction : public IGeniusTransactions
    {
    public:
        struct InputUTXOInfo
        {
            base::Hash256 txid_hash_;
            uint32_t      output_idx_;
            std::string   signature_;
        };
        struct OutputDestInfo
        {
            uint256_t encrypted_amount; ///< El Gamal encrypted amount
            uint256_t dest_address;     ///< Destination node address
        };
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
        ~TransferTransaction() = default;

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

        const std::vector<OutputDestInfo> GetDstInfos() const;
        const std::vector<InputUTXOInfo> GetInputInfos() const;

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
