/**
 * @file       NilFileHelper.hpp
 * @brief      
 * @date       2024-09-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _NIL_FILE_HELPER_HPP_
#define _NIL_FILE_HELPER_HPP_

#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <nil/marshalling/status_type.hpp>

#include "outcome/outcome.hpp"

namespace sgns
{
    class NilFileHelper
    {
    public:
        template <typename MarshalledData>
        static bool PrintMarshalledData( const MarshalledData &output,
                                         std::ostream         &out    = std::cout,
                                         bool                  binary = true )
        {
            std::vector<std::uint8_t> cv;
            cv.resize( output.length(), 0x00 );
            auto                          write_iter = cv.begin();
            nil::marshalling::status_type status     = output.write( write_iter, cv.size() );

            return binary ? WriteAsBin( cv, out ) : WriteAsHex( cv, out );
        }

        template <typename MarshalledData>
        static outcome::result<MarshalledData> DecodeMarshalledData( std::ifstream &in, bool binary = true )

        {
            std::vector<std::uint8_t> v;
            if ( binary )
            {
                OUTCOME_TRY((auto &&, result), ReadFromBin( in ) );
                v = std::move( result );
            }
            else
            {
                OUTCOME_TRY( (auto &&, result), ReadFromHex( in ) );
                v = std::move( result );
            }

            MarshalledData marshalled_data;
            auto           read_iter = v.begin();
            auto           status    = marshalled_data.read( read_iter, v.size() );
            if ( status != nil::marshalling::status_type::success )
            {
                return outcome::failure( boost::system::error_code{} );
            }
            return marshalled_data;
        }

    private:
        static bool WriteAsHex( const std::vector<std::uint8_t> &vector, std::ostream &out )
        {
            bool ret = true;
            out << "0x" << std::hex;
            for ( auto it = vector.cbegin(); it != vector.cend(); ++it )
            {
                out << std::setfill( '0' ) << std::setw( 2 ) << std::right << int( *it );
            }
            out << std::dec;

            if ( out.fail() )
            {
                ret = false;
            }

            return ret;
        }

        static bool WriteAsBin( const std::vector<std::uint8_t> &vector, std::ostream &out )
        {
            bool ret = true;
            out.write( reinterpret_cast<const char *>( vector.data() ), vector.size() );

            if ( out.fail() )
            {
                ret = false;
            }

            return ret;
        }

        static outcome::result<std::vector<std::uint8_t>> ReadFromHex( std::ifstream &in )
        {
            std::vector<uint8_t> result;
            std::string          line;
            while ( std::getline( in, line ) )
            {
                if ( line.rfind( "0x", 0 ) == 0 && line.length() >= 3 )
                {
                    for ( size_t i = 2; i < line.length(); i += 2 )
                    {
                        std::string hex_string = line.substr( i, 2 );
                        uint8_t     byte       = static_cast<uint8_t>( std::stoul( hex_string, nullptr, 16 ) );
                        result.push_back( byte );
                    }
                }
                else
                {
                    return outcome::failure( boost::system::error_code{} );
                }
            }

            return result;
        }

        static outcome::result<std::vector<std::uint8_t>> ReadFromBin( std::ifstream &in )
        {
            std::streamsize fsize = in.tellg();
            in.seekg( 0, std::ios::beg );
            std::vector<std::uint8_t> v( static_cast<size_t>( fsize ) );

            in.read( reinterpret_cast<char *>( v.data() ), fsize );

            if ( in.fail() )
            {
                return outcome::failure( boost::system::error_code{} );
            }

            return v;
        }
    };
}

#endif
