
#include "api/transport/impl/ws/ws_session.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/config.hpp>
#include <cstring>

#include "outcome/outcome.hpp"

namespace sgns::api {

  WsSession::WsSession(Context &context, Configuration config, SessionId id)
      : strand_(boost::asio::make_strand(context)),
        socket_(strand_),
        config_{config},
        stream_(socket_),
        id_(id) {}

  void WsSession::start() {
      boost::asio::dispatch(stream_.get_executor(),
          boost::asio::bind_executor(stream_.get_executor(),
              std::bind(&WsSession::onRun,
                  shared_from_this())));
  }

  void WsSession::stop() {
    boost::system::error_code ec;
    stream_.close(boost::beast::websocket::close_reason(), ec);
    boost::ignore_unused(ec);
    notifyOnClose(id_, type());
  }

  void WsSession::handleRequest(std::string_view data) {
    processRequest(data, shared_from_this());
  }

  void WsSession::asyncRead() {
      stream_.async_read(rbuffer_,
          boost::asio::bind_executor(stream_.get_executor(),
              std::bind(&WsSession::onRead,
                  shared_from_this(),
                  std::placeholders::_1,
                  std::placeholders::_2)));
  }

  void WsSession::asyncWrite() {
      stream_.async_write(wbuffer_.data(),
          boost::asio::bind_executor(stream_.get_executor(),
              std::bind(&WsSession::onWrite,
                  shared_from_this(),
                  std::placeholders::_1,
                  std::placeholders::_2)));
  }

  sgns::api::Session::SessionId WsSession::id() const {
    return id_;
  }

  void WsSession::respond(std::string_view response) {
    boost::asio::buffer_copy(
        wbuffer_.prepare(response.size()),
        boost::asio::const_buffer(response.data(), response.size()));
    wbuffer_.commit(response.size());
    stream_.text(true);
    asyncWrite();
  }

  void WsSession::onRun() {
      // Set suggested timeout settings for the websocket
      stream_.set_option(boost::beast::websocket::stream_base::timeout::suggested(
          boost::beast::role_type::server));

      // Set a decorator to change the Server of the handshake
      stream_.set_option(boost::beast::websocket::stream_base::decorator(
          [](boost::beast::websocket::response_type& res) {
              res.set(boost::beast::http::field::server,
                  std::string(BOOST_BEAST_VERSION_STRING)
                  + " websocket-server-async");
          }));
      // Accept the websocket handshake
      stream_.async_accept(boost::asio::bind_executor(stream_.get_executor(),
          std::bind(&WsSession::onAccept,
              shared_from_this(),
              std::placeholders::_1)));
  }

  void WsSession::onAccept(boost::system::error_code ec) {
    if (ec) {
      auto error_message = (ec == WsError::closed) ? "connection was closed"
                                                   : "unknown error occurred";
      reportError(ec, error_message);
      stop();
      return;
    }

    asyncRead();
  };

  void WsSession::onRead(boost::system::error_code ec,
                         std::size_t bytes_transferred) {
    if (ec) {
      auto error_message = (ec == WsError::closed) ? "connection was closed"
                                                   : "unknown error occurred";
      reportError(ec, error_message);
      stop();
      return;
    }

    handleRequest(
        {static_cast<char *>(rbuffer_.data().data()), bytes_transferred});

    rbuffer_.consume(bytes_transferred);
  }

  void WsSession::onWrite(boost::system::error_code ec,
                          std::size_t bytes_transferred) {
    if (ec) {
      reportError(ec, "failed to write message");
      return stop();
    }

    wbuffer_.consume(bytes_transferred);

    asyncRead();
  }

  void WsSession::reportError(boost::system::error_code ec,
                              std::string_view message) {
    logger_->error("error occured: {}, code: {}", message, ec);
  }

}  // namespace sgns::api
