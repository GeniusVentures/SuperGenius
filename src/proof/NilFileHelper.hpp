/**
 * @file       NilFileHelper.hpp
 * @brief      
 * @date       2024-09-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _NIL_FILE_HELPER_HPP_
#define _NIL_FILE_HELPER_HPP_

#include <nil/marshalling/status_type.hpp>

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
    };
}

#endif