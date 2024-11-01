#ifndef WS_CLIENT_IMPL_HPP
#define WS_CLIENT_IMPL_HPP

#include <boost/asio/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>  // Include for strand
#include <boost/beast/ssl/ssl_stream.hpp>  // Include for SSL stream
#include <variant>
#include <regex>
#include <optional>
#include <boost/beast/core/flat_buffer.hpp>
#include <functional>

namespace sgns::api {

class WsClientImpl : public std::enable_shared_from_this<WsClientImpl> {
public:
    // Define a type alias for context to make usage clear for the caller
    using Context = boost::asio::io_context;
    using ExecutorType = boost::asio::strand<boost::asio::io_context::executor_type>;

    // Define socket types with executor
    using StreamSocket = boost::asio::basic_stream_socket<boost::asio::ip::tcp>;
    using PlainWebSocketStream = boost::beast::websocket::stream<StreamSocket>;
    using SSLStream = boost::beast::ssl_stream<StreamSocket>;
    using SecureWebSocketStream = boost::beast::websocket::stream<SSLStream>;
    // Constructor that takes the io_context and URL, creating SSL context if needed
    WsClientImpl(Context& context, const std::string& url);

    // Method to start the WebSocket client
    void start();

    // Set a callback handler for incoming messages
    void setMessageHandler(std::function<void(const std::string&)> handler);

    // Send a message over the WebSocket
    void send(const std::string& message);

    // Stop the WebSocket client gracefully
    void stop();

private:
    std::optional<boost::asio::ssl::context> sslContext; // SSL context created internally if needed
    std::optional<std::variant<PlainWebSocketStream, SecureWebSocketStream>> stream;
    ExecutorType strand;
    boost::asio::ip::tcp::resolver resolver;

    boost::beast::flat_buffer buffer;  // Used to read WebSocket messages

    std::string host;
    std::string port;
    bool secure;

    // Helper function to parse the URL
    void parseUrl(const std::string& url, std::string& protocol, std::string& host, std::string& port);

    // Handles the WebSocket handshake once TCP connection is established
    void onWebSocketConnect();

    // Report error function to handle errors in asynchronous operations
    void reportError(const boost::system::error_code& ec, const std::string& context);
};

}  // namespace sgns::api

#endif // WS_CLIENT_IMPL_HPP
