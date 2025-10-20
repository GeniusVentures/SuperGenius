#include "DriveSecureStorage.hpp"
#include "outcome/outcome.hpp"
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/format.h>
#include <iostream>
#include <rapidjson/document.h>

using namespace boost::beast;

namespace sgns
{

    constexpr std::string_view CLIENT_ID = "195392825921-lq1nl25jdp2p04fi0h7nom9fj8v9f38v.apps.googleusercontent.com";

    constexpr std::string_view GOOGLE_OAUTH2_AUTH_URL  = "https://accounts.google.com/o/oauth2/auth";
    constexpr std::string_view GOOGLE_OAUTH2_TOKEN_URL = "https://oauth2.googleapis.com/token";
    constexpr std::string_view GOOGLE_DRIVE_API_BASE   = "https://www.googleapis.com/drive/v3";

    outcome::result<std::stringstream> DriveSecureStorage::HttpGet( std::string_view host, std::string_view target )
    {
        if ( !tokens.has_value() )
        {
            return outcome::failure( std::errc::permission_denied );
        }
        try
        {
            boost::beast::ssl_stream<boost::beast::tcp_stream> stream( *this->ioc, this->ctx );
            auto const                                         results = resolver.resolve( host, "443" );
            net::connect( stream.next_layer().socket(), results.begin(), results.end() );
            stream.handshake( boost::asio::ssl::stream_base::client );

            http::request<http::string_body> req{ http::verb::get, target, 11 };
            req.set( http::field::host, host );
            req.set( http::field::user_agent, BOOST_BEAST_VERSION_STRING );
            req.set( http::field::authorization, "Bearer " + tokens->access_token );

            http::write( stream, req );

            flat_buffer                        buffer;
            http::response<http::dynamic_body> res;
            boost::system::error_code          ec;
            http::read( stream, buffer, res, ec );

            std::cout << ec.to_string();
            std::cout << ec.value();
            std::cout << ec.what();

            if ( res.result() != http::status::ok )
            {
                std::cout << res.result() << std::endl;
                return outcome::failure( std::errc::io_error );
            }

            std::stringstream ss;
            ss << make_printable( res.body().data() );
            return ss;
        }
        catch ( const boost::system::system_error &e )
        {
            return outcome::failure( e.code() );
        }
    }

    // Function to perform HTTP POST request
    outcome::result<std::stringstream> DriveSecureStorage::HttpPost( std::string_view host,
                                                                     std::string_view target,
                                                                     std::string_view body,
                                                                     std::string_view content_type )
    {
        try
        {
            auto const                                         results = resolver.resolve( host, "443" );
            boost::beast::ssl_stream<boost::beast::tcp_stream> stream( *this->ioc, this->ctx );
            net::connect( stream.next_layer().socket(), results.begin(), results.end() );
            stream.handshake( boost::asio::ssl::stream_base::client );

            http::request<http::string_body> req{ http::verb::post, target, 11 };
            req.set( http::field::host, host );
            req.set( http::field::user_agent, BOOST_BEAST_VERSION_STRING );
            req.set( http::field::content_type, content_type );
            req.body() = body;
            req.prepare_payload();

            http::write( stream, req );

            flat_buffer                        buffer;
            http::response<http::dynamic_body> res;
            http::read( stream, buffer, res );

            if ( res.result() != http::status::ok )
            {
                return outcome::failure( std::errc::io_error );
            }

            std::stringstream ss;
            ss << make_printable( res.body().data() );
            return ss;
        }
        catch ( const boost::system::system_error &e )
        {
            return outcome::failure( e.code() );
        }
    }

    std::string DriveSecureStorage::GenerateAuthUrl()
    {
        return fmt::format(
            "{}?response_type=code&client_id={}&redirect_uri=urn:ietf:wg:oauth:2.0:oob&scope=https://www.googleapis.com/auth/drive",
            GOOGLE_OAUTH2_AUTH_URL,
            CLIENT_ID );
    }

