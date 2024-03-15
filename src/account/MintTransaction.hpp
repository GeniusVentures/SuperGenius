/**
 * @file       MintTransaction.hpp
 * @brief      
 * @date       2024-03-15
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _MINT_TRANSACTION_HPP_
#define _MINT_TRANSACTION_HPP_
#include <vector>
#include <cstdint>
#include "account/IGeniusTransactions.hpp"

namespace sgns
{
    class MintTransaction : public IGeniusTransactions
    {
    public:
        MintTransaction( const uint64_t &new_amount ) : amount( new_amount )
        {
        }
        ~MintTransaction() = default;

        const std::string GetType() const override
        {
            return "mint";
        };

        std::vector<uint8_t> SerializeByteVector() override
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
        static MintTransaction DeSerializeByteVector( const std::vector<uint8_t> &data )
        {
            uint64_t v64;
            std::memcpy( &v64, &( *data.begin() ), sizeof( v64 ) );

            return MintTransaction( v64 ); // Return new instance
        }
        const uint64_t GetAmount() const
        {
            return amount;
        }

    private:
        uint64_t amount;
    };
}

#endif
