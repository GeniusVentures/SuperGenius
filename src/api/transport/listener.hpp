#ifndef SUPERGENIUS_SRC_API_TRANSPORT_LISTENER_HPP
#define SUPERGENIUS_SRC_API_TRANSPORT_LISTENER_HPP

#include <boost/asio/ip/tcp.hpp>
#include <memory>

#include "api/transport/rpc_io_context.hpp"
#include "api/transport/session.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::api {
  /**
   * @brief server which listens for incoming connection,
   * accepts connections making session from socket
   */
  class Listener : public IComponent {
   protected:
    using Acceptor = boost::asio::ip::tcp::acceptor;
    using Endpoint = boost::asio::ip::tcp::endpoint;
    using NewSessionHandler =
        std::function<void(const std::shared_ptr<Session> &)>;

   public:
    using Context = RpcContext;

    struct Configuration {
      Endpoint endpoint{};  ///< listning endpoint
      Configuration() {
        endpoint.address(boost::asio::ip::address_v4::any());
        endpoint.port(0);
      }
    };

    ~Listener() override = default;

    /**
     * @brief Bind endpoint
     * @see AppStateManager::takeControl
     */
    virtual bool prepare() = 0;

    /**
     * @brief Start handling inner connection
     * @see AppStateManager::takeControl
     */
    virtual bool start() = 0;

    /**
     * @brief Stop working
     * @see AppStateManager::takeControl
     */
    virtual void stop() = 0;

    /// Set handler for working new session
    virtual void setHandlerForNewSession(
        NewSessionHandler &&on_new_session) = 0;

   protected:
    /// Accept incoming connection
    virtual void acceptOnce() = 0;
  };
}

#endif
