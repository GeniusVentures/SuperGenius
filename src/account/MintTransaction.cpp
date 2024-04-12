/**
 * @file       MintTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/MintTransaction.hpp"

namespace sgns
{
    MintTransaction::MintTransaction( const uint64_t &new_amount,  const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "mint" , SetDAGWithType(dag,"processing")), //
        amount( new_amount )           //
    {
    }
    std::vector<uint8_t> MintTransaction::SerializeByteVector()
    {
        std::vector<uint8_t> data;
        uint8_t             *ptr = reinterpret_cast<uint8_t *>( &amount );
        // For little-endian systems; if on a big-endian system, reverse the order of insertion
        for ( size_t i = 0; i < sizeof( amount ); ++i )
        {
            data.push_back( ptr[i] );
        }
        return data;
    }
    MintTransaction MintTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        uint64_t v64;
        std::memcpy( &v64, &( *data.begin() ), sizeof( v64 ) );

        return MintTransaction( v64 ,{} ); // Return new instance
    }
    const uint64_t MintTransaction::GetAmount() const
    {
        return amount;
    }
}