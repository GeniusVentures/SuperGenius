#include "trustedpeer/genesis_tool/GenesisCeremonyPlatform.hpp"

#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <io.h>

#include <vector>

namespace sgns::trustedpeer::genesis_ceremony_platform
{
    namespace
    {
        constexpr size_t MAX_KEY_FILE_BYTES = 256;

        void TrimLineEnding( std::string &value )
        {
            while ( !value.empty() && ( value.back() == '\n' || value.back() == '\r' ) )
                value.pop_back();
        }

        bool IsOwnerOnlyDacl( PACL dacl, PSID owner )
        {
            ACL_SIZE_INFORMATION information{};
            if ( !dacl || !owner || !GetAclInformation( dacl, &information, sizeof( information ), AclSizeInformation ) )
                return false;
            for ( DWORD index = 0; index < information.AceCount; ++index )
            {
                void *entry = nullptr;
                if ( !GetAce( dacl, index, &entry ) ) return false;
                const auto *header = static_cast<const ACE_HEADER *>( entry );
                if ( ( header->AceFlags & INHERIT_ONLY_ACE ) != 0 || header->AceType == ACCESS_DENIED_ACE_TYPE )
                    continue;
                if ( header->AceType != ACCESS_ALLOWED_ACE_TYPE ) return false;
                const auto *allow = static_cast<const ACCESS_ALLOWED_ACE *>( entry );
                const auto sid = reinterpret_cast<PSID>( const_cast<DWORD *>( &allow->SidStart ) );
                if ( !EqualSid( sid, owner ) && !IsWellKnownSid( sid, WinLocalSystemSid ) &&
                     !IsWellKnownSid( sid, WinBuiltinAdministratorsSid ) )
                    return false;
            }
            return true;
        }

        GenesisCeremony::Error StatusError( const GenesisCeremony::KeyFileStatus &status )
        {
            if ( status.symlink ) return GenesisCeremony::Error::KEY_FILE_SYMLINK;
            if ( !status.regular ) return GenesisCeremony::Error::KEY_FILE_NOT_REGULAR;
            if ( !status.owner ) return GenesisCeremony::Error::KEY_FILE_OWNER;
            if ( status.mode != 0600 ) return GenesisCeremony::Error::KEY_FILE_MODE;
            return GenesisCeremony::Error::SUCCESS;
        }

        outcome::result<std::vector<BYTE>> CurrentUserToken()
        {
            HANDLE token = nullptr;
            if ( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
            DWORD bytes = 0;
            GetTokenInformation( token, TokenUser, nullptr, 0, &bytes );
            std::vector<BYTE> user( bytes );
            const bool read = bytes != 0 && GetTokenInformation( token, TokenUser, user.data(), bytes, &bytes );
            CloseHandle( token );
            if ( !read ) return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
            return user;
        }

        outcome::result<GenesisCeremony::KeyFileStatus> InspectOpenKeyFile( HANDLE file )
        {
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if ( !GetFileInformationByHandleEx( file, FileAttributeTagInfo, &attributes, sizeof( attributes ) ) )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
            const bool symlink = ( attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) != 0;
            const bool regular = !symlink && ( attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 &&
                                 GetFileType( file ) == FILE_TYPE_DISK;

            PSECURITY_DESCRIPTOR descriptor = nullptr;
            PSID owner = nullptr;
            PACL dacl = nullptr;
            if ( GetSecurityInfo( file,
                                  SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &owner,
                                  nullptr,
                                  &dacl,
                                  nullptr,
                                  &descriptor ) != ERROR_SUCCESS )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );

            bool owner_matches = false;
            const auto user = CurrentUserToken();
            if ( user.has_value() )
            {
                const auto *token_user = reinterpret_cast<const TOKEN_USER *>( user.value().data() );
                owner_matches = owner && EqualSid( owner, token_user->User.Sid );
            }
            const bool owner_only = owner_matches && IsOwnerOnlyDacl( dacl, owner );
            LocalFree( descriptor );
            return GenesisCeremony::KeyFileStatus{ true, regular, symlink, owner_matches, owner_only ? 0600U : 0U };
        }

        HANDLE OpenKeyFile( const std::string &path )
        {
            return CreateFileA( path.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr );
        }
    } // namespace

