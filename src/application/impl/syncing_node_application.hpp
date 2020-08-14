
#ifndef SUPERGENIUS_SRC_APPLICATION_IMPL_SYNCING_NODE_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_IMPL_SYNCING_NODE_APPLICATION_HPP

#include "application/sgns_application.hpp"

#include "application/app_config.hpp"
#include "base/logger.hpp"
#include "injector/syncing_node_injector.hpp"

namespace sgns::application {

  class SyncingNodeApplication : public SgnsApplication {
    template <class T>
    using sptr = std::shared_ptr<T>;

    template <class T>
    using uptr = std::unique_ptr<T>;

   public:
    using InjectorType =
        decltype(injector::makeSyncingNodeInjector(AppConfigPtr{}));

    ~SyncingNodeApplication() override = default;

    SyncingNodeApplication(const AppConfigPtr &app_config);

    void run() override;

   private:
    // need to keep all of these instances, since injector itself is destroyed
    InjectorType injector_;

    std::shared_ptr<AppStateManager> app_state_manager_;

    sptr<boost::asio::io_context> io_context_;

    sptr<ConfigurationStorage> config_storage_;
    sptr<network::Router> router_;

    sptr<api::ApiService> jrpc_api_service_;

    base::Logger logger_;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APPLICATION_IMPL_SYNCING_NODE_APPLICATION_HPP
