/**
 * @file       TransferTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransferTransaction.hpp"

namespace sgns
{
    TransferTransaction::TransferTransaction( const uint256_t &amount, const uint256_t &destination ) :
        IGeniusTransactions( "transfer" ), //
        encrypted_amount( amount ),        //
        dest_address( destination )        //
    {
    }
    std::vector<uint8_t> TransferTransaction::SerializeByteVector()
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
    TransferTransaction TransferTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        std::vector<uint8_t> serialized_class;
        uint256_t            amount;
        uint256_t            address;
        auto                 half = data.size() / 2;
        import_bits( amount, data.begin(), data.begin() + half );
        import_bits( address, data.begin() + half, data.end() );

        return TransferTransaction( amount, address ); // Return new instance
    }
    template <>
    const std::string TransferTransaction::GetAddress<std::string>() const
    {
        std::ostringstream oss;
        oss << std::hex << dest_address;

        return ( "0x" + oss.str() );
    }
    template <>
    const uint256_t TransferTransaction::GetAddress<uint256_t>() const
    {
        return dest_address;
    }

    template <>
    const std::string TransferTransaction::GetAmount<std::string>() const
    {
        std::ostringstream oss;
        oss << encrypted_amount;

        return ( oss.str() );
    }
    template <>
    const uint256_t TransferTransaction::GetAmount<uint256_t>() const
    {
        return encrypted_amount;
    }
}