
#ifndef SUPERGENIUS_SRC_API_SERVICE_HPP
#define SUPERGENIUS_SRC_API_SERVICE_HPP

#include <functional>
#include <gsl/span>

#include "api/jrpc/jrpc_server_impl.hpp"
#include "api/transport/listener.hpp"
#include "api/transport/rpc_thread_pool.hpp"
#include "application/app_state_manager.hpp"
#include "base/logger.hpp"

namespace sgns::api {

  class JRpcProcessor;

  /**
   * Service listening for incoming JSON RPC request
   */
  class ApiService final : public std::enable_shared_from_this<ApiService> {
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
        gsl::span<std::shared_ptr<JRpcProcessor>> processors);

    virtual ~ApiService() = default;

    void prepare();
    void start();
    void stop();

   private:
    std::shared_ptr<api::RpcThreadPool> thread_pool_;
    std::vector<sptr<Listener>> listeners_;
    std::shared_ptr<JRpcServer> server_;
    base::Logger logger_;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_SERVICE_HPP
