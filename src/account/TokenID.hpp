/**
 * @file       TokenID.hpp
 * @brief      Fixed-size token identifier wrapper with GNUS compatibility helpers.
 * @date       2025-06-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <sstream>
#include <algorithm>

namespace sgns
{
    /**
     * @brief Represents a 32-byte token identifier while preserving legacy GNUS semantics.
     */
    class TokenID
    {
    public:
        /**
         * @brief Fixed-size byte storage used for token identifiers.
         */
        using ByteArray = std::array<uint8_t, 32>;

        /**
         * @brief Constructs an invalid or legacy-default token identifier.
         */
        constexpr TokenID() : data_{}, valid_( false ) {}

        /**
         * @brief Copy-constructs a token identifier.
         * @param[in] other Token identifier to copy.
         */
        TokenID( const TokenID &other ) = default;

        /**
         * @brief Move-constructs a token identifier.
         * @param[in] other Token identifier to move from.
         */
        TokenID( TokenID &&other )      = default;

        /**
         * @brief Copy-assigns a token identifier.
         * @param[in] other Token identifier to copy.
         * @return Reference to this token identifier.
         */
        TokenID &operator=( const TokenID &other ) = default;

        /**
         * @brief Move-assigns a token identifier.
         * @param[in] other Token identifier to move from.
         * @return Reference to this token identifier.
         */
        TokenID &operator=( TokenID &&other ) = default;

        /**
         * @brief Builds a token identifier from a byte initializer list.
         * @param[in] list Bytes used to build the identifier.
         * @return Token identifier containing @p list, left-padded to 32 bytes when shorter.
         */
        static TokenID FromBytes( std::initializer_list<uint8_t> list )
        {
            return FromBytes( list.begin(), list.size() );
        }

        /**
         * @brief Builds a token identifier from up to 32 bytes, left-padding shorter inputs.
         * @param[in] data Pointer to the source bytes.
         * @param[in] size Number of bytes available at @p data.
         * @return Valid token identifier when @p data is non-null and @p size is 1 to 32; otherwise an invalid identifier.
         */
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

        /**
         * @brief Returns the raw 32-byte storage buffer.
         * @return Const reference to the internal 32-byte array.
         */
        const ByteArray &bytes() const
        {
            return data_;
        }

        /**
         * @brief Returns 32 for valid token identifiers or 0 for legacy-invalid ones.
         * @return Serialized byte size of this identifier.
         */
        size_t size() const
        {
            return valid_ ? 32 : 0;
        }

        /**
         * @brief Tests exact equality including validity state.
         * @param[in] other Token identifier to compare against.
         * @return True when both identifiers have the same validity state and bytes.
         */
        bool operator==( const TokenID &other ) const
        {
            return valid_ == other.valid_ && data_ == other.data_;
        }

        /**
         * @brief Tests exact inequality including validity state.
         * @param[in] other Token identifier to compare against.
         * @return True when @p other is not exactly equal to this identifier.
         */
        bool operator!=( const TokenID &other ) const
        {
            return !( *this == other );
        }

        /**
         * @brief Orders token identifiers by raw byte value.
         * @param[in] other Token identifier to compare against.
         * @return True when this identifier's bytes compare lexicographically before @p other.
         */
        bool operator<( const TokenID &other ) const
        {
            return data_ < other.data_; // lexicographic comparison
        }

        /**
         * @brief Converts the token identifier to a lowercase hexadecimal string.
         * @return Lowercase hexadecimal representation of the 32-byte storage buffer.
         */
        std::string ToHex() const
        {
            std::ostringstream oss;
            for ( uint8_t byte : data_ )
            {
                oss << std::hex << std::setw( 2 ) << std::setfill( '0' ) << (int)byte;
            }
            return oss.str();
        }

        /**
         * @brief Returns true when this identifier refers to the default GNUS token.
         * @return True when the identifier is invalid or all bytes are zero.
         */
        bool IsGNUS() const
        {
            return !valid_ || std::all_of( data_.begin(), data_.end(), []( uint8_t b ) { return b == 0; } );
        }

        /**
         * @brief Compares token identifiers while treating all GNUS representations as equivalent.
         * @param[in] other Token identifier to compare against.
         * @return True when the identifiers are exactly equal or both represent the GNUS token.
         */
        bool Equals( const TokenID &other ) const
        {
            if ( *this == other )
            {
                return true;
            }
            return this->IsGNUS() && other.IsGNUS();
        }

    private:
        ByteArray data_;  ///< Raw 32-byte token identifier storage.
        bool      valid_; ///< Whether the identifier was constructed from non-empty byte input.
    };
}

/**
 * @brief Streams a token identifier as hexadecimal text.
 * @param[in,out] os Output stream to write to.
 * @param[in] id Token identifier to stream.
 * @return Reference to @p os after writing the token identifier.
 */
inline std::ostream &operator<<( std::ostream &os, const sgns::TokenID &id )
{
    return os << id.ToHex();
}
