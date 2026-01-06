#include "Linux.hpp"
#include "outcome/outcome.hpp"
#include <glib.h>
#include <iostream>
#include <libsecret/secret.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace rj = rapidjson;

namespace sgns
{
    LinuxSecureStorage::LinuxSecureStorage() : schema( { "SuperGenius", SECRET_SCHEMA_NONE } ) {}

    outcome::result<rapidjson::Document> LinuxSecureStorage::LoadJSON() const
    {
        GError      *error  = nullptr;
        SecretValue *result = secret_password_lookup_binary_sync( &schema, nullptr, &error, NULL );

        if ( result == nullptr )
        {
            rj::Document foo( rj::Type::kObjectType );
            return foo;
        }

        if ( error != nullptr )
        {
            std::cerr << "Error loading secret: " << error->message << '\n';
            g_error_free( error );
            return outcome::failure( std::errc::bad_message );
        }

        gsize        length = 0;
        const gchar *data   = secret_value_get( result, &length );

        rj::Document d;
        d.Parse( data, length );

        secret_value_unref( result );

        if ( d.HasParseError() || ( !d.IsObject() && !d.Empty() ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        return d;
    }

    outcome::result<void> LinuxSecureStorage::SaveJSON( rapidjson::Document document )
    {
        rj::StringBuffer password;
        rj::Writer       writer( password );
        document.Accept( writer );

        GError      *error = nullptr;
        SecretValue *value = secret_value_new( password.GetString(), password.GetLength(), "application/json" );

        if ( !secret_password_store_binary_sync( &schema,
                                                 SECRET_COLLECTION_DEFAULT,
                                                 "SuperGenius",
                                                 value,
                                                 nullptr,
                                                 &error,
                                                 NULL ) )
        {
            if ( error != nullptr )
            {
                std::cerr << "Error saving secret: " << error->message << '\n';
                g_error_free( error );
            }
            return outcome::failure( std::errc::bad_message );
        }

        return outcome::success();
    }

    outcome::result<ISecureStorage::SecureBufferType> LinuxSecureStorage::Load( const std::string &key )
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

    outcome::result<void> LinuxSecureStorage::Save( const std::string &key, const SecureBufferType &buffer )
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

    outcome::result<bool> LinuxSecureStorage::DeleteKey( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        bool ret = d.RemoveMember( key.c_str() );

        OUTCOME_TRY( SaveJSON( std::move( d ) ) );

        return ret;
    }
}