    outcome::result<void> DriveSecureStorage::Authenticate( std::string_view auth_code )
    {
        std::string body = fmt::format(
            "code={}&client_id={}&client_secret={}&redirect_uri=urn:ietf:wg:oauth:2.0:oob&grant_type=authorization_code&scope=https://www.googleapis.com/auth/drive.file",
            auth_code,
            CLIENT_ID,
            client_secret );

        OUTCOME_TRY( auto response, HttpPost( "oauth2.googleapis.com", "/token", body ) );

        OAuthTokens                 tokens;
        boost::property_tree::ptree pt;
        boost::property_tree::read_json( response, pt );

        tokens.access_token  = pt.get<std::string>( "access_token", "" );
        tokens.refresh_token = pt.get<std::string>( "refresh_token", "" );
        tokens.token_type    = pt.get<std::string>( "token_type", "" );
        tokens.expires_in    = pt.get<int>( "expires_in", 0 );

        this->tokens = tokens;
        return outcome::success();
    }

    outcome::result<DriveSecureStorage::SecureBufferType> DriveSecureStorage::Load( const std::string &key,
                                                                                    std::string        directory )
    {
        if ( !tokens.has_value() )
        {
            return outcome::failure( std::errc::permission_denied );
        }

        std::string target = fmt::format( R"(/drive/v3/files?orderBy=createdTime&q=name+=+'{}'&orderBy=createdTime+desc)", key );
        OUTCOME_TRY( auto file_list, HttpGet( "www.googleapis.com", target ) );

        rapidjson::Document document;
        auto                as_str = file_list.str();
        document.Parse( as_str.c_str() );

        if ( document["files"].GetArray().Size() == 0 )
        {
            return outcome::failure( std::errc::message_size );
        }

        auto id = document["files"][0]["id"].GetString();

        target = fmt::format( "/drive/v3/files/{}?alt=media", id );
        OUTCOME_TRY( auto file_content, HttpGet( "www.googleapis.com", target ) );

        return file_content.str();
    }

    outcome::result<void> DriveSecureStorage::Save( const std::string      &key,
                                                    const SecureBufferType &buffer,
                                                    std::string             directory )
    {
        if ( !tokens.has_value() )
        {
            return outcome::failure( std::errc::permission_denied );
        }

        std::string        boundary = "FormBoundary7MA4YWxkTrZu0gW";
        std::ostringstream body;
        body << "--" << boundary << "\r\n"
             << "Content-Type: application/json; charset=UTF-8\r\n\r\n"
             << fmt::format( R"({{"name": "{}"}})", key ) << "\r\n"
             << "--" << boundary << "\r\n"
             << "Content-Type: application/octet-stream\r\n\r\n";
        body.write( reinterpret_cast<const char *>( buffer.data() ), buffer.size() );
        body << "\r\n--" << boundary << "--\r\n";

        try
        {
            boost::beast::ssl_stream<boost::beast::tcp_stream> stream( *this->ioc, this->ctx );
            auto const results = resolver.resolve( "www.googleapis.com", "443" );
            net::connect( stream.next_layer().socket(), results.begin(), results.end() );
            stream.handshake( boost::asio::ssl::stream_base::client );

            http::request<http::string_body> req{ http::verb::post, "/upload/drive/v3/files?uploadType=multipart", 11 };
            req.set( http::field::host, "www.googleapis.com" );
            req.set( http::field::user_agent, BOOST_BEAST_VERSION_STRING );
            req.set( http::field::authorization, "Bearer " + tokens->access_token );
            req.set( http::field::content_type, "multipart/related; boundary=" + boundary );
            req.body() = body.str();
            req.prepare_payload();

            http::write( stream, req );

            flat_buffer                        buffer;
            http::response<http::dynamic_body> res;
            http::read( stream, buffer, res );

            if ( res.result() != http::status::ok )
            {
                return outcome::failure( std::errc::io_error );
            }

            return outcome::success();
        }
        catch ( const boost::system::system_error &e )
        {
            std::cout << ( e.what() );
            return outcome::failure( e.code() );
        }
    }
}
