#include "infura_ipfs.hpp"
#include "root_certificates.hpp"

#include <boost/beast/websocket.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <rapidjson/document.h>

#include <stdlib.h>
#include <iostream>

namespace http = boost::beast::http;    // from <boost/beast/http.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>

InfuraIPFS::InfuraIPFS(const std::string& authorizationKey)
    : m_authorizationKey(authorizationKey)
{
}

void InfuraIPFS::GetIPFSFile(const std::string& cid, std::string& data)
{
    std::string host = "ipfs.infura.io";
    std::string port = "5001";
    std::string target = "/api/v0/cat?arg=" + cid;
    int version = 11;

    // The io_context is required for all I/O
    boost::asio::io_context ioc;

    // The SSL context is required, and holds certificates
    ssl::context ctx(ssl::context::tlsv12_client);

    // This holds the root certificate used for verification
    load_root_certificates(ctx);

    // Verify the remote server's certificate
    ctx.set_verify_mode(ssl::verify_peer);

    // These objects perform our I/O
    tcp::resolver resolver(ioc);
    boost::asio::ssl::stream<boost::beast::tcp_stream> stream(ioc, ctx);

    // Set SNI Hostname (many hosts need this to handshake successfully)
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
    {
        boost::beast::error_code ec{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() };
        throw boost::beast::system_error{ ec };
    }

    // Look up the domain name
    auto const results = resolver.resolve(host, port);

    // Make the connection on the IP address we get from a lookup
    boost::beast::get_lowest_layer(stream).connect(results);

    // Perform the SSL handshake
    stream.handshake(ssl::stream_base::client);

    // Set up an HTTP GET request message
    http::request<http::string_body> req{ http::verb::post, target, version };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::authorization, "Basic " + m_authorizationKey);

    // Send the HTTP request to the remote host
    http::write(stream, req);

    // This buffer is used for reading and must be persisted
    boost::beast::flat_buffer buffer;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Receive the HTTP response
    http::read(stream, buffer, res);

    // Write the message to standard out
    data = boost::beast::buffers_to_string(res.body().data());

    // Gracefully close the stream
    boost::beast::error_code ec;
    stream.shutdown(ec);
    if (ec == boost::asio::error::eof)
    {
        // Rationale:
        // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
        ec = {};
    }
    if (ec)
    {
        throw boost::beast::system_error{ ec };
    }

    // If we get here then the connection is closed gracefully
}

std::string InfuraIPFS::AddIPFSFile(const std::string& name, const std::string& content)
{
    std::string host = "ipfs.infura.io";
    std::string port = "5001";
    std::string target = "/api/v0/add";
    int version = 11;

    // The io_context is required for all I/O
    boost::asio::io_context ioc;

    // The SSL context is required, and holds certificates
    ssl::context ctx(ssl::context::tlsv12_client);

    // This holds the root certificate used for verification
    load_root_certificates(ctx);

    // Verify the remote server's certificate
    ctx.set_verify_mode(ssl::verify_peer);

    // These objects perform our I/O
    tcp::resolver resolver(ioc);
    boost::asio::ssl::stream<boost::beast::tcp_stream> stream(ioc, ctx);

    // Set SNI Hostname (many hosts need this to handshake successfully)
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
    {
        boost::beast::error_code ec{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() };
        throw boost::beast::system_error{ ec };
    }

    // Look up the domain name
    auto const results = resolver.resolve(host, port);

    // Make the connection on the IP address we get from a lookup
    boost::beast::get_lowest_layer(stream).connect(results);

    // Perform the SSL handshake
    stream.handshake(ssl::stream_base::client);

    // Set up an HTTP GET request message
    std::string boundary = "-------------0123456789";
    std::string body =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + name + "\"\r\n"
        "Content-Type : application/octet-stream\r\n\r\n"
        + content + "\r\n"
        "--" + boundary + "--\r\n";

    http::request<http::string_body> req{ http::verb::post, target, version };
    req.set(http::field::host, host + ":" + port);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::accept, "*/*");
    req.set(http::field::authorization, "Basic " + m_authorizationKey);
    req.set(http::field::content_type, "multipart/form-data; boundary=" + boundary);



    req.body() = body;

    req.prepare_payload();

    // @todo Log request
    //std::stringstream ss;
    //ss << req;
    //std::cout << ss.str() << std::endl;

    // Send the HTTP request to the remote host
    http::write(stream, req);

    // This buffer is used for reading and must be persisted
    boost::beast::flat_buffer buffer;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Receive the HTTP response
    http::read(stream, buffer, res);

    // Write the message to standard out
    std::string response = boost::beast::buffers_to_string(res.body().data());

    // Gracefully close the stream
    boost::beast::error_code ec;
    stream.shutdown(ec);
    if (ec == boost::asio::error::eof)
    {
        // Rationale:
        // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
        ec = {};
    }
    if (ec)
    {
        // Log response
        //std::cout << ec.message() << std::endl;
        throw boost::beast::system_error{ ec };
    }

    std::cout << response;
    rapidjson::Document document;
    document.Parse(response.c_str());

    if (!document.HasMember("Hash"))
    {
        std::cout << response;
        throw std::domain_error("Hash key was not found in HTTP response");
    }

    std::string cid = document["Hash"].GetString();
    return cid;
    // If we get here then the connection is closed gracefully
}

