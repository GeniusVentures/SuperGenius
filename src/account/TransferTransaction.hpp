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
        TransferTransaction( const uint256_t &amount, const uint256_t &destination );

        /**
         * @brief      Default Transfer Transaction destructor
         */
        ~TransferTransaction() = default;

        std::vector<uint8_t> SerializeByteVector() override;

        static TransferTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );
        
        template <typename T>
        const T GetAddress() const;


        template <typename T>
        const T GetAmount() const;


    private:
        uint256_t encrypted_amount; ///< El Gamal encrypted amount
        uint256_t dest_address;     ///< Destination node address
    };

}

#endif
