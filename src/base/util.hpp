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
#include <vector>
#include <type_traits>
#include <optional>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <boost/random.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include "outcome/outcome.hpp"

namespace sgns
{
    using namespace boost::multiprecision;
    namespace br = boost::random;

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
        auto         *bytePtr = reinterpret_cast<std::uint8_t *>( &num );

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
            std::is_integral_v<T> || std::is_same_v<T, boost::multiprecision::uint128_t> ||
                std::is_same_v<T, boost::multiprecision::uint256_t>,
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
        const auto *bytesPtr = reinterpret_cast<const uint8_t *>( &num );
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
    static std::enable_if_t<std::is_same_v<typename T::value_type, uint8_t>> AdjustEndianess(
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

    static uint16_t GenerateRandomPort( uint16_t base, const std::string &seed )
    {
        uint32_t seed_hash = 0;
        for ( char c : seed )
        {
            seed_hash = seed_hash * 31 + static_cast<uint8_t>( c );
        }

        br::mt19937                    rng( seed_hash );
        br::uniform_int_distribution<> dist( 0, 300 );

        // Generate a random port and add to the base
        return base + dist( rng );
    }

    static std::string Uint256ToString( const uint256_t &value )
    {
        std::ostringstream oss;
        oss << "0x";
        oss << std::hex << std::setw( 64 ) << std::setfill( '0' ) << value;
        return oss.str();
    }

    namespace fixed_point
    {
        /**
         * @brief       Convert a string to fixed-point representation
         * @param[in]   str_value Numeric string with integer and fractional parts
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of fixed-point representation or error
         */
        static outcome::result<uint64_t> fromString( const std::string &str_value, uint64_t precision = 9 )
        {
            if ( str_value.empty() )
            {
                return outcome::failure( std::make_error_code( std::errc::invalid_argument ) );
            }

            size_t           dot_pos = str_value.find( '.' );
            std::string_view integer_str, fractional_str;

            if ( dot_pos != std::string::npos )
            {
                integer_str    = std::string_view( str_value ).substr( 0, dot_pos );
                fractional_str = std::string_view( str_value ).substr( dot_pos + 1 );
            }
            else
            {
                integer_str = str_value;
            }

            // Parse the integer and fractional parts
            uint64_t integer_part    = 0;
            uint64_t fractional_part = 0;

            auto [ptr_int, ec_int] = std::from_chars( integer_str.data(),
                                                      integer_str.data() + integer_str.size(),
                                                      integer_part );
            if ( ec_int != std::errc() )
            {
                return outcome::failure( std::make_error_code( ec_int ) );
            }

            if ( !fractional_str.empty() )
            {
                auto [ptr_frac, ec_frac] = std::from_chars( fractional_str.data(),
                                                            fractional_str.data() + fractional_str.size(),
                                                            fractional_part );
                if ( ec_frac != std::errc() )
                {
                    return outcome::failure( std::make_error_code( ec_frac ) );
                }
            }

            // Adjust fractional part to match required precision
            if ( fractional_str.length() > precision )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            for ( uint64_t i = fractional_str.length(); i < precision; ++i )
            {
                fractional_part *= 10;
            }

            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            return outcome::success( ( integer_part * scale ) + fractional_part );
        }

        /**
         * @brief       Convert fixed-point representation to string
         * @param[in]   value Fixed-point number
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      String representation
         */
        static std::string toString( uint64_t value, uint64_t precision = 9 )
        {
            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }
            uint64_t integer_part    = value / scale;
            uint64_t fractional_part = value % scale;
            return std::to_string( integer_part ) + "." + std::to_string( fractional_part );
        }

        /**
         * @brief       Multiply two fixed-point numbers with optional precision
         * @param[in]   a First number
         * @param[in]   b Second number
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Result of multiplication in fixed-point representation
         */
        static uint64_t multiply( uint64_t a, uint64_t b, uint64_t precision = 9 )
        {
            uint64_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            uint128_t result = static_cast<__uint128_t>( a ) * static_cast<__uint128_t>( b );
            return static_cast<uint64_t>( result / scale );
        }

        /**
         * @brief       Divide two fixed-point numbers with optional precision
         * @param[in]   a Dividend
         * @param[in]   b Divisor
         * @param[in]   precision Number of decimal places (default: 9)
         * @return      Outcome of division in fixed-point representation
         */
        static outcome::result<uint64_t> divide( uint64_t a, uint64_t b, uint64_t precision = 9 )
        {
            if ( b == 0 )
            {
                return outcome::failure( std::make_error_code( std::errc::result_out_of_range ) );
            }

            uint128_t scale = 1;
            for ( uint64_t i = 0; i < precision; ++i )
            {
                scale *= 10;
            }

            uint128_t result = ( static_cast<uint128_t>( a ) * scale ) / b;

            if ( result > std::numeric_limits<uint64_t>::max() )
            {
                return outcome::failure( std::make_error_code( std::errc::value_too_large ) );
            }

            return outcome::success( static_cast<uint64_t>( result ) );
        }
    }

}
#endif //_UTIL_HPP
