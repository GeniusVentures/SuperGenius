#include "ws_client_impl.hpp"
#include <boost/asio/connect.hpp>
#include <iostream>

namespace sgns::api {

void WsClientImpl::parseUrl(const std::string& url, std::string& protocol, std::string& host, std::string& port) {
    std::regex urlRegex(R"(^(ws|wss)://([^/:]+)(?::(\d+))?.*)");
    std::smatch matches;

    if (std::regex_match(url, matches, urlRegex)) {
        if (matches.size() >= 3) {
            protocol = matches[1];
            host = matches[2];

            if (matches.size() == 4 && matches[3].matched) {
                port = matches[3];
            } else {
                port = (protocol == "wss") ? "443" : "80";
            }
        }
    } else {
        throw std::runtime_error("Invalid URL format");
    }
}

    WsClientImpl::WsClientImpl(Context& context, const std::string& url)
        : strand(boost::asio::make_strand(context)),  // Properly initialize the strand first
          resolver(strand),
          secure(false)
{
    // Parse the URL to determine protocol, host, and port
    std::string protocol;
    std::string hostParsed;
    std::string portParsed;
    parseUrl(url, protocol, hostParsed, portParsed);
    host = hostParsed;
    port = portParsed;
    secure = (protocol == "wss");

    if (secure) {
        // Step 1: Create an SSL context for the secure connection
        sslContext.emplace(boost::asio::ssl::context::tlsv12_client);
        sslContext->set_default_verify_paths();

        // Step 2: Create the SecureWebSocketStream directly
        stream.emplace<SecureWebSocketStream>(
            boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::basic_stream_socket<boost::asio::ip::tcp>>>(
                boost::beast::ssl_stream<boost::asio::ip::tcp::socket>(make_strand(context), *sslContext))
        );
    } else {
        // Construct PlainWebSocketStream using the strand
        stream.emplace<PlainWebSocketStream>(boost::beast::websocket::stream<boost::asio::basic_stream_socket<boost::asio::ip::tcp>>(
            make_strand(context)));
    }
}

void WsClientImpl::start() {
    auto self = shared_from_this();

    resolver.async_resolve(host, port,
        [self, this](boost::system::error_code ec, boost::asio::ip::tcp::resolver::results_type results) {
            if (ec) {
                self->reportError(ec, "resolve");
                return;
            }

            // Ensure the stream is initialized before visiting
            if (!stream.has_value()) {
                self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
                return;
            }

            std::visit([self, this, &results](auto& streamVariant) {
                using StreamType = std::decay_t<decltype(streamVariant)>;
                if constexpr (std::is_same_v<StreamType, PlainWebSocketStream>) {
                    boost::asio::async_connect(
                        streamVariant.next_layer(),
                        results.begin(), results.end(),
                        [self](boost::system::error_code ec, const auto&) {
                            if (ec) {
                                self->reportError(ec, "connect");
                                return;
                            }
                            // Proceed to WebSocket handshake
                            self->onWebSocketConnect();
                        });
                } else if constexpr (std::is_same_v<StreamType, SecureWebSocketStream>) {
                    boost::asio::async_connect(
                        streamVariant.next_layer().next_layer(), // SSL layer -> TCP socket
                        results.begin(), results.end(),
                        [self, this, &streamVariant](boost::system::error_code ec, const auto&) {
                            if (ec) {
                                self->reportError(ec, "connect");
                                return;
                            }
                            // SSL Handshake
                            streamVariant.next_layer().async_handshake(
                                boost::asio::ssl::stream_base::client,
                                [self, this](boost::system::error_code ec) {
                                    if (ec) {
                                        self->reportError(ec, "SSL handshake");
                                        return;
                                    }

                                    // After SSL handshake is successful, initiate WebSocket handshake
                                    if (!stream.has_value()) {
                                        self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
                                        return;
                                    }

                                    std::visit([self](auto& innerStreamVariant) {
                                        innerStreamVariant.async_handshake(self->host, "/",
                                            [self](boost::system::error_code ec) {
                                                if (ec) {
                                                    self->reportError(ec, "WebSocket handshake");
                                                    return;
                                                }

                                                std::cout << "Successfully connected to WebSocket server." << std::endl;
                                            });
                                    }, stream.value());
                                });
                        });
                }
            }, stream.value());
        });
}

void WsClientImpl::onWebSocketConnect() {
    auto self = shared_from_this();

    if (!stream.has_value()) {
        self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
        return;
    }

    std::visit([self](auto& streamVariant) {
        streamVariant.async_handshake(self->host, "/",
            [self](boost::system::error_code ec) {
                if (ec) {
                    self->reportError(ec, "WebSocket handshake");
                    return;
                }

                std::cout << "Successfully connected to WebSocket server." << std::endl;
            });
    }, stream.value());
}

void WsClientImpl::setMessageHandler(std::function<void(const std::string&)> handler) {
    auto self = shared_from_this();

    if (!stream.has_value()) {
        self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
        return;
    }
    std::visit([self, handler, this](auto& streamVariant) {
        streamVariant.async_read(buffer,
            [self, handler](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (!ec) {
                    std::string message(boost::asio::buffer_cast<const char*>(self->buffer.data()), bytes_transferred);
                    handler(message);
                    self->buffer.consume(bytes_transferred);
                    self->setMessageHandler(handler);
                } else {
                    self->reportError(ec, "WebSocket read");
                }
            });
    }, stream.value());
}

void WsClientImpl::send(const std::string& message) {
    auto self = shared_from_this();

    if (!stream.has_value()) {
        self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
        return;
    }

    std::visit([self, message](auto& streamVariant) {
        streamVariant.async_write(boost::asio::buffer(message),
            [self](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    self->reportError(ec, "WebSocket write");
                }
            });
    }, stream.value());
}

void WsClientImpl::stop() {
    auto self = shared_from_this();

    if (!stream.has_value()) {
        self->reportError(boost::asio::error::not_connected, "Stream is not initialized");
        return;
    }
    std::visit([self](auto& streamVariant) {
        streamVariant.async_close(boost::beast::websocket::close_code::normal,
            [self](boost::system::error_code ec) {
                if (ec) {
                    self->reportError(ec, "WebSocket close");
                } else {
                    std::cout << "WebSocket connection closed gracefully." << std::endl;
                }
            });
    }, stream.value());
}

void WsClientImpl::reportError(const boost::system::error_code& ec, const std::string& context) {
    std::cerr << "Error in " << context << ": " << ec.message() << std::endl;
}

}  // namespace sgns::api  // namespace sgns::api