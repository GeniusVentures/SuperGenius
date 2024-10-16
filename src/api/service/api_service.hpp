
#ifndef SUPERGENIUS_SRC_API_SERVICE_HPP
#define SUPERGENIUS_SRC_API_SERVICE_HPP

#include <functional>
#include <mutex>
#include <unordered_map>

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/transport/listener.hpp"
#include "api/transport/rpc_thread_pool.hpp"
#include "application/app_state_manager.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "primitives/common.hpp"
#include "subscription/subscriber.hpp"
#include "integration/IComponent.hpp"

namespace sgns::api {

  class JRpcProcessor;

  /**
   * Service listening for incoming JSON RPC request
   */
  class ApiService final : public std::enable_shared_from_this<ApiService>, public IComponent {
    using SessionPtr = std::shared_ptr<Session>;

    using SubscribedSessionType =
        subscription::Subscriber<base::Buffer,
                                 SessionPtr,
                                 base::Buffer,
                                 primitives::BlockHash>;
    using SubscribedSessionPtr = std::shared_ptr<SubscribedSessionType>;

    using SubscriptionEngineType =
        subscription::SubscriptionEngine<base::Buffer,
                                         SessionPtr,
                                         base::Buffer,
                                         primitives::BlockHash>;
    using SubscriptionEnginePtr = std::shared_ptr<SubscriptionEngineType>;

   public:
    template <class T>
    using sptr = std::shared_ptr<T>;

    /**
     * @brief constructor
     * @param context - reference to the io context
     * @param listener - a shared ptr to the endpoint listener instance
     * @param processors - shared ptrs to JSON processor instances
     */
    ApiService(
        const std::shared_ptr<application::AppStateManager> &app_state_manager,
        std::shared_ptr<api::RpcThreadPool> thread_pool,
        std::vector<std::shared_ptr<Listener>> listeners,
        std::shared_ptr<JRpcServer> server,
        const std::vector<std::shared_ptr<JRpcProcessor>> &processors,
        SubscriptionEnginePtr subscription_engine);

    virtual ~ApiService() = default;

    /** @see AppStateManager::takeControl */
    bool prepare();

    /** @see AppStateManager::takeControl */
    bool start();

    /** @see AppStateManager::takeControl */
    void stop();

    outcome::result<uint32_t> subscribeSessionToKeys(
        const std::vector<base::Buffer> &keys);

    outcome::result<void> unsubscribeSessionFromIds(
        const std::vector<uint32_t> &subscription_id);

    std::string GetName() override
    {
        return "ApiService";
    }

   private:
    SubscribedSessionPtr findSessionById(Session::SessionId id);
    void removeSessionById(Session::SessionId id);
    SubscribedSessionPtr storeSessionWithId(
        Session::SessionId id, const std::shared_ptr<Session> &session);

   private:
    std::shared_ptr<api::RpcThreadPool> thread_pool_;
    std::vector<sptr<Listener>> listeners_;
    std::shared_ptr<JRpcServer> server_;
    base::Logger logger_;
    std::mutex subscribed_sessions_cs_;
    std::unordered_map<Session::SessionId, SubscribedSessionPtr>
        subscribed_sessions_;
    SubscriptionEnginePtr subscription_engine_;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_SERVICE_HPP
