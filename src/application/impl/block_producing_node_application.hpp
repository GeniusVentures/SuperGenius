#ifndef SUPERGENIUS_SRC_APPLICATION_IMPL_BLOCK_PRODUCING_NODE_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_IMPL_BLOCK_PRODUCING_NODE_APPLICATION_HPP

#include "application/sgns_application.hpp"

#include "application/app_config.hpp"
#include "injector/block_producing_node_injector.hpp"

namespace sgns::application {

  class BlockProducingNodeApplication : public SgnsApplication {
    using Production = verification::Production;
    using InjectorType =
        decltype(injector::makeBlockProducingNodeInjector(AppConfigPtr{}));

    template <class T>
    using sptr = std::shared_ptr<T>;

    template <class T>
    using uptr = std::unique_ptr<T>;

   public:
    ~BlockProducingNodeApplication() override = default;

    /**
     * @param sgns_config sgns configuration parameters
     */
    BlockProducingNodeApplication(const AppConfigPtr &app_config);

    void run() override;

   private:
    // need to keep all of these instances, since injector itself is destroyed
    InjectorType injector_;

    std::shared_ptr<AppStateManager> app_state_manager_;

    sptr<boost::asio::io_context> io_context_;

    sptr<ConfigurationStorage> config_storage_;
    sptr<KeyStorage> key_storage_;
    sptr<clock::SystemClock> clock_;
    sptr<Production> production_;
    sptr<network::Router> router_;

    sptr<api::ApiService> jrpc_api_service_;

    base::Logger logger_;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APPLICATION_IMPL_BLOCK_PRODUCING_NODE_APPLICATION_HPP
