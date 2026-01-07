#include "Windows.hpp"

#include <iostream>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "windows.h"
#include "wincred.h"

namespace rj = rapidjson;

namespace sgns
{
    outcome::result<ISecureStorage::SecureBufferType> WindowsSecureStorage::Load( const std::string &key )
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

    outcome::result<void> WindowsSecureStorage::Save( const std::string &key, const SecureBufferType &buffer )
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

    outcome::result<bool> WindowsSecureStorage::DeleteKey( const std::string &key )
    {
        OUTCOME_TRY( rj::Document d, LoadJSON() );

        bool ret = d.RemoveMember( key.c_str() );

        OUTCOME_TRY( SaveJSON( std::move( d ) ) );

        return ret;
    }

    outcome::result<rapidjson::Document> WindowsSecureStorage::LoadJSON() const
    {
        PCREDENTIALW p_cred;

        auto exists = CredReadW( L"SuperGenius", CRED_TYPE_GENERIC, 0, &p_cred );

        if ( !exists )
        {
            auto error = GetLastError();
            if ( error == ERROR_NOT_FOUND )
            {
                rj::Document empty_document( rj::Type::kObjectType );
                return empty_document;
            }
            std::cerr << "Error loading secret: " << GetLastError() << '\n';
            return outcome::failure( std::errc::bad_message );
        }

        rj::Document d;
        d.Parse( reinterpret_cast<const char*>(p_cred->CredentialBlob), p_cred->CredentialBlobSize );

        CredFree( p_cred );

        if ( d.HasParseError() || ( !d.IsObject() && !d.Empty() ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        return d;
    }

    outcome::result<void> WindowsSecureStorage::SaveJSON( rapidjson::Document document )
    {
        rj::StringBuffer password;
        rj::Writer       writer( password );
        document.Accept( writer );

        CREDENTIALW cred        = {};
        cred.Type               = CRED_TYPE_GENERIC;
        cred.TargetName         = L"SuperGenius";
        cred.UserName           = L"";
        cred.CredentialBlobSize = password.GetLength();
        cred.CredentialBlob     = reinterpret_cast<LPBYTE>(const_cast<char*>(password.GetString()));
        cred.Persist            = CRED_PERSIST_LOCAL_MACHINE;

        if ( !CredWriteW( &cred, 0 ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        return outcome::success();
    }
}
