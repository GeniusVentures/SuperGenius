/**
 * @file       TokenID.hpp
 * @brief      
 * @date       2025-06-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>

namespace sgns
{

    class TokenID
    {
    public:
        using ByteArray = std::array<uint8_t, 32>;

        TokenID() : data_{}, valid_( false ) {}

        TokenID( const TokenID &other ) : data_( other.data_ ), valid_( other.valid_ ) {}

        static TokenID FromBytes( std::initializer_list<uint8_t> list )
        {
            return FromBytes( list.begin(), list.size() );
        }

        static TokenID FromBytes( const void *data, size_t size )
        {
            TokenID id;
            if ( !data || size == 0 )
            {
                // legacy/invalid case
                return id;
            }

            if ( size <= 32 )
            {
                // size 1–32: left-pad into the 32-byte buffer
                size_t copy_size = std::min( size, id.data_.size() );
                std::memcpy( id.data_.data() + ( id.data_.size() - copy_size ), data, copy_size );
                id.valid_ = true;
            }

            return id;
        }

        const ByteArray &bytes() const
        {
            return data_;
        }

        size_t size() const
        {
            return valid_ ? 32 : 0;
        }

        bool operator==( const TokenID &other ) const
        {
            return valid_ == other.valid_ && data_ == other.data_;
        }

        bool operator!=( const TokenID &other ) const
        {
            return !( *this == other );
        }

        bool operator<( const TokenID &other ) const
        {
            return data_ < other.data_; // lexicographic comparison
        }

        std::string ToHex() const
        {
            std::ostringstream oss;
            for ( uint8_t byte : data_ )
            {
                oss << std::hex << std::setw( 2 ) << std::setfill( '0' ) << (int)byte;
            }
            return oss.str();
        }

        bool IsGNUS() const
        {
            return !valid_ || std::all_of( data_.begin(), data_.end(), []( uint8_t b ) { return b == 0; } );
        }

        bool Equals( const TokenID &other ) const
        {
            if ( *this == other )
            {
                return true;
            }
            return this->IsGNUS() && other.IsGNUS();
        }

    private:
        ByteArray data_;
        bool      valid_;
    };
}

// Overload for printing to streams
inline std::ostream &operator<<( std::ostream &os, const sgns::TokenID &id )
{
    return os << id.ToHex();
}
