#include "../PasswordRepository.hpp"

#include <string>
#include <system_error>
#include <windows.h>
#include <wincred.h>

namespace sgns::password
{
    static constexpr std::wstring_view TARGET_NAME = L"GeniusSDK";

    outcome::result<void> StoreCredentials( std::string_view userName, std::string_view password )
    {
        if ( userName.empty() || password.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        std::wstring wideName     = boost::locale::conv::to_utf<wchar_t>( userName, "UTF-8" );
        std::wstring widePassword = boost::locale::conv::to_utf<wchar_t>( password, "UTF-8" );

        CREDENTIALW cred        = {};
        cred.Type               = CRED_TYPE_GENERIC;
        cred.TargetName         = const_cast<LPWSTR>( TARGET_NAME.c_str() );
        cred.UserName           = username.empty() ? nullptr : const_cast<LPWSTR>( wideName.c_str() );
        cred.CredentialBlob     = reinterpret_cast<LPBYTE>( const_cast<wchar_t *>( widePassword.c_str() ) );
        cred.CredentialBlobSize = static_cast<DWORD>( ( password.size() + 1 ) *
                                                      sizeof( wchar_t ) ); // Include null terminator
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE; // Allows sharing across apps on the same machine under the user

        if ( CredWriteW( &cred, 0 ) )
        {
            return outcome::success;
        }
        else
        {
            auto error = GetLastError();

            switch ( error )
            {
                case ERROR_BAD_USERNAME:
                    return outcome::failure( std::errc::invalid_argument );
                case ERROR_INVALID_PARAMETER:
                    return outcome::failure( std::errc::invalid_argument );
                case ERROR_NOT_FOUND:
                    return outcome::failure(std::errc::no_such_file_or_directory);
                default:
                    return outcome::failure();
            }
        }
    }

    outcome::result<std::pair<std::string, std::string>> RetrieveCredential()
    {
        PCREDENTIALW pCred = nullptr;

        if ( !CredReadW( TARGET_NAME.c_str(), CRED_TYPE_GENERIC, 0, &pCred ) )
        {
            auto error = GetLastError();

            switch (error) {
                case ERROR_NOT_FOUND:
                    return outcome::failure(std::errc::no_such_file_or_directory);
                case ERROR_NO_SUCH_LOGON_SESSION:
                    return outcome::failure(std::errc::no_such_file_or_directory);
                default:
                    return outcome::failure();
            }
        }

        std::string username = boost::locale::conv::to_utf<char>( pCred->UserName );
        std::string password = boost::locale::conv::to_utf<char>( pCred->CredentialBlob );

        CredFree( pCred );

        return std::make_pair(username, password);
    }
}
