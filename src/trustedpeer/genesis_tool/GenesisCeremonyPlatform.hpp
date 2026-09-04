#ifndef SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_PLATFORM_HPP
#define SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_PLATFORM_HPP

#include <istream>
#include <ostream>
#include <string>

#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"

namespace sgns::trustedpeer::genesis_ceremony_platform
{
    enum class ProtectedInputResult
    {
        SUCCESS,
        NOT_A_TERMINAL,
        READ_FAILED,
    };

    outcome::result<GenesisCeremony::KeyFileStatus> InspectKeyFile( const std::string &path );
    outcome::result<std::string>                    ReadKeyFile( const std::string &path );
    int                                             RestrictKeyFileToCurrentUser( const std::string &path );
    int                                             RemoveKeyFile( const std::string &path );
    ProtectedInputResult ReadProtectedLine( std::istream &input, std::ostream &output, std::string &line );

    inline void TrimLineEnding( std::string &value )
    {
        while ( !value.empty() && ( value.back() == '\n' || value.back() == '\r' ) )
        {
            value.pop_back();
        }
    }
} // namespace sgns::trustedpeer::genesis_ceremony_platform

#endif // SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_PLATFORM_HPP
