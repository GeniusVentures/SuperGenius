
#ifndef SUPERGENIUS_SRC_API_RPC_THREAD_POOL_HPP
#define SUPERGENIUS_SRC_API_RPC_THREAD_POOL_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <set>
#include <thread>

#include "api/transport/rpc_io_context.hpp"
#include "application/app_state_manager.hpp"
#include "base/logger.hpp"
#include "singleton/IComponent.hpp"

using sgns::application::AppStateManager;

namespace sgns::api {

  /**
   * @brief thread pool for serve RPC calls
   */
  class RpcThreadPool : public std::enable_shared_from_this<RpcThreadPool>, public IComponent {
   public:
    using Context = RpcContext;

    struct Configuration {
      size_t min_thread_number = 1;
      size_t max_thread_number = 10;
    };

    RpcThreadPool(std::shared_ptr<Context> context,
                  const Configuration &configuration);

    ~RpcThreadPool() = default;

    /**
     * @brief starts pool
     */
    void start();

    /**
     * @brief stops pool
     */
    void stop();

    std::string GetName() override
    {
      return "RpcThreadPool";
    }

   private:
    std::shared_ptr<Context> context_;
    const Configuration config_;

    std::vector<std::shared_ptr<std::thread>> threads_;

    base::Logger logger_ = base::createLogger("RPC thread pool");
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_RPC_THREAD_POOL_HPP
