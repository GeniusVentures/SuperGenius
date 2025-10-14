#pragma once

#include "../../ISecureStorage.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <utility>
#include <optional>

namespace sgns
{
    class DriveSecureStorage : public ISecureStorage
    {
    public:
        struct OAuthTokens
        {
            std::string access_token;
            std::string refresh_token;
            std::string token_type;
            int         expires_in;
        };

        DriveSecureStorage( std::string client_secret, std::shared_ptr<boost::asio::io_context> ioc ) :
            client_secret( std::move( client_secret ) ),
            ioc( std::move( ioc ) ),
            ctx( boost::asio::ssl::context::tlsv12_client ),
            resolver( *this->ioc )
        {
        }

        ~DriveSecureStorage() override = default;

        outcome::result<SecureBufferType> Load( const std::string &key, std::string directory ) override;

        outcome::result<void> Save( const std::string      &key,
                                    const SecureBufferType &buffer,
                                    std::string             directory ) override;

        static std::string GenerateAuthUrl();

        outcome::result<void> Authenticate( std::string_view auth_code );

        std::string GetName() override {
            return "DriveSecureStorage";
        }

    private:
        outcome::result<std::stringstream> HttpGet( std::string_view host, std::string_view target );

        outcome::result<std::stringstream> HttpPost( std::string_view host,
                                               std::string_view target,
                                               std::string_view body,
                                               std::string_view content_type = "application/x-www-form-urlencoded" );

        std::string                                        client_secret;
        std::shared_ptr<boost::asio::io_context>           ioc;
        boost::asio::ssl::context                          ctx;
        boost::asio::ip::tcp::resolver                     resolver;
        std::optional<OAuthTokens>                         tokens;
    };
}
