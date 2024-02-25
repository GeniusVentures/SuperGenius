
#ifndef SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP

#include "application/sgns_application.hpp"

#include "api/service/api_service.hpp"
#include "application/app_config.hpp"
#include "application/configuration_storage.hpp"
#include "application/impl/local_key_storage.hpp"
#include "verification/finality/finality.hpp"
#include "verification/production.hpp"
#include "network/router.hpp"
namespace sgns::application
{

    class ValidatingNodeApplication : public SgnsApplication
    {
        using Production = verification::Production;
        using Finality   = verification::finality::Finality;

    public:
        ~ValidatingNodeApplication() override = default;

        /**
     * @param config_path genesis configs path
     * @param keystore_path local peer's keys
     * @param rocksdb_path storage path
     * @param p2p_port port for p2p interactions
     * @param rpc_http_endpoint endpoint for http based rpc
     * @param rpc_ws_endpoint endpoint for ws based rpc
     * @param is_only_finalizing true if this node should be the only finalizing
     * node
     * @param verbosity level of logging
     */
        ValidatingNodeApplication( const std::shared_ptr<AppConfiguration> &config );

        void run() override;

    private:
        // need to keep all of these instances, since injector itself is destroyed

        std::shared_ptr<AppStateManager> app_state_manager_;

        std::shared_ptr<boost::asio::io_context> io_context_;

        std::shared_ptr<ConfigurationStorage> config_storage_;
        std::shared_ptr<KeyStorage>           key_storage_;
        std::shared_ptr<clock::SystemClock>   clock_;
        std::shared_ptr<Production>           production_;
        std::shared_ptr<Finality>             finality_;
        std::shared_ptr<network::Router>      router_;

        std::shared_ptr<api::ApiService> jrpc_api_service_;

        Production::ExecutionStrategy production_execution_strategy_;

        base::Logger logger_;
    };

} // namespace sgns::application

#endif // SUPERGENIUS_SRC_APPLICATION_IMPL_VALIDATING_NODE_APPLICATION_HPP