    outcome::result<GenesisCeremony::KeyFileStatus> InspectKeyFile( const std::string &path )
    {
        const HANDLE file = OpenKeyFile( path );
        if ( file == INVALID_HANDLE_VALUE ) return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        auto status = InspectOpenKeyFile( file );
        CloseHandle( file );
        return status;
    }

    outcome::result<std::string> ReadKeyFile( const std::string &path )
    {
        const HANDLE file = OpenKeyFile( path );
        if ( file == INVALID_HANDLE_VALUE ) return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        const auto status = InspectOpenKeyFile( file );
        if ( status.has_error() )
        {
            CloseHandle( file );
            return outcome::failure( status.error() );
        }
        const auto status_error = StatusError( status.value() );
        if ( status_error != GenesisCeremony::Error::SUCCESS )
        {
            CloseHandle( file );
            return outcome::failure( status_error );
        }
        std::string value( MAX_KEY_FILE_BYTES + 1, '\0' );
        DWORD bytes_read = 0;
        const BOOL read = ReadFile( file, value.data(), static_cast<DWORD>( value.size() ), &bytes_read, nullptr );
        const BOOL closed = CloseHandle( file );
        if ( !read || !closed || bytes_read > MAX_KEY_FILE_BYTES )
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        value.resize( bytes_read );
        TrimLineEnding( value );
        return value;
    }

    int RestrictKeyFileToCurrentUser( const std::string &path )
    {
        const auto user = CurrentUserToken();
        if ( user.has_error() ) return -1;
        const auto *token_user = reinterpret_cast<const TOKEN_USER *>( user.value().data() );

        EXPLICIT_ACCESSA access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.ptstrName = static_cast<LPSTR>( token_user->User.Sid );

        PACL dacl = nullptr;
        if ( SetEntriesInAclA( 1, &access, nullptr, &dacl ) != ERROR_SUCCESS ) return -1;
        const auto result = SetNamedSecurityInfoA( const_cast<LPSTR>( path.c_str() ),
                                                   SE_FILE_OBJECT,
                                                   OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                                                       PROTECTED_DACL_SECURITY_INFORMATION,
                                                   token_user->User.Sid,
                                                   nullptr,
                                                   dacl,
                                                   nullptr );
        LocalFree( dacl );
        return result == ERROR_SUCCESS ? 0 : -1;
    }

    int RemoveKeyFile( const std::string &path )
    {
        return DeleteFileA( path.c_str() ) ? 0 : -1;
    }

    ProtectedInputResult ReadProtectedLine( std::istream &input, std::ostream &output, std::string &line )
    {
        if ( &input != &std::cin )
            return std::getline( input, line ) ? ProtectedInputResult::SUCCESS : ProtectedInputResult::READ_FAILED;
        const HANDLE console = GetStdHandle( STD_INPUT_HANDLE );
        DWORD original = 0;
        if ( ::_isatty( ::_fileno( stdin ) ) == 0 || console == INVALID_HANDLE_VALUE ||
             !GetConsoleMode( console, &original ) ||
             !SetConsoleMode( console, original & ~static_cast<DWORD>( ENABLE_ECHO_INPUT ) ) )
            return ProtectedInputResult::NOT_A_TERMINAL;
        const bool read = static_cast<bool>( std::getline( input, line ) );
        SetConsoleMode( console, original );
        output << '\n';
        return read ? ProtectedInputResult::SUCCESS : ProtectedInputResult::READ_FAILED;
    }
} // namespace sgns::trustedpeer::genesis_ceremony_platform
