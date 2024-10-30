#ifndef WS_CLIENT_IMPL_HPP
#define WS_CLIENT_IMPL_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>

namespace sgns::api {

    class WsClientImpl : public std::enable_shared_from_this<WsClientImpl> {
    public:
        using Context = boost::asio::io_context;
        using MessageHandler = std::function<void(const std::string&)>;

        WsClientImpl(Context &context, const std::string &url);

        void start();
        void stop();
        void send(const std::string &message);
        void setMessageHandler(MessageHandler handler);

    private:
        void onResolve(boost::system::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
        void onConnect(boost::system::error_code ec);
        void onHandshake(boost::system::error_code ec);
        void onRead(boost::system::error_code ec, std::size_t bytes_transferred);
        void onWrite(boost::system::error_code ec, std::size_t bytes_transferred);
        void asyncRead();
        void asyncWrite();
        void reportError(boost::system::error_code ec, std::string_view message);

        boost::asio::strand<boost::asio::io_context::executor_type> strand;
        boost::beast::websocket::stream<boost::asio::ip::tcp::socket> stream;
        boost::asio::ip::tcp::resolver resolver;
        boost::beast::flat_buffer rbuffer;
        boost::beast::flat_buffer wbuffer;
        std::string url;
        MessageHandler messageHandler;
    };

}  // namespace sgns::api

#endif // WS_CLIENT_IMPL_HPP
