#include "Linux.hpp"

#include <iostream>

#include <glib.h>
#include <libsecret/secret.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "outcome/outcome.hpp"

namespace rj = rapidjson;

namespace sgns
{
    LinuxSecureStorage::LinuxSecureStorage( std::string identifier ) :
        identifier_( std::move( identifier ) ), schema_( { identifier_.c_str(), SECRET_SCHEMA_NONE } )
    {
    }

    outcome::result<rj::Document> LinuxSecureStorage::LoadJSON() const
    {
        GError *error = nullptr;
        // libsecret >= 0.19 provides the binary-safe sync API (Debian bullseye+
        // hosts). Serialized JSON contains no NUL bytes, so both branches are
        // functionally equivalent here; the string API is the fallback for
        // libsecret 0.18.x (AlmaLinux 8 ships 0.18.6).
#if LIBSECRET_CHECK_VERSION( 0, 19, 0 )
        SecretValue *result = secret_password_lookup_binary_sync( &schema_, nullptr, &error, NULL );

        if ( result == nullptr )
        {
            rj::Document empty_document( rj::Type::kObjectType );
            return empty_document;
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
#else
        gchar *result = secret_password_lookup_sync( &schema_, nullptr, &error, NULL );

        if ( result == nullptr )
        {
            rj::Document empty_document( rj::Type::kObjectType );
            return empty_document;
        }

        if ( error != nullptr )
        {
            std::cerr << "Error loading secret: " << error->message << '\n';
            g_error_free( error );
            secret_password_free( result );
            return outcome::failure( std::errc::bad_message );
        }

        rj::Document d;
        d.Parse( result );

        secret_password_free( result );
#endif

        if ( d.HasParseError() || ( !d.IsObject() && !d.Empty() ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        return d;
    }

    outcome::result<void> LinuxSecureStorage::SaveJSON( rj::Document document )
    {
        rj::StringBuffer password;
        rj::Writer       writer( password );
        document.Accept( writer );

        GError *error = nullptr;
#if LIBSECRET_CHECK_VERSION( 0, 19, 0 )
        SecretValue *value = secret_value_new( password.GetString(), password.GetLength(), "application/json" );

        if ( !secret_password_store_binary_sync( &schema_,
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
            secret_value_unref( value );
            return outcome::failure( std::errc::bad_message );
        }

        secret_value_unref( value );
#else
        if ( !secret_password_store_sync( &schema_,
                                          SECRET_COLLECTION_DEFAULT,
                                          "SuperGenius",
                                          password.GetString(),
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
#endif
        return outcome::success();
    }
}
