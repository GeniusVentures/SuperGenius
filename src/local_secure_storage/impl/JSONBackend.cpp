#include "JSONBackend.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace rj = rapidjson;

namespace sgns
{
    outcome::result<ISecureStorage::SecureBufferType> JSONBackend::Load( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        if ( !d.HasMember( key.c_str() ) )
        {
            return outcome::failure( std::errc::no_message );
        }

        auto &value = d[key.c_str()];
        if ( !value.IsString() )
        {
            return outcome::failure( std::errc::bad_message );
        }

        SecureBufferType ret( value.GetString(), value.GetStringLength() );

        return ret;
    }

    outcome::result<void> JSONBackend::Save( const std::string &key, const SecureBufferType &buffer )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        rj::Value val( rj::StringRef( buffer.c_str(), buffer.length() ), d.GetAllocator() );

        if ( d.HasMember( key.c_str() ) )
        {
            d[key.c_str()] = val;
        }
        else
        {
            d.AddMember( rj::StringRef( key.c_str(), key.size() ), val, d.GetAllocator() );
        }

        return SaveJSON( std::move( d ) );
    }

    outcome::result<bool> JSONBackend::DeleteKey( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        bool ret = d.RemoveMember( key.c_str() );

        OUTCOME_TRY( SaveJSON( std::move( d ) ) );

        return ret;
    }
}
