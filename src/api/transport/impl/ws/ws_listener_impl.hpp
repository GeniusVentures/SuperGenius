
#ifndef SUPERGENIUS_SRC_API_TRANSPORT_IMPL_WS_LISTENER_IMPL_HPP
#define SUPERGENIUS_SRC_API_TRANSPORT_IMPL_WS_LISTENER_IMPL_HPP

#include "api/transport/listener.hpp"

#include "api/transport/impl/ws/ws_session.hpp"
#include "application/app_state_manager.hpp"
#include "base/logger.hpp"

namespace sgns::api {
  /**
   * @brief server which listens for incoming connection,
   * accepts connections making session from socket
   */
  class WsListenerImpl : public Listener,
                         public std::enable_shared_from_this<WsListenerImpl> {
   public:
    using SessionImpl = WsSession;

    WsListenerImpl(
        const std::shared_ptr<application::AppStateManager> &app_state_manager,
        std::shared_ptr<Context> context,
        Configuration listener_config,
        SessionImpl::Configuration session_config);

    ~WsListenerImpl() override = default;

    /** @see AppStateManager::takeControl */
    bool prepare() override;

    /** @see AppStateManager::takeControl */
    bool start() override;

    /** @see AppStateManager::takeControl */
    void stop() override;

    void setHandlerForNewSession(NewSessionHandler &&on_new_session) override;

    std::string GetName() override
    {
      return "WsListenerImpl";
    }

   private:
    void acceptOnce() override;

    std::shared_ptr<Context> context_;
    const Configuration config_;
    const SessionImpl::Configuration session_config_;

    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<NewSessionHandler> on_new_session_;

    std::atomic<Session::SessionId> next_session_id_;
    std::shared_ptr<SessionImpl> new_session_;

    base::Logger logger_ = base::createLogger("RPC_Websocket_Listener");
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_TRANSPORT_IMPL_WS_LISTENER_IMPL_HPP
