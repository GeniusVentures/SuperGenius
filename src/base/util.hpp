/**
 * @file       util.hpp
 * @brief      Utilities functions header file
 * @date       2024-01-12
 * @author     Super Genius (ken@gnus.ai)
 * @author     Henrique A. Klein (henryaklein@gmail.com)
 */

#ifndef _UTIL_HPP
#define _UTIL_HPP

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <type_traits>
#include <optional>
#include <algorithm>
#include <tuple>
#include <array>
#include <boost/random.hpp>
#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;

namespace sgns
{
    /**
     * @brief       Convert a byte array to a hexadecimal string.
     * @param[in]   bytes A vector of bytes to be converted.
     * @return      A hexadecimal string representation of the bytes.
     */
    static std::string to_string( const std::vector<unsigned char> &bytes )
    {
        std::string out_str;
        char        temp_buf[3];
        for ( auto it = bytes.rbegin(); it != bytes.rend(); ++it )
        {
            snprintf( temp_buf, sizeof( temp_buf ), "%02x", *it );
            out_str.append( temp_buf, sizeof( temp_buf ) - 1 );
        }
        return out_str;
    }

    /**
     * @brief       Checks if the architecture is little endian
     * @return      true if little endian, false otherwise
     */
    static bool isLittleEndian()
    {
        std::uint32_t num     = 1;
        std::uint8_t *bytePtr = reinterpret_cast<std::uint8_t *>( &num );

        return *bytePtr == 1;
    }

    /**
     * @brief       Converts a hexadecimal ASCII char array into a number
     * @param[in]   p_char Hexadecimal ASCII char array
     * @param[in]   num_nibbles_resolution How many nibbles will constitute a number
     * @tparam      T uint8_t, uint16_t, uint32_t or uint64_t
     * @return      The converted number (8-64 bit variable)
     */

    template <typename T>
    static T Vector2Num( const std::vector<uint8_t> &bytes )
    {
        static_assert(
            std::is_integral<T>::value || std::is_same<T, boost::multiprecision::uint128_t>::value ||
                std::is_same<T, boost::multiprecision::uint256_t>::value,
            "T must be an integral type or boost::multiprecision::uint128_t or boost::multiprecision::uint256_t" );
        if ( bytes.size() > sizeof( T ) )
        {
            throw std::invalid_argument( "Byte vector too large for conversion" );
        }

        T value = 0;
        for ( size_t i = 0; i < bytes.size(); ++i )
        {
            value |= static_cast<T>( bytes[i] ) << ( i * 8 );
        }
        return value;
    }

    template <>
    uint128_t Vector2Num( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.size() > 16 )
        {
            throw std::invalid_argument( "Byte vector too large for conversion" );
        }

        uint128_t value = 0;
        for ( size_t i = 0; i < bytes.size(); ++i )
        {
            value |= static_cast<uint128_t>( bytes[i] ) << ( i * 8 );
        }
        return value;
    }

    template <>
    uint256_t Vector2Num( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.size() > 32 )
        {
            throw std::invalid_argument( "Byte vector too large for conversion" );
        }

        uint256_t value = 0;
        for ( size_t i = 0; i < bytes.size(); ++i )
        {
            value |= static_cast<uint256_t>( bytes[i] ) << ( i * 8 );
        }
        return value;
    }

    /**
     * @brief       Converts a hexadecimal ASCII char array into a number
     * @param[in]   p_char Hexadecimal ASCII char array
     * @param[in]   num_nibbles_resolution How many nibbles will constitute a number
     * @tparam      T uint8_t, uint16_t, uint32_t or uint64_t
     * @return      The converted number (8-64 bit variable)
     */
    template <typename T>
    static std::vector<uint8_t> Num2Vector( const T &num, std::size_t num_bytes_resolution = sizeof( T ) )
    {
        const uint8_t *bytesPtr = reinterpret_cast<const uint8_t *>( &num );
        return std::vector<uint8_t>( bytesPtr, bytesPtr + sizeof( T ) );
    }

    /**
     * @brief       Converts a hexadecimal ASCII char array into a number
     * @param[in]   p_char Hexadecimal ASCII char array
     * @param[in]   num_nibbles_resolution How many nibbles will constitute a number
     * @tparam      T uint8_t, uint16_t, uint32_t or uint64_t
     * @return      The converted number (8-64 bit variable)
     */
    template <typename T>
    static T HexASCII2Num( const char *p_char, std::size_t num_nibbles_resolution = sizeof( T ) * 2 )
    {
        T sum = 0;

        for ( std::size_t i = 0; i < num_nibbles_resolution; ++i )
        {
            if ( std::isdigit( p_char[i] ) )
            {
                sum += ( ( p_char[i] - '0' ) << ( 4 * ( num_nibbles_resolution - i - 1 ) ) );
            }
            else
            {
                sum += ( ( std::toupper( p_char[i] ) - 'A' + 10 ) << ( 4 * ( num_nibbles_resolution - i - 1 ) ) );
            }
        }

        return sum;
    }

    /**
     * @brief       Converts a hexadecimal ASCII char array into a vector of numbers
     * @param[in]   p_char Hexadecimal ASCII char array
     * @param[in]   char_ptr_size Size of the char array
     * @tparam      T uint8_t, uint16_t, uint32_t or uint64_t
     * @return      The vector of converted numbers
     */
    template <typename T>
    static std::vector<T> HexASCII2NumStr( const char *p_char, std::size_t char_ptr_size )
    {
        static_assert( std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
                       std::is_same_v<T, uint64_t> );
        std::vector<T> out_vect;
        std::size_t    num_nibbles_resolution = ( sizeof( T ) * 2 );
        auto           point_of_insertion     = [&]()
        {
            if ( isLittleEndian() )
            {
                return out_vect.begin();
            }
            
                return out_vect.end();
            
        };

        for ( std::size_t i = 0; i < char_ptr_size; i += num_nibbles_resolution )
        {
            out_vect.insert( point_of_insertion(), static_cast<T>( HexASCII2Num<T>( &p_char[i] ) ) );
        }
        return out_vect;
    }

    /**
     * @brief       Adjust endianess if needed
     * @param[in]   data The container of data (vector/array)
     * @param[in]   start Optional beginning of the valid data
     * @param[in]   finish Optional ending of the valid data
     * @tparam      T std::vector<uint8_t> or std::array<uint8_t,N>
     */
    template <typename T>
    static typename std::enable_if<std::is_same<typename T::value_type, uint8_t>::value>::type AdjustEndianess(
        T                                  &data,
        std::optional<typename T::iterator> start  = std::nullopt,
        std::optional<typename T::iterator> finish = std::nullopt )
    {
        if ( !start )
        {
            start = data.begin();
        }
        if ( !finish )
        {
            finish = data.end();
        }
        if ( !isLittleEndian() )
        {
            std::reverse( start.value(), finish.value() );
        }
    }

    static uint256_t GenerateRandomNumber()
    {
        boost::random::mt19937                             gen( clock() );
        boost::random::uniform_int_distribution<uint256_t> dist( std::numeric_limits<uint256_t>::min(),
                                                                 std::numeric_limits<uint256_t>::max() );
        return dist( gen );
    }

}

#endif //_UTIL_HPP
