
#ifndef SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP

#include "application/sgns_application.hpp"

#include "api/service/api_service.hpp"
#include "application/app_config.hpp"
#include "application/configuration_storage.hpp"
#include "application/impl/local_key_storage.hpp"
#include "injector/validating_node_injector.hpp"
#include "runtime/dummy/finality_api_dummy.hpp"
#include "verification/finality/finality.hpp"
namespace sgns::application {

  class ValidatingNodeApplication : public SgnsApplication {
    using Production = verification::Production;
    using Finality = verification::finality::Finality;
    using InjectorType =
        decltype(injector::makeFullNodeInjector(AppConfigPtr{}));

    template <class T>
    using sptr = std::shared_ptr<T>;

    template <class T>
    using uptr = std::unique_ptr<T>;

   public:
    ~ValidatingNodeApplication() override = default;

    /**
     * @param config_path genesis configs path
     * @param keystore_path local peer's keys
     * @param leveldb_path storage path
     * @param p2p_port port for p2p interactions
     * @param rpc_http_endpoint endpoint for http based rpc
     * @param rpc_ws_endpoint endpoint for ws based rpc
     * @param is_only_finalizing true if this node should be the only finalizing
     * node
     * @param verbosity level of logging
     */
    ValidatingNodeApplication(const AppConfigPtr &config);

    void run() override;

   private:
    // need to keep all of these instances, since injector itself is destroyed
    InjectorType injector_;

    std::shared_ptr<AppStateManager> app_state_manager_;

    std::shared_ptr<boost::asio::io_context> io_context_;

    sptr<ConfigurationStorage> config_storage_;
    sptr<KeyStorage> key_storage_;
    sptr<clock::SystemClock> clock_;
    sptr<Production> production_;
    sptr<Finality> finality_;
    sptr<network::Router> router_;

    sptr<api::ApiService> jrpc_api_service_;

    Production::ExecutionStrategy prodcution_execution_strategy_;

    base::Logger logger_;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP
