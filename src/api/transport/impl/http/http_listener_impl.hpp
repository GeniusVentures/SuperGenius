
#ifndef SUPERGENIUS_SRC_API_TRANSPORT_IMPL_LISTENER_IMPL_HPP
#define SUPERGENIUS_SRC_API_TRANSPORT_IMPL_LISTENER_IMPL_HPP

#include "api/transport/listener.hpp"

#include "api/transport/impl/http/http_session.hpp"
#include "application/app_state_manager.hpp"
#include "base/logger.hpp"

namespace sgns::api {
  /**
   * @brief server which listens for incoming connection,
   * accepts connections making session from socket
   */
  class HttpListenerImpl
      : public Listener,
        public std::enable_shared_from_this<HttpListenerImpl> {
   public:
    using SessionImpl = HttpSession;

    HttpListenerImpl(
        const std::shared_ptr<application::AppStateManager> &app_state_manager,
        std::shared_ptr<Context> context,
        Configuration listener_config,
        SessionImpl::Configuration session_config);

    ~HttpListenerImpl() override = default;

    void prepare() override;
    void start() override;
    void stop() override;

    void setHandlerForNewSession(NewSessionHandler &&on_new_session) override;

   private:
    void acceptOnce() override;

    std::shared_ptr<Context> context_;
    const Configuration config_;
    const SessionImpl::Configuration session_config_;

    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<NewSessionHandler> on_new_session_;

    std::shared_ptr<SessionImpl> new_session_;

    base::Logger logger_ = base::createLogger("RPC_HTTP_Listener");
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_TRANSPORT_IMPL_LISTENER_IMPL_HPP
