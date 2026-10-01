#include "Linux.hpp"

// String-API implementation of LinuxSecureStorage for libsecret < 0.19
// (e.g. 0.18.6 on AlmaLinux 8), which lacks secret_password_lookup_binary_sync
// / secret_password_store_binary_sync. Selected by CMake when pkg-config
// reports libsecret-1 < 0.19; impl/Linux.cpp is used otherwise. Serialized
// JSON contains no NUL bytes, so the two implementations are functionally
// equivalent.

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

        return outcome::success();
    }
}
