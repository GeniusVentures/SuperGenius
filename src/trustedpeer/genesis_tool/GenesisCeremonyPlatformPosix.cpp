#include "trustedpeer/genesis_tool/GenesisCeremonyPlatform.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace sgns::trustedpeer::genesis_ceremony_platform
{
    namespace
    {
        constexpr size_t MAX_KEY_FILE_BYTES = 256;

    } // namespace

    outcome::result<GenesisCeremony::KeyFileStatus> InspectKeyFile( const std::string &path )
    {
        struct stat metadata
        {
        };
        if ( ::lstat( path.c_str(), &metadata ) != 0 )
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        return GenesisCeremony::KeyFileStatus{ true,
                                               S_ISREG( metadata.st_mode ) != 0,
                                               S_ISLNK( metadata.st_mode ) != 0,
                                               metadata.st_uid == ::geteuid(),
                                               static_cast<uint32_t>( metadata.st_mode & 0777 ) };
    }

    outcome::result<std::string> ReadKeyFile( const std::string &path )
    {
        const int descriptor = ::open( path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW );
        if ( descriptor < 0 )
            return outcome::failure( errno == ELOOP ? GenesisCeremony::Error::KEY_FILE_SYMLINK
                                                    : GenesisCeremony::Error::KEY_FILE_IO );

        struct stat metadata
        {
        };
        if ( ::fstat( descriptor, &metadata ) != 0 )
        {
            ::close( descriptor );
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        }
        if ( !S_ISREG( metadata.st_mode ) )
        {
            ::close( descriptor );
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_NOT_REGULAR );
        }
        if ( metadata.st_uid != ::geteuid() )
        {
            ::close( descriptor );
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_OWNER );
        }
        if ( ( metadata.st_mode & 0777 ) != 0600 )
        {
            ::close( descriptor );
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_MODE );
        }

        std::string value( MAX_KEY_FILE_BYTES + 1, '\0' );
        const auto bytes_read = ::read( descriptor, value.data(), value.size() );
        const int close_result = ::close( descriptor );
        if ( bytes_read < 0 || close_result != 0 || static_cast<size_t>( bytes_read ) > MAX_KEY_FILE_BYTES )
            return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        value.resize( static_cast<size_t>( bytes_read ) );
        TrimLineEnding( value );
        return value;
    }

    int RestrictKeyFileToCurrentUser( const std::string &path )
    {
        return ::chmod( path.c_str(), 0600 );
    }

    int RemoveKeyFile( const std::string &path )
    {
        return ::unlink( path.c_str() );
    }

    ProtectedInputResult ReadProtectedLine( std::istream &input, std::ostream &output, std::string &line )
    {
        if ( &input != &std::cin )
            return std::getline( input, line ) ? ProtectedInputResult::SUCCESS : ProtectedInputResult::READ_FAILED;
        if ( ::isatty( STDIN_FILENO ) == 0 ) return ProtectedInputResult::NOT_A_TERMINAL;

        struct termios original
        {
        };
        if ( ::tcgetattr( STDIN_FILENO, &original ) != 0 ) return ProtectedInputResult::NOT_A_TERMINAL;
        auto protected_mode = original;
        protected_mode.c_lflag &= static_cast<tcflag_t>( ~ECHO );
        if ( ::tcsetattr( STDIN_FILENO, TCSAFLUSH, &protected_mode ) != 0 )
            return ProtectedInputResult::NOT_A_TERMINAL;
        const bool read = static_cast<bool>( std::getline( input, line ) );
        ::tcsetattr( STDIN_FILENO, TCSAFLUSH, &original );
        output << '\n';
        return read ? ProtectedInputResult::SUCCESS : ProtectedInputResult::READ_FAILED;
    }
} // namespace sgns::trustedpeer::genesis_ceremony_platform
