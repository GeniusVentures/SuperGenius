/**
 * @file       ProcessingTransaction.cpp
 * @brief      
 * @date       2024-04-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "account/ProcessingTransaction.hpp"

namespace sgns
{
    ProcessingTransaction::ProcessingTransaction( uint256_t hash ) :
        IGeniusTransactions( "processing" ), //
        hash_process_data( hash )            //
    {
    }

    std::vector<uint8_t> ProcessingTransaction::SerializeByteVector()
    {
        std::vector<uint8_t> serialized_class;
        export_bits( hash_process_data, std::back_inserter( serialized_class ), 8 );

        return serialized_class;
    }

    ProcessingTransaction ProcessingTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        uint256_t hash;
        import_bits( hash, data.begin(), data.end() );

        return ProcessingTransaction( hash ); // Return new instance
    }
}