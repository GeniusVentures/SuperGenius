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
        TransferTransaction( const uint256_t &amount, const uint256_t &destination ) : encrypted_amount( amount ), dest_address( destination ){};

        /**
         * @brief      Default Transfer Transaction destructor
         */
        ~TransferTransaction() = default;

        const std::string GetType() const override
        {
            return "transfer";
        }
        std::vector<uint8_t> SerializeByteVector() override
        {
            std::vector<uint8_t> serialized_class;
            export_bits( encrypted_amount, std::back_inserter( serialized_class ), 8 );
            auto filled_size = serialized_class.size();
            if ( filled_size < 32 )
            {
                // If the exported data is smaller than the fixed size, pad with zeros
                serialized_class.insert( serialized_class.begin(), 32 - filled_size, 0 );
            }
            export_bits( dest_address, std::back_inserter( serialized_class ), 8 );
            filled_size = serialized_class.size();
            if ( filled_size < 64 )
            {
                // If the exported data is smaller than the fixed size, pad with zeros
                serialized_class.insert( serialized_class.begin() + 32, 64 - filled_size, 0 );
            }
            return serialized_class;
        }
        static TransferTransaction DeSerializeByteVector( const std::vector<uint8_t> &data )
        {
            std::vector<uint8_t> serialized_class;
            uint256_t            amount;
            uint256_t            address;
            auto                 half = data.size() / 2;
            import_bits( amount, data.begin(), data.begin() + half );
            import_bits( address, data.begin() + half, data.end() );

            return TransferTransaction( amount, address ); // Return new instance
        }
        template <typename T>
        const T GetAddress() const;

        template <>
        const std::string GetAddress<std::string>() const
        {
            std::ostringstream oss;
            oss << std::hex << dest_address;

            return ( "0x" + oss.str() );
        }
        template <>
        const uint256_t GetAddress<uint256_t>() const
        {
            return dest_address;
        }
        template <typename T>
        const T GetAmount() const;

        template <>
        const std::string GetAmount<std::string>() const
        {
            std::ostringstream oss;
            oss << encrypted_amount;

            return ( oss.str() );
        }
        template <>
        const uint256_t GetAmount<uint256_t>() const
        {
            return encrypted_amount;
        }

    private:
        uint256_t encrypted_amount; ///< El Gamal encrypted amount
        uint256_t dest_address;     ///< Destination node address
    };

}

#endif
