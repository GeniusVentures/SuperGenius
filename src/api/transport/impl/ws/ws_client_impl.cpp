#include "ws_client_impl.hpp"
#include <iostream>
#include <boost/asio/connect.hpp>
#include <boost/beast/core/error.hpp>

namespace sgns::api {

    WsClientImpl::WsClientImpl(Context &context, const std::string &url)
            : strand(boost::asio::make_strand(context)),
              stream(strand),
              resolver(strand),
              url(url) {}

    void WsClientImpl::start() {
        auto self = shared_from_this();
        auto endpoint = resolver.resolve("localhost", "8546");

        // Use move semantics to pass the socket to async_connect
        boost::asio::async_connect(stream.next_layer(), endpoint,
                                   [self](boost::system::error_code ec, const boost::asio::ip::tcp::endpoint&) {
                                       self->onConnect(ec);
                                   });
    }

    void WsClientImpl::onConnect(boost::system::error_code ec) {
        if (ec) {
            reportError(ec, "connect");
            return;
        }

        // Set the suggested timeout for WebSocket operations
        stream.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));

        // Perform the WebSocket handshake
        stream.async_handshake(url, "/",
                               boost::beast::bind_front_handler(&WsClientImpl::onHandshake, shared_from_this()));
    }

    void WsClientImpl::onHandshake(boost::system::error_code ec) {
        if (ec) {
            reportError(ec, "handshake");
            return;
        }

        // Start reading messages from the WebSocket server
        asyncRead();
    }

    void WsClientImpl::stop() {
        boost::system::error_code ec;
        stream.close(boost::beast::websocket::close_code::normal, ec);
        if (ec) {
            reportError(ec, "close");
        }
    }

    void WsClientImpl::send(const std::string &message) {
        boost::asio::buffer_copy(wbuffer.prepare(message.size()), boost::asio::buffer(message));
        wbuffer.commit(message.size());
        asyncWrite();
    }

    void WsClientImpl::setMessageHandler(MessageHandler handler) {
        messageHandler = std::move(handler);
    }

    void WsClientImpl::asyncRead() {
        stream.async_read(rbuffer,
                          boost::beast::bind_front_handler(&WsClientImpl::onRead, shared_from_this()));
    }

    void WsClientImpl::asyncWrite() {
        stream.async_write(wbuffer.data(),
                           boost::beast::bind_front_handler(&WsClientImpl::onWrite, shared_from_this()));
    }

    void WsClientImpl::onRead(boost::system::error_code ec, std::size_t bytes_transferred) {
        if (ec) {
            reportError(ec, "read");
            stop();
            return;
        }

        if (messageHandler) {
            messageHandler({static_cast<const char*>(rbuffer.data().data()), bytes_transferred});
        }

        rbuffer.consume(bytes_transferred);
        asyncRead();
    }

    void WsClientImpl::onWrite(boost::system::error_code ec, std::size_t bytes_transferred) {
        if (ec) {
            reportError(ec, "write");
            stop();
            return;
        }

        wbuffer.consume(bytes_transferred);
        asyncRead();
    }

    void WsClientImpl::reportError(boost::system::error_code ec, std::string_view message) {
        std::cerr << "Error (" << ec.message() << "): " << message << std::endl;
    }

}  // namespace sgns::api
